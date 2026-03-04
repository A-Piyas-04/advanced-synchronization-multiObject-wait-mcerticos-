# Project 8: Wait Object Implementation

## Problem Overview
The goal of this project is to implement a simplified epoll-like multi-wait synchronization system in the mCertiKOS kernel. This allows a user thread to wait for multiple notification sources (currently primarily timers) using a single wait object.

## Design Idea
We introduced a `wait_object` abstraction. A process can create a wait object, add event sources (like a timer) to it, and then wait on it. The wait operation blocks the thread until an event occurs (e.g., timer fires).

## Wait Object Structure
The `wait_object` structure is defined in `kern/sync/waitobj.h`:

```c
typedef struct wait_object {
    spinlock_t lock;
    int used;
    int owner;
    int ready;
    int nsources;
    wait_event_source_t sources[WAITOBJ_MAX_SOURCES];
} wait_object_t;
```

- `lock`: Protects the object.
- `used`: Allocation flag.
- `owner`: PID of the process that owns this object.
- `ready`: Flag indicating if an event has occurred (level-triggered).
- `sources`: Array of event sources (e.g., TIMER).

## System Call Flow
We implemented three new system calls:

1.  `sys_waitobj_create()`:
    - Calls `waitobj_create(owner)`.
    - Allocates a free `wait_object`.
    - Returns the Wait Object ID (WOID).

2.  `sys_waitobj_add(woid, type)`:
    - Calls `waitobj_add(woid, type)`.
    - Adds a source (e.g., `WAITOBJ_SRC_TIMER`) to the object.

3.  `sys_waitobj_wait(woid)`:
    - Calls `waitobj_wait(woid)`.
    - Checks if `ready` is set.
    - If not, calls `thread_sleep(wo, &wo->lock)` to block.
    - When woken up, clears `ready` and returns.

4.  `sys_exit()`:
    - Cleans up wait objects owned by the process.
    - Terminates the thread.

## Thread Sleep/Wakeup Mechanism
- **Sleep**: `waitobj_wait` uses `thread_sleep` to put the current thread in `TSTATE_SLEEP` and context switch to another thread. The `wait_object` pointer is used as the channel.
- **Wakeup**: `waitobj_notify_timer` scans all wait objects. If a timer source is present, it sets `ready = 1` and calls `thread_wakeup(wo)`. This wakes up any thread sleeping on this object.

## Timer Notification Mechanism
The `timer_intr_handler` in `kern/trap/TTrapHandler/TTrapHandler.c` calls `waitobj_notify_timer()` on every timer interrupt.
`waitobj_notify_timer` iterates through all wait objects. If a valid object has a `WAITOBJ_SRC_TIMER` source, it triggers the event (sets ready and wakes up).

## Module Interactions
- **User**: Calls `sys_waitobj_*`.
- **Trap Handler**: Dispatches syscalls; calls `waitobj_notify_timer` on timer IRQ.
- **Sync (WaitObj)**: Manages wait objects, logic for add/wait.
- **Thread**: Handles sleep/wakeup and context switching.
- **Process**: `sys_exit` ensures cleanup via `waitobj_cleanup_owner`.

## Test Programs
The primary test program is `user/wait_demo.c`, which demonstrates basic usage.

### Test 1: Basic Wait (Implemented in `wait_demo.c`)
Creates a wait object, adds a timer, and waits for events in a loop.
```c
int woid = sys_waitobj_create();
sys_waitobj_add(woid, WAITOBJ_SRC_TIMER);
sys_waitobj_wait(woid); // Blocks until timer interrupt
```

### Test 2: Polling Behavior
Verifies that if an event is already ready, wait returns immediately.
```c
void test_polling() {
    int woid = sys_waitobj_create();
    sys_waitobj_add(woid, WAITOBJ_SRC_TIMER);
    
    // Busy wait long enough for a timer tick to happen and set ready=1
    // (In a real test, we might yield or sleep briefly)
    int i;
    for(i=0; i<1000000; i++) asm volatile("nop"); 
    
    // Should return immediately because ready is already 1
    sys_waitobj_wait(woid); 
    printf("Polling test passed\n");
}
```

### Test 3: Multiple Sources
Verifies adding multiple sources (currently only TIMER is supported).
```c
void test_multiple() {
    int woid = sys_waitobj_create();
    // Add timer twice (or imagine another source)
    sys_waitobj_add(woid, WAITOBJ_SRC_TIMER);
    sys_waitobj_add(woid, WAITOBJ_SRC_TIMER);
    
    sys_waitobj_wait(woid);
    printf("Multiple source wake passed\n");
}
```

### Test 4: Resource Limits
Verifies that we cannot create more than `WAITOBJ_MAX` objects.
```c
void test_limits() {
    int i;
    for (i = 0; i < 70; i++) { // Try to create more than 64
        int w = sys_waitobj_create();
        if (w < 0) {
            printf("Limit reached at %d\n", i);
            break;
        }
    }
}
```

