#include <proc.h>
#include <stdio.h>
#include <syscall.h>

int main(int argc, char **argv)
{
    int woid;
    int i;

    woid = sys_waitobj_create();
    if (woid < 0) {
        printf("waitobj_create failed\n");
        return 0;
    }

    if (sys_waitobj_add(woid, WAITOBJ_SRC_TIMER) < 0) {
        printf("waitobj_add failed\n");
        return 0;
    }

    printf("wait_demo: waiting on timer events\n");

    for (i = 0; i < 5; i++) {
        if (sys_waitobj_wait(woid) < 0) {
            printf("waitobj_wait failed\n");
            break;
        }
        printf("wait_demo: woke up %d\n", i + 1);
    }

    sys_exit();
    return 0;
}

