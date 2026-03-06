## OS Lab Project Base Template

### How to Run

1.  **Build and Run**:
    Execute the following command in the terminal to build the kernel and user programs, and start the QEMU emulator:
    ```bash
    make qemu
    ```
    This will launch the mCertiKOS shell.

2.  **Run Waitset Demo**:
    Inside the mCertiKOS shell, use the `spawn` command with ID `6` to run the `waitset_demo` application:
    ```
    $ spawn 6
    ```
    The demo will:
    -   Create a waitset.
    -   Register for `SIGUSR1` and `SIGUSR2`.
    -   Register for IPC from PID 2.
    -   Send `SIGUSR1` to itself.
    -   Wait for events and print the triggered event.

### Project Modifications

Implemented **Advanced Synchronization – Wait for Multiple Objects**.

#### Features
-   **Waitset Object**: Aggregates multiple notification sources.
-   **Sources**: Supports Signals and IPC.
-   **System Calls**:
    -   `sys_waitset_create`: Create a waitset.
    -   `sys_waitset_ctl`: Register/Unregister sources.
    -   `sys_waitset_wait`: Wait for events.

#### Modified Files
-   **Kernel**:
    -   `kern/sync/waitset.h`, `kern/sync/waitset.c`: Core implementation.
    -   `kern/trap/TSyscall/TSyscall.c`: Syscall handlers and hooks.
    -   `kern/trap/TDispatch/TDispatch.c`: Syscall dispatch.
    -   `kern/lib/syscall.h`: Syscall numbers.
    -   `kern/init/init.c`: Initialization.
    -   `kern/Makefile.inc`: Build configuration.
-   **User**:
    -   `user/include/syscall.h`: User-level API.
    -   `user/waitset_demo.c`: Demo application.
    -   `user/shell/shell.c`: Added `spawn 6` support.
    -   `user/Makefile.inc`: Added `waitset_demo` build rules.
