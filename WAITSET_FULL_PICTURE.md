# Waitset / Wait-Object: Full Picture (This Project)

## Objective (what “wait for multiple objects” means here)

The goal is to let **one process** block in **one wait call**, and get woken up when **any** of its registered sources becomes ready (signal delivery, IPC arrival, etc.).

Key idea:
- The thread does **not** sleep on multiple objects.
- The thread sleeps on **one wait object** (a channel pointer).
- The waitset holds **multiple registrations** (“objects”) and tells you which one triggered.

## Terms (in this codebase)

### Wait object (mechanism)
The kernel has a generic sleep/wakeup mechanism based on a **channel pointer**:

- `thread_sleep(chan, lock)` sleeps the current thread on `chan`
- `thread_wakeup(chan)` wakes threads sleeping on `chan`

In this project, the **wait object** is simply the channel value (a pointer).

### Waitset (abstraction)
A **waitset** is a kernel object that:

- collects many “things you care about” (`sources`)
- records which of those things have become ready (`triggered`)
- sleeps/wakes using the wait-object mechanism

The waitset gives you “wait for multiple objects” behavior using one blocking call.

### Waitset address (the bridge)
When a thread blocks in `waitset_wait`, it sleeps on:

- `chan == ws` (the address of the kernel `struct waitset`)

So the waitset’s address is the *wait object channel* used by `thread_sleep/thread_wakeup`.

## Key Data Structures (important fields only)

### `struct waitset` (kernel)
Source: `kern/sync/waitset.h`

```c
struct waitset {
    spinlock_t lock;
    SLIST_HEAD(source_list, notif_source) sources;
    SLIST_HEAD(triggered_list, notif_source) triggered;
    unsigned int waiting_thread; // PID of waiting thread, or NUM_IDS if none
    unsigned int owner_pid;      // PID of the process that owns this waitset
    int active;                  // Is this waitset allocated?
};
```

Meaning:
- `sources`: “what I’m waiting for”
- `triggered`: “what is ready right now”
- `owner_pid`: waitsets are owned/used by one process (ownership checks in wait/waitset_ctl)
- `waiting_thread`: the implementation is designed around at most one waiter per waitset

### `struct notif_source` (kernel registration entry)
Source: `kern/sync/waitset.h`

```c
struct notif_source {
    SLIST_ENTRY(notif_source) entry;
    SLIST_ENTRY(notif_source) triggered_entry;
    int type;        // WS_SOURCE_IPC or WS_SOURCE_SIGNAL
    int id;          // e.g., signal number or sender pid (or -1 wildcard)
    int events;      // stored mask (not currently used to filter in notify)
    int triggered;   // dedup flag: queued once until consumed
    void *data;
    struct waitset *ws;
};
```

Meaning:
- each `notif_source` is one “object” you can wait on (signal X, IPC from pid Y, etc.)
- `id == -1` acts as a wildcard match in `waitset_notify`

### TCB “channel” (how sleeping works)
Source: `kern/thread/PTCBIntro/PTCBIntro.c`

```c
struct TCB {
  t_state state;
  unsigned int prev;
  unsigned int next;
  void *channel;
  ...
};
```

Meaning:
- if a thread is sleeping, `TCB.channel` stores the wait-object channel pointer
- in waitset’s case, `channel == ws` (the waitset address)

## The Full Runtime Picture (diagram)

```
           (User process P)
                 |
        sys_waitset_wait(wsid)
                 |
           waitset_wait(wsid)
                 |
     if ws->triggered empty:
         thread_sleep(chan=ws, lock=&ws->lock)
                 |
     [Thread is now SLEEP, TCB.channel == ws]
                 |
  ---------------------------------------------------
  Meanwhile, some producer runs in kernel:
   - IPC send to P    OR   signal delivered to P
                 |
           waitset_notify(target_pid=P, ...)
                 |
     find waitsets owned by P, match ws->sources
                 |
     push matching source into ws->triggered
                 |
           thread_wakeup(chan=ws)
                 |
     sleeping thread becomes READY, scheduled later
  ---------------------------------------------------
                 |
  Thread resumes in waitset_wait(), sees ws->triggered
  dequeues events and returns them to user
```

## Core Flows (brief, but end-to-end)

### 1) Waitset creation (control plane)
Source: `kern/sync/waitset.c`

- `waitset_create()` finds a free `waitset_pool[]` entry, marks it active, sets:
  - `owner_pid = get_curid()`
  - empty `sources` and `triggered`

