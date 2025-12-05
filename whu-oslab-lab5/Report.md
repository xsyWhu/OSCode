# Lab5 报告 —— 进程管理与调度

## 1. 系统设计

### 1.1 架构概述

本实验在已有内核基础上补齐“内核线程”级别的进程管理与调度。总体结构：

- **进程子系统（`kernel/proc/proc.c`）**：负责 PID 分配、进程生命周期管理、上下文保存（`struct context`）以及轮转调度 `scheduler()`。
- **中断与计时器**：在 `kernel/trap/trap_kernel.c` 的时钟中断中更新系统 tick，并在 Hart0 上调用 `yield()` 实现抢占。
- **启动与测试驱动（`kernel/boot/main.c`）**：启动时创建一个测试进程 `run_all_tests()`，依次运行教师要求的各项测试，最后启动 worker demo 展示调度效果。

### 1.2 关键数据结构

| 数据结构 | 定义位置 | 作用 |
| --- | --- | --- |
| `struct context` | `include/proc/proc.h` | 存放 `ra/sp` 与 `s0~s11`，与 `swtch.S` 中保存寄存器保持一致。 |
| `enum proc_state` | `include/proc/proc.h` | 记录进程状态：`UNUSED/RUNNABLE/RUNNING/SLEEPING/ZOMBIE`。 |
| `struct proc` | `include/proc/proc.h` | 记录 PID、状态、内核栈地址、上下文、父进程及退出码等。 |
| `struct cpu` | `include/proc/proc.h` | 保存每个 hart 当前运行的进程及调度器上下文，支持 `mycpu()/myproc()`。 |
| `proc_table[NPROC]` | `kernel/proc/proc.c` | 进程表，线性扫描寻找空槽并追踪所有进程。 |

### 1.3 与 xv6 的对比

- **范围更小**：只实现内核线程，不包含用户态地址空间、系统调用、文件句柄等复杂资源。
- **调度策略相同**：同样使用最简单的轮转调度，遍历进程表选择 `RUNNABLE` 进程。
- **中断驱动**：保持 `timer_interrupt_handler()` 中调用 `yield()` 的结构，与 xv6 的抢占设计一致。

### 1.4 设计决策

1. **先实现 kernel thread**：先确保上下文切换、抢占调度正确，再考虑用户态。
2. **单核 tick 驱动**：只在 Hart0 更新 tick 并调度，Hart1 进入 idle，使逻辑更清晰。
3. **测试内嵌**：以进程形式运行所有测试，方便提交时快速展示结果。

## 2. 实验过程

### 2.1 实现步骤

1. **补充数据结构**：完善 `struct context/struct proc/struct cpu`，与 `swtch.S` 对接。
2. **实现进程管理接口**：在 `kernel/proc/proc.c` 中完成 `proc_init/create_process/exit_process/wait_process/yield/scheduler`。
3. **时钟抢占**：`timer_interrupt_handler()` 在 Hart0 上调用 `timer_update()` 后判断 `PROC_RUNNING` 进程并 `yield()`。
4. **测试框架**：在 `main.c` 中实现 `run_all_tests()`，包含 `test_process_creation`、`test_scheduler`、`test_synchronization`、`debug_proc_table`。
5. **worker demo**：测试结束后启动 fast/medium/slow 三个 worker 进程，展示轮转效果。

### 2.2 问题与解决

| 问题 | 解决方案 |
| --- | --- |
| `exit_process` 标记 `noreturn` 但编译器认为可能返回 | 在 `panic` 后加入死循环，保证控制流不返回。 |
| worker 输出刷屏 | 在 `worker_body()` 中通过 `last_report` 控制每 100 次迭代才打印一次。 |
| 测试产生日志过多难以区分 | 将每组测试封装为函数，添加 `[TEST]` 前缀方便阅读。 |
| `test_process_creation` 未使用变量导致 `-Werror` | 删除 `pids[]`，只统计创建数量。 |

### 2.3 源码理解

- `swtch()` 是核心的上下文切换入口，保存/恢复 `s` 寄存器与 `sp/ra`，让 C 层只需维护 `struct context`。
- `scheduler()` 自身作为一个“内核线程”，在 `cpu.ctx` 中运行；`proc_trampoline()` 是所有新进程的入口，执行完用户函数后调用 `exit_process()`。
- 时钟中断通过软件中断委托至 S-mode，`timer_interrupt_handler()` 既维护 tick 又触发 `yield()`，实现抢占式多任务。

## 3. 测试验证

### 3.1 功能测试

1. **test_process_creation**：批量创建 `simple_task`，观察其多次 `yield` 与退出，验证 PID 分配、进程回收。
2. **test_scheduler**：三个 `cpu_task` 执行不同强度计算，轮流占用 CPU，证明调度器公平且能被时钟抢占。
3. **test_synchronization**：使用 `spinlock` 实现生产者-消费者模型，检查 `produced_total == consumed_total`。
4. **debug_proc_table**：遍历 `proc_table`，打印每个活跃进程的状态，便于调试 ZOMBIE/UNUSED 转换。

### 3.2 行为观察

- Hart1 进入 `wfi` 等待，调度全部发生在 Hart0。
- worker demo 中，fast/medium/slow 的 `iter` 与 `ticks` 体现了不同负载下的调度公平性。

### 3.3 异常/性能测试

- 目前重点在进程管理，未启用 Lab4 的非法指令/访存异常测试。若需要，可在 `main.c` 中重新启用原先的 `trigger_*` 函数。
- 性能层面主要通过观察 tick 与迭代计数验证调度响应时间；`worker_do_work()` 使用 `nop` 循环模拟不同耗时。

### 3.4 典型输出

```
[TEST] test_process_creation
[simple_task] pid=2 iteration=0
...
[producer] produced item #1 (buffer=1)
[consumer] consumed item #1 (buffer=0)
...
[TEST] debug_proc_table
  slot=0 pid=1 state=RUNNABLE name=proc-tests
Spawned worker threads: fast=4 medium=5 slow=6
[fast] hart=0 iter=0 ticks=1
...
```

## 4. 结论与展望

本次实验完成了内核层面的进程管理与调度框架，实现了抢占式轮转调度，并通过内置测试验证了关键功能。下一步可考虑：

- 引入 `sleep/wakeup`，将 `wait_process` 从忙等改为事件驱动；
- 加入优先级或多级反馈队列，提升调度策略；
- 扩展至用户态进程，接入系统调用、页表等更完整的 OS 功能。
