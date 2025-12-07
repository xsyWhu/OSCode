# 综合实验报告lab5 —— 进程管理与调度

## **系统设计**

- **架构设计说明**：
  - 项目实现为内核级“内核线程”调度子系统，主要模块及职责：
    - **`kernel/proc/proc.c`**：进程表、PID 分配、进程生命周期（创建、运行、退出、回收）、调度器 `scheduler()`。
    - **`kernel/proc/swtch.S`**：保存/恢复寄存器的上下文切换汇编实现（`s0~s11`, `ra`, `sp` 等）。
    - **`kernel/trap/trap_kernel.c`**：时钟中断处理，递增 `ticks` 并在 Hart0 上触发 `yield()` 实现抢占。
    - **`kernel/boot/main.c`**：引导与测试驱动，启动内置测试进程 `run_all_tests()` 并在测试后启动 worker demo。

- **关键数据结构**：
  - `struct context` (`include/proc/proc.h`): 保存上下文寄存器，用于 `swtch()` 的保存与恢复。
  - `enum proc_state` (`include/proc/proc.h`): 进程状态集合：`UNUSED`, `SLEEPING`, `RUNNABLE`, `RUNNING`, `ZOMBIE`。
  - `struct proc` (`include/proc/proc.h`): 记录 PID、父进程指针、进程名、内核栈基址、`struct context`、退出码等元数据。
  - `struct cpu` (`include/proc/proc.h`): 每个 hart 的当前进程与调度器上下文（`cpu.ctx`），支持 `mycpu()` / `myproc()`。
  - `proc_table[NPROC]` (`kernel/proc/proc.c`): 固定大小的进程表，调度通过线性扫描查找 `RUNNABLE` 条目。

- **与 xv6 对比分析**：
  - 功能范围：本实现聚焦于内核线程（kernel threads），不涉及用户态地址空间、系统调用、文件系统等；因此实现更精简。
  - 调度策略：采用与 xv6 类似的简单轮转（round-robin）策略，遍历 `proc_table` 寻找 `RUNNABLE` 进程并切换。
  - 抢占机制：通过时钟中断驱动 `yield()`，与 xv6 的抢占模型基本一致，但本实验在设计上仅在 Hart0 上维护 tick 和主要调度逻辑，Hart1 做 idle 处理以简化实验观察。

- **设计决策理由**：
  - 先实现内核线程（kernel threads），确保上下文切换与抢占正确，再扩展到用户态，降低实现复杂度与调试成本。
  - 将 tick 更新与主要调度逻辑集中在 Hart0，便于观测与复现调度行为（实验平台为多 hart，但以单 hart 调度为主）。
  - 集成测试驱动 `run_all_tests()`，通过进程形式依次运行测试用例，确保实现的可验证性与复现性。

## **实验过程**

- **实现步骤记录**：
  1. 在 `include/proc/proc.h` 中定义并校准 `struct context`、`struct proc`、`struct cpu`，保证与 `swtch.S` 的寄存器保存顺序一致。
  2. 在 `kernel/proc/proc.c` 中实现基本接口：`proc_init()`、`alloc_proc()`/`create_process()`、`exit_process()`、`wait_process()`、`yield()`、`scheduler()`。
  3. 在 `kernel/trap/trap_kernel.c` 中的时钟中断处理里调用 `timer_update()` 并在 Hart0 上触发 `yield()`，实现抢占。
  4. 在 `kernel/boot/main.c` 中实现 `run_all_tests()`，包含 `test_process_creation`、`test_scheduler`、`test_synchronization`、`debug_proc_table` 等测试函数，测试通过后启动三个 worker（fast/medium/slow）进行演示。
  5. 调整日志与打印频率，避免输出刷屏，加入 `[TEST]` 前缀便于日志过滤。

