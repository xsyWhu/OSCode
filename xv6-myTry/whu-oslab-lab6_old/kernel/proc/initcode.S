#include "syscall.h"

    .section .text
    .align 2
    .globl initcode_start
    .globl initcode_end

initcode_start:
    li a0, 1                # fd = stdout
    la a1, msg              # buffer
    la t0, msg_end
    la t1, msg
    sub a2, t0, t1          # length
    li a7, SYS_write
    ecall

    li a0, 0
    li a7, SYS_exit
    ecall

1:  j 1b                    # never return

    .section .rodata
msg:
    .ascii "Hello from user mode!\n"
msg_end:

initcode_end:
