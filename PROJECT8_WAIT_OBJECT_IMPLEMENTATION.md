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
