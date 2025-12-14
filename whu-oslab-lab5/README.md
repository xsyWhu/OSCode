# Lab5：进程管理与调度

## 1. 环境与依赖

- Ubuntu/WSL2 + `riscv64-linux-gnu-*` 交叉编译工具链  
- QEMU (virt, riscv64)  
- 已完成前置实验（内存管理、中断等）

## 2. 编译与运行

```bash
make           # 构建 kernel-qemu
make qemu      # 启动 QEMU (nographic)
```

若需要清理：

```bash
make clean
```

> 注意：在某些受限环境中编译可能因 `/tmp` 权限报错，可通过 `export TMPDIR=$PWD/tmp` 等方式调整。

## 3. 主要特性

- **内核线程调度**：`create_process()` 支持创建最多 `NPROC` 个内核线程，`scheduler()` 采用轮转策略，时钟中断驱动 `yield()` 实现抢占。
- **进程生命周期**：完成 `exit_process()`、`wait_process()`、内核栈分配/释放等逻辑。
- **测试驱动**：`run_all_tests()` 进程依次执行 `test_process_creation`、`test_scheduler`、`test_synchronization`、`debug_proc_table`，最后启动 worker demo。
- **同步原语**：使用自旋锁实现生产者-消费者测试，验证进程间协作。

## 4. 目录说明

```
whu-oslab-lab5/
├── Makefile / common.mk
│   ├── 构建入口：交叉编译、链接、生成 kernel-qemu
│   └── 常用目标：make / make qemu / make clean
│
├── kernel/                           # 内核源码（S-mode 为主，含少量 M-mode 启动/定时器）
│   ├── kernel.ld                     # 链接脚本：段布局、符号导出
│   │
│   ├── boot/                         # 启动链路：从 entry 到进入 scheduler
│   │   ├── entry.S                   # 最早入口：栈/基础环境
│   │   ├── start.c                   # M-mode 早期初始化、委托、定时器初始化、mret 到 S-mode
│   │   └── main.c                    # 内核主函数：初始化各子系统、创建/演示进程(内核线程)、进入调度器
│   │
│   ├── proc/                         # 进程管理与调度（Lab5 重点）
│   │   ├── proc.c                    # proc_table/状态机/create/exit/wait/yield/sleep&wakeup/scheduler
│   │   └── swtch.S                   # 上下文切换：保存/恢复 context（被 scheduler 调用）
│   │
│   ├── trap/                         # trap 框架：异常/中断入口与分发
│   │   ├── trap.S                    # trap 入口向量：保存现场→调用 C→恢复→sret
│   │   └── trap_kernel.c             # 中断/异常处理与分发（为时钟 tick、抢占/让出等提供支撑）
│   │
│   ├── dev/                          # 设备驱动
│   │   ├── uart.c                    # 串口 16550：输出/输入/中断
│   │   ├── plic.c                    # PLIC：外部中断 claim/complete
│   │   ├── timer.c                   # 定时器：tick 维护（调度/时间片的时钟来源）
│   │   └── console.c                 # console 抽象（通常薄封装 UART）
│   │
│   ├── mem/                          # 内存管理（实验继承/复用）
│   │   ├── pmem.c                    # 物理页分配/释放（进程栈、页表等都依赖它）
│   │   └── vmem.c                    # Sv39 页表/映射、启用分页
│   │
│   └── lib/                          # 基础库
│       ├── print.c                   # printf/panic
│       ├── spinlock.c                # 自旋锁（proc/irq 关键同步原语）
│       └── string.c                  # memset/memcpy 等
│
├── include/                          # 头文件接口（与 kernel 子系统一一对应）
│   ├── proc/ (proc.h, cpu.h)         # struct proc/context/cpu + 调度/进程 API 声明
│   ├── trap/ (trap.h)                # trap 相关结构/接口
│   ├── mem/ (pmem.h, vmem.h)         # 内存接口
│   ├── dev/ (uart.h, plic.h, ...)    # 设备接口
│   ├── lib/ (lock.h, print.h, ...)   # 锁/输出/字符串接口
│   ├── riscv.h / memlayout.h / common.h
│   └── CSR/位定义、地址布局、基础类型与宏
│
├── README.md / Report.md             # 说明与实验报告
├── picture/                          # 实验截图
├── 操作系统实践PPT-进程管理与调度.pdf  # 实验资料
├── 从零构建操作系统-学生指导手册.(pdf/docx) # 参考资料
└── kernel-qemu / kernel.bin          # 构建产物（可执行镜像/裸二进制）
```

## 5. 后续扩展

- 实现 `sleep/wakeup`，让 `wait_process` 不再忙等。
- 支持优先级/多级反馈队列等调度算法。
- 引入用户态地址空间与系统调用，完成从 kernel thread 到 user process 的过渡。
