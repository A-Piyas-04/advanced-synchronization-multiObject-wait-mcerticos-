#ifndef _KERN_TRAP_TSYSCALL_H_
#define _KERN_TRAP_TSYSCALL_H_

#ifdef _KERN_

void sys_puts(tf_t *tf);
void sys_spawn(tf_t *tf);
void sys_yield(tf_t *tf);
void sys_waitobj_create(tf_t *tf);
void sys_waitobj_add(tf_t *tf);
void sys_waitobj_wait(tf_t *tf);
void sys_exit(tf_t *tf);

#endif  /* _KERN_ */

#endif  /* !_KERN_TRAP_TSYSCALL_H_ */