Why it matters:
- only the owner process can `wait` or `ctl` the waitset

### 2) Register “multiple objects” (control plane)
Source: `kern/sync/waitset.c`

- repeated calls to `waitset_ctl(wsid, WS_CTL_ADD, type, id, ...)` add multiple entries into:
  - `ws->sources`

This is where “multiple objects” live: as many entries in `ws->sources`.

### 3) Block waiting (event plane)
Source: `kern/sync/waitset.c`

- `waitset_wait()` loops while `ws->triggered` is empty:
  - `timeout == 0` => return 0 (poll)
  - else:
    - record `ws->waiting_thread = get_curid()`
    - `thread_sleep(ws, &ws->lock)`

Important:
- the thread sleeps on the waitset address `ws`
- only one channel is used, but many sources can wake it

### 4) Event injection (producers)

#### IPC producer
Source: `kern/trap/TSyscall/TSyscall.c`

- when a sender does `sys_sync_send(recv_pid, ...)`:
  - the kernel calls `waitset_notify_ipc(recv_pid, send_pid)`

This targets the receiver process via `recv_pid`.

#### Signal producer
Source: `kern/trap/TSyscall/TSyscall.c`

- when a sender does `sys_kill(pid, signum)`:
  - the kernel sets the signal pending
  - calls `waitset_notify_signal(pid, signum)`

This targets the victim process via `pid`.

### 5) Matching + wakeup (waitset_notify)
Source: `kern/sync/waitset.c`

- `waitset_notify(target_pid, type, id, event)`:
  - scans `waitset_pool[]`
  - selects waitsets where `ws->owner_pid == target_pid`
  - matches `(type, id)` against `ws->sources`
  - queues matched sources into `ws->triggered` (dedup using `s->triggered`)
  - calls `thread_wakeup(ws)` to wake sleepers on channel `ws`

Key point:
- IPC for one process does NOT wake other processes, because the owner check filters by `target_pid`.

## IPC: receiver id vs sender id vs “target pid”

- `recv_pid` (receiver id): the process that should receive the message.
- `send_pid`: the process sending the message.
- `target_pid` (in `waitset_notify`): the owner process whose waitsets will be checked/woken.

For IPC in this project:
- `target_pid == recv_pid`
- the IPC event’s `source_id` is the sender pid (so the receiver can know who sent it).

## Why only the intended process wakes (targeting)

When an event arrives, the waitset subsystem only considers waitsets owned by the intended process:

```c
if (ws->owner_pid != target_pid) {
    spinlock_release(&ws->lock);
    continue;
}
```

This is why:
- IPC to PID 7 does not affect PID 8’s waitsets.
- a signal sent to PID 7 does not affect PID 8’s waitsets.

## The race and the locks (why `thread_sleep(ws, &ws->lock)` is “safe”)

The classic bug to avoid is a **missed wakeup**:

1) Waiter checks “no events ready”
2) Waiter releases `ws->lock`
3) Notifier queues an event + calls `thread_wakeup(ws)`
4) Waiter finally marks itself sleeping on channel `ws`

If the wakeup happens before the waiter is recorded as sleeping on `ws`, the wake is “lost” and the waiter can sleep forever.

This implementation avoids the race by using two locks with a consistent order:
- `ws->lock` protects waitset state (`sources`, `triggered`, `waiting_thread`)
- `sched_lk` protects sleep/wakeup bookkeeping (thread states, channels, ready queue)

The important part of `thread_sleep` is:

```c
spinlock_acquire(&sched_lk);
spinlock_release(lk);
tcb_set_state(curid, TSTATE_SLEEP);
tcb_set_chan(curid, chan);
...
spinlock_release(&sched_lk);
kctx_switch(curid, new_cur_pid);
```

And `thread_wakeup(chan)` acquires `sched_lk` while scanning sleepers and moving them to READY.

Because the waiter transitions into “sleeping on chan=ws” while holding `sched_lk`, `thread_wakeup(ws)` can’t race past that transition and miss it.

## Terminal walkthrough (what each command means)

### You boot into a shell (inside QEMU)

Host command:

```bash
make qemu-nox
```

You then see the OS shell prompt:

```text
>:
```

This shell is a user process (`user/shell/shell.c`) running inside your OS.

### What is `spawn`?

`spawn` is a shell command that asks the kernel to create a new user process by calling the syscall `sys_spawn(elf_id, quota)`.

In the shell:
- you type `spawn <elf_id>`
- the shell calls `sys_spawn(elf_id, 1000)`

