#ifndef _KERN_SYNC_WAITOBJ_H_
#define _KERN_SYNC_WAITOBJ_H_

#ifdef _KERN_

#include <lib/types.h>
#include <lib/spinlock.h>

#define EVENT_TIMER    1
#define EVENT_KEYBOARD 2
#define EVENT_SOFTWARE 3

#define MAX_WAIT_OBJECTS 16
#define MAX_EVENT_SOURCES  32

typedef struct wait_object wait_object_t;

typedef struct event_source {
	int id;
	int type;
	int ready;
	uint32_t period_ms;
	uint32_t next_fire_tick;
	wait_object_t *parent;
	struct event_source *next;
} event_source_t;

struct wait_object {
	int wid;
	unsigned int owner_pid;
	int in_use;
	spinlock_t lk;
	event_source_t *sources;
};

void waitobj_init(void);

int waitobj_create(unsigned int owner_pid);
int waitobj_add(unsigned int owner_pid, int wid, int type, uint32_t arg_ms);
/* Returns 0 on success, -1 on timeout, -2 on invalid arguments. */
int waitobj_wait(unsigned int owner_pid, int wid, unsigned int timeout_ms,
		  int *out_src_id);
int waitobj_signal_source(unsigned int owner_pid, int source_id);

void event_notify(event_source_t *src);
void waitobj_timer_tick(void);
void waitobj_console_input_notify(void);
void waitobj_on_timer_irq(void);

#endif /* _KERN_ */

#endif /* !_KERN_SYNC_WAITOBJ_H_ */
