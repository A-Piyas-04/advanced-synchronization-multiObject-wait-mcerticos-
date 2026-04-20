#include <lib/types.h>
#include <lib/x86.h>
#include <lib/debug.h>
#include <lib/spinlock.h>
#include <thread/PThread/export.h>

#include "waitobj.h"

/*
 * Undefine WAITOBJ_VERBOSE for quiet kernel (beginner tutorial relies on
 * user printf). To see kernel-side waitobj traces, add to KERN_CFLAGS:
 *   -DWAITOBJ_VERBOSE
 */
#ifdef WAITOBJ_VERBOSE
#define WAITOBJ_LOG(...) KERN_INFO(__VA_ARGS__)
#else
#define WAITOBJ_LOG(...) do { } while (0)
#endif

volatile uint32_t waitobj_ticks;

static wait_object_t wobjs[MAX_WAIT_OBJECTS];
static event_source_t esrc[MAX_EVENT_SOURCES];

void
waitobj_init(void)
{
	unsigned int i;

	waitobj_ticks = 0;
	for (i = 0; i < MAX_WAIT_OBJECTS; i++) {
		wobjs[i].wid = (int)i;
		wobjs[i].owner_pid = NUM_IDS;
		wobjs[i].in_use = 0;
		wobjs[i].sources = NULL;
		spinlock_init(&wobjs[i].lk);
	}
	for (i = 0; i < MAX_EVENT_SOURCES; i++) {
		esrc[i].id = 0;
		esrc[i].type = 0;
		esrc[i].ready = 0;
		esrc[i].period_ms = 0;
		esrc[i].next_fire_tick = 0;
		esrc[i].parent = NULL;
		esrc[i].next = NULL;
	}
}

static event_source_t *
alloc_source(void)
{
	unsigned int i;

	for (i = 0; i < MAX_EVENT_SOURCES; i++) {
		if (esrc[i].parent == NULL && esrc[i].id == 0)
			return &esrc[i];
	}
	return NULL;
}

static wait_object_t *
get_wo(int wid)
{
	if (wid < 0 || wid >= MAX_WAIT_OBJECTS)
		return NULL;
	if (!wobjs[wid].in_use)
		return NULL;
	return &wobjs[wid];
}

int
waitobj_create(unsigned int owner_pid)
{
	int i;

	for (i = 0; i < MAX_WAIT_OBJECTS; i++) {
		if (!wobjs[i].in_use) {
			spinlock_acquire(&wobjs[i].lk);
			wobjs[i].in_use = 1;
			wobjs[i].owner_pid = owner_pid;
			wobjs[i].sources = NULL;
			spinlock_release(&wobjs[i].lk);
			WAITOBJ_LOG("[Kernel] waitobj_create(): wid=%d owner=%u\n", i,
				    owner_pid);
			return i;
		}
	}
	return -1;
}

int
waitobj_add(unsigned int owner_pid, int wid, int type, uint32_t arg_ms)
{
	wait_object_t *wo;
	event_source_t *s;
	uint32_t now;

	wo = get_wo(wid);
	if (wo == NULL || wo->owner_pid != owner_pid)
		return -1;

	s = alloc_source();
	if (s == NULL)
		return -1;

	spinlock_acquire(&wo->lk);
	s->id = (int)(s - esrc) + 1;
	s->type = type;
	s->ready = 0;
	s->parent = wo;
	s->next = wo->sources;
	wo->sources = s;

	if (type == EVENT_TIMER) {
		s->period_ms = arg_ms ? arg_ms : 1;
		now = waitobj_ticks;
		s->next_fire_tick = now + s->period_ms;
	} else {
		s->period_ms = 0;
		s->next_fire_tick = 0;
	}
	spinlock_release(&wo->lk);

	WAITOBJ_LOG("[Kernel] waitobj_add(): wid=%d type=%d src_id=%d\n", wid,
		    type, s->id);
	return s->id;
}

/*
 * After any delivered event, reset all timer sources on this wait object so the
 * next wait always observes a full interval from "now" (tick-ms). This avoids
 * the timer appearing to never fire after a keyboard wakeup.
 */
