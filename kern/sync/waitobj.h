#ifndef _KERN_SYNC_WAITOBJ_H_
#define _KERN_SYNC_WAITOBJ_H_

#ifdef _KERN_

#include <lib/types.h>
#include <lib/spinlock.h>

#define WAITOBJ_MAX 64
#define WAITOBJ_MAX_SOURCES 8

typedef enum {
    WAITOBJ_SRC_NONE = 0,
    WAITOBJ_SRC_TIMER = 1
} waitobj_source_type_t;

typedef struct wait_event_source {
    int type;
} wait_event_source_t;

typedef struct wait_object {
    spinlock_t lock;
    int used;
    int owner;
    int ready;
    int nsources;
    wait_event_source_t sources[WAITOBJ_MAX_SOURCES];
} wait_object_t;

void waitobj_init(void);
int waitobj_create(int owner);
int waitobj_add(int woid, int type);
int waitobj_wait(int woid);
void waitobj_notify_timer(void);
void waitobj_cleanup_owner(int owner);

#endif

#endif

