// Dump the kernel's scheduling log, oldest dispatch first.
//   schedlog          print to the console
//   schedlog FILE     write to FILE instead

#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "kernel/schedlog.h"
#include "user/user.h"

static struct schedrec recs[SCHEDLOG_NREC];

// Output is gathered here and written in large pieces: fprintf()
// issues one write() per character, which is very slow on a file.
static char out[1024];
static int nout;
static int outfd = 1;

static void
flush(void)
{
  if (nout > 0 && write(outfd, out, nout) != nout) {
    fprintf(2, "schedlog: write error\n");
    exit(1);
  }
  nout = 0;
}

static void
putch(char c)
{
  if (nout == sizeof(out))
    flush();
  out[nout++] = c;
}

static void
putstr(char *s)
{
  while (*s)
    putch(*s++);
}

// Append x in decimal, followed by the character end.
static void
putnum(int x, char end)
{
  char digits[12];
  int i = 0;
  uint u = x;

  if (x < 0) {
    putch('-');
    u = -x;
  }
  do {
    digits[i++] = '0' + u % 10;
    u /= 10;
  } while (u > 0);
  while (i > 0)
    putch(digits[--i]);
  putch(end);
}

int
main(int argc, char **argv)
{
  int n, i;

  if (argc > 2) {
    fprintf(2, "usage: schedlog [file]\n");
    exit(1);
  }

  // Read the log before opening the file, so that the dispatches
  // caused by the file system work below are not part of the dump.
  n = schedlog(recs, SCHEDLOG_NREC);
  if (n < 0) {
    fprintf(2, "schedlog: cannot read the log\n");
    exit(1);
  }

  if (argc == 2) {
    outfd = open(argv[1], O_CREATE | O_WRONLY | O_TRUNC);
    if (outfd < 0) {
      fprintf(2, "schedlog: cannot open %s\n", argv[1]);
      exit(1);
    }
  }

  putstr("seq tick pid priority\n");
  for (i = 0; i < n; i++) {
    putnum(recs[i].seq, ' ');
    putnum(recs[i].tick, ' ');
    putnum(recs[i].pid, ' ');
    putnum(recs[i].priority, '\n');
  }
  flush();

  exit(0);
}
