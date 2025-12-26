    .section .initcode,"ax"
    .globl _initcode
    .globl _initcode_end
_initcode:
    li a0, 0
    li a7, 2
    ecall
_initcode_end:
