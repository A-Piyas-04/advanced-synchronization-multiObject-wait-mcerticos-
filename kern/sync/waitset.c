#include <kern/sync/waitset.h>
#include <kern/lib/spinlock.h>
#include <kern/lib/debug.h>
#include <kern/lib/types.h>
#include <thread/PThread/export.h>
#include <thread/PTCBIntro/export.h>
#include <thread/PCurID/export.h>

#define MAX_SOURCES (MAX_WAITSETS * MAX_SOURCES_PER_WAITSET)

struct waitset waitset_pool[MAX_WAITSETS];
struct notif_source source_pool[MAX_SOURCES];

/* Free list for sources */
struct notif_source *free_sources;
spinlock_t source_pool_lock;

void waitset_init(void)
{
    int i;
    spinlock_init(&source_pool_lock);
    
    /* Initialize waitsets */
    for (i = 0; i < MAX_WAITSETS; i++) {
        spinlock_init(&waitset_pool[i].lock);
        TAILQ_INIT(&waitset_pool[i].sources);
        TAILQ_INIT(&waitset_pool[i].triggered);
        waitset_pool[i].active = 0;
        waitset_pool[i].waiting_thread = NUM_IDS;
    }

    /* Initialize source pool */
    /* Link all sources into a free list */
    free_sources = &source_pool[0];
    for (i = 0; i < MAX_SOURCES - 1; i++) {
        /* Abuse 'ws' pointer as next pointer for free list */
        source_pool[i].ws = (struct waitset *)&source_pool[i+1];
    }
    source_pool[MAX_SOURCES - 1].ws = NULL;
}

static struct notif_source *source_alloc(void)
{
    struct notif_source *s;
    spinlock_acquire(&source_pool_lock);
    s = free_sources;
    if (s) {
        free_sources = (struct notif_source *)s->ws;
        s->ws = NULL; 
    }
    spinlock_release(&source_pool_lock);
    return s;
}

static void source_free(struct notif_source *s)
{
    spinlock_acquire(&source_pool_lock);
    s->ws = (struct waitset *)free_sources;
    free_sources = s;
    spinlock_release(&source_pool_lock);
}

int waitset_create(void)
{
    int i;
    for (i = 0; i < MAX_WAITSETS; i++) {
        spinlock_acquire(&waitset_pool[i].lock);
        if (!waitset_pool[i].active) {
            waitset_pool[i].active = 1;
            waitset_pool[i].owner_pid = get_curid();
            TAILQ_INIT(&waitset_pool[i].sources);
            TAILQ_INIT(&waitset_pool[i].triggered);
            waitset_pool[i].waiting_thread = NUM_IDS;
            spinlock_release(&waitset_pool[i].lock);
            return i;
        }
        spinlock_release(&waitset_pool[i].lock);
    }
    return -1;
}

int waitset_ctl(int wsid, int op, int type, int id, int events, void *data)
{
    struct waitset *ws;
    struct notif_source *s;
    struct notif_source *target = NULL;

    if (wsid < 0 || wsid >= MAX_WAITSETS) return -1;
    ws = &waitset_pool[wsid];

    spinlock_acquire(&ws->lock);
    if (!ws->active || ws->owner_pid != get_curid()) {
        spinlock_release(&ws->lock);
        return -1;
    }

    /* Find existing source if any */
    TAILQ_FOREACH(s, &ws->sources, entry) {
        if (s->type == type && s->id == id) {
            target = s;
            break;
        }
    }

    if (op == WS_CTL_ADD) {
        if (target) {
            spinlock_release(&ws->lock);
            return -1; /* Already exists */
        }
        s = source_alloc();
        if (!s) {
            spinlock_release(&ws->lock);
            return -1; /* No memory */
        }
        s->type = type;
        s->id = id;
        s->events = events;
        s->triggered = 0;
        s->data = data;
        s->ws = ws;
        TAILQ_INSERT_TAIL(&ws->sources, s, entry);
    } else if (op == WS_CTL_DEL) {
        if (!target) {
            spinlock_release(&ws->lock);
            return -1;
        }
        TAILQ_REMOVE(&ws->sources, target, entry);
        if (target->triggered) {
            TAILQ_REMOVE(&ws->triggered, target, triggered_entry);
        }
        source_free(target);
    } else if (op == WS_CTL_MOD) {
        if (!target) {
            spinlock_release(&ws->lock);
            return -1;
        }
        target->events = events;
        target->data = data;
    }

    spinlock_release(&ws->lock);
    return 0;
}

