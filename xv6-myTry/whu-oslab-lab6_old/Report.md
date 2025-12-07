# 综合实验报告 — 进程管理、调度 与 系统调用（Lab5 & Lab6 总结）

本报告按要求整理为三大部分：系统设计、实验过程与实现记录、测试验证。文中同时覆盖 Lab5 的进程调度实现与 Lab6 的最小系统调用框架实现，包含设计说明、关键数据结构、实现细节、测试结果及思考题回答。

**说明**：涉及的源文件主要包括 `kernel/proc/proc.c`、`kernel/proc/syscall.c`、`kernel/proc/sysproc.c`、`kernel/proc/initcode.S`、`kernel/boot/main.c`、`kernel/trap/trap_kernel.c` 等。

---

## 系统设计部分

### 架构设计说明

- 进程子系统：`kernel/proc/proc.c` 管理进程生命周期（`alloc_process/create_process/exit_process/wait_process`）、内核栈分配、上下文与调度。调度器使用简单轮转（round-robin），由 `scheduler()` 在可运行进程间切换。
- 中断与计时器：`kernel/trap/trap_kernel.c` 提供 trap 向量与外设中断处理，CLINT 提供的时钟中断驱动 `timer_interrupt_handler()` 在 Hart0 上更新 ticks 并触发抢占（调用 `yield()`）。
- 用户/内核边界与系统调用（Lab6）：用户通过 `ecall` 进入内核（`usertrap()` 处理 scause==8），内核在 `kernel/proc/syscall.c` 中根据 `a7` 分发到 `sys_*` 实现（如 `sys_getpid`, `sys_write`, `sys_exit`）。参数通过 `a0..a5` 传递，返回值放回 `a0`。

### 关键数据结构

- `struct context` (`include/proc/proc.h`)：保存 `ra/sp` 与 callee-saved 寄存器，与汇编 `swtch.S` 协作。
- `struct proc` (`include/proc/proc.h`)：PID、状态（RUNNABLE/RUNNING/...）、内核栈地址、trapframe 指针、页表、父进程指针、退出码等。
- `struct cpu` (`include/proc/proc.h`)：每个 hart 的当前进程和内核上下文（`cpu.ctx`）用于在调度器与进程间切换。
- `pgtbl_t`/页表：用户进程的页表由 `uvm_create/uvm_load` 管理，`proc_pagetable()` 在创建时映射 trampoline 与 trapframe 页面。

### 与 xv6 对比分析

- 相同点：两者均使用简单轮转调度、中断驱动抢占和 trapframe 保存/恢复用户态寄存器以支持系统调用与异常处理。
- 不同点：本实现范围更小（起初只实现内核线程），Lab6 才加入最小 syscall 支持；尚未实现完整文件系统、复杂权限模型或用户态丰富系统调用集合。

### 设计决策理由

- 先实现 kernel thread 与上下文切换，确保调度机制正确后再扩展用户态与 syscall，可降低实现复杂度并便于定位问题。
- 只在 Hart0 执行调度并更新 tick、让其他 hart 进入 idle，简化多核同步问题，便于教学演示。
- 在 syscall 设计上采用最小原则：内核提供必要且可验证的原语（write/getpid/exit），由用户态库负责更高级组合。

---

## 实验过程部分

### 实现步骤记录

1. 初始化与数据结构：补全 `struct proc/struct cpu/struct context`，实现 `proc_init()`。
2. 进程管理：实现 `alloc_process()`、`create_process()`、`proc_trampoline()`、`exit_process()`、`wait_process()` 等核心函数。
3. 上下文切换：引入/使用 `swtch()`（汇编），并在 `yield()` 与 `scheduler()` 中调用以切换进程与调度器上下文。
4. 中断/时钟：实现 `trap_init()`、`trap_inithart()`、`timer_interrupt_handler()`；在 `usertrap()`/`trap_kernel_handler()` 中分发中断与异常。
5. 用户态启动与 syscall 路径：`userinit()` 加载 `initcode.S` 到用户进程地址空间；实现 `include/syscall.h`、`kernel/proc/syscall.c`、`kernel/proc/sysproc.c`，并在 `usertrap()` 检测 `ecall` 后调用 `syscall()`。
6. 测试：在 `kernel/boot/main.c` 添加 `run_all_tests()`，包括进程创建、调度、同步以及 syscall 的参数/安全/性能测试。

### 问题与解决方案

- `exit_process` noreturn 语义：在 `exit_process` 调用 `panic()` 后加死循环，防止函数返回。
- 输出过多导致日志混乱：在 `worker_body()` 中使用 `last_report` 控制打印频率。
- 用户内存访问安全问题：`sys_write` 使用 `copyin`、分批复制缓冲区并检查 fd、len 范围以防止越界访问。
- `wait_process` 仍为忙等：目前使用 `yield()` 让出 CPU，后续建议实现 `sleep/wakeup` 改为阻塞式等待以节省 CPU。

### 源码理解总结

- `swtch()` 与 `struct context`：负责保存/恢复内核态寄存器与栈指针，使 C 层不必直接操作寄存器。
- `trapframe`：保存用户态寄存器，内核通过该结构获取系统调用参数并在返回时恢复用户态执行上下文。
- `trampoline` 与 `userret`：统一处理从内核返回用户态时的页表切换和 sret 返回序列。

---

## 测试验证部分

### 功能测试结果

