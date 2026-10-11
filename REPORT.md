# CSCE 410 — Programming Assignment P1: Logging Functionality

**Name:** Jun Du
**Date:** September 21, 2026

---

## 1. Submission Information

**a.** https://github.com/Carkappa/xv6-riscv-xv6-riscv-633009631-PA1

**b.** No late-day tokens used.

---

## 2. Resources Used

> **TO FILL IN.** This asks about your own process. The question names
> generative AI tools explicitly — if you used one, list it here.

**a.** *(resources)*

**b.** *(how they helped)*

**c.** *(time saved)*

**d.** *(hours to redo without them)*

---

## 3. Source Code Added or Modified

**Added:** `kernel/debug.h` (categories, levels, `dprintf`, `DBGCTL_*` ops, shared
name tables), `kernel/debug.c` (globals and prefix helpers), `user/dbg.c` (Part C
tool), `tools/drive.py` (test harness, not part of the build).

**Modified:** `Makefile` (`debug.o`, `_dbg`, `DEBUG` switch), `kernel/syscall.h`
(`SYS_debugctl 23`), `kernel/syscall.c` (extern + table entry), `kernel/sysproc.c`
(`sys_debugctl`), `kernel/proc.c` (instrumentation; `reparent()` now returns a
count), `kernel/exec.c` (instrumentation), `user/user.h` (prototype),
`user/usys.pl` (stub entry).

Builds clean with `-Wall -Werror` at both `make` and `make DEBUG=0`.

---

## 4. Transcripts

In `demo/`, recorded with `script -c "make qemu" demo/DemoN.txt`.

| File | Scenario | Result |
|---|---|---|
| `Demo1.txt` | Boot at mask 0 | `ls` and `echo hi` run with zero debug output. |
| `Demo2.txt` | `dbg on proc`, `ls`, `echo hi` | 15 lifecycle records; parent and child pids agree. |
| `Demo3.txt` | `forktest` | 62 processes created and freed. No leaks, no double-frees. |
| `Demo4.txt` | Workload, `dbg off all`, same workload | 18 records, then silence. |
| `Demo5.txt` | `dbg only sched`, `dbg level trace` | 11 context-switch records from one command. |
| `Demo6.txt` | `usertests` at mask 0 | Suite passes; no `[proc` or `[sched` output. |

**Scenario 2 — pids line up.** The shell (pid 2) allocates, forks, reaps and
frees; the child (pid 4) execs and exits:

```
[proc   ][ 2][kfork] child pid 4 created, sz 20480
[proc   ][ 4][kexec] exec ls, 1 args, sz 20480
[proc   ][ 4][kexit] exiting, status 0, 0 children reparented to init
[proc   ][ 2][kwait] reaped pid 4, status 0
[proc   ][ 2][freeproc] freeing slot of pid 4 (ls)
```

**Scenario 3 — no leaks or double-frees.** 62 allocations, 63 frees, 63 exits,
63 reaps. Nothing allocated went unfreed; nothing was freed twice. The extra
free is pid 3 — the `dbg on proc` process itself, allocated before the mask was
set, so only its teardown is logged. The one `no free proc slot` warning is
`forktest` deliberately exhausting the process table.

**Scenario 6 — expected fault messages.** `usertests` contains many
`usertrap(): unexpected scause` lines. These come from stock xv6 and are
expected: `kernmem`, `MAXVAplus`, `nowrite`, `stacktest`, `sbrkbugs` and
`lazy_unmap` deliberately trigger faults to check the kernel rejects them. Each
block ends in `OK`.

---

## 5. Design Comments

**Macro.** `dprintf(cat, level, fmt, ...)` keeps its arguments inside the `if`,
so they are evaluated only when a message prints. Under `make DEBUG=0` it
expands to an empty `do { } while (0)` — no branch, no mask load, nothing.

**Prefix.** `[category][pid][function]`, padded to fixed width so
`grep '\[proc' log.txt` returns exactly the process lifecycle. The pid comes
from `myproc()` and prints `-` when no process is running. `printk` here has no
field widths, so the helpers pad manually.

