#ifndef __SYSCALL_NUMBERS_H__
#define __SYSCALL_NUMBERS_H__

// 系统调用号定义（需与用户态 ecall 对应）
#define SYS_exit    1
#define SYS_getpid  2
#define SYS_write   3
#define SYS_open    4
#define SYS_read    5
#define SYS_close   6
#define SYS_unlink  7

#endif
