---
title: "Multi-Object Wait (waitobj) Implementation Summary"
subtitle: "mCertiKOS / OS Project"
date: "April 2026"
---

# Multi-Object Wait (waitobj) Implementation Summary

This document describes how **multi-object wait** (wait-for-multiple-sources) was implemented in the teaching kernel, where the important code lives, and how the pieces fit together. It is suitable for printing or conversion to PDF (see end of file).

---

## 1. Objective

Implement a **kernel-level** mechanism so one thread can:

- Register **multiple event sources** (timer, keyboard, optional software) on a **wait object**
- **Block** without busy-waiting (`thread_sleep`)
- **Wake** when **any** source becomes ready (`thread_wakeup`)
- **Return** which source satisfied the wait

Conceptually similar to a simplified **epoll / kqueue** style wait hub, constrained to **one waiter per wait object** for clarity.

---

## 2. High-Level Architecture

| Concept | Role |
|--------|------|
| **Wait object** | Holds a list of event sources, a per-object **spinlock**, **owner process id**, and is used as the **sleep channel** pointer for `thread_sleep` / `thread_wakeup`. |
| **Event source** | Per-source **id**, **type** (timer / keyboard / software), **ready** flag, **parent** wait object, **next** pointer in a list; timers also store **period_ms** and **next_fire_tick**. |
| **Global tick** | `waitobj_ticks` incremented once per timer IRQ; used for timer deadlines and optional wait timeouts. |

**Blocking model:** `waitobj_wait` holds the wait object lock, scans for a ready source; if none, it calls `thread_sleep(wait_object_ptr, &wait_object->lk)`. Sources call `event_notify`, which sets `ready` and `thread_wakeup(parent_wait_object)`.

---

## 3. Repository Map (Important Files)

| Path | Purpose |
|------|---------|
| `kern/sync/waitobj.h` | Public types: `event_source_t`, `wait_object_t`, `EVENT_*`, API declarations. |
| `kern/sync/waitobj.c` | Pools, `waitobj_create` / `add` / `wait`, `event_notify`, timer tick, console hook, `reschedule_all_timer_sources`. |
| `kern/sync/Makefile.inc` | Compiles `waitobj.c` into the kernel. |
| `kern/Makefile.inc` | Includes `sync/Makefile.inc`. |
| `kern/dev/devinit.c` | Calls `waitobj_init()` after IPC init. |
| `kern/lib/syscall.h` | `SYS_waitobj_*` numbers, `E_WAIT_TIMEOUT`. |
| `kern/trap/TDispatch/TDispatch.c` | Syscall dispatch `switch` cases. |
| `kern/trap/TDispatch/import.h` | Declarations for `sys_waitobj_*`. |
| `kern/trap/TSyscall/TSyscall.c` | `sys_waitobj_create` / `add` / `wait` / `signal`; `sys_spawn` elf_id 6 for embedded `wait_demo`. |
| `kern/trap/TTrapHandler/TTrapHandler.c` | Timer IRQ: `waitobj_on_timer_irq()` after `sched_update()`; keyboard IRQ: `keyboard_intr()` + EOI. |
| `kern/dev/console.c` | After releasing console lock, `waitobj_console_input_notify()` for keyboard-related wakeups. |
| `kern/thread/PThread/PThread.c` | Existing `thread_sleep` / `thread_wakeup` (unchanged scheduler policy). |
| `kern/init/init.c` | Boot: **idle** then **wait_demo** (no shell); ensures a second runnable thread exists when `wait_demo` sleeps. |
| `user/wait_demo/wait_demo.c` | Demonstration program (formal output, timer + keyboard). |
| `user/wait_demo/Makefile.inc` | Builds `wait_demo`, adds binary to `KERN_BINFILES`. |
| `user/Makefile.inc` | Includes wait_demo target. |
| `user/include/syscall.h` | User stubs: `sys_waitobj_*`. |

---

## 4. Data Structures (Kernel)

Defined in **`kern/sync/waitobj.h`**:

- **`event_source_t`**: `id`, `type`, `ready`, `period_ms`, `next_fire_tick`, `parent`, `next`.
- **`wait_object_t`** (struct `wait_object`): `wid`, `owner_pid`, `in_use`, `lk` (`spinlock_t`), `sources` (head of list).

**Storage:** Fixed **static pools** (`MAX_WAIT_OBJECTS`, `MAX_EVENT_SOURCES`) — no `kmalloc`; same style as other kernel subsystems in this tree.

---

## 5. System Calls

Declared in **`kern/lib/syscall.h`** (shared numbering with userland via include path):

| Syscall | Typical arguments (EBX, ECX, EDX) | Behavior |
|---------|-----------------------------------|----------|
| `SYS_waitobj_create` | — | Allocates a wait object for current process; returns **wid** in EBX. |
| `SYS_waitobj_add` | wid, type, arg_ms | Adds a source; **arg_ms** is the timer period for `EVENT_TIMER`; ignored for keyboard. Returns **source id** in EBX. |
| `SYS_waitobj_wait` | wid, timeout_ms | `0` = infinite wait. Returns **source id** in EBX; `E_WAIT_TIMEOUT` / `E_INVAL_ID` on failure paths. |
| `SYS_waitobj_signal` | source_id | Wakes software-type source (`EVENT_SOFTWARE`) for testing. |

