#ifndef _KERN_SYNC_WAITSET_H_
#define _KERN_SYNC_WAITSET_H_

#ifdef _KERN_

#include <lib/queue.h>
#include <lib/spinlock.h>
#include <lib/types.h>


#define MAX_WAITSETS 64
#define MAX_SOURCES_PER_WAITSET 32

/* Event Types */
#define WS_EVENT_NONE 0
#define WS_EVENT_READ 1
#define WS_EVENT_WRITE 2
#define WS_EVENT_ERROR 4
#define WS_EVENT_SIGNAL 8
#define WS_EVENT_IPC 16

/* Source Types */
#define WS_SOURCE_IPC 1
#define WS_SOURCE_SIGNAL 2

/* Operations for waitset_ctl */
#define WS_CTL_ADD 1
#define WS_CTL_DEL 2
#define WS_CTL_MOD 3

struct wait_event {
    int source_type;
    int source_id;
    int events;
    void *data;
};

struct notif_source {
    SLIST_ENTRY(notif_source) entry;
    SLIST_ENTRY(notif_source) triggered_entry;
    int type;
    int id;
    int events;
    int triggered; // Flag to check if already in triggered list
    void *data;
    struct waitset *ws;
};

struct waitset {
    spinlock_t lock;
    SLIST_HEAD(source_list, notif_source) sources;
    SLIST_HEAD(triggered_list, notif_source) triggered;
    unsigned int waiting_thread; // PID of waiting thread, or NUM_IDS if none
    unsigned int owner_pid; // PID of the process that owns this waitset
    int active; // Is this waitset allocated?
};

/* Function Prototypes */
void waitset_init(void);
int waitset_create(void);
int waitset_ctl(int wsid, int op, int type, int id, int events, void *data);
int waitset_wait(int wsid, struct wait_event *events, int maxevents, int timeout);
void waitset_notify(int target_pid, int type, int id, int event);
void waitset_notify_ipc(int recv_pid, int send_pid);
void waitset_notify_signal(int pid, int signum);

#endif /* _KERN_ */

#endif /* !_KERN_SYNC_WAITSET_H_ */
