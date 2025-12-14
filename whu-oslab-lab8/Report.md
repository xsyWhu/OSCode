# 实验报告（Lab8 扩展项目）

> 本报告仅覆盖扩展项目（Lab8），描述实现的功能、设计思路、关键改动与测试结果。

## 1. 实验环境
- 硬件：开发机 2 核 / 128MB（QEMU virt 默认配置）
- 软件：`qemu-system-riscv64`，`riscv64-linux-gnu-gcc` 工具链
- 编译运行：`make -j4`，`make qemu`

## 2. 扩展目标与完成情况
- 扩展 1：优先级调度 / MLFQ —— 已完成
- 扩展 2：内核日志系统 —— 已完成
- 扩展 3：ELF 加载器 —— 已完成
- 扩展 4：IPC（管道 + 消息队列）—— 已完成
- 扩展 5：Copy-on-Write (COW) Fork —— 已完成
- 扩展 6：实时调度（简易 EDF）—— 已完成

## 3. 设计与实现概述
### 3.1 优先级调度 / 多级反馈队列
- 在 `struct proc` 增加 `priority/queue_level/ticks_in_level/wait_ticks`，定义 `PRIORITY_MIN/MAX/DEFAULT`。
- 调度器按 MLFQ 扫描队列，从高到低选择 RUNNABLE 进程；时间片用 `mlfq_quantum[level]` 控制。
- Aging/Boost：周期性老化、全局提升，防止低优先级饥饿。
- Syscall：`setpriority/getpriority`，可在用户态动态调整优先级。
- 主要文件：`kernel/proc/proc.c`、`include/proc/proc.h`、`include/syscall.h`、`kernel/syscall/sysproc.c`、`user/usys.S`、`include/user/user.h`。

### 3.2 内核日志系统
- 环形缓冲 `klog_buffer` + 日志级别过滤（DEBUG/INFO/WARN/ERROR）。
- `klog()` 简化 printf 支持 `%s/%d/%x/%p/%u/%lu`，自动换行；`sys_klog` 将缓冲区内容 copyout 到用户态。
- 用户程序 `logread` 循环读取日志，便于实时查看调度/测试过程。
- 主要文件：`include/lib/klog.h`、`kernel/lib/klog.c`、`include/syscall.h`、`kernel/syscall/sysproc.c`、`include/user/user.h`、`user/logread.c`。

### 3.3 ELF 加载器
- `exec_process` 解析 ELF 头和程序头，校验魔数/段界限，按段映射到用户页表，设置 R/W/X/U 权限，构建用户栈与参数。
- 内置 ELF 镜像：`/init`、`/logread`、`/nice`、`/elfdemo` 等通过 `initbin.S` 嵌入。
- `elfdemo` 测试打印 PID/argc/argv 及代码/数据地址，验证段映射正确。
- 主要文件：`include/elf.h`、`kernel/proc/exec.c`、`kernel/proc/initbin.S`、`kernel/proc/Makefile`、`user/elfdemo.c`。

### 3.4 IPC（管道 + 消息队列）
- 管道保持原有 `pipealloc/pipewrite/piperead/sys_pipe` 功能。
- 新增消息队列：`msgget` 获取/创建队列，`msgsend/msgrecv` 阻塞式收发（环形缓冲 + sleep/wakeup，深度 16，单条 128 字节）。
- 用户测试 `msgdemo`：父写子读，验证收发路径和阻塞唤醒。
- 主要文件：`include/ipc/msg.h`、`kernel/ipc/msg.c`、`kernel/syscall/sysmsg.c`、`include/syscall.h`、`user/msgdemo.c`。

### 3.5 Copy-on-Write (COW) Fork
- 软件标志 `PTE_C` 标记 COW 页；`uvmcopy` 不再复制数据，去掉 PTE_W，置 PTE_C，并递增物理页引用计数。
- 物理页引用计数：`pmem_refcnt`，`pmem_incref/pmem_decref`，释放/解除映射时递减，归零回收。
- 缺页处理：用户写或 copyout 遇到 COW 页调用 `cow_handle`，若共享计数>1 则分配新页复制；计数为1则直接去 C 标志、加写权限。
- 用户测试 `cowtest`：子进程写后父缓冲仍为 A，验证写时复制生效。
- 主要文件：`include/mem/vmem.h`（PTE_C）、`kernel/mem/vmem.c`（COW 逻辑）、`kernel/mem/pmem.c`（引用计数）、`kernel/trap/trap_kernel.c`（页故障处理）、`user/cowtest.c`。

### 3.6 实时调度（简易 EDF）
- 进程新增 `rt_enabled/rt_deadline`，`setrealtime(pid, deadline_ticks)` 将进程标记为实时任务，截止时间 = 当前 ticks + deadline。
- 调度器先扫描所有 RUNNABLE 实时任务，按最早截止期选择（EDF）；若无实时任务则回退 MLFQ。
- 用户测试 `rtdemo`：创建不同 deadline 的实时任务，观察完成顺序。
- 主要文件：`include/proc/proc.h`、`kernel/proc/proc.c`（EDF 选择、setrealtime）、`include/syscall.h`、`kernel/syscall/sysproc.c`、`user/rtdemo.c`。

## 4. 测试与验证
- `make qemu` 自动运行以下用户测试：
  - `priority_test`：验证 setpriority/getpriority 与 MLFQ 行为。
  - `elfdemo`：验证 ELF 加载段映射与参数传递。
  - `msgdemo`：消息队列父子通信，收发成功状态。
  - `cowtest`：COW 写时复制，父缓冲保持原值。
  - `rtdemo`：实时任务按 deadline 优先运行，所有子任务正常退出。
  - MLFQ showcase：不同优先级/工作量的 worker，配合日志观察调度。
- 内核日志：`logread` 自动运行，将 `klog` 输出打印，辅助观察调度顺序与测试状态。

## 5. 遇到的问题与解决
- **日志输出重复/粘连**：初版 `klog` 未自动换行，logread 输出粘在一行；后续在 `klog_vprintf` 强制补换行并增加格式化支持。
- **嵌入用户程序失败**：最初仅嵌入 `.bin`，ELF exec 失败；改为嵌入 `.elf` 并在 `initbin.S`/Makefile 依赖全量 ELF。
- **COW page fault 处理**：需要在 `copyout` 也检测 PTE_C，否则内核向用户写数据会失败；增加 `cow_handle` 调用。
- **实时调度插队**：为保持兼容，实时任务只在 RUNNABLE 集合中按最早截止期抢占 MLFQ 选择，未实现抢占式时钟检查，简化为“先选 EDF 再选 MLFQ”。

## 6. 运行指南（扩展相关）
- 构建：`make -j4`
- 运行：`make qemu`
- 观察：
  - 控制台输出包含各测试结果；`logread` 会打印内核 `klog`，可在 `kernel/lib/klog.c` 调整级别或停用 `logread` 以减少冗余。
  - 可在用户态调用 `setpriority`、`setrealtime`、`msgget/msgsend/msgrecv`、`klog` 等接口进一步实验。

## 7. 总结
- 已完成 Lab8 所有扩展：优先级/MLFQ、日志系统、ELF 加载、IPC（管道+消息队列）、COW fork、简易 EDF 实时调度。
- 所有功能均通过内置用户测试验证，日志系统便于运行时观测；架构保持模块化，便于后续扩展与调试。
