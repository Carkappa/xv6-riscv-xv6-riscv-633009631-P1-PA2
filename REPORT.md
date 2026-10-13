CSCE 410 - P2
Justin Du
633009631
Submission information
https://github.com/Carkappa/xv6-riscv-xv6-riscv-633009631-P1-PA2
No
Resources used
I used Claude Opus 5.5 to help me go through the assignment, correct my understanding of the assignment itself, break down my decision to steps that I can do, and work on the features before and after I review it. It was really helpful, I would estimate about 5 - 6 hours that it helped me save time. 
Queue design and implementation
I put down the NB_PRIORITY_LEVELS in the kernal/param.h where the compiler or the program itself keeps the compile time limits, and both the kernel and the user test programs are included. In terms of the queue, one First in First out(FIFO) queue per priority level is implemented in the kernel/proc.c. Processes are linked together through the “next” field in struc proc. “Enqueue” adds to the tail of the queue, and the “dequeue_highest” will take the head of the highest priority non-empty queue. This will provide a round robin within a level.

There is synchronization included, one spinlock protects all of the queues, it will always take after p lock so the two locks cannot deadlock. As in terms of the Unit test, there is no separate unit test for the queue itself, instead, the kernel panics if a process is enqueued with a invalid priority, or if that the scheduler dequeues a process that is not runnable. 
Process Lifecycle
Here is the lifecycle of how an event is handled
A new process: priority is set to 1
Fork: the child copies the parent's priority and is enqueued. 
Scheduler picks a process: it is removed from its queue
Timer: it goes to the tail of tis queue
Wakeup or kill of a sleeping process: when it is enqueued.
Sleep and exit: it is in no queue
Slot freed: the priority and its link are cleared
Set priority: if any of the values changed, the process moves to a new level and gives up the CPU, if not then it will return without any yielding. 

Non Trivial part
The scheduler must remove a process from its queue before running it.
A reused slot must not keep the old priority, so it has to reset on allocation. 

Problem Encounted
Something that I had a problem with is when making the debug to 0 with an unused variable error left from P1 and another one would be when adding enqueue calls before changing the schedule would have put the process in a queue twice. 
Initial Test Coverage
The files and scripts that it has is making 
CPUS=1 make qemu, then ls, echo, and forktest.
CPUS=1 python3 test-xv6.py -q usertests
user/priotest.c, run as priotest

What priotest test is it will default the priority to 1, invalid values will return -1 and change nothing. Check every valid level can be set and read back. 

For each of the checks, it will print PASS or FAIL. All 13 checks passed and usertests printed ALL TESTS PASSED
Increased Test coverage
There is the problem that might have and how it is debugged and fixed:
A timer interrupted could disturb the “does not yield” check
The test now retires up to 5 times to have sure this doesn’t happen 
The test script timed out when run from the window drive under WSL.
Running it from the WSL home directory fixed it, this was not a scheduler error or bug

How I did it when testing normally when you run it, you don’t see which process the scheduler picks, so every time the scheduler picks a process, it now writes down three things in kernel memory. When typing schedlog in the xv6 shell it will print the record, and schedlog file saves it to a file
For example:
seq tick pid priority
55  94   2   1
56  94   4   1
This means that at time 94 the scheduler ran process 2, then process 4, both at priority 1. My tests read this record to check the scheduler's choice. 

User/schedtest.c checks
It will set the same priority again and does not make the process give up the CPU. Changing the priority does. 
With one busy process at each of the priority, they finish in order 0, 1, and then 2.
Two processes with the same priority take turns. 
A low priority does not run when a high one is running.
A low priority process can be killed, whether it is sleeping or waiting.
A high priority process that wake up runs right away, even if a low one is running
It will create 300 processes with different priority works 
Create processes until the system is full, then clean them all up. 
All 12 checks printed pass. 
