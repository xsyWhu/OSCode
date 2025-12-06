/* memory leyout */
#ifndef __MEMLAYOUT_H__
#define __MEMLAYOUT_H__

// Sv39 支持的最大虚拟地址 (与 xv6 相同的简化)
#define MAXVA (1L << (9 + 9 + 9 + 12 - 1))

// 预留的高地址 trampoline / trapframe 映射
#define TRAMPOLINE (MAXVA - 0x1000)
#define TRAPFRAME  (TRAMPOLINE - 0x1000)

// UART 相关
#define UART_BASE  0x10000000ul
#define UART_IRQ   10

// virtio block device MMIO base (first slot) + IRQ; we map a few slots for probing
#define VIRTIO0     0x10001000ul
#define VIRTIO_MMIO_STRIDE 0x1000
#define VIRTIO_MMIO_SLOTS  16
#define VIRTIO0_IRQ 1

// 内核基地址
#define KERNEL_BASE 0x80000000ul
#define PHYSTOP (KERNEL_BASE + 128*1024*1024)

// platform-level interrupt controller(PLIC)
#define PLIC_BASE 0x0c000000ul
#define PLIC_PRIORITY(id) (PLIC_BASE + (id) * 4)
#define PLIC_PENDING (PLIC_BASE + 0x1000)
#define PLIC_MENABLE(hart) (PLIC_BASE + 0x2000 + (hart)*0x100)
#define PLIC_SENABLE(hart) (PLIC_BASE + 0x2080 + (hart)*0x100)
#define PLIC_MPRIORITY(hart) (PLIC_BASE + 0x200000 + (hart)*0x2000)
#define PLIC_SPRIORITY(hart) (PLIC_BASE + 0x201000 + (hart)*0x2000)
#define PLIC_MCLAIM(hart) (PLIC_BASE + 0x200004 + (hart)*0x2000)
#define PLIC_SCLAIM(hart) (PLIC_BASE + 0x201004 + (hart)*0x2000)

// core local interruptor(CLINT)
#define CLINT_BASE 0x2000000ul
#define CLINT_MSIP(hartid) (CLINT_BASE + 4 * (hartid))
#define CLINT_MTIMECMP(hartid) (CLINT_BASE + 0x4000 + 8 * (hartid))
#define CLINT_MTIME (CLINT_BASE + 0xBFF8)

#endif
