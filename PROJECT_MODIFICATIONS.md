# Project Modifications

## Summary
The project now includes a kernel waitset mechanism for "wait for multiple objects" style synchronization. A process can:
- create a waitset,
- register signal and IPC sources,
- block or poll for ready events.

## Kernel Changes

### Syscall numbers and dispatch
- `kern/lib/syscall.h`
  - Added: `SYS_waitset_create`, `SYS_waitset_ctl`, `SYS_waitset_wait`.
- `kern/trap/TDispatch/import.h`
  - Added syscall handler prototypes for waitset APIs.
- `kern/trap/TDispatch/TDispatch.c`
  - Added dispatch cases for the three waitset syscalls.

### Waitset core module
- `kern/sync/waitset.h`
  - Defines waitset API and data structures:
    - `struct waitset`
    - `struct notif_source`
    - `struct wait_event`
  - Defines limits and constants:
    - `MAX_WAITSETS = 64`
    - `MAX_SOURCES_PER_WAITSET = 32`
    - source type constants (`WS_SOURCE_IPC`, `WS_SOURCE_SIGNAL`)
    - event constants (`WS_EVENT_*`)
    - ctl operations (`WS_CTL_ADD`, `WS_CTL_DEL`, `WS_CTL_MOD`)
- `kern/sync/waitset.c`
  - Implements static pools:
    - waitset pool (`MAX_WAITSETS`)
    - source pool (`MAX_WAITSETS * MAX_SOURCES_PER_WAITSET`)
  - Implements:
    - `waitset_init()`
    - `waitset_create()`
    - `waitset_ctl()`
    - `waitset_wait()`
    - `waitset_notify()` and wrappers for IPC/signal
  - Uses `thread_sleep`/`thread_wakeup` for blocking wakeup integration.
- `kern/sync/Makefile.inc`
  - Adds `kern/sync/waitset.c` into kernel build.
- `kern/Makefile.inc`
  - Includes `kern/sync/Makefile.inc`.

### Syscall-side waitset integration
- `kern/trap/TSyscall/TSyscall.c`
  - Added `sys_waitset_create`, `sys_waitset_ctl`, `sys_waitset_wait`.
  - Added event notification hooks:
    - `sys_sync_send` -> `waitset_notify_ipc(recv_pid, cur_pid)`
    - `sys_kill` -> `waitset_notify_signal(pid, signum)`
  - `sys_waitset_wait` details:
    - caps returned events to 16 per syscall (`k_events[16]`)
    - supports polling when `timeout == 0`
    - treats non-zero timeout as blocking wait (no timed wakeup logic)
    - copies event array back to user via `pt_copyout`.

### Boot initialization
- `kern/init/init.c`
  - Calls `waitset_init()` during `kern_init()`.

## User-Space Changes

### User syscall API
- `user/include/syscall.h`
  - Added waitset constants and `struct wait_event`.
  - Added syscall wrappers:
    - `sys_waitset_create()`
    - `sys_waitset_ctl()`
    - `sys_waitset_wait()`
  - Also includes signal wrappers used by demo (`sys_kill`, `sys_sigaction`, `sys_pause`).

### Demo and build plumbing
- `user/waitset_demo.c`
  - Demo program that:
    - creates a waitset,
    - registers `SIGUSR1`, `SIGUSR2`, and IPC source PID `2`,
    - performs poll wait (`timeout = 0`),
    - triggers signals via `sys_kill`,
    - performs blocking wait (`timeout = -1`).
- `user/Makefile.inc`
  - Adds compilation/link rules for `waitset_demo`.
  - Includes demo binary in `KERN_BINFILES`.
- `kern/trap/TSyscall/TSyscall.c` (`sys_spawn`)
  - Adds `elf_id == 6` mapping to `waitset_demo`.
- `user/shell/shell.c`
  - Help text for `spawn` includes `6=waitset_demo`.

## Current Behavioral Notes
- Ownership enforced: only the creator PID can control or wait on a waitset.
- One waiting thread slot per waitset (`waiting_thread` field).
- Trigger deduplication: a source is queued once until consumed.
- Source matching supports wildcard source id `-1` in kernel matching logic.
- Requested event mask is stored but not filtered during notify (`waitset_notify` matches type/id and marks source triggered).
- No explicit waitset destroy syscall is present.
