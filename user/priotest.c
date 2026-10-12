// Simple tests for setpriority() and getpriority().
// Each check prints PASS or FAIL; the exit status is the number of failures.

#include "kernel/types.h"
#include "kernel/param.h"
#include "user/user.h"

#define LOWEST (NB_PRIORITY_LEVELS - 1)

static int fails;

static void
check(int ok, char *what)
{
  printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok)
    fails++;
}

int
main(void)
{
  int pid, st;

  // The shell was never given a priority, so it and this program
  // still carry the default handed down from the first process.
  check(getpriority() == DEFAULT_PRIORITY, "default priority is 1");

  // Invalid values are rejected and change nothing.
  check(setpriority(NB_PRIORITY_LEVELS) == -1,
        "one past the last level is rejected");
  check(getpriority() == DEFAULT_PRIORITY, "priority unchanged after that");
  check(setpriority(-1) == -1, "negative value is rejected");
  check(getpriority() == DEFAULT_PRIORITY, "priority unchanged after that");
  check(setpriority(1000000) == -1, "huge value is rejected");
  check(getpriority() == DEFAULT_PRIORITY, "priority unchanged after that");

  // Every valid level can be set and read back.
  int roundtrip = 1;
  for (int i = 0; i < NB_PRIORITY_LEVELS; i++)
    if (setpriority(i) != 0 || getpriority() != i)
      roundtrip = 0;
  check(roundtrip, "every valid level can be set and read back");

  // Setting the current priority again succeeds.
  check(setpriority(LOWEST) == 0 && getpriority() == LOWEST,
        "setting the current priority again succeeds");

  // A child starts with its parent's priority.
  pid = fork();
  if (pid == 0)
    exit(getpriority());
  wait(&st);
  check(st == LOWEST, "child inherits the parent's priority");

  // So does a grandchild.
  pid = fork();
  if (pid == 0) {
    int gst;
    if (fork() == 0)
      exit(getpriority());
    wait(&gst);
    exit(gst);
  }
  wait(&st);
  check(st == LOWEST, "grandchild inherits it too");

  // A child changing its own priority does not affect the parent.
  pid = fork();
  if (pid == 0) {
    setpriority(0);
    exit(getpriority());
  }
  wait(&st);
  check(st == 0 && getpriority() == LOWEST,
        "child's change does not affect the parent");

  check(setpriority(DEFAULT_PRIORITY) == 0, "back to the default");

  printf("priotest: %s (%d failures)\n", fails ? "FAILED" : "ALL PASSED",
         fails);
  exit(fails);
}
