# Lab6：系统调用 (Syscall)

本实验在 Lab5 线程调度、物理/虚拟内存和中断基础上，引入了完整的 **用户态 ↔ 内核态** 交互路径。实现内容覆盖 `trapframe` 扩展、`trampoline.S`、系统调用分发表、`sys_*` 处理函数、简化版文件描述符层以及用户态运行库/程序，使得 `init` 用户程序可以通过 `ecall` 调用 `write/getpid/exit` 等接口。

## 1. 构建与运行

### 1.1 开发环境
- Ubuntu 24.04 / WSL2（含 `make`、`gcc`、`python3` 等基础工具）
- RISC-V 交叉工具链：`riscv64-unknown-elf-gcc` 或等价发行版
- QEMU 5.0+，并启用 `virt` RISC-V 64 机器

### 1.2 常用命令
```bash
make           # 构建内核 + 用户程序，生成 kernel-qemu
make qemu      # 以 nographic 模式启动 QEMU，自动运行内核与 init
make clean     # 清理构建产物
```

运行后可观察到：
1. 内核启动日志 → Lab5 调度测试 → `run_lab6_syscall_tests()` 输出（`kernel/boot/main.c`）
2. 用户态 `init` 程序在控制台打印 `Hello from user init!`

若希望单独调试系统调用，可在 QEMU 内启用 `Ctrl+ A` → `x` 退出后再次 `make qemu`。

## 2. 系统调用通路概览

1. **用户库**：`user/usys.S` 为每个系统调用生成桩代码（在 `a7` 填系统调用号、执行 `ecall`），`user/ulib.c` 提供 C 封装。
2. **陷阱入口**：`trampoline.S` 通过 `uservec` 保存用户寄存器，切换到内核栈后跳入 `usertrap()`（`kernel/trap/trap_kernel.c:260`）。
3. **分发**：`syscall()`（`kernel/syscall/syscall.c:115`）读取 `trapframe->a7`，依据 `include/syscall.h` 中的表项调用 `sys_*`，返回值写回 `a0`。
4. **内核实现**：
   - 进程类：`kernel/syscall/sysproc.c`（`sys_fork/exit/wait/kill/getpid/sbrk`）
   - 文件类：`kernel/syscall/sysfile.c`（`sys_open/close/read/write`，并通过 `kernel/fs/file.c` 的 `struct file` 驱动 UART 控制台）
   - 程序替换：`kernel/syscall/sysexec.c` → `exec_process()`
5. **返回用户态**：`usertrapret()`（`kernel/trap/trap_kernel.c:303`）恢复 `trapframe`，设定 `sstatus`、`satp`，跳转至 `userret` 执行 `sret`。

该通路与 RISC-V 特权规范保持一致，支持 6 个参数（`a0..a5`）与 `a0` 返回值，并在内核侧统一进行指针校验、页表切换和中断控制。

## 3. 已实现的系统调用

| 调用 | 文件 | 说明 |
| ---- | ---- | ---- |
| `fork` | `kernel/syscall/sysproc.c:5` + `fork_process()` | 复制父进程页表/文件描述符，并令子进程 `a0=0` |
| `exit`/`wait`/`kill`/`getpid` | `sysproc.c` | 退出状态回传、僵尸回收、父子同步、软杀进程 |
| `sbrk` | `sysproc.c:45` | 调整 `proc->sz` 并调用 `growproc()` |
| `open`/`close`/`read`/`write` | `kernel/syscall/sysfile.c` | 只支持 `/dev/console`，通过 `copyin/out` 完成缓冲区访问，内部调用 `file.c` 的 `file{read,write}` |
| `exec` | `kernel/syscall/sysexec.c` | 解析用户态 argv 数组，复制字符串到内核临时缓冲区，再调用 `exec_process()` 加载嵌入式 `/init` |

所有 `argint/argaddr/argstr` 均来自 `kernel/syscall/syscall.c`，对越界地址立即返回 -1，保证用户态无法绕过页表。

## 4. 关键模块速览

- **`struct trapframe` & `proc` (`include/proc/proc.h`)**：为每个进程分配独立 trapframe/page table/文件表，`proc->ctx` 继续用于调度器；`kstack` 与 `trapframe` 物理页均由 `pmem_alloc()` 提供。
- **虚拟内存 (`kernel/mem/vmem.c`)**：新增 `proc_pagetable()`、`proc_freepagetable()`、`growproc()` 等，用于在 `fork/exec/sbrk` 中扩展或复制用户地址空间；`copyin/copyout/copyinstr` 承担内核访问用户内存的唯一入口。
- **文件抽象 (`include/fs/file.h`, `kernel/fs/file.c`)**：实现 `struct file` 表、`filealloc/dup/close`，并提供 console 设备读写（对应 UART）。
- **用户态运行库 (`user/`)**：`crt0.S` 完成最小 C 运行时初始化，`init.c` 调用 `write()` 与 `exit()` 验证链路；`init` 镜像由 `kernel/proc/exec.c` 作为内置镜像装载。

## 5. 测试与调试

`kernel/boot/main.c` 中 `run_lab6_syscall_tests()` 采用了指导书的测试代码，覆盖：

1. `lab6_test_basic_syscalls`：`fork/wait/getpid` 的功能路径，输出父子进程状态。
2. `lab6_test_parameter_passing`：验证 `/dev/console` 打开失败/成功的场景、负数/空指针参数。
3. `lab6_test_security`：向 `write`/`read` 传递非法地址/超大长度，确保 `copyin/out` 报错。
4. `lab6_test_syscall_performance`：循环调用 `getpid` 1 万次测量 `timer_get_ticks()` 周期。

默认日志位于 QEMU 控制台，可通过 `LAB6_ENABLE_SYSCALL_TESTS` 宏开关（位于 `kernel/boot/main.c` 顶部）。若需要观察系统调用轨迹，可在 `kernel/syscall/syscall.c` 中将 `debug_syscalls` 设为 1，内核会打印 `pid, syscall name, return value`。