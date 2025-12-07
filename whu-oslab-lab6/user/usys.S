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
SYSCALL open,    6
SYSCALL close,   7
SYSCALL read,    8
SYSCALL write,   9
SYSCALL sbrk,    10
SYSCALL exec,    11
