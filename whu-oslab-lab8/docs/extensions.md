# 扩展功能实现说明

本文档总结本项目各扩展的实现思路、功能与主要改动文件，便于回顾与查阅。

## 扩展 1：优先级调度 / 多级反馈队列 (MLFQ)
- **实现要点**
  - 在 `struct proc` 中增加 `priority`、`queue_level`、`ticks_in_level`、`wait_ticks` 字段；默认优先级 `PRIORITY_DEFAULT`。
  - 调度器采用 MLFQ：多级队列按优先级从高到低扫描，可运行进程选第一个匹配队列；每级有独立时间片 `mlfq_quantum[level]`。
  - Aging/Boost：周期性老化、全局提升，防止饥饿。
  - `setpriority/getpriority` 系统调用调节进程优先级，自动映射到队列等级。
- **主要改动文件**
  - `include/proc/proc.h`：进程调度字段。
  - `kernel/proc/proc.c`：MLFQ 调度器、aging/boost、`setpriority/getpriority`、`proc_tick`。
  - `include/syscall.h`、`kernel/syscall/sysproc.c`、`user/usys.S`、`include/user/user.h`：syscall 号与用户接口。
  - `user/init.c`、`kernel/boot/main.c`：示例/测试输出。

## 扩展 2：内核日志系统
- **实现要点**
  - 环形缓冲区 `klog_buffer`，支持日志级别过滤（DEBUG/INFO/WARN/ERROR）。
  - `klog()` 支持简化版 `printf`（%s/%d/%x/%p/%u/%lu），自动换行。
  - `sys_klog` 允许用户态读取日志；`logread` 用户程序轮询打印。
  - 在关键路径（调度 demo 等）添加 `klog`，支持运行时观察。
- **主要改动文件**
  - `include/lib/klog.h`、`kernel/lib/klog.c`：日志缓冲/格式化实现。
  - `include/syscall.h`、`kernel/syscall/sysproc.c`、`kernel/syscall/syscall.c`、`include/user/user.h`、`user/usys.S`：`SYS_klog` 定义与用户接口。
  - `user/logread.c`：用户态日志读取工具。
  - `kernel/boot/main.c`：初始化日志、关键事件记录。

## 扩展 3：ELF 加载器
- **实现要点**
  - `exec_process` 支持解析标准 ELF：校验魔数/程序头，按段映射到用户页表，设置权限 (R/W/X/U)，分配用户栈，设置入口/参数。
  - 内置用户 ELF 镜像通过 `initbin.S` 嵌入（/init、/logread、/nice、/elfdemo 等）。
  - `elfdemo` 测试程序打印 PID/参数、代码和数据段地址，验证段映射正确。
- **主要改动文件**
  - `include/elf.h`：ELF 头/程序头定义。
  - `kernel/proc/exec.c`：ELF 解析、段映射、栈构建。
  - `kernel/proc/initbin.S`、`kernel/proc/Makefile`：嵌入各 ELF。
  - `user/Makefile`、`user/elfdemo.c`、`user/init.c`：用户测试与打包。

## 扩展 4：IPC（管道 + 消息队列）
- **实现要点**
  - 管道：已有 `pipealloc/pipewrite/piperead`、`sys_pipe`，保持兼容。
  - 消息队列：新增键值获取 `msgget`，阻塞式 `msgsend/msgrecv`（环形缓冲 + sleep/wakeup）。
  - 用户程序 `msgdemo`：父写子读，验证消息传递。
- **主要改动文件**
  - `include/ipc/msg.h`、`kernel/ipc/msg.c`、`kernel/ipc/Makefile`：消息队列数据结构与实现。
  - `include/syscall.h`、`kernel/syscall/sysmsg.c`、`kernel/syscall/syscall.c`、`include/user/user.h`、`user/usys.S`：`SYS_msgget/msgsend/msgrecv`。
  - `kernel/boot/main.c`：`msg_init()` 初始化。
  - `user/msgdemo.c`、`user/init.c`：用户测试程序与启动。

## 扩展 5：Copy-on-Write (COW) Fork
- **实现要点**
  - 引入软件标志 `PTE_C`；`uvmcopy` 改为共享物理页，去掉写位，置 COW，并递增物理页引用计数。
  - 物理页引用计数：`pmem_refcnt`，`pmem_incref/pmem_decref`；释放页或解除映射时递减。
  - 缺页处理：用户态写/页错误时在 `cow_handle` 中分配私有页或清除 COW；`copyout` 遇到 COW 页先处理。
  - `cowtest`：父子共享后子写，父验证未被篡改，证明 COW 生效。
- **主要改动文件**
  - `include/mem/vmem.h`：`PTE_C`、`cow_handle` 声明。
  - `kernel/mem/vmem.c`：COW 复制、`uvmcopy` 共享、`copyout` COW 检测、`uvmdealloc`/`vm_unmappages` 用引用计数释放。
  - `kernel/mem/pmem.c`、`include/mem/pmem.h`：引用计数实现。
  - `kernel/trap/trap_kernel.c`：用户页故障触发 `cow_handle`。
  - `user/cowtest.c`、`user/init.c`、`kernel/proc/initbin.S`、`kernel/proc/exec.c`：测试嵌入与运行。

## 扩展 6：实时调度（简易 EDF）
- **实现要点**
  - 进程增加实时标记 `rt_enabled` 和截止时间 `rt_deadline`。
  - 新增 syscall `setrealtime(pid, deadline_ticks)`，设置进程为实时任务并记录绝对截止时间（当前 ticks + deadline）。
  - 调度器优先扫描所有 RUNNABLE 实时任务，按最早截止期选择；若无实时任务则回退 MLFQ。
  - `rtdemo`：创建多实时任务，设置不同截止期，观察完成顺序。
- **主要改动文件**
  - `include/proc/proc.h`、`kernel/proc/proc.c`：实时字段、`setrealtime` 实现、调度器 EDF 选择。
  - `include/syscall.h`、`kernel/syscall/sysproc.c`、`kernel/syscall/syscall.c`、`include/user/user.h`、`user/usys.S`：`SYS_setrealtime`。
  - `user/rtdemo.c`、`user/init.c`、`kernel/proc/initbin.S`、`kernel/proc/exec.c`：测试程序嵌入。

## 其他说明
- 所有用户程序 ELF 通过 `kernel/proc/initbin.S` 嵌入，`kernel/proc/Makefile`/`user/Makefile` 负责生成。
- `kernel/boot/main.c` 初始化顺序：klog → pmem → kvm → trap → UART/VirtIO → FS → proc/file → IPC msg → userinit，然后启动测试。
