/* include/proc/cpu.h */
#ifndef __CPU_H__
#define __CPU_H__

#include "common.h"

struct cpu {
    int id;
    int started;
};

typedef struct cpu cpu_t;

/* 获取当前 hart id（使用 tp 寄存器） */
static inline int cpuid()
{
    int id;
    asm volatile("mv %0, tp" : "=r"(id));
    return id;
}

/* 声明：在 kernel/proc/proc.c 中实现 */
cpu_t* mycpu(void);
int mycpuid(void);

#endif