- **遇到的问题与解决方案**：
  - `exit_process` 被标注为 `noreturn`，但编译器仍报“noreturn function does return”：在 `panic()` 后加入死循环以保证控制流不返回。
  - 测试与 demo 输出过多导致难以阅读：在 worker 中使用 `last_report` 控制打印频率（例如每 100 次迭代打印一次），并为测试添加统一前缀。
  - `-Werror` 下因未使用变量导致编译失败：移除或标注未使用变量（或使用 `__attribute__((unused))`）以消除警告。

- **源码理解总结**：
  - `swtch()`（`kernel/proc/swtch.S`）负责保存当前进程的一组 callee-saved 寄存器并加载目标进程寄存器，C 代码只需维护 `struct context`。
  - `scheduler()` 作为每个 `cpu` 的内核调度循环（在 `cpu.ctx` 上运行），通过遍历 `proc_table` 寻找 `RUNNABLE` 进程并调用 `swtch()` 切换到该进程；进程返回后由 `scheduler()` 继续循环。
  - 新进程通过 `proc_trampoline()` 或类似的入口统一执行传入函数，函数返回后调用 `exit_process()` 完成清理与回收。

## **测试与验证**

- **功能测试结果（概要）**：
  - `test_process_creation`：成功创建并回收多个 `simple_task`，PID 分配与回收正常；日志显示进程创建、运行、退出序列。
  - `test_scheduler`：不同 CPU 工作强度任务（fast/medium/slow）在 Hart0 上被轮流调度，时钟中断能触发抢占切换。
  - `test_synchronization`：使用 `spinlock` 实现的生产者-消费者测试通过，`produced_total == consumed_total`。
  - `debug_proc_table`：可以打印进程表中每个 slot 的状态（`UNUSED`/`RUNNABLE`/`RUNNING`/`ZOMBIE`），用于确认进程生命周期转换正确。

- **性能数据（采集方法与占位）**：
  - 采集方法：在 QEMU 中运行 `make qemu`（或 `make qemu-gdb`），观察内置测试日志并记录各 worker 的 `iter` 与 `ticks` 值；也可在 `worker_body()` 中增加统计变量并在结束时打印汇总。
  - 推荐命令：
    ```bash
    make
    make qemu         # or `make qemu-gdb` for debugging
    ```
  - 占位数据（请在真实运行后替换）：
    - 测试运行耗时（wall-clock）: <待测量> 秒
    - 平均调度切换间隔（ticks）: <待测量> ticks
    - worker iterations（运行 10s 取样）: fast=<待测量>, medium=<待测量>, slow=<待测量>

- **异常测试**：
  - 本实验以进程调度为主，未对非法指令/访存异常做深入测试；如需运行，请在 `kernel/boot/main.c` 中恢复 Lab4 的触发函数（`trigger_illegal_instruction()` 等），并观察内核对异常的处理路径。

- **运行截图 / 录屏（说明与占位）**：
  - 建议截图点：
    - `run_all_tests()` 开始时显示的 `[TEST]` 标记日志。
    - `debug_proc_table` 输出展示的进程表快照。
    - worker demo 运行中不同 `fast/medium/slow` 输出的对比。
  - 在 QEMU（nographic）模式下，可将输出重定向到文件并用终端截屏（示例）：
    ```bash
    make
    make qemu | tee qemu-output.log
    # 然后在宿主机上截图 qemu-output.log 的关键部分或录屏终端窗口
    ```
  - 占位：请将生成的截图/录屏文件放到 `picture/` 目录并在此处列出文件名，例如：`picture/test_creation.png`, `picture/proc_table.png`。

## **结论与后续工作**

- 实验达成：实现了内核线程级别的进程管理与抢占式轮转调度，关键函数（`create_process`、`exit_process`、`wait_process`、`yield`、`scheduler`）已实现并通过内置测试验证。
- 后续建议：
  - 引入 `sleep/wakeup`，将 `wait_process` 从忙等改为事件驱动以降低 CPU 空转。
  - 添加优先级或多级反馈队列，提升调度策略以适配差异化负载。
  - 扩展到用户态进程，加入页表与系统调用框架，完成更完整的操作系统功能链路。

