/* kernel/proc/proc.c */
#include "proc/cpu.h"

/* 占位数组（NCPU 从 include/common.h 获得） */
cpu_t cpus[NCPU];

cpu_t* mycpu(void)
{
    return &cpus[cpuid()];
}

int mycpuid(void)
{
    return cpuid();
}

/* 如果需要可以添加 proc_init() 等函数 */
