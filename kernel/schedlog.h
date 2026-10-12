#define SCHEDLOG_NREC 1024 // dispatches remembered; older ones are overwritten

struct schedrec {
  uint seq;     // dispatch number, counting from 0 at boot
  uint tick;    // clock tick at which the process was dispatched
  int pid;      // process that was given the CPU
  int priority; // its priority at that moment
};