### What is `elf_id`?

This OS doesn’t load programs from a filesystem at runtime. Instead, several user programs are embedded into the kernel image, and `elf_id` selects which one to start.

The kernel-side mapping is in the `sys_spawn` handler:

```c
} else if (elf_id == 6) {
    elf_addr = _binary___obj_user_waitset_demo_start;
} else {
    syscall_set_errno(tf, E_INVAL_PID);
    syscall_set_retval1(tf, NUM_IDS);
    return;
}
```

So:
- `spawn 6` means “run the embedded `waitset_demo` program”.

### What is PID?

PID is the unique process id assigned by the kernel to the newly created process instance.

So when you see:

```text
Process spawned with PID 7
```

it means:
- a new instance of `waitset_demo` is running
- its id is 7 (you target it with `kill` or `ipcsend`)

## Demo walkthrough (how it proves multi-object waiting)

### Step 1: start the demo process

```text
>: spawn 6
Spawning process with elf_id 6...
Process spawned with PID 7
```

Inside `waitset_demo`, it registers multiple sources into one waitset:
- SIGUSR1
- SIGUSR2
- IPC (any sender)

This is the “multiple objects”: multiple `notif_source` entries in `ws->sources`.

### Step 2: wake it via signals (source type = SIGNAL)

```text
>: kill -10 7
Sending signal 10 to process 7...
Signal sent successfully.

>: kill -12 7
Sending signal 12 to process 7...
Signal sent successfully.
```

What this triggers in the kernel:
- `sys_kill(pid=7, signum=10/12)` calls `waitset_notify_signal(7, signum)`
- `waitset_notify` matches the registered `(WS_SOURCE_SIGNAL, signum)` source
- it pushes that source into `ws->triggered` and calls `thread_wakeup(ws)`
- the demo’s blocked `waitset_wait` returns and prints which event triggered

### Step 3: wake it via IPC (source type = IPC)

```text
>: ipcsend 7 hello_from_shell
Sending IPC to process 7: hello_from_shell
IPC send completed (receiver consumed message).
```

What this triggers in the kernel:
- `sys_sync_send(recv_pid=7, ...)` calls `waitset_notify_ipc(7, send_pid)`
- `waitset_notify` matches the registered `(WS_SOURCE_IPC, -1)` source
- it queues an IPC event and calls `thread_wakeup(ws)`
- the demo prints `IPC` and then receives the message

### Why this demonstrates the objective

The demo process is blocked on one call (`sys_waitset_wait`) but can be woken by **different kinds of sources** (signal or IPC).

That is “wait for multiple objects” in this project:
- multiple registrations in `ws->sources`
- one sleeping channel (`chan == ws`)
- one wakeup path (`thread_wakeup(ws)`)

## Plain-English explanation: why do we need `triggered` list?

It’s true that **one event is enough to wake the thread**, but the wakeup alone does not answer the important question:

> “Okay, I woke up… but **what exactly happened**?”

The `triggered` list exists because `thread_wakeup(ws)` is only a “nudge” that says:

> “Something related to this waitset may be ready—go check.”

It does **not** carry any details like “SIGUSR1 happened” or “IPC from PID 12 arrived”.

So the waitset needs a place to **store the details** of what happened. That place is `ws->triggered`.

### In simple words, the `triggered` list does 3 jobs

1) **Remembers which source fired**
   - Waking on a channel doesn’t tell you *which* source caused it.
   - `triggered` is the list of “ready sources” that will be returned to user space.

2) **Buffers multiple events that happen close together**
   - Suppose SIGUSR1 and IPC both happen before your waiting thread gets CPU time again.
   - The waitset can queue both sources in `triggered`.
   - Then one `waitset_wait()` call can return multiple events (up to `maxevents`).

3) **Prevents lost information**
   - Wakeups can be “spurious” or can happen before the thread runs.
   - The correct design is: wake up, then check shared state.
   - Here, the shared state is `triggered`. If it’s empty, you go back to sleep.

### Why not just “scan everything again” after waking?

You *could* scan all sources again, but you still need some way to answer:
- “which ones became ready while I was asleep?”
- “how many happened?”
- “what order should I return them?”

The `triggered` list is the simplest answer: it is literally the queue of “things that are ready to report”.

### One more detail: deduplication

Each source has a small flag:

- `s->triggered`

It prevents the same source from being inserted into the triggered list multiple times before the user consumes it.

In plain English:
- “Don’t spam the queue with duplicates of the same source until the waiter has handled it.”
