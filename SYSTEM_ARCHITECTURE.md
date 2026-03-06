# System Architecture: Waitset

## Overview
The waitset subsystem is a kernel synchronization layer that lets one process wait on multiple notification sources through a single blocking call.

Implemented sources:
- signal delivery (`WS_SOURCE_SIGNAL`)
- synchronous IPC arrival (`WS_SOURCE_IPC`)

Main entry points:
- `sys_waitset_create`
- `sys_waitset_ctl`
- `sys_waitset_wait`

## Core Components

### Global pools
The implementation is static-pool based:
- `waitset_pool[MAX_WAITSETS]` where `MAX_WAITSETS = 64`
- `source_pool[MAX_WAITSETS * MAX_SOURCES_PER_WAITSET]` where `MAX_SOURCES_PER_WAITSET = 32`
- a global free-list for `notif_source` objects protected by `source_pool_lock`

### `struct waitset`
Each waitset contains:
- `lock`: per-waitset spinlock
- `sources`: all registered sources
- `triggered`: ready sources pending delivery
- `waiting_thread`: PID currently blocked in wait, or `NUM_IDS`
- `owner_pid`: creator PID (ownership check)
- `active`: allocation state

### `struct notif_source`
Each source registration stores:
- `type`, `id`, `events`
- `triggered` dedup flag
- `data` (currently passed as `NULL` from syscall path)
- back-pointer to parent waitset

### `struct wait_event`
Returned to user space:
- `source_type`
- `source_id`
- `events`
- `data`

## Control Plane

### Create
`waitset_create()` scans `waitset_pool` for an inactive slot, marks it active, sets owner to `get_curid()`, and returns the waitset ID.

### Register / Modify / Delete
`waitset_ctl(wsid, op, type, id, events, data)`:
- validates ID and ownership
- `WS_CTL_ADD`: allocates source from global pool and inserts into `sources`
- `WS_CTL_MOD`: updates `events` and `data`
- `WS_CTL_DEL`: removes from `sources`; also removes from `triggered` if queued; frees source to global free-list

## Event Plane

### Producers
Events are injected from existing kernel paths:
- IPC: `sys_sync_send` -> `waitset_notify_ipc(recv_pid, send_pid)`
- Signal: `sys_kill` -> `waitset_notify_signal(pid, signum)`

### Matching and queueing
`waitset_notify(target_pid, type, id, event)`:
- scans active waitsets owned by `target_pid`
- matches source by `type` and `id` (`id == -1` is wildcard)
- if not already triggered, pushes source to `triggered` list and sets `triggered = 1`
- wakes blocked waiter via `thread_wakeup(ws)` when `waiting_thread != NUM_IDS`

Note: current matching uses source `type/id`; the `event` parameter is not used to filter by event mask.

## Wait Path

### Kernel wait behavior
`waitset_wait(wsid, events, maxevents, timeout)`:
- validates ID and ownership
- if `triggered` empty:
  - returns immediately when `timeout == 0` (poll mode)
  - otherwise sleeps on waitset channel (`thread_sleep(ws, &ws->lock)`) until notified
- dequeues up to `maxevents` triggered entries into output array
- clears each source `triggered` flag when consumed

### Syscall wrapper behavior
`sys_waitset_wait` in `TSyscall.c` adds:
- user pointer validation (`VM_USERLO <= events_uva < VM_USERHI`)
- local kernel buffer cap of 16 events (`struct wait_event k_events[16]`)
- copyout of returned events to user memory

Effective result: user `maxevents` is capped to 16 per syscall.

## Integration Points
- Initialization: `waitset_init()` is called from `kern_init()`.
- Dispatch: waitset syscalls are wired in `syscall_dispatch`.
- User ABI: wrappers and constants are exposed in `user/include/syscall.h`.
- Demo: `user/waitset_demo.c` exercises poll wait, signal-triggered wake, and blocking wait.

## Current Constraints
- No waitset destroy syscall.
- Single recorded waiter (`waiting_thread`) per waitset.
- Non-zero timeout does not implement timed expiration; it blocks until wakeup.