**Dispatch:** `kern/trap/TDispatch/TDispatch.c`  
**Handlers:** `kern/trap/TSyscall/TSyscall.c`

**User stubs:** `user/include/syscall.h`

---

## 6. Core Kernel Logic (`kern/sync/waitobj.c`)

### 6.1 Initialization

`waitobj_init()` zeros pools, initializes locks; invoked from **`kern/dev/devinit.c`**.

### 6.2 Create / Add

- **`waitobj_create`**: Finds a free slot, sets `owner_pid` to caller, empty source list.
- **`waitobj_add`**: Validates owner and wid; allocates a source node; prepends to list; for timers sets `next_fire_tick = waitobj_ticks + period_ms`.

### 6.3 Wait

- Validates **owner** and **wid**.
- Under **`spinlock_acquire(&wo->lk)`**: loop — if any source has **`ready`**, pick first in list, clear that source’s `ready`, then **`reschedule_all_timer_sources(wo)`** (see below), return id.
- Otherwise, if wait timed out (when `timeout_ms != 0`), return error.
- Otherwise **`thread_sleep(wo, &wo->lk)`** — channel is the **address of the wait object** (unique per hub).

### 6.4 Timer rescheduling (important fix)

After **any** delivered wakeup, **`reschedule_all_timer_sources`** runs for that wait object:

- For every **timer** source: `ready = 0`, `next_fire_tick = waitobj_ticks + period_ms`.

This guarantees the **next** wait observes a **full** interval after a **keyboard** wakeup (otherwise the timer deadline could be stale).

### 6.5 `event_notify`

Sets `src->ready = 1`, then **`thread_wakeup(src->parent)`**.  
For **timer** sources, one **always-on** kernel log line is printed when the timer fires (for demonstration traceability). Other verbose lines are gated by **`WAITOBJ_VERBOSE`** (compile with `-DWAITOBJ_VERBOSE` in kernel flags to enable).

### 6.6 Timer IRQ path

**`kern/trap/TTrapHandler/TTrapHandler.c`**: after `sched_update()`, calls **`waitobj_on_timer_irq()`**, which increments **`waitobj_ticks`** and **`waitobj_timer_tick()`** (scans timer sources, calls **`event_notify`** when `now >= next_fire_tick`).

### 6.7 Keyboard path

- **IRQ:** `T_IRQ0 + IRQ_KBD` calls **`keyboard_intr()`** then **`intr_eoi()`** (so the controller is drained).
- **Console:** after characters are placed in the ring buffer (in **`kern/dev/console.c`**), **`waitobj_console_input_notify()`** walks keyboard-type sources and **`event_notify`** as appropriate.

---

## 7. Boot and Scheduler Note (`kern/init/init.c`)

The kernel originally booted **shell** as the foreground process; that was changed so the demonstration **owns the console**.

**Critical detail:** **`thread_sleep` assumes another runnable thread exists.** If only **`wait_demo`** were created, the ready queue could be empty when it slept. The boot sequence therefore:

1. **`proc_create(idle)`** — idle remains on the ready queue.
2. **`proc_create(wait_demo)`** — tutorial process.
3. Remove **`wait_demo`** from the ready queue and **`kctx_switch`** to it (same bootstrap pattern as before, but targeting **`wait_demo`** instead of shell).

While **`wait_demo`** blocks in **`waitobj_wait`**, the CPU can run **idle** (`yield` loop). **Shell is not started** at boot (still linked for **`spawn`** if needed elsewhere).

---

## 8. User Demonstration (`user/wait_demo/wait_demo.c`)

- Registers **timer** (3000 ms) and **keyboard** sources.
- Loop: print iteration header, **`sys_waitobj_wait(wid, 0)`**, print which source fired and cumulative counts.
- Explains that keyboard input is **not echoed** and that the kernel prints a line when the **timer** fires.

**Embedding in kernel:** `user/wait_demo` is linked as a binary blob; **`sys_spawn`** supports **elf_id 6** pointing at **`_binary___obj_user_wait_demo_wait_demo_start`** in **`kern/trap/TSyscall/TSyscall.c`** (used if you spawn the demo from another program).

---

## 9. Build and Run

1. From project root: **`make`** (builds user + kernel + links `wait_demo` into the kernel image).
2. Run QEMU per **`Makefile`** targets (`make qemu` or `make qemu-nox`).
3. Observe: formal demo text; after ~3 s without keyboard activity, **timer** path; with keys, **keyboard** path; optional kernel line on timer fire.

---

## 10. Optional: Verbose Kernel Traces

To log all historical **`[Kernel] waitobj_...`** messages again, define **`WAITOBJ_VERBOSE`** when compiling the kernel (see comment at top of **`kern/sync/waitobj.c`**).

---

## 11. Generating a PDF from This Document

This file is **`docs/Multi_Object_Wait_Implementation.md`**.

**Option A — Pandoc (recommended):**

```bash
pandoc docs/Multi_Object_Wait_Implementation.md -o docs/Multi_Object_Wait_Implementation.pdf
```

**Option B — From a browser or editor:** open the Markdown file, print, choose **Save as PDF**.

**Option C — Microsoft Word:** import the `.md` or paste content, then **File → Save As → PDF**.

---

*End of document.*
