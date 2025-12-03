# Lab4 —— 中断

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

## Lab4实验说明

### 这次中断实验我主要完成了三块内容：
① RISC-V 中断寄存器与委托的初始化；
② 内核 trap 框架：上下文保存/恢复 + 中断/异常分发；
③ 时钟中断处理与测试函数（test_xxx 调试三件套）。

（1）
#### 在机器模式初始化代码里
我按照讲义要求配置了 RISC-V 的中断相关 CSR：设置 mideleg / medeleg，把时钟中断等委托给 S 模式来处理，符合“M 模式兜底，S 模式真正跑 OS”这个模型。开启了 sie/sstatus 里的中断使能位，保证 S 模式能收到外部/时钟中断。
#### 设置了中断向量：
为 M 模式设置 mtvec 指向机器定时器入口（类似 xv6 里的 timervec），为 S 模式设置 stvec 指向内核 trap 入口（我的 kernelvec 汇编入口），这样一旦有 trap，硬件就会跳到我写的入口代码。
#### 时钟中断部分：
通过 SBI 的 sbi_set_timer（或等价接口）在启动时设置第一次时钟中断，在每次时钟中断处理完之后，再次调用 sbi_set_timer 预约下一次时钟中断，实现周期性 tick。

（2）trap框架
#### 汇编入口 kernelvec：
参照讲义和 xv6，我在汇编里按固定顺序保存了通用寄存器到当前 CPU 的内核栈/trapframe 结构里，包括 ra/sp/gp/t0-t6/s0-s11/a0-a7 等需要在 C 里用到的寄存器。然后跳到 C 函数 kerneltrap（或者统一的 trap_handler）做逻辑判断。
#### C 端的 trap 分发：
在 kerneltrap() 里，我读取 scause / sepc / stval：如果是“中断位 = 1 且原因为时钟中断”，就调用自己的 timer_interrupt()；如果是其他外设中断（如果有），进入相应分支；如果是异常，就交给 handle_exception() 做统一处理，比如非法指令、访存错误、将来可以扩展系统调用等。
#### 返回路径：
C 处理完后返回汇编，汇编根据刚才保存的顺序恢复寄存器，最后通过 sret 回到被打断的那条指令之后继续执行，确保中断对原程序是“透明”的。

（3）Test
#### 时钟中断处理逻辑：
在 timer_interrupt() 里，我维护了一个全局 tick 计数器，每次时钟中断 tick++，可以作为“系统时间片”的基础。同时在这个函数里重新调用 sbi_set_timer(get_time() + interval)，保证时钟中断是周期性的，而不是只来一次。
目前我主要用 tick 做基础心跳/调试输出，为后面的调度器实验预留了接口，比如将来可以在这里触发 schedule() 做时间片轮转。
#### 测试与调试三件套（test_xxx 系列）：
test_timer_interrupt()：参考讲义里的思路，我在中断处理里增加一个计数变量，然后在主循环里打印“等待第 N 次中断”，直到计数达到预期值，通过这个方式验证时钟中断确实按期触发，tick 在递增。
test_exception_handling()：人为制造几种异常场景（比如访问非法地址或执行非法指令），看是否能进入统一的异常处理分支，并且不会直接把内核打崩。
test_interrupt_overhead()：通过 get_time() 在中断前后打点，粗略估算一次中断处理的开销，为后续性能分析做准备。