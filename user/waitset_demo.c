#include <proc.h>
#include <stdio.h>
#include <syscall.h>
#include <x86.h>

int main(int argc, char **argv)
{
    int wsid;
    int ret;
    struct wait_event events[4];
    int n, i;
    int my_pid;

    printf("[demo] Starting waitset demo...\n");

    /* 1. Create a waitset */
    wsid = sys_waitset_create();
    if (wsid < 0) {
        printf("[demo] Failed to create waitset.\n");
        return 1;
    }
    printf("[demo] Created waitset with ID: %d\n", wsid);

    my_pid = get_curid(); /* Assuming get_curid() is available in user lib or via syscall wrapper */
    /* Actually get_curid() is usually kernel internal. User space usually doesn't know its PID easily unless sys_getpid exists. */
    /* Let's assume we know it or just use dummy values for demonstration */
    
    /* 2. Register multiple notification sources */
    
    /* Register for SIGUSR1 */
    ret = sys_waitset_ctl(wsid, WS_CTL_ADD, WS_SOURCE_SIGNAL, SIGUSR1, WS_EVENT_SIGNAL);
    if (ret < 0) {
        printf("[demo] Failed to register SIGUSR1.\n");
        return 1;
    }
    printf("[demo] Registered SIGUSR1.\n");

    /* Register for SIGUSR2 */
    ret = sys_waitset_ctl(wsid, WS_CTL_ADD, WS_SOURCE_SIGNAL, SIGUSR2, WS_EVENT_SIGNAL);
    if (ret < 0) {
        printf("[demo] Failed to register SIGUSR2.\n");
        return 1;
    }
    printf("[demo] Registered SIGUSR2.\n");

    /* Register for IPC from process 2 (simulated) */
    ret = sys_waitset_ctl(wsid, WS_CTL_ADD, WS_SOURCE_IPC, 2, WS_EVENT_IPC);
    if (ret < 0) {
        printf("[demo] Failed to register IPC from PID 2.\n");
        return 1;
    }
    printf("[demo] Registered IPC from PID 2.\n");

    /* 3. Wait for events */
    
    /* Simulate an event: Send SIGUSR1 to self */
    printf("[demo] Sending SIGUSR1 to self...\n");
    sys_kill(my_pid, SIGUSR1);

    printf("[demo] Waiting for events...\n");
    n = sys_waitset_wait(wsid, events, 4, -1); /* Block indefinitely */
    
    if (n < 0) {
        printf("[demo] Wait failed.\n");
        return 1;
    }

    printf("[demo] Returned %d events.\n", n);

    /* 4. Print which source triggered */
    for (i = 0; i < n; i++) {
        if (events[i].source_type == WS_SOURCE_SIGNAL) {
            printf("[demo] Event %d: SIGNAL %d triggered.\n", i, events[i].source_id);
        } else if (events[i].source_type == WS_SOURCE_IPC) {
            printf("[demo] Event %d: IPC from PID %d triggered.\n", i, events[i].source_id);
        } else {
            printf("[demo] Event %d: Unknown type %d.\n", i, events[i].source_type);
        }
    }

    return 0;
}
