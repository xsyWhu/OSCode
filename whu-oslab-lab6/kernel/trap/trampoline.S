#include "memlayout.h"

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

    .section .trampsec,"ax",@progbits
    .align 4
    .globl trampoline
trampoline:
    .globl uservec
uservec:
    csrrw a0, sscratch, a0        # a0 <- TRAPFRAME (虚拟地址), sscratch <- 用户a0
    sd ra, TF_RA(a0)
    sd sp, TF_SP(a0)
    sd gp, TF_GP(a0)
    sd tp, TF_TP(a0)
    sd t0, TF_T0(a0)
    sd t1, TF_T1(a0)
    sd t2, TF_T2(a0)
    sd s0, TF_S0(a0)
    sd s1, TF_S1(a0)
    sd a1, TF_A1(a0)
    sd a2, TF_A2(a0)
    sd a3, TF_A3(a0)
    sd a4, TF_A4(a0)
    sd a5, TF_A5(a0)
    sd a6, TF_A6(a0)
    sd a7, TF_A7(a0)
    sd s2, TF_S2(a0)
    sd s3, TF_S3(a0)
    sd s4, TF_S4(a0)
    sd s5, TF_S5(a0)
    sd s6, TF_S6(a0)
    sd s7, TF_S7(a0)
    sd s8, TF_S8(a0)
    sd s9, TF_S9(a0)
    sd s10, TF_S10(a0)
    sd s11, TF_S11(a0)
    sd t3, TF_T3(a0)
    sd t4, TF_T4(a0)
    sd t5, TF_T5(a0)
    sd t6, TF_T6(a0)
    csrr t0, sscratch            # 取回用户a0
    sd t0, TF_A0(a0)

    ld t0, TF_KERNEL_SATP(a0)
    ld t1, TF_KERNEL_SP(a0)
    ld t2, TF_KERNEL_TRAP(a0)
    mv sp, t1
    csrw satp, t0
    sfence.vma zero, zero
    jr t2

    .globl userret
userret:
    # a0: 用户页表的 satp 值
    csrw satp, a0
    sfence.vma zero, zero
    li t2, TRAPFRAME
    csrw sscratch, t2

    # 恢复寄存器（除了最后的 t2）
    ld ra, TF_RA(t2)
    ld sp, TF_SP(t2)
    ld gp, TF_GP(t2)
    ld tp, TF_TP(t2)
    ld t0, TF_T0(t2)
    ld t1, TF_T1(t2)
    ld s0, TF_S0(t2)
    ld s1, TF_S1(t2)
    ld a0, TF_A0(t2)
    ld a1, TF_A1(t2)
    ld a2, TF_A2(t2)
    ld a3, TF_A3(t2)
    ld a4, TF_A4(t2)
    ld a5, TF_A5(t2)
    ld a6, TF_A6(t2)
    ld a7, TF_A7(t2)
    ld s2, TF_S2(t2)
    ld s3, TF_S3(t2)
    ld s4, TF_S4(t2)
    ld s5, TF_S5(t2)
    ld s6, TF_S6(t2)
    ld s7, TF_S7(t2)
    ld s8, TF_S8(t2)
    ld s9, TF_S9(t2)
    ld s10, TF_S10(t2)
    ld s11, TF_S11(t2)
    ld t3, TF_T3(t2)
    ld t4, TF_T4(t2)
    ld t5, TF_T5(t2)
    ld t6, TF_T6(t2)
    ld t2, TF_T2(t2)

    sret
