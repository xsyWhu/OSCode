# Lab3 —— 内存管理子系统实现

## 组织结构
### 代码组织结构

whu-oslab-lab3
├── include 
│   ├── uart.h                         // (串口头文件)
│   ├── lib 
│   │   ├── print.h                    // (打印库头文件)
│   │   └── lock.h                     // (锁/自旋锁头文件)
│   ├── proc 
│   │   ├── cpu.h                      // (CPU/Hart 相关)
│   │   └── proc.h                     // (进程/线程相关)
│   ├── mem 
│   │   ├── pmem.h                     // (新增: 物理内存管理接口)
│   │   └── vmem.h                     // (新增: 虚拟内存/页表管理接口)
│   ├── common.h                       // (基本类型定义)
│   ├── memlayout.h                    // (物理内存布局)
│   └── riscv.h                        // (RISC-V 寄存器和页宏)
├── kernel 
│   ├── boot 
│   │   ├── main.c                     // (内核入口，包含 pmem/vmem 测试)
│   │   ├── start.c                    // (启动代码 C 部分)
│   │   ├── entry.S                    // (启动代码 汇编部分)
│   │   └── Makefile 
│   ├── dev 
│   │   ├── uart.c                     // (串口驱动实现)
│   │   └── Makefile 
│   ├── lib 
│   │   ├── print.c                    // (打印库实现)
│   │   ├── spinlock.c                 // (自旋锁实现)
│   │   └── Makefile 
│   ├── proc 
│   │   ├── pro.c                      // (进程/线程实现)
│   │   └── Makefile 
│   ├── mem 
│   │   ├── pmem.c                     // (新增: 物理内存分配器实现)
│   │   ├── vmem.c                     // (新增: 页表管理与虚拟内存初始化)
│   │   └── Makefile                   // (mem 目录下的 Makefile)
│   ├── Makefile 
│   └── kernel.ld                      // (链接脚本)
├── picture 
│   └── *.png                          // (图片资源)
├── Makefile                           // (顶层 Makefile)
├── common.mk                          // (通用配置)
├── README.md                          // (项目说明)
└── Report.md                          // (实验报告)

## Lab3实验说明

本次实验主要实现：

内存管理相关的功能——alloc_pages、destroy_pagetables等

运行方法：

bash：
make qemu

预期效果：

成功通过五个测试——map 、 unmap 、连续alloc 、destroy 、 对齐检查及double free check——panic
