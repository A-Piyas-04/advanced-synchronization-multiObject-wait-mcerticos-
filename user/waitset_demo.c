#include <proc.h>
#include <stdio.h>
#include <syscall.h>
#include <x86.h>

static const char *event_name(const struct wait_event *ev)
{
    if (ev->source_type == WS_SOURCE_SIGNAL) {
        if (ev->source_id == SIGUSR1) return "SIGUSR1";
        if (ev->source_id == SIGUSR2) return "SIGUSR2";
        return "SIGNAL";
    }

    if (ev->source_type == WS_SOURCE_IPC) return "IPC";
    return "UNKNOWN";
}

int main(int argc, char **argv)
{
    int wsid;
    int ret;
    struct wait_event events[4];
    int n, i;
    int target_pid = 0;

    printf("[demo] Starting waitset demo\n");

    /* Step 1: create waitset */
    printf("[demo] Creating waitset...\n");
    wsid = sys_waitset_create();
    if (wsid < 0) {
        printf("[demo] Failed to create waitset.\n");
        return 1;
    }
    printf("[demo] Created waitset ID: %d\n", wsid);

    /* Step 2: register multiple sources */
    ret = sys_waitset_ctl(wsid, WS_CTL_ADD, WS_SOURCE_SIGNAL, SIGUSR1, WS_EVENT_SIGNAL);
    if (ret < 0) {
        printf("[demo] Failed to register SIGUSR1.\n");
        return 1;
    }
    printf("[demo] Registered SIGUSR1\n");

    ret = sys_waitset_ctl(wsid, WS_CTL_ADD, WS_SOURCE_SIGNAL, SIGUSR2, WS_EVENT_SIGNAL);
    if (ret < 0) {
        printf("[demo] Failed to register SIGUSR2.\n");
        return 1;
    }
    printf("[demo] Registered SIGUSR2\n");

    ret = sys_waitset_ctl(wsid, WS_CTL_ADD, WS_SOURCE_IPC, 2, WS_EVENT_IPC);
    if (ret < 0) {
        printf("[demo] Failed to register IPC source.\n");
        return 1;
    }
    printf("[demo] Registered IPC\n");
    printf("\n");

    /* Step 3: polling mode */
    printf("[demo] Polling wait...\n");
    n = sys_waitset_wait(wsid, events, 4, 0);
    if (n < 0) {
        printf("[demo] Poll failed.\n");
        return 1;
    }
    if (n == 0) {
        printf("[demo] No event ready\n");
    } else {
        for (i = 0; i < n; i++) {
            printf("[demo] Event triggered: %s\n", event_name(&events[i]));
        }
    }
    printf("\n");

    /* Step 4: trigger events */
    printf("[demo] Sending SIGUSR1\n");
    sys_kill(target_pid, SIGUSR1);
    printf("[demo] Sending SIGUSR2\n");
    sys_kill(target_pid, SIGUSR2);
    printf("[demo] IPC source is registered (trigger when matching IPC arrives)\n");
    printf("\n");

    /* Step 5: blocking mode */
    printf("[demo] Waiting for events...\n");
    n = sys_waitset_wait(wsid, events, 4, -1);
    if (n < 0) {
        printf("[demo] Wait failed.\n");
        return 1;
    }

    /* Step 6: report all triggered events */
    for (i = 0; i < n; i++) {
        printf("[demo] Event triggered: %s\n", event_name(&events[i]));
    }

    return 0;
}
