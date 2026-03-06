#ifndef _USER_SYSCALL_H_
#define _USER_SYSCALL_H_
#include <stdio.h>
#include <lib/syscall.h>

#include <debug.h>
#include <gcc.h>
#include <proc.h>
#include <types.h>
#include <x86.h>
#include <file.h>
#include <signal.h>

/* Waitset Event Types */
#define WS_EVENT_NONE 0
#define WS_EVENT_READ 1
#define WS_EVENT_WRITE 2
#define WS_EVENT_ERROR 4
#define WS_EVENT_SIGNAL 8
#define WS_EVENT_IPC 16

/* Waitset Source Types */
#define WS_SOURCE_IPC 1
#define WS_SOURCE_SIGNAL 2

/* Waitset Control Operations */
#define WS_CTL_ADD 1
#define WS_CTL_DEL 2
#define WS_CTL_MOD 3

struct wait_event {
    int source_type;
    int source_id;
    int events;
    void *data;
};

static gcc_inline void
sys_puts(const char *s, size_t len)
{
	asm volatile("int %0" :
		     : "i" (T_SYSCALL),
		       "a" (SYS_puts),
		       "b" (s),
		       "c" (len)
		     : "cc", "memory");
}

// TODO : assign 6 ipc, added by sbo
static gcc_inline void
sys_sync_send(int recv_pid, const char* addr, size_t len)
{
	asm volatile("int %0" :
		     : "i" (T_SYSCALL),
		       "a" (SYS_sync_send),
		       "b" (recv_pid),
		       "c" (addr),
                       "d" (len)
		     : "cc", "memory");
}
static gcc_inline int
sys_sync_receive(int send_pid, char* addr, size_t len)
{
       int errno, length;
	asm volatile("int %2"
                     : "=a" (errno),
                       "=b" (length)
		     : "i" (T_SYSCALL),
		       "a" (SYS_sync_recv),
		       "b" (send_pid),
		       "c" (addr),
                       "d" (len)
		     : "cc", "memory");
       return errno? -1: length;
}
static gcc_inline pid_t

sys_spawn(uintptr_t exec, unsigned int quota)
{
	int errno;
	pid_t pid;

	asm volatile("int %2"
		     : "=a" (errno),
		       "=b" (pid)
		     : "i" (T_SYSCALL),
		       "a" (SYS_spawn),
		       "b" (exec),
		       "c" (quota)
		     : "cc", "memory");

	return errno ? -1 : pid;
}

static gcc_inline void
sys_yield(void)
{
	asm volatile("int %0" :
		     : "i" (T_SYSCALL),
		       "a" (SYS_yield)
		     : "cc", "memory");
}

static gcc_inline void
sys_produce(void)
{
	asm volatile("int %0" :
		     : "i" (T_SYSCALL),
		       "a" (SYS_produce)
		     : "cc", "memory");
}

static gcc_inline void
sys_consume(void)
{
	asm volatile("int %0" :
		     : "i" (T_SYSCALL),
		       "a" (SYS_consume)
		     : "cc", "memory");
}

static gcc_inline int
sys_read(int fd, char *buf, size_t n)
{
	int errno;
	int ret;

	asm volatile("int %2"
		     : "=a" (errno),
		       "=b" (ret)
		     : "i" (T_SYSCALL),
		       "a" (SYS_read),
		       "b" (fd),
		       "c" (buf),
		       "d" (n)
		     : "cc", "memory");

	return errno ? -1 : ret;
}

static gcc_inline int
sys_write(int fd, const char *buf, size_t n)
{
	int errno;
	int ret;

	asm volatile("int %2"
		     : "=a" (errno),
		       "=b" (ret)
		     : "i" (T_SYSCALL),
		       "a" (SYS_write),
		       "b" (fd),
		       "c" (buf),
		       "d" (n)
		     : "cc", "memory");

	return errno ? -1 : ret;
}

static gcc_inline int
sys_sigaction(int signum, const struct sigaction *act, struct sigaction *oldact)
{
	int errno;
	asm volatile("int %1"
		     : "=a" (errno)
		     : "i" (T_SYSCALL),
		       "a" (SYS_sigaction),
		       "b" (signum),
		       "c" (act),
		       "d" (oldact)
		     : "cc", "memory");
	return errno ? -1 : 0;
}

static gcc_inline int
sys_kill(int pid, int signum)
{
	int errno;
	asm volatile("int %1"
		     : "=a" (errno)
		     : "i" (T_SYSCALL),
		       "a" (SYS_kill),
		       "b" (pid),
		       "c" (signum)
		     : "cc", "memory");
	return errno ? -1 : 0;
}

static gcc_inline int
sys_pause(void)
{
	int errno;
	asm volatile("int %1"
		     : "=a" (errno)
		     : "i" (T_SYSCALL),
		       "a" (SYS_pause)
		     : "cc", "memory");
	return errno ? -1 : 0;
}

static gcc_inline int
sys_waitset_create(void)
{
	int errno, wsid;
	asm volatile("int %2"
		     : "=a" (errno),
		       "=b" (wsid)
		     : "i" (T_SYSCALL),
		       "a" (SYS_waitset_create)
		     : "cc", "memory");
	return errno ? -1 : wsid;
}

static gcc_inline int
sys_waitset_ctl(int wsid, int op, int type, int id, int events)
{
	int errno, ret;
	asm volatile("int %2"
		     : "=a" (errno),
		       "=b" (ret)
		     : "i" (T_SYSCALL),
		       "a" (SYS_waitset_ctl),
		       "b" (wsid),
		       "c" (op),
		       "d" (type),
		       "S" (id),
		       "D" (events)
		     : "cc", "memory");
	return errno ? -1 : ret;
}

static gcc_inline int
sys_waitset_wait(int wsid, struct wait_event *events, int maxevents, int timeout)
{
	int errno, count;
	asm volatile("int %2"
		     : "=a" (errno),
		       "=b" (count)
		     : "i" (T_SYSCALL),
		       "a" (SYS_waitset_wait),
		       "b" (wsid),
		       "c" (events),
		       "d" (maxevents),
		       "S" (timeout)
		     : "cc", "memory");
	return errno ? -1 : count;
}

#endif
