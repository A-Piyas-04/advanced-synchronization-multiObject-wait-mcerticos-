# Advanced Synchronization: Wait for Multiple Objects in mCertiKOS

## 1. Project Overview

This project extends the educational mCertiKOS kernel with a simple, kernel-level “wait for multiple objects” mechanism.

The goal is to let a user thread wait on one logical **wait object** that can be associated with multiple **event sources** (e.g., a periodic timer). The design is intentionally lightweight and reuses the existing threading and interrupt infrastructure:

- Threads still sleep and wake using the existing `thread_sleep` / `thread_wakeup` primitives.
- A new `wait_object` abstraction multiplexes multiple event sources into a single sleep channel.
- A small set of system calls expose this to user space:
  - `SYS_waitobj_create`
  - `SYS_waitobj_add`
  - `SYS_waitobj_wait`
- A demo program `wait_demo` shows how a user process can block on timer-driven events.

All changes integrate cleanly with the existing mCertiKOS template, without changing the core scheduler or file system logic.

---

## 2. Problem Statement

The original mCertiKOS template allows a thread to sleep on a single channel (`void *chan`) using:

- `thread_sleep(void *chan, spinlock_t *lk)`
- `thread_wakeup(void *chan)`

This is sufficient for simple cases (e.g., sleeping on a single buffer or lock), but it does not support a higher-level abstraction similar to `select`, `poll`, `epoll`, or `kqueue`, where a single blocking call can wait on *multiple* heterogeneous events.

We want to provide:

- A **single syscall interface** that lets a user thread:
  - Create a wait object.
  - Attach one or more event sources to it.
  - Block until **any** of those sources fire.
- Integration with an existing device interrupt (the timer), to demonstrate end-to-end interrupt-driven wakeup.
- A **simple and educational** design:
  - One wait object is used by one thread at a time.
  - Only one event source type is implemented (timer), but the design is easily extensible.
  - No file descriptor integration and no epoll-scale optimizations.

---

## 3. High-Level Architecture Diagram (ASCII)

```text
          +-------------------------+
          |       User Space        |
          |-------------------------|
          | wait_demo               |
          |  - sys_waitobj_create   |
          |  - sys_waitobj_add      |
          |  - sys_waitobj_wait     |
          +-------------+-----------+
                        |
                        | int 48 (T_SYSCALL)
                        v
          +-------------------------+
          |   Syscall Dispatch      |
          | (kern/trap/TDispatch)   |
          +-------------------------+
          |  SYS_waitobj_create --> sys_waitobj_create
          |  SYS_waitobj_add    --> sys_waitobj_add
          |  SYS_waitobj_wait   --> sys_waitobj_wait
          +-------------+-----------+
                        |
                        v
          +-------------------------+
          |  Wait Object Subsystem  |
          | (kern/sync/waitobj.c)   |
          +-------------------------+
          | waitobj_create          |
          | waitobj_add             |
          | waitobj_wait            |
          | waitobj_notify_timer    |
          +-------------+-----------+
                        |
                        | uses thread_sleep / thread_wakeup
                        v
          +-------------------------+
          |   Thread Scheduler      |
          | (kern/thread/PThread)   |
          +-------------------------+
                        ^
                        |
                        | timer interrupt
                        |
          +-------------+-----------+
          |  Timer & Trap Handling  |
          | kern/dev/timer.c        |
          | kern/dev/lapic.c        |
          | kern/trap/TTrapHandler  |
          +-------------------------+
          | timer_intr_handler      |
          |  -> sched_update        |
          |  -> waitobj_notify_timer|
          +-------------------------+
```

---

## 4. Module Interconnection Explanation

