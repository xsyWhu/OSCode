#include "memlayout.h"

    .section .text
    .align 4
    .globl trampoline
trampoline:

// struct trapframe field offsets (see include/proc/trapframe.h)
#define TF_KERNEL_SATP   0
#define TF_KERNEL_SP     8
#define TF_KERNEL_TRAP   16
#define TF_EPC           24
#define TF_KERNEL_HARTID 32
#define TF_RA            40
#define TF_SP            48
#define TF_GP            56
#define TF_TP            64
#define TF_T0            72
#define TF_T1            80
#define TF_T2            88
#define TF_S0            96
#define TF_S1            104
#define TF_A0            112
#define TF_A1            120
#define TF_A2            128
#define TF_A3            136
#define TF_A4            144
#define TF_A5            152
#define TF_A6            160
#define TF_A7            168
#define TF_S2            176
#define TF_S3            184
#define TF_S4            192
#define TF_S5            200
#define TF_S6            208
#define TF_S7            216
#define TF_S8            224
#define TF_S9            232
#define TF_S10           240
#define TF_S11           248
#define TF_T3            256
#define TF_T4            264
#define TF_T5            272
#define TF_T6            280

    .globl uservec
uservec:
    // swap user stack pointer (in sscratch) with kernel pointer to trapframe
    csrrw sp, sscratch, sp

    sd ra, TF_RA(sp)
    sd sp, TF_SP(sp)
    sd gp, TF_GP(sp)
    sd tp, TF_TP(sp)
    sd t0, TF_T0(sp)
    sd t1, TF_T1(sp)
    sd t2, TF_T2(sp)
    sd s0, TF_S0(sp)
    sd s1, TF_S1(sp)
    sd a0, TF_A0(sp)
    sd a1, TF_A1(sp)
    sd a2, TF_A2(sp)
    sd a3, TF_A3(sp)
    sd a4, TF_A4(sp)
    sd a5, TF_A5(sp)
    sd a6, TF_A6(sp)
    sd a7, TF_A7(sp)
    sd s2, TF_S2(sp)
    sd s3, TF_S3(sp)
    sd s4, TF_S4(sp)
    sd s5, TF_S5(sp)
    sd s6, TF_S6(sp)
    sd s7, TF_S7(sp)
    sd s8, TF_S8(sp)
    sd s9, TF_S9(sp)
    sd s10, TF_S10(sp)
    sd s11, TF_S11(sp)
    sd t3, TF_T3(sp)
    sd t4, TF_T4(sp)
    sd t5, TF_T5(sp)
    sd t6, TF_T6(sp)

    csrr t0, sepc
    sd t0, TF_EPC(sp)

    ld t0, TF_KERNEL_HARTID(sp)
    mv tp, t0

    ld t0, TF_KERNEL_SATP(sp)
    csrw satp, t0
    sfence.vma zero, zero

    ld sp, TF_KERNEL_SP(sp)
    ld t0, TF_KERNEL_TRAP(sp)
    jr t0

    .globl userret
userret:
    // a0: trapframe pointer (lower mapped at TRAPFRAME)
    // a1: satp value for user pagetable
    csrw satp, a1
    sfence.vma zero, zero

    // place trapframe pointer in sscratch so uservec can locate it
    csrw sscratch, a0

    ld t0, TF_EPC(a0)
    csrw sepc, t0

    ld ra, TF_RA(a0)
    ld sp, TF_SP(a0)
    ld gp, TF_GP(a0)
    ld t0, TF_TP(a0)
    mv tp, t0

    ld t0, TF_T0(a0)
    ld t1, TF_T1(a0)
    ld t2, TF_T2(a0)
    ld s0, TF_S0(a0)
    ld s1, TF_S1(a0)
    ld a0, TF_A0(a0)
    ld a1, TF_A1(a0)
    ld a2, TF_A2(a0)
    ld a3, TF_A3(a0)
    ld a4, TF_A4(a0)
    ld a5, TF_A5(a0)
    ld a6, TF_A6(a0)
    ld a7, TF_A7(a0)
    ld s2, TF_S2(a0)
    ld s3, TF_S3(a0)
    ld s4, TF_S4(a0)
    ld s5, TF_S5(a0)
    ld s6, TF_S6(a0)
    ld s7, TF_S7(a0)
    ld s8, TF_S8(a0)
    ld s9, TF_S9(a0)
    ld s10, TF_S10(a0)
    ld s11, TF_S11(a0)
    ld t3, TF_T3(a0)
    ld t4, TF_T4(a0)
    ld t5, TF_T5(a0)
    ld t6, TF_T6(a0)

    sret