int waitset_wait(int wsid, struct wait_event *events, int maxevents, int timeout)
{
    struct waitset *ws;
    struct notif_source *s;
    int count = 0;
    int i;

    if (wsid < 0 || wsid >= MAX_WAITSETS) return -1;
    ws = &waitset_pool[wsid];

    spinlock_acquire(&ws->lock);
    if (!ws->active || ws->owner_pid != get_curid()) {
        spinlock_release(&ws->lock);
        return -1;
    }

    /* Check triggered events */
    while (TAILQ_EMPTY(&ws->triggered)) {
        /* No events. Should we wait? */
        /* If timeout == 0, return immediately (polling) */
        
        if (timeout == 0) {
            spinlock_release(&ws->lock);
            return 0;
        }

        /* Go to sleep */
        ws->waiting_thread = get_curid();
        
        /* thread_sleep releases lock and sleeps */
        thread_sleep(ws, &ws->lock);
        
        /* Re-acquired lock automatically upon return */
        /* Check active again just in case */
        if (!ws->active) {
            spinlock_release(&ws->lock);
            return -1;
        }
        
        /* Loop back to check triggered list */
    }

    /* We have triggered events */
    s = TAILQ_FIRST(&ws->triggered);
    while (s && count < maxevents) {
        struct notif_source *next = TAILQ_NEXT(s, triggered_entry);
        
        events[count].source_type = s->type;
        events[count].source_id = s->id;
        events[count].events = s->events; 
        events[count].data = s->data;
        
        TAILQ_REMOVE(&ws->triggered, s, triggered_entry);
        s->triggered = 0;
        
        count++;
        s = next;
    }
    
    ws->waiting_thread = NUM_IDS;
    spinlock_release(&ws->lock);
    return count;
}

void waitset_notify(int target_pid, int type, int id, int event)
{
    int i;
    struct waitset *ws;
    struct notif_source *s;
    
    for (i = 0; i < MAX_WAITSETS; i++) {
        ws = &waitset_pool[i];
        
        if (!ws->active) continue;
        
        spinlock_acquire(&ws->lock);
        if (!ws->active) {
            spinlock_release(&ws->lock);
            continue;
        }

        /* Check if this waitset belongs to target_pid */
        if (ws->owner_pid != target_pid) {
            spinlock_release(&ws->lock);
            continue;
        }
        
        /* Check sources */
        TAILQ_FOREACH(s, &ws->sources, entry) {
            if (s->type == type && (s->id == id || s->id == -1)) { /* Support wildcard id -1? */
                /* Found a match */
                if (!s->triggered) {
                    s->triggered = 1;
                    TAILQ_INSERT_TAIL(&ws->triggered, s, triggered_entry);
                    
                    if (ws->waiting_thread != NUM_IDS) {
                        thread_wakeup(ws);
                    }
                }
            }
        }
        spinlock_release(&ws->lock);
    }
}

void waitset_notify_ipc(int recv_pid, int send_pid)
{
    waitset_notify(recv_pid, WS_SOURCE_IPC, send_pid, WS_EVENT_IPC);
}

void waitset_notify_signal(int pid, int signum)
{
    waitset_notify(pid, WS_SOURCE_SIGNAL, signum, WS_EVENT_SIGNAL);
}
