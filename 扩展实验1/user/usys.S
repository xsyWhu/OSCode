.macro SYSCALL name, num
    .globl \name
\name:
    li a7, \num
    ecall
    ret
.endm

SYSCALL fork,    1
SYSCALL exit,    2
SYSCALL wait,    3
SYSCALL kill,    4
SYSCALL getpid,  5
SYSCALL pipe,    6
SYSCALL open,    7
SYSCALL close,   8
SYSCALL read,    9
SYSCALL write,   10
SYSCALL sbrk,    11
SYSCALL exec,    12
SYSCALL mkdir,   13
SYSCALL mknod,   14
SYSCALL chdir,   15
SYSCALL fstat,   16
SYSCALL unlink,  17
SYSCALL link,    18
SYSCALL dup,     19
SYSCALL setpriority, 20
SYSCALL getpriority, 21
SYSCALL klog, 22
SYSCALL msgget, 23
SYSCALL msgsend, 24
SYSCALL msgrecv, 25