**Defaults.** Mask 0 (silent boot, zero cost), level `DBG_INFO` so that
`dbg on proc` alone produces output without a second `dbg level` command.

**System call.** One multiplexed `uint64 debugctl(int op, uint64 arg)` rather
than separate get/set calls: it uses one syscall number and can gain new
operations (such as a pid filter) without re-touching five files. Each operation
returns the previous value. An invalid op returns `-1` with no side effect; an
out-of-range level is rejected rather than clamped, so a failure cannot look
like a success. Known wart: `DBG_ALL` is `~0UL`, identical to the `-1` error
return, so those two cases are indistinguishable — a cleaner design would return
a status and pass the old value by pointer.

**Tool.** `dbg` takes category names from `DBG_CATNAMES` in the shared header,
so names and bits cannot drift. It parses all arguments before issuing any
syscall, so `dbg on proc bogus` leaves the kernel untouched instead of applying
half the request. Bad verbs, bad categories and bad levels are all rejected with
usage; none reaches the kernel.

**Levels.** `ERR` for allocation failures (`kalloc`, page table, `uvmcopy`,
`exec`); `WARN` for resource exhaustion and lookup misses (proc table full,
`kill` on a missing pid); `INFO` for normal lifecycle; `TRACE` for context
switches. `INFO` shows the shape of activity, `TRACE` shows everything.

**Placement.** `freeproc()` prints first, before teardown clears `p->pid`.
`kwait()` prints before `freeproc()` so `xstate` is intact. `scheduler()` uses
the loop variable, since `myproc()` is null there — which is why those lines
show `-` as the prefix pid.

**Changes to existing code.** `reparent()` went from `void` to `int` so `kexit()`
can report how many children moved to init; it is file-local with one caller.
This tree renames the kernel entry points, so `__func__` prints `kfork`,
`kexit`, `kexec` rather than `fork`, `exit`, `exec`.

**Making the scheduler message usable (scenario 5).** One `echo hi` produces
eleven records, six of them repeating pid 5, each written synchronously through
`uartputc_sync`. Options, in increasing order of effort: log only when the
chosen pid changes; filter by pid; buffer records in memory and drain them on
demand instead of writing to the UART inline; or sample every Nth switch. The
facility already provides the crudest control — the message is in its own
category at `TRACE`, so it is off unless asked for.

---

## 6. Concurrency

**No synchronization on `debug_mask` or `debug_level`.**

Both are single naturally aligned words on RV64, so loads and stores are
indivisible and no reader can see a torn value. Nothing in the kernel depends on
the mask for correctness — it only decides whether a line prints — so a reader
racing a writer sees either the old or new value and both are fine; the worst
case is one mis-filtered log line. A lock would be actively harmful: the mask is
read on nearly every instrumented path including `scheduler()`, and a spinlock
there would add an atomic and an interrupt disable even when logging is off,
which is the overhead the design is meant to avoid. Message interleaving is
already handled by the console lock inside `printk`.

Two accepted races: mask and level are separate words, so a mask change followed
by a level change can be observed half-applied (one mis-filtered line); and
`dbg on`/`off` is a user-space read-modify-write across two syscalls, so
concurrent `dbg` invocations could lose an update — acceptable for an
interactive tool, and fixable with kernel-side OR/AND operations if needed.

Some `dprintf` calls run with `p->lock` held (`allocproc`, `kexit`,
`scheduler`). That is safe: `printk` is a leaf, takes only the console lock, and
never sleeps. No `dprintf` was placed in `printk`, `consputc` or
`uartputc_sync`, which would recurse without bound.

---

## 7. Bonus Points

**a.** *(TO FILL IN — did you submit the P1 survey before Sept 13? Yes / No)*

**b.** No. Neither bonus extension was implemented. The operation-code interface
in §5 would accommodate a pid filter without changing the syscall signature.