- `test_process_creation`：成功创建并回收多个 `simple_task`，进程生命周期流程正常。
- `test_scheduler`：运行多个 `cpu_task`，通过任务输出和 ticks 观察到轮转执行，表示抢占有效。
- `test_synchronization`：生产者/消费者示例在自旋锁保护下运行完毕，`produced_total == consumed_total`。
- Lab6 `syscall` 测试：`initcode.S` 的 `Hello from user mode!` 通过 `SYS_write` 输出；`SYS_getpid` 返回一致的 PID；对非法 fd/指针/长度的 `write` 返回 -1，且不会导致内核 panic。

### 性能数据（测量说明与占位）

- `test_syscall_performance` 在 `kernel/boot/main.c` 中测量了多次 `getpid()` 的 ticks 耗时（通过 timer_get_ticks 输出）。
- 由于不同宿主/模拟器环境的差异，建议在你的 QEMU 环境中运行并记录数值。运行建议命令：

```bash
make
make qemu
```

- 请将测试输出中关于 `test_syscall_performance` 的行（例如 "1000 getpid() calls took X ticks"）粘贴到本报告中以补齐性能数据。

### 异常测试

- 内核异常：`trap_kernel_handler()` 在遇到非法指令或页错误时会打印信息并 `panic()`，当前实现避免内核态触发此类异常。
- 用户态异常与安全边界：`usertrap()` 会在无法识别的异常中将进程标记为被 kill，并由进程退出；`sys_write` 对非法参数返回错误码，验证了内核对用户内存访问的防护。

### 运行截图/录屏（占位）

- 建议截图：
  - QEMU 启动日志（包含 `run_all_tests()` 的输出）；
  - syscall 测试输出段；
  - producer/consumer 运行片段；
  - worker demo 的部分输出。

- 占位：请将截图放在仓库的 `picture/` 目录并在此处列出文件名，我可以在报告中嵌入对应路径。

---

## 思考题与解答

1. 设计权衡：
   - 系统调用的数量应该如何确定？
     - 回答：优先支持最小必要集（Minimal API）以简化内核面向安全和正确性的实现。先实现进程管理、基本 I/O、内存管理和进程控制相关的调用；随着功能需求增加再逐步添加。过多的系统调用增加维护负担和攻击面，过少则会把复杂性推给用户态库。
   - 如何平衡功能性和安全性？
     - 回答：所有功能都应遵循“最小权限原则”与“防守式编程”。将复杂或危险的功能放在用户态库中实现，内核只提供经审核的、最小且必要的原语；对所有用户提供的指针和长度进行严格验证；对敏感操作增加权限检查或能力模型。

2. 性能优化：
   - 系统调用的主要开销在哪里？
     - 回答：主要开销来自用户态/内核态切换（切换上下文、刷新或切换地址空间相关寄存器）、参数拷贝（用户-内核内存复制）、以及可能的同步/阻塞开销。不同架构对这些开销敏感度不同。
   - 如何减少用户态/内核态切换开销？
     - 回答：常见手段包括：批处理用户请求以减少调用频度（例如 writev/batched IO）、使用内核提供的零拷贝接口或 DMA 支持、通过 fast-path 实现常见系统调用（内联或使用轻量中断/软中断机制）、以及减少不必要的地址空间切换（保持页表或使用走私寄存器缓存）。另外还可以把部分可验证的操作放到用户态库以避免系统调用。

3. 安全考虑：
   - 如何防止系统调用被滥用？
     - 回答：实施权限检查（UID/GID、能力与访问控制列表）、限制资源使用（速率限制、配额）、对危险系统调用施加审计和日志、以及使用沙箱/容器机制将进程隔离。此外，最小化内核接口并采用可验证的实现可以减小滥用风险。
   - 如何设计安全的参数传递机制？
     - 回答：不要直接信任用户传来的指针，必须使用 `copyin`/`copyout` 或内核映射页表验证每次内存访问；对长度和偏移做防溢出检查；对于复杂对象使用句柄/索引替代直接指针；尽量在内核中统一进行边界检查与错误处理。

4. 扩展性：
   - 如何添加新的系统调用？
     - 回答：在 `include/syscall.h` 中分配新的 syscall 编号，在内核中实现对应的 `sys_*` 函数，并在分发表（如 `syscall.c` 的 `syscalls[]`）注册。为兼容用户态，应更新用户态头文件和库，并确保 `ecall` 参数约定保持一致。
   - 如何保持向后兼容性？
     - 回答：不要重用或改变已有的系统调用号；为新增功能提供可选的新接口，而不是修改旧接口的行为；在用户态引入库抽象层（libc）来封装调用变化；对于重大变更提供版本标识或 ioctl/feature-flag 机制。

5. 错误处理：
   - 系统调用失败时应该如何处理？
     - 回答：在内核中确保错误被规范化为一组负数错误码（或 errno 值），并通过寄存器返回给用户态。对可恢复错误尽可能提供明确的错误码，避免内核直接 panic 除非遇到致命错误。
   - 如何向用户程序报告详细的错误信息？
     - 回答：通过标准化的错误码（例如 POSIX errno），并通过用户态库（libc）将其转换为可读文本（`strerror`）。对于调试目的，保留内核审计日志（仅供管理员查看）以免泄露敏感信息给普通进程。

---

## 结论与后续工作建议

本次实验实现了内核级的进程管理、抢占式轮转调度，并扩展了 Lab6 的最小系统调用框架（`getpid/write/exit`）。系统在参数校验与安全边界上表现良好，基础测试通过。建议的后续工作：

- 扩展系统调用集合与文件/设备抽象；
- 实现阻塞/唤醒机制改进 `wait_process`；
- 性能优化（零拷贝、fast-path）；
- 引入更完整的权限与审计机制。

---

*报告文件已生成为 `Report_complete.md`。若你希望我将其覆盖原 `Report.md` 或追加合并，请确认，我可以替你执行覆盖操作或直接将内容追加到原文件。*