1. **User-level API (wrapper functions)**
   - Defined in:
     - `user/include/syscall.h`
   - Functions:
     - `sys_waitobj_create(void)`
     - `sys_waitobj_add(int woid, int type)`
     - `sys_waitobj_wait(int woid)`
   - These wrappers marshal arguments into registers and trigger `int T_SYSCALL` (#48).

2. **Syscall dispatcher**
   - File:
     - `kern/trap/TDispatch/TDispatch.c`
   - Reads the syscall number (`SYS_*`) and routes to:
     - `sys_waitobj_create(tf_t *tf)`
     - `sys_waitobj_add(tf_t *tf)`
     - `sys_waitobj_wait(tf_t *tf)`

3. **Syscall handlers**
   - File:
     - `kern/trap/TSyscall/TSyscall.c`
   - Implement kernel-visible semantics:
     - Translate arguments from the trap frame.
     - Call into the wait object subsystem (`waitobj_*` functions).
     - Set error numbers (`E_SUCC` / `E_INVAL_ID`) and return values.

4. **Wait object subsystem**
   - Files:
     - `kern/sync/waitobj.h`
     - `kern/sync/waitobj.c`
   - Maintains a small global table of wait objects:
     - Allocates and initializes wait objects.
     - Attaches event sources to a wait object.
     - Puts the calling thread to sleep until the wait object becomes ready.
     - Provides `waitobj_notify_timer()` to be called from the timer interrupt handler.

5. **Timer interrupt and trap handler**
   - Files:
     - `kern/dev/timer.c` (hardware PIT setup)
     - `kern/dev/lapic.c` (LAPIC timer)
     - `kern/trap/TTrapHandler/TTrapHandler.c` (top-level interrupt handler)
   - The timer interrupt handler:
     - Acknowledges the interrupt (EOI).
     - Advances scheduler ticks and performs preemption via `sched_update()`.
     - Calls `waitobj_notify_timer()` to mark timer-based wait objects as ready and wake sleeping threads.

6. **Threading and scheduling**
   - Files:
     - `kern/thread/PThread/PThread.c`
   - Provides:
     - `thread_sleep(void *chan, spinlock_t *lk)`
     - `thread_wakeup(void *chan)`
   - Wait objects are implemented by using the wait object pointer as the sleep channel (`chan`).

7. **Device and kernel initialization**
   - File:
     - `kern/dev/devinit.c`
   - Calls:
     - `waitobj_init()` during device initialization so that the wait object table is ready before any user process runs.

8. **Demo program and kernel-static ELF mapping**
   - Files:
     - `user/wait_demo/wait_demo.c`
     - `user/wait_demo/Makefile.inc`
     - `kern/trap/TSyscall/TSyscall.c` (for `_binary___obj_user_wait_demo_wait_demo_start` and `elf_id` mapping)
   - Allows `sys_spawn(6, quota)` to launch the demo process.

---

## 5. File-by-File Changes

Below are all files modified or added for this feature, with their exact paths.

### 5.1 Kernel build integration

- **File**: `kern/Makefile.inc`
  - **Change**: Added inclusion of the new `sync` subdirectory:
    - `include $(KERN_DIR)/sync/Makefile.inc`

- **File**: `kern/sync/Makefile.inc` (new)
  - **Role**: Builds the `waitobj.c` module.
  - **Key contents**:
    - Adds `$(KERN_DIR)/sync/waitobj.c` to `KERN_SRCFILES`.
    - Provides object rules for `KERN_OBJDIR)/sync/%.o`.

### 5.2 Wait object kernel module

- **File**: `kern/sync/waitobj.h` (new)
  - Declares:
    - `wait_event_source_t`
    - `wait_object_t`
    - `waitobj_init`
    - `waitobj_create`
    - `waitobj_add`
    - `waitobj_wait`
    - `waitobj_notify_timer`
    - `waitobj_cleanup_owner`

- **File**: `kern/sync/waitobj.c` (new)
  - Implements the wait object subsystem:
    - Global table of wait objects.
    - Creation, addition of sources, waiting, timer notification, and cleanup.

### 5.3 Device initialization

- **File**: `kern/dev/devinit.c`
  - **Change**: Included `kern/sync/waitobj.h` and called:
    - `waitobj_init();`
  - Location: after buffer cache and inode initialization.

### 5.4 Trap handler integration

- **File**: `kern/trap/TTrapHandler/TTrapHandler.c`
  - **Change**:
    - Includes `kern/sync/waitobj.h`.
    - Updates `timer_intr_handler` to:
      - Acknowledge interrupt via `intr_eoi()`.
      - Call `sched_update()`.
      - Call `waitobj_notify_timer()` after scheduling update.

### 5.5 Syscall numbering

- **File**: `kern/lib/syscall.h`
  - **Change**: Extended `enum __syscall_nr` with:
    - `SYS_waitobj_create`
    - `SYS_waitobj_add`
    - `SYS_waitobj_wait`

### 5.6 Syscall dispatch and handlers

- **File**: `kern/trap/TDispatch/import.h`
  - **Change**: Declared new syscall handlers:
    - `void sys_waitobj_create(tf_t *tf);`
    - `void sys_waitobj_add(tf_t *tf);`
    - `void sys_waitobj_wait(tf_t *tf);`

- **File**: `kern/trap/TDispatch/TDispatch.c`
  - **Change**: Added new `case` labels:
    - `case SYS_waitobj_create: sys_waitobj_create(tf); break;`
    - `case SYS_waitobj_add:    sys_waitobj_add(tf);    break;`
    - `case SYS_waitobj_wait:   sys_waitobj_wait(tf);   break;`

- **File**: `kern/trap/TSyscall/TSyscall.c`
  - **Changes**:
    - Included `kern/sync/waitobj.h`.
    - Implemented handlers:
      - `sys_waitobj_create(tf_t *tf)`
      - `sys_waitobj_add(tf_t *tf)`
      - `sys_waitobj_wait(tf_t *tf)`
    - Extended the set of ELF binaries recognized by `sys_spawn`:
      - Declared `_binary___obj_user_wait_demo_wait_demo_start[]`.
      - Added `case 6: elf_addr = _binary___obj_user_wait_demo_wait_demo_start; break;`

### 5.7 User-space syscall wrappers

- **File**: `user/include/syscall.h`
  - **Changes**:
    - Added:
      - `#define WAITOBJ_SRC_TIMER 1`
    - Added user-level wrappers:
      - `sys_waitobj_create(void)`
      - `sys_waitobj_add(int woid, int type)`
      - `sys_waitobj_wait(int woid)`

### 5.8 User demo program and build integration

- **File**: `user/wait_demo/wait_demo.c` (new)
  - Implements a simple test program that:
    - Creates a wait object.
    - Adds a timer source to it.
    - Waits in a loop and prints a message each time the timer wakes it.

- **File**: `user/wait_demo/Makefile.inc` (new)
  - Builds:
    - `$(USER_OBJDIR)/wait_demo/wait_demo`
  - Adds the binary to:
    - `KERN_BINFILES`

- **File**: `user/Makefile.inc`
  - **Change**:
    - Includes `user/wait_demo/Makefile.inc`.
    - Extends `user:` target to depend on `wait_demo`.

---

## 6. New Data Structures

All new wait-object-related types are defined in `kern/sync/waitobj.h`.

### 6.1 Event source type

```c
typedef enum {
    WAITOBJ_SRC_NONE = 0,
    WAITOBJ_SRC_TIMER = 1
} waitobj_source_type_t;
```

### 6.2 Event source descriptor

```c
typedef struct wait_event_source {
    int type;
} wait_event_source_t;
```

- Currently only the `type` field is needed.
- In future, this can be extended with additional identifiers (e.g., fd, inode pointer, or device id).

### 6.3 Wait object descriptor

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

Fields:

- `lock`: Protects the wait object’s internal state (`ready`, `nsources`, `sources`).
- `used`: Marks that this table entry is currently in use.
- `owner`: Thread id (`pid`) of the owning thread.
- `ready`: Boolean flag indicating that at least one event source has fired.
- `nsources`: Number of active sources in `sources[]`.
- `sources[]`: A fixed-size array of the event sources associated with this wait object.

Global storage (in `kern/sync/waitobj.c`):

```c
static wait_object_t wait_table[WAITOBJ_MAX];
static spinlock_t wait_table_lock;
```

- `wait_table_lock` protects allocation and deallocation of table entries.
- Each `wait_object_t::lock` protects that object’s internal state.

Constants:

- `WAITOBJ_MAX` — maximum number of wait objects in the system.
- `WAITOBJ_MAX_SOURCES` — maximum number of event sources per wait object.

---

## 7. Syscall Flow Explanation

This section follows the full path of each new syscall, from user space to kernel and back.

### 7.1 waitobj_create

**User space:**

- `int w = sys_waitobj_create();`
- Wrapper (in `user/include/syscall.h`):
  - Puts `SYS_waitobj_create` into `EAX`.
  - Triggers `int T_SYSCALL`.
  - Returns `woid` (`>= 0`) or `-1` on error (based on `errno` in `EAX`).

**Dispatcher:**

- `kern/trap/TDispatch/TDispatch.c`:
  - Reads the syscall number.
  - On `SYS_waitobj_create`, calls `sys_waitobj_create(tf)`.

**Handler:**

- `kern/trap/TSyscall/TSyscall.c`:

  ```c
  void sys_waitobj_create(tf_t *tf)
  {
      unsigned int owner = get_curid();
      int woid = waitobj_create((int) owner);

      if (woid < 0) {
          syscall_set_errno(tf, E_INVAL_ID);
          syscall_set_retval1(tf, (unsigned int) -1);
      } else {
          syscall_set_errno(tf, E_SUCC);
          syscall_set_retval1(tf, (unsigned int) woid);
      }
  }
  ```

- `waitobj_create` allocates a free entry in `wait_table`, initializes it, and returns its index.

### 7.2 waitobj_add

**User space:**

- `sys_waitobj_add(w, WAITOBJ_SRC_TIMER);`

**Dispatcher:**

- On `SYS_waitobj_add`, calls `sys_waitobj_add(tf)`.

**Handler:**

- `kern/trap/TSyscall/TSyscall.c`:

  ```c
  void sys_waitobj_add(tf_t *tf)
  {
      int woid = (int) syscall_get_arg2(tf);
      int type = (int) syscall_get_arg3(tf);
      int ret = waitobj_add(woid, type);

      if (ret < 0) {
          syscall_set_errno(tf, E_INVAL_ID);
      } else {
          syscall_set_errno(tf, E_SUCC);
      }
  }
  ```

- `waitobj_add` ensures:
  - The wait object exists.
  - The caller owns it (`owner == get_curid()`).
  - The type is recognized (`WAITOBJ_SRC_TIMER`).
  - The source array is not full.

### 7.3 waitobj_wait

**User space:**

- `sys_waitobj_wait(w);`
- Blocks until an event source attached to `w` fires.

**Dispatcher:**

- On `SYS_waitobj_wait`, calls `sys_waitobj_wait(tf)`.

**Handler:**

- `kern/trap/TSyscall/TSyscall.c`:

  ```c
  void sys_waitobj_wait(tf_t *tf)
  {
      int woid = (int) syscall_get_arg2(tf);
      int ret = waitobj_wait(woid);

      if (ret < 0) {
          syscall_set_errno(tf, E_INVAL_ID);
      } else {
          syscall_set_errno(tf, E_SUCC);
      }
  }
  ```

**Wait logic (in `waitobj_wait`):**

- Validates the wait object and ownership.
- Acquires `wo->lock`.
- While `wo->ready == 0`, calls:

  ```c
  thread_sleep(wo, &wo->lock);
  ```

- After being woken:
  - Clears `wo->ready`.
  - Releases `wo->lock`.
  - Returns success to the caller.

---

## 8. Interrupt → Notify → Wake Flow

This section shows how a hardware timer interrupt ultimately wakes a user thread that is blocked in `waitobj_wait`.

1. **Timer hardware interrupt**
   - PIT/LAPIC timer fires.
   - Interrupt vector for timer is delivered to the CPU.
   - Entry point is defined in `kern/dev/idt.S` (e.g., `Xirq_timer`).

2. **Top-level interrupt handler**
   - `kern/trap/TTrapHandler/TTrapHandler.c`:
     - `interrupt_handler(tf_t *tf)` is called.
     - Dispatches on `tf->trapno`.
     - For `T_IRQ0 + IRQ_TIMER`, calls:

       ```c
       timer_intr_handler();
       ```

3. **Timer interrupt handler**
   - Still in `TTrapHandler.c`:

     ```c
     static int timer_intr_handler(void)
     {
         intr_eoi();
         sched_update();
         waitobj_notify_timer();
         return 0;
     }
     ```

   - Responsibilities:
     - Acknowledge the interrupt (`intr_eoi`).
     - Advance scheduler time and preempt if necessary (`sched_update`).
     - Notify the wait object subsystem that a timer tick has occurred (`waitobj_notify_timer`).

4. **Wait object notification**
   - `waitobj_notify_timer` in `kern/sync/waitobj.c`:
     - Iterates all entries in `wait_table`.
     - For each `used` wait object:
       - Acquires `wo->lock`.
       - Checks for any `WAITOBJ_SRC_TIMER` in `wo->sources`.
       - If at least one is found:
         - Sets `wo->ready = 1`.
         - Releases `wo->lock`.
         - Calls:

           ```c
           thread_wakeup(wo);
           ```

         - This matches the channel used in `thread_sleep`.
       - If none is found:
         - Releases `wo->lock` and moves on.

5. **Waking the thread**
   - `thread_wakeup(void *chan)` is implemented in:
     - `kern/thread/PThread/PThread.c`
   - It:
     - Iterates over all TCBs.
     - For any thread whose `tcb_get_chan(pid) == chan` and is in `TSTATE_SLEEP`:
       - Sets it to `TSTATE_READY`.
       - Enqueues it on the ready queue.
   - Eventually, the scheduler will run this thread again.

6. **Return to user space**
   - When the woken thread is scheduled, it resumes in `thread_sleep` after the context switch and reacquires `wo->lock`.
   - `waitobj_wait` sees `wo->ready == 1`, clears it, releases the lock, and returns.
   - Control then returns across the syscall boundary, and the user code continues after `sys_waitobj_wait`.

---

## 9. Thread State Transitions

The wait object mechanism reuses the existing thread state machine and sleep/wake model. The main states (from `lib/thread.h` and TCB code) are:

- `TSTATE_RUN` — currently running.
- `TSTATE_READY` — ready to run, on a run queue.
- `TSTATE_SLEEP` — sleeping, waiting on a channel.
- `TSTATE_DEAD` — inactive / unused.

For a thread using `waitobj_wait`:

1. **Running → Sleeping**
   - The thread is in `TSTATE_RUN` and calls `sys_waitobj_wait(woid)`.
   - Kernel handler `waitobj_wait` acquires `wo->lock`.
   - If `wo->ready == 0`, it calls:

     ```c
     thread_sleep(wo, &wo->lock);
     ```

   - Internally, `thread_sleep`:
     - Sets the thread’s channel to `wo`.
     - Sets `TSTATE_SLEEP`.
     - Picks a new runnable thread and switches context.

2. **Sleeping → Ready**
   - At some later timer interrupt:
     - `waitobj_notify_timer` sets `wo->ready = 1` and calls `thread_wakeup(wo)`.
   - `thread_wakeup`:
     - For each thread whose channel is `wo` and state is `TSTATE_SLEEP`:
       - Sets `TSTATE_READY`.
       - Adds it to the ready queue.

3. **Ready → Running**
   - The scheduler (`thread_yield`, `sched_update`, and ready queue logic) eventually selects the ready thread and:
     - Sets `TSTATE_RUN`.
     - Switches context into it.

4. **Running after wakeup**
   - The thread resumes execution in `thread_sleep`, which:
     - Reacquires the lock passed to `thread_sleep`.
     - Returns to `waitobj_wait`.
   - `waitobj_wait`:
     - Sees `wo->ready == 1`.
     - Clears `wo->ready`.
     - Releases `wo->lock`.
     - Returns success to the syscall handler, which then returns to user mode.

---

## 10. Design Limitations

The implementation is intentionally simple and has the following limitations:

1. **Single-thread ownership**
   - A `wait_object_t` is associated with a single owner (`owner` field).
   - Only the owning thread is allowed to:
     - Add sources.
     - Wait on the object.
   - This avoids cross-thread sharing complexity but also limits flexibility.

2. **Fixed global table**
   - Wait objects are stored in a statically sized global array:
     - `wait_table[WAITOBJ_MAX]`
   - `WAITOBJ_MAX` is a fixed compile-time limit.
   - There is no dynamic memory allocation for wait objects.

3. **Fixed per-object source list**
   - Each wait object has a fixed-size `sources[WAITOBJ_MAX_SOURCES]`.
   - No dynamic resizing or linked-list structures are used.
   - The implementation currently only checks for the presence of `WAITOBJ_SRC_TIMER` and does not distinguish between multiple timer sources.

4. **Single event type (timer)**
   - Only one source type (`WAITOBJ_SRC_TIMER`) is supported.
   - There is no direct integration with:
     - File descriptors.
     - Pipes.
     - Sockets.
     - Other device interrupts.

5. **Coarse-grained notification**
   - Timer notifications are global:
     - Every timer interrupt iterates over all `wait_table` entries.
   - This is acceptable for a small educational kernel but would not scale in a large system.

6. **Process cleanup not fully integrated**
   - `waitobj_cleanup_owner(int owner)` exists but is not wired into a process exit path.
   - The current mCertiKOS template does not yet implement process termination logic that would call this cleanup.

7. **No fairness guarantees at wait-object level**
   - The mechanism relies on the underlying scheduler.
   - If multiple threads could wait on a single wait object (not currently allowed by ownership rules), there would be no explicit fairness policy about which one wakes first.

---

## 11. Possible Extensions (epoll-like Future Work)

The current design is a minimal educational foundation. The following extensions would move it closer to an `epoll`-like facility:

1. **Richer event types**
   - Extend `wait_event_source_t` with identifiers for:
     - File descriptors (`struct file *` or fd integers).
     - Other device interrupts (disk, network, etc.).
     - User-defined channels or synchronization primitives.
   - Add type-specific fields (e.g., event masks).

2. **Per-process wait object namespaces**
   - Instead of a global index, treat wait object ids as per-process descriptors.
   - Store them in the TCB (similar to `openfiles`) and manage via a per-process table.

3. **Edge-triggered and level-triggered semantics**
   - Support different notification modes:
     - Edge-triggered: only notify when a new event occurs.
     - Level-triggered: notify as long as the condition holds.
   - Extend `wait_object_t` with flags and counters to track pending events.

4. **Efficient event queues**
   - Replace the linear scan in `waitobj_notify_timer` with:
     - Per-source lists of wait objects.
     - A ready-list of wait objects with pending events.
   - This reduces latency and improves scalability.

5. **Multiple waiters per object**
   - Relax the one-owner constraint.
   - Allow multiple threads to wait on the same wait object with:
     - Fairness (round-robin wakeup).
     - Options for waking one vs. waking all.

6. **Integration with file system and networking**
   - Connect wait objects to:
     - File read/write readiness.
     - Socket readiness (for a future networking stack).
   - Use `file.c`, `sysfile.c`, and device drivers as integration points.

7. **User-space abstraction layer**
   - Provide a higher-level user library API that wraps the raw syscalls into:
     - A more expressive event registration model (like `epoll_ctl`).
     - A richer `wait` API that returns specific events and metadata.

These extensions can be incrementally implemented on top of the current design, making this feature a good teaching platform for building real-world event multiplexing abstractions.