## Limitations
- Only supports `WAITOBJ_SRC_TIMER`.
- Timer resolution is tied to the system timer tick.
- "Ready" state is binary; doesn't indicate *which* source triggered if multiple sources were supported.
- Fixed number of wait objects (64) and sources (8).

## Implementation Details (Edited/Added Files)

The following files were modified or created to implement the project:

### 1. Kernel Synchronization (`kern/sync/`)
*   **`waitobj.h`**: Added `wait_object_t` structure, constants (`WAITOBJ_MAX`, `WAITOBJ_SRC_TIMER`), and function prototypes.
*   **`waitobj.c`**: Implemented the core logic:
    *   `waitobj_init`: Initializes the global wait object table.
    *   `waitobj_create`: Finds a free slot in the table and initializes it.
    *   `waitobj_add`: Adds a notification source to a wait object.
    *   `waitobj_wait`: Sleeps the thread if the object is not ready.
    *   `waitobj_notify_timer`: Scans all objects and wakes up threads waiting on timers.
    *   `waitobj_cleanup_owner`: Frees all wait objects owned by a specific process.

### 2. System Calls & Traps (`kern/trap/`)
*   **`TSyscall/TSyscall.c`**: Implemented the system call handlers:
    *   `sys_waitobj_create`: Wrapper for `waitobj_create`.
    *   `sys_waitobj_add`: Wrapper for `waitobj_add`.
    *   `sys_waitobj_wait`: Wrapper for `waitobj_wait`.
    *   `sys_exit`: New syscall that calls `waitobj_cleanup_owner` and `thread_exit`.
*   **`TDispatch/TDispatch.c`**: Added dispatch case for `SYS_exit`.
*   **`TTrapHandler/TTrapHandler.c`**: Modified `timer_intr_handler` to call `waitobj_notify_timer()`.
*   **`TSyscall/export.h`, `import.h`**: Updated to export/import necessary functions.

### 3. Thread Management (`kern/thread/`)
*   **`PThread/PThread.c`**: Added `thread_exit()` function to handle thread termination (sets state to `TSTATE_DEAD` and switches context).
*   **`PThread/export.h`**: Exported `thread_exit`.

### 4. User Space (`user/`)
*   **`include/syscall.h`**: Added inline assembly wrappers for `sys_waitobj_*` and `sys_exit`.
*   **`wait_demo/wait_demo.c`**: Created a test program that demonstrates creating a wait object, adding a timer source, and waiting for events.
*   **`shell/commands.c`**: Added `cmd_waitdemo` to launch the test program.
*   **`shell/shell.c`**: Registered the `waitdemo` command.

## Detailed Code Flow

### Creation Flow
1.  **User**: Calls `sys_waitobj_create()`.
2.  **Trap**: `int 48` triggers `trap()` -> `syscall_dispatch()`.
3.  **Kernel**: Calls `sys_waitobj_create()` in `TSyscall.c`.
4.  **WaitObj**: `waitobj_create()` locks the table, finds a free slot, marks it used, and returns the index (WOID).
5.  **Return**: WOID is returned to user space.

### Wait Flow
1.  **User**: Calls `sys_waitobj_wait(woid)`.
2.  **Trap**: `int 48` triggers `trap()` -> `syscall_dispatch()`.
3.  **Kernel**: Calls `sys_waitobj_wait()` in `TSyscall.c`.
4.  **WaitObj**: `waitobj_wait()` checks `wo->ready`.
    *   **If Ready**: Returns immediately.
    *   **If Not Ready**: Calls `thread_sleep(wo, &wo->lock)`.
        *   Sets thread state to `TSTATE_SLEEP`.
        *   Sets thread channel to `wo`.
        *   Calls `kctx_switch` to run the next thread.

### Notification Flow (Timer)
1.  **Hardware**: Timer interrupt triggers `trap()` with `T_IRQ0 + IRQ_TIMER`.
2.  **Trap**: Calls `timer_intr_handler()`.
3.  **Handler**: Calls `waitobj_notify_timer()`.
4.  **WaitObj**: Iterates over all wait objects.
    *   Checks if object has `WAITOBJ_SRC_TIMER`.
    *   If yes, sets `wo->ready = 1`.
    *   Calls `thread_wakeup(wo)`.
5.  **Scheduler**: `thread_wakeup` finds all threads sleeping on channel `wo`, sets them to `TSTATE_READY`, and enqueues them.
6.  **Resume**: Eventually the scheduler picks the thread, and it returns from `waitobj_wait`.

### Exit Flow
1.  **User**: Calls `sys_exit()` (or `wait_demo` finishes).
2.  **Trap**: `int 48` triggers `trap()` -> `syscall_dispatch()`.
3.  **Kernel**: Calls `sys_exit()` in `TSyscall.c`.
4.  **Cleanup**: Calls `waitobj_cleanup_owner(pid)` to free wait objects.
5.  **Termination**: Calls `thread_exit()` to mark thread as `TSTATE_DEAD` and switch away permanently.
