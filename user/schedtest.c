// Scheduler tests for more complex scenarios: strict priority between
// levels, round-robin within a level, yielding on a priority change,
// sleep/wakeup and kill at different priorities, and slot reuse.
// Several checks read the kernel's scheduling log to see which process
// was actually dispatched. Meant to be run with one CPU.
// Each check prints PASS or FAIL; the exit status is the number of failures.

#include "kernel/types.h"
#include "kernel/param.h"
#include "kernel/schedlog.h"
#include "user/user.h"

#define LOWEST (NB_PRIORITY_LEVELS - 1)

static int fails;
static struct schedrec recs[SCHEDLOG_NREC];
static volatile uint64 sink;

static void
check(int ok, char *what)
{
  printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok)
    fails++;
}

// Burn CPU for n loop iterations.
static void
work(uint64 n)
{
  for (uint64 i = 0; i < n; i++)
    sink += i;
}

// Number of work() iterations that take about `ticks` clock ticks.
static uint64
calibrate(int ticks)
{
  uint64 n = 0;
  int t0 = uptime();

  while (uptime() == t0)
    ;
  t0 = uptime();
  while (uptime() - t0 < ticks) {
    work(100000);
    n += 100000;
  }
  return n;
}

// The most recent dispatch in the scheduling log.
static struct schedrec
last(void)
{
  struct schedrec r;

  if (schedlog(&r, 1) != 1) {
    printf("schedtest: cannot read the scheduling log\n");
    exit(1);
  }
  return r;
}

