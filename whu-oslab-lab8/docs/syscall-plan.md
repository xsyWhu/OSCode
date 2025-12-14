# 系统调用功能总体规划

> 依据《操作系统实践PPT-系统调用》中的任务拆解，结合当前内核（Lab5）仅支持内核线程的现状，梳理后续实现阶段与代码落点。

## 现状评估

- **陷阱 & 中断**：`kernel/trap/trap_kernel.c` 只有 S 态入口 `kernel_vector()`，处理时钟/UART 中断以及内核态异常；未区分 U 态陷阱，也没有用户态上下文保存。
- **进程模型**：`struct proc` 仅含内核栈与 `struct context`，运行 `proc_trampoline()` 执行内核函数，没有用户地址空间、trapframe、文件表等字段。
- **内存管理**：`vmem.c` 只构建内核页表；无 `uvm_init/alloc/dealloc/copyin/copyout` 等用户页表接口。
- **系统调用层**：缺少 `syscall.c`、`syscall.h`、参数抽取/校验、`sys_*` 实现、以及用户态 stub（`usys.S`）。
- **用户态运行库**：不存在 `user/` 目录、`crt0`、最小 C 库或测试程序。

## 分阶段目标

1. **Trap/上下文扩展**（对应 PPT 任务1、2）：
   - 增强 `struct trapframe` 保存 32 个通用寄存器 + CSR。
   - 为每个 `proc` 分配独立 `trapframe` 和用户页表，新增 `usertrap()/usertrapret()` 路径和 `trampoline.S`，完成 U↔S 特权级切换。

2. **Syscall 分发框架**（任务2、3、6）：
   - 新增 `include/syscall.h`，定义系统调用号、描述符 `syscall_desc`、名称表。
   - 实现 `kernel/syscall.c`：`syscall()`、`argint/argaddr/argstr`、`copyin/copyout`、无效号处理/调试输出。
   - 在 `trap_kernel_handler` 中识别 `ecall` 并转发，`syscall()` 将返回值写入 `trapframe->a0`。

3. **核心系统调用实现**（任务4）：
   - 进程类：`sys_fork/exit/wait/kill/getpid`，完善 `proc` 结构（父子关系、内核栈、trapframe 复制、killed 标志）。
   - 文件类：接入 `file.c/fs.c/log.c`（需要从 xv6 或 PPT 示例裁剪），并提供 `sys_open/close/read/write`、`copyin/out` 检查。
   - 内存类：实现 `sys_sbrk`，维护 per-proc `sz` 与用户堆增长，必要时预留 `mmap/munmap` 接口。

4. **用户态接口**（任务5）：
   - 添加 `user/` 目录：`user.h`、`ulib.c`、`usys.S`（按 PPT 示例），`initcode.S`、若干测试程序。
   - 构建流程更新：`Makefile` 生成 `user/initcode`、`fs.img` 等，QEMU 启动后运行用户测试。

5. **安全与测试**（任务6、测试章节）：
   - 在参数抽取、`copyin/out`、文件/进程操作中落实边界检查、防止非法指针。
   - 根据 PPT 建议实现 `test_basic_syscalls`、`test_parameter_passing`、`test_security` 等回归。
   - 可选：调试开关 `debug_syscalls`、统计信息输出。

## 关键代码落点一览

| 功能 | 文件/位置 | 备注 |
| ---- | ---------- | ---- |
| trapframe 扩展 | `include/trap/trap.h`, `proc/proc.h` | 增加寄存器字段、每进程 trapframe 指针 |
| trampoline | `kernel/trap/trampoline.S` (新增) | 完成 uservec/userret 流程 |
| usertrap & 返回 | `kernel/trap/trap_kernel.c` | 识别 U-mode ecall，切换 satp/stvec |
| syscall 号/表 | `include/syscall.h`, `kernel/syscall.c` | 描述符 & 分发器 |
| 参数/内存辅助 | `kernel/syscall.c`, `mem/vmem.c` | `copyin/out`, `fetchaddr`, `fetchstr` |
| 进程结构扩展 | `include/proc/proc.h`, `kernel/proc/proc.c` | `sz`, `pagetable`, `tf`, `ofile`, `cwd` 等 |
| 文件系统 | `kernel/file.c`, `kernel/fs.c`, `kernel/log.c`, `include/fs.h` | 若当前仓库缺失需引入（基于 xv6） |
| 用户库/测试 | `user/` 目录、`Makefile` | `usys.S`, `ulib.c`, demo/test 程序 |

> **说明**：以上规划覆盖 PPT 中的 6 大任务；后续实现将严格按该路线推进，并在关键阶段（trap、syscall、用户库）各自完成后再进入下一阶段。
