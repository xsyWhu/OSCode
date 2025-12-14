#ifndef __TRAP_H__
#define __TRAP_H__

#include "common.h"

/*
 * 在 RISC-V 里 trap 分为 interrupt 和 exception：
 *  - interrupt：异步（和当前执行的指令无关），返回时执行下一条指令
 *  - exception：同步（由当前指令触发），返回时通常重新执行/跳过那条指令
 */

// 中断处理函数类型（用在中断向量表里）
typedef void (*interrupt_handler_t)(void);

// 顶层 trap 接口（main.c 调这个层）
void trap_init(void);        // 全局一次性初始化（CPU0）
void trap_inithart(void);    // 每个 hart 本地初始化

// 兼容旧名字（别的文件还在用也没事）
void trap_kernel_init(void);
void trap_kernel_inithart(void);

// S 态 trap 的 C 入口（在 trap.S 的 kernel_vector 里调用）
void trap_kernel_handler(void);
void usertrapret(void);

// 中断注册与控制接口（任务3）
void register_interrupt(int irq, interrupt_handler_t handler);
void enable_interrupt(int irq);
void disable_interrupt(int irq);

// 辅助函数: 外设中断和时钟中断处理
void external_interrupt_handler(void);
void timer_interrupt_handler(void);

#endif
