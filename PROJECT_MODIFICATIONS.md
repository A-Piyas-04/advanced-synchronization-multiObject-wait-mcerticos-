# Project Modifications

## Summary
Implemented an advanced synchronization mechanism "Wait for Multiple Objects" (Waitset) similar to Linux epoll. This feature allows a process to wait for multiple event sources (IPC, Signals) simultaneously.

## Modified Files

### `kern/lib/syscall.h`
- Added system call numbers: `SYS_waitset_create`, `SYS_waitset_ctl`, `SYS_waitset_wait`.

### `kern/trap/TDispatch/import.h`
- Added function prototypes for the new system calls.

### `kern/trap/TDispatch/TDispatch.c`
- Added dispatch logic for the new system calls in `syscall_dispatch`.

### `kern/trap/TSyscall/TSyscall.c`
- Implemented `sys_waitset_create`, `sys_waitset_ctl`, `sys_waitset_wait`.
- Added hooks in `sys_sync_send` and `sys_kill` to notify waitsets when events occur.

### `kern/Makefile.inc`
- Included `kern/sync/Makefile.inc` to build the new synchronization module.

### `kern/init/init.c`
- Added call to `waitset_init()` during kernel initialization.

### `user/include/syscall.h`
- Added user-level system call stubs (inline assembly) for waitset operations.
- Added definition of `struct wait_event` and event/source type constants.
- Added missing stubs for `sys_kill`, `sys_sigaction`, `sys_pause`.

## Added Files

### `kern/sync/waitset.h`
- Header file defining `struct waitset`, `struct notif_source`, and kernel API for waitset management.

### `kern/sync/waitset.c`
- Implementation of the waitset mechanism.
- Manages a pool of waitsets and notification sources.
- Implements `waitset_wait` using existing `thread_sleep`/`thread_wakeup`.
- Implements `waitset_notify` to trigger events.

### `kern/sync/Makefile.inc`
- Makefile for the `kern/sync` directory.

### `user/waitset_demo.c`
- User-level application demonstrating the usage of waitset API.
- Registers for Signal and IPC events and waits for them.
