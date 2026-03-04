#include <lib/x86.h>
#include <lib/thread.h>
#include <lib/spinlock.h>
#include <thread/PCurID/export.h>

#include <kern/sync/waitobj.h>

static wait_object_t wait_table[WAITOBJ_MAX];
static spinlock_t wait_table_lock;

void waitobj_init(void)
{
    int i;

    spinlock_init(&wait_table_lock);
    for (i = 0; i < WAITOBJ_MAX; i++) {
        wait_table[i].used = 0;
        wait_table[i].owner = -1;
        wait_table[i].ready = 0;
        wait_table[i].nsources = 0;
        spinlock_init(&wait_table[i].lock);
    }
}

static wait_object_t *waitobj_lookup(int woid, int owner)
{
    wait_object_t *wo;

    if (woid < 0 || woid >= WAITOBJ_MAX)
        return 0;

    wo = &wait_table[woid];
    if (!wo->used)
        return 0;
    if (wo->owner != owner)
        return 0;
    return wo;
}

int waitobj_create(int owner)
{
    int i;
    int woid = -1;

    spinlock_acquire(&wait_table_lock);
    for (i = 0; i < WAITOBJ_MAX; i++) {
        if (!wait_table[i].used) {
            woid = i;
            wait_table[i].used = 1;
            wait_table[i].owner = owner;
            wait_table[i].ready = 0;
            wait_table[i].nsources = 0;
            break;
        }
    }
    spinlock_release(&wait_table_lock);

    return woid;
}

int waitobj_add(int woid, int type)
{
    int owner = get_curid();
    wait_object_t *wo;

    wo = waitobj_lookup(woid, owner);
    if (!wo)
        return -1;

    if (type != WAITOBJ_SRC_TIMER)
        return -1;

    spinlock_acquire(&wo->lock);
    if (wo->nsources >= WAITOBJ_MAX_SOURCES) {
        spinlock_release(&wo->lock);
        return -1;
    }

    wo->sources[wo->nsources].type = type;
    wo->nsources++;
    spinlock_release(&wo->lock);

    return 0;
}

int waitobj_wait(int woid)
{
    int owner = get_curid();
    wait_object_t *wo;

    wo = waitobj_lookup(woid, owner);
    if (!wo)
        return -1;

    spinlock_acquire(&wo->lock);
    while (!wo->ready) {
        thread_sleep(wo, &wo->lock);
    }
    wo->ready = 0;
    spinlock_release(&wo->lock);

    return 0;
}

void waitobj_notify_timer(void)
{
    int i;

    for (i = 0; i < WAITOBJ_MAX; i++) {
        wait_object_t *wo = &wait_table[i];
        int j;
        int has_timer = 0;

        if (!wo->used)
            continue;

        if (spinlock_try_acquire(&wo->lock)) {
            for (j = 0; j < wo->nsources; j++) {
                if (wo->sources[j].type == WAITOBJ_SRC_TIMER) {
                    has_timer = 1;
                    break;
                }
            }
            if (has_timer) {
                wo->ready = 1;
                spinlock_release(&wo->lock);
                thread_wakeup(wo);
            } else {
                spinlock_release(&wo->lock);
            }
        }
    }
}

void waitobj_cleanup_owner(int owner)
{
    int i;

    spinlock_acquire(&wait_table_lock);
    for (i = 0; i < WAITOBJ_MAX; i++) {
        wait_object_t *wo = &wait_table[i];

        if (!wo->used)
            continue;
        if (wo->owner != owner)
            continue;

        spinlock_acquire(&wo->lock);
        wo->used = 0;
        wo->owner = -1;
        wo->ready = 0;
        wo->nsources = 0;
        spinlock_release(&wo->lock);
    }
    spinlock_release(&wait_table_lock);
}