int
main(void)
{
  int fd[2], i, n, pid, st, t0;
  struct schedrec before, after;

  if (setpriority(0) != 0) {
    printf("schedtest: setpriority failed\n");
    exit(1);
  }
  uint64 unit = calibrate(3);

  // 1. Setting the current priority does not give up the CPU, so no
  // new dispatch appears in the log. A timer tick can land in between,
  // so one clean attempt out of several is enough.
  int quiet = 0;
  for (i = 0; i < 5 && !quiet; i++) {
    before = last();
    setpriority(0);
    after = last();
    if (after.seq == before.seq)
      quiet = 1;
  }
  check(quiet, "setting the same priority does not yield");

  // 2. Changing the priority does give up the CPU: this process is
  // dispatched again, and the log shows it at the new priority.
  int yielded = 1;
  for (i = 0; i < 5; i++) {
    int to = (i % 2) ? 0 : LOWEST;
    before = last();
    setpriority(to);
    after = last();
    if (after.seq == before.seq || after.pid != getpid() ||
        after.priority != to)
      yielded = 0;
  }
  check(yielded, "changing the priority yields");
  setpriority(0);

  // 3. Strict priority: one child per level, all doing the same work,
  // started lowest first. They must finish highest first.
  pipe(fd);
  for (i = LOWEST; i >= 0; i--) {
    if (fork() == 0) {
      char c = '0' + i;
      setpriority(i);
      work(unit);
      write(fd[1], &c, 1);
      exit(0);
    }
  }
  int inorder = 1;
  printf("finish order:");
  for (i = 0; i < NB_PRIORITY_LEVELS; i++) {
    char c;
    read(fd[0], &c, 1);
    printf(" %c", c);
    if (c != '0' + i)
      inorder = 0;
  }
  printf("\n");
  for (i = 0; i < NB_PRIORITY_LEVELS; i++)
    wait(0);
  check(inorder, "higher priority finishes first");
  close(fd[0]);
  close(fd[1]);

  // 4. Round-robin: two children at the same level doing equal work
  // take turns, so they finish close together and the log shows the
  // CPU passing back and forth between them.
  int kid[2], done[2];
  pipe(fd);
  before = last();
  t0 = uptime();
  for (i = 0; i < 2; i++) {
    kid[i] = fork();
    if (kid[i] == 0) {
      setpriority(DEFAULT_PRIORITY);
      work(unit * 3);
      int t = uptime();
      write(fd[1], &t, sizeof(t));
      exit(0);
    }
  }
  read(fd[0], &done[0], sizeof(done[0]));
  read(fd[0], &done[1], sizeof(done[1]));
  wait(0);
  wait(0);
  n = schedlog(recs, SCHEDLOG_NREC);
  int handoffs = 0, prev = 0;
  for (i = 0; i < n; i++) {
    if (recs[i].seq <= before.seq || recs[i].priority != DEFAULT_PRIORITY)
      continue;
    if (recs[i].pid != kid[0] && recs[i].pid != kid[1])
      continue;
    if (prev != 0 && recs[i].pid != prev)
      handoffs++;
    prev = recs[i].pid;
  }
  printf("round-robin: finished at +%d and +%d ticks, %d handoffs\n",
         done[0] - t0, done[1] - t0, handoffs);
  check(done[1] - done[0] <= (done[1] - t0) / 4,
        "equal-priority processes finish close together");
  check(handoffs >= 4, "the log shows them taking turns");
  close(fd[0]);
  close(fd[1]);

  // 5. A lower-priority process gets no CPU while a higher-priority
  // one stays runnable: the child lowers itself, then this process
  // spins for 10 ticks without blocking.
  pipe(fd);
  t0 = uptime();
  pid = fork();
  if (pid == 0) {
    setpriority(LOWEST);
    int t = uptime();
    write(fd[1], &t, sizeof(t));
    exit(0);
  }
  while (uptime() - t0 < 10)
    ;
  int tend = uptime();
  after = last();
  n = schedlog(recs, SCHEDLOG_NREC);
  int ran_low = 0;
  for (i = 0; i < n; i++)
    if (recs[i].pid == pid && recs[i].priority == LOWEST &&
        recs[i].seq <= after.seq)
      ran_low++;
  int tchild;
  read(fd[0], &tchild, sizeof(tchild));
  wait(0);
  printf("starvation: child resumed at +%d, parent spun until +%d\n",
         tchild - t0, tend - t0);
  check(tchild >= tend, "low priority waits while high priority is busy");
  check(ran_low == 0, "the log shows no low-priority dispatch meanwhile");
  close(fd[0]);
  close(fd[1]);

  // 6. Kill a low-priority process that is asleep.
  pid = fork();
  if (pid == 0) {
    setpriority(LOWEST);
    pause(100000);
    exit(0);
  }
  pause(3);
  kill(pid);
  check(wait(&st) == pid && st == -1, "kill a sleeping low-priority process");

  // 7. Kill a low-priority process that is runnable but not running.
  pid = fork();
  if (pid == 0) {
    setpriority(LOWEST);
    for (;;)
      ;
  }
  work(unit);
  kill(pid);
  check(wait(&st) == pid && st == -1, "kill a runnable low-priority process");

  // 8. A sleeping high-priority process takes the CPU back from a
  // low-priority spinner as soon as it wakes up.
  pid = fork();
  if (pid == 0) {
    setpriority(LOWEST);
    for (;;)
      ;
  }
  t0 = uptime();
  pause(5);
  int woke = uptime() - t0;
  kill(pid);
  wait(0);
  printf("wakeup: pause(5) returned after %d ticks\n", woke);
  check(woke >= 5 && woke <= 7, "woken high priority preempts low priority");

  // 9. Slot reuse: many short-lived children that each pick a
  // different priority. A new process must start with its parent's
  // priority, not with whatever the slot's last owner had.
  int ok = 1;
  for (i = 0; i < 300; i++) {
    pid = fork();
    if (pid < 0) {
      ok = 0;
      break;
    }
    if (pid == 0) {
      if (getpriority() != 0)
        exit(1);
      setpriority(i % NB_PRIORITY_LEVELS);
      exit(getpriority() == i % NB_PRIORITY_LEVELS ? 0 : 1);
    }
    if (i % 4 == 3) {
      for (int k = 0; k < 4; k++) {
        wait(&st);
        if (st != 0)
          ok = 0;
      }
    }
  }
  check(ok, "300 forks at mixed priorities");

  // 10. Fill the process table at the lowest priority, then reap.
  setpriority(LOWEST);
  int made = 0;
  for (;;) {
    pid = fork();
    if (pid < 0)
      break;
    if (pid == 0)
      exit(0);
    made++;
  }
  int reaped = 0;
  while (wait(0) > 0)
    reaped++;
  printf("table: forked %d, reaped %d\n", made, reaped);
  check(made > 0 && made == reaped, "fork until the table is full, reap all");

  setpriority(DEFAULT_PRIORITY);
  printf("schedtest: %s (%d failures)\n", fails ? "FAILED" : "ALL PASSED",
         fails);
  exit(fails);
}
