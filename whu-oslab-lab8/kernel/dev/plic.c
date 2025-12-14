// Platform-level interrupt controller

#include "memlayout.h"
#include "dev/plic.h"
#include "proc/proc.h"

// PLIC初始化
void plic_init()
{
    // 设置中断优先级
    *(uint32*)(PLIC_PRIORITY(UART_IRQ)) = 1;
    *(uint32*)(PLIC_PRIORITY(VIRTIO_IRQ)) = 1;
}

// PLIC核心初始化
void plic_inithart()
{   
    int hartid = mycpuid();
    // 这里只设置阈值，不打开具体设备中断
    *(uint32*)PLIC_SPRIORITY(hartid) = 0;
}

// 获取中断号
int plic_claim(void)
{
    int hartid = mycpuid();
    int irq = *(uint32*)PLIC_SCLAIM(hartid);
    return irq;
}

// 确认该中断号对应中断已经完成
void plic_complete(int irq)
{
    int hartid = mycpuid();
    *(uint32*)PLIC_SCLAIM(hartid) = irq;
}
