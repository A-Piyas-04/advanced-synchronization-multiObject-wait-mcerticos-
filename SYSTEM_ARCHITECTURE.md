# System Architecture: Waitset

## Overview
The "Waitset" mechanism provides an advanced synchronization primitive similar to Linux `epoll` or BSD `kqueue`. It allows a process to create a kernel object (waitset), register multiple notification sources (IPC, Signals), and wait for any of these sources to become ready. This enables efficient event-driven programming and avoids busy polling.

## Data Structures

### `struct waitset`
The core object representing a collection of event sources.
- **sources**: A linked list of registered `struct notif_source`.
- **triggered**: A linked list of `struct notif_source` that have triggered an event.
- **waiting_thread**: The PID of the thread currently waiting on this waitset.
- **lock**: A spinlock protecting the waitset.
- **owner_pid**: The PID of the process that owns this waitset.

### `struct notif_source`
Represents a specific event source (e.g., "IPC from PID 5" or "Signal SIGUSR1").
- **type**: The type of source (IPC, Signal).
- **id**: The identifier (PID, Signal Number).
- **events**: The events to monitor (Read, Write, Signal, IPC).
- **triggered**: A flag indicating if this source has triggered.
- **ws**: Pointer back to the waitset.

### `struct wait_event`
A structure used to return triggered events to the user.
- **source_type**: The type of source that triggered.
- **source_id**: The identifier of the source.
- **events**: The events that occurred.
- **data**: User-defined data associated with the source (optional).

## Event Flow

1.  **Registration**:
    -   User calls `sys_waitset_create()` to allocate a waitset.
    -   User calls `sys_waitset_ctl(wsid, WS_CTL_ADD, type, id, events)` to register a source.
    -   The kernel allocates a `notif_source` and adds it to the waitset's `sources` list.

2.  **Notification (Triggering)**:
    -   When an event occurs in the kernel (e.g., `sys_sync_send` or `sys_kill`), the kernel calls `waitset_notify(target_pid, type, id, event)`.
    -   `waitset_notify` scans active waitsets belonging to `target_pid`.
    -   It checks if any source matches the event type and ID.
    -   If a match is found, the source is marked as triggered and moved to the `triggered` list.
    -   If a thread is waiting on the waitset, `thread_wakeup(ws)` is called.

3.  **Waiting**:
    -   User calls `sys_waitset_wait(wsid, events, maxevents, timeout)`.
    -   The kernel checks the `triggered` list.
    -   If the list is empty, the calling thread sets itself as `waiting_thread` and calls `thread_sleep(ws, &ws->lock)`.
    -   The thread sleeps until woken up by `waitset_notify` or timeout (if implemented).
    -   Upon wakeup, the thread dequeues triggered sources from the `triggered` list, copies event information to the user buffer, and clears the `triggered` flag (Edge-Triggered behavior).

## Interaction with Scheduler
The waitset mechanism integrates with the existing mCertiKOS scheduler using `thread_sleep` and `thread_wakeup`.
-   **Blocking**: `waitset_wait` uses `thread_sleep` to block the current thread, setting its state to `TSTATE_SLEEP` and yielding the CPU.
-   **Wakeup**: `waitset_notify` uses `thread_wakeup` to wake up the waiting thread, setting its state to `TSTATE_READY` and adding it to the ready queue.

## Interaction with Syscall Interface
New system calls are added to the syscall table and dispatched via `syscall_dispatch`.
-   `sys_waitset_create`: Allocates a new waitset.
-   `sys_waitset_ctl`: Modifies the waitset configuration.
-   `sys_waitset_wait`: Blocks until events occur and returns them.

## Code Integration
-   **IPC**: `sys_sync_send` calls `waitset_notify_ipc` to notify the receiver.
-   **Signals**: `sys_kill` calls `waitset_notify_signal` to notify the target process.
-   **Initialization**: `waitset_init` is called during kernel initialization to set up the waitset pool.