---

附：关键源码路径回顾：
- `kernel/proc/proc.c`  — 进程管理与调度实现
- `kernel/proc/swtch.S` — 上下文切换汇编
- `kernel/trap/trap_kernel.c` — 时钟中断与中断处理
- `kernel/boot/main.c` — 启动与测试驱动

（请在真实运行后补充“性能数据”与 `picture/` 中的截图文件名）

## **思考题与解答**

1. 进程模型：
   - 为什么选择这种进程结构设计？
     - 选择以内核线程（kernel threads）为主的设计，可以把注意力集中在上下文切换、调度与同步上，而不引入用户态地址空间、页表与系统调用的复杂性，便于实验验证与调试。
     - 结构简单、实现成本低，符合实验教学目标：先保证调度正确，再扩展功能。
   - 如何支持轻量级线程？
     - 实现独立的线程控制块（TCB）和内核栈，但共享进程级资源（如地址空间、文件表），线程创建使用类似 `thread_create()` 的接口，避免复制整个地址空间。
     - 使用较小的创建/销毁开销（例如不走完整 `fork()` 路径），提供轻量级同步原语（自旋锁/互斥）和线程局部数据。

2. 调度策略：
   - 轮转调度的公平性如何？
     - 轮转（round-robin）在相同时间片下对所有可运行任务提供时间公平性，但对 I/O 密集型与 CPU 密集型任务的感知不足，也无法保证实时约束或优先级区分。
     - 实际公平性还依赖时间片长度、遍历顺序与中断触发频率；短时间片提升响应但增加上下文切换开销。
   - 如何实现实时调度？
     - 引入优先级队列或实时调度算法（如 RMS、EDF），将实时任务放入独立的调度通路，并保证优先级可抢占。
     - 需要支持优先级继承、时间预算/带宽限制，以及在内核中减少长不可抢占区间以满足延迟约束。

3. 性能优化：
   - `fork()` 的性能瓶颈如何解决？
     - 采用 Copy-On-Write（COW）机制，延迟页面复制直到写时，避免在 `fork()` 时立即复制整个地址空间。
     - 提供 `vfork()` 或 `posix_spawn` 等更轻量的创建接口，并对大对象使用懒分配/映射策略。
   - 上下文切换开销如何降低？
     - 减少不必要的切换（增大时间片或使用协作式调度点），在汇编层只保存必要寄存器，减少保存/恢复工作量；优化锁设计以降低争用。
     - 使用 per-CPU 数据结构 和 本地缓存来降低跨核同步成本，必要时采用批量切换或用户态线程方案减少内核切换频度。

4. 资源管理：
   - 如何实现进程资源限制？
     - 在内核中对每类资源做计量（内存、文件描述符、CPU 时间等），并在分配点检查配额；提供类似 `ulimit` / cgroups 的接口以配置和执行限制。
   - 如何处理进程资源泄漏？
     - 使用引用计数、统一的资源释放路径（on-exit cleanup），并在退出路径中确保释放所有分配；增加内核诊断（日志/统计）和定期清理线程（reclaimer）。

5. 扩展性：
   - 如何支持多核调度？
     - 使用 per-core runqueue 与轻量锁（或无锁结构），在每核上运行本地调度器以减少全局争用；保留全局或分区式调度器以处理全局策略。
   - 如何实现负载均衡？
     - 定期收集每核负载信息并进行任务迁移（work-stealing 或主动迁移），考虑亲和性与缓存热度，迁移策略需权衡迁移成本与负载均衡收益。

以上思路既包含理论说明，也给出可落地的实现建议；在后续扩展时可以逐步引入 COW、优先级队列、多核 runqueue 与资源配额机制，将实验内核一步步拓展为更完整的调度与资源管理子系统。