static void
reschedule_all_timer_sources(wait_object_t *wo)
{
	uint32_t now;
	event_source_t *p;

	now = waitobj_ticks;
	for (p = wo->sources; p != NULL; p = p->next) {
		if (p->type != EVENT_TIMER)
			continue;
		p->ready = 0;
		p->next_fire_tick = now + p->period_ms;
	}
}

static int
first_ready_source(wait_object_t *wo, event_source_t **out)
{
	event_source_t *p;

	for (p = wo->sources; p != NULL; p = p->next) {
		if (p->ready) {
			*out = p;
			return p->id;
		}
	}
	return -1;
}

int
waitobj_wait(unsigned int owner_pid, int wid, unsigned int timeout_ms,
	       int *out_src_id)
{
	wait_object_t *wo;
	event_source_t *ready_src;
	int sid;
	uint32_t deadline;
	int use_timeout;

	wo = get_wo(wid);
	if (wo == NULL || wo->owner_pid != owner_pid)
		return -2;

	use_timeout = (timeout_ms != 0);
	deadline = waitobj_ticks + timeout_ms;

	spinlock_acquire(&wo->lk);
	for (;;) {
		sid = first_ready_source(wo, &ready_src);
		if (sid >= 0) {
			ready_src->ready = 0;
			reschedule_all_timer_sources(wo);
			spinlock_release(&wo->lk);
			*out_src_id = sid;
			WAITOBJ_LOG("[Kernel] waitobj_wait(): returning ready src=%d\n",
				    sid);
			return 0;
		}
		if (use_timeout && waitobj_ticks >= deadline) {
			spinlock_release(&wo->lk);
			*out_src_id = -1;
			WAITOBJ_LOG("[Kernel] waitobj_wait(): timeout\n");
			return -1;
		}
		WAITOBJ_LOG("[Kernel] waitobj_wait(): no events ready, putting thread to sleep\n");
		thread_sleep(wo, &wo->lk);
	}
}

int
waitobj_signal_source(unsigned int owner_pid, int source_id)
{
	event_source_t *s;
	int idx;

	if (source_id < 1 || source_id > (int)MAX_EVENT_SOURCES)
		return -1;
	idx = source_id - 1;
	s = &esrc[idx];
	if (s->id != source_id || s->parent == NULL
	    || s->parent->owner_pid != owner_pid)
		return -1;
	if (s->type != EVENT_SOFTWARE)
		return -1;
	event_notify(s);
	return 0;
}

void
event_notify(event_source_t *src)
{
	if (src == NULL || src->parent == NULL)
		return;
	src->ready = 1;
	if (src->type == EVENT_TIMER) {
		KERN_INFO(
			"[Kernel] waitobj: timer source fired (period %u ms); "
			"unblocking waiter.\n",
			src->period_ms);
		WAITOBJ_LOG("[Kernel] Timer event triggered\n");
		WAITOBJ_LOG("[Kernel] Waking thread...\n");
	} else if (src->type == EVENT_KEYBOARD) {
		WAITOBJ_LOG("[Kernel] Keyboard interrupt detected\n");
		WAITOBJ_LOG("[Kernel] Waking thread...\n");
	} else {
		WAITOBJ_LOG("[Kernel] event_notify(): event id=%d type=%d ready\n",
			    src->id, src->type);
		WAITOBJ_LOG("[Kernel] waking up thread (wait_object %p)\n",
			    src->parent);
	}
	thread_wakeup(src->parent);
}

void
waitobj_on_timer_irq(void)
{
	waitobj_ticks++;
	waitobj_timer_tick();
}

void
waitobj_timer_tick(void)
{
	unsigned int i;
	event_source_t *s;
	uint32_t now;

	now = waitobj_ticks;
	for (i = 0; i < MAX_EVENT_SOURCES; i++) {
		s = &esrc[i];
		if (s->parent == NULL || s->type != EVENT_TIMER)
			continue;
		if (s->ready)
			continue;
		if (now >= s->next_fire_tick)
			event_notify(s);
	}
}

void
waitobj_console_input_notify(void)
{
	unsigned int i;
	event_source_t *s;

	for (i = 0; i < MAX_EVENT_SOURCES; i++) {
		s = &esrc[i];
		if (s->parent == NULL || s->type != EVENT_KEYBOARD)
			continue;
		if (s->ready)
			continue;
		event_notify(s);
	}
}
