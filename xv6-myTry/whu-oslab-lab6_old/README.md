## Lab6：系统调用 (Syscall)

- **环境与依赖**: 同 Lab5（交叉编译工具链 `riscv64-linux-gnu-*`、QEMU 等）。
- **编译与运行**: 使用与 Lab5 相同的构建流程：

```bash
make
make qemu
```

- **主要目标**: 实现一个最小的系统调用框架，包含系统调用号、从用户态到内核态的参数传递与返回值约定、若干基础系统调用实现，以及内核态测试用例来验证参数传递、安全性和性能。

- **已实现的功能**:
  - **系统调用号**：定义在 `include/syscall.h`（例如 `SYS_exit`, `SYS_getpid`, `SYS_write`）。
  - **系统调用分发**：在 `kernel/proc/syscall.c` 中实现 `syscall()`，从当前进程的 `trapframe` 读取系统调用号（`a7`）和参数（`a0..a5`），并调用内核中的处理函数。
  - **基本系统调用**：在 `kernel/proc/sysproc.c` 实现了 `sys_exit`, `sys_getpid`, `sys_write`（`write` 支持向 UART 输出，并做了用户内存拷贝与边界检查）。
  - **用户态示例**：`kernel/proc/initcode.S` 演示了在用户态调用 `ecall` 触发 `SYS_write` 与 `SYS_exit`。
  - **内核测试套件**：`kernel/boot/main.c` 中新增了 Lab6 的若干测试：
    - `test_basic_syscalls`：验证 `getpid`/`write` 基本行为；
    - `test_parameter_passing`：检查参数复制、错误参数处理；
    - `test_security`：对越界/非法地址的拒绝；
    - `test_syscall_performance`：测量系统调用开销（ticks）。

- **设计要点**:
  - **参数约定**：遵循 RISC‑V 约定，系统调用号放在 `a7`，参数通过 `a0..a5` 传递，返回值放回 `a0`。内核通过 `struct trapframe` 访问这些寄存器。 
  - **安全检查**：在 `sys_write` 中使用 `copyin`/`copyout`（以及页表边界检查）确保内核不会读取用户态非法地址。
  - **内核/用户边界**：`usertrap()` 在收到 `ecall`（scause == 8）时，先将 `sepc` 前移 4 字节，再打开中断并调用 `syscall()` 来执行分发。

- **后续扩展建议**:
  - 增加更多系统调用（`open/read/close/fork/exec/wait` 等），以及基于文件描述符的 I/O 子系统；
  - 引入用户态同步/阻塞机制（阻塞 `read`、睡眠/唤醒）；
  - 将 `sys_write` 扩展为支持字符设备/文件系统。

> 注：更多实现细节见 `kernel/proc/syscall.c`、`kernel/proc/sysproc.c` 与 `kernel/boot/main.c` 的 Lab6 测试段。
## 2. 代码框架

以下是本次 Lab6 的代码目录结构（简化视图）：

```
whu-oslab-lab6/
├── LICENSE
├── Makefile
├── common.mk
├── README.md
├── Report.md
├── Report_complete.md
├── kernel.bin
├── .gdbinit.tmpl-riscv
├── .gitignore
├── .vscode/
│   ├── launch.json
│   ├── settings.json
│   └── tasks.json
├── picture/
│   ├── test1_map.png
│   ├── test2_unmap.png
│   ├── test3_5_alloc&free.png
│   └── test4_Align.png
├── include/
│   ├── common.h
│   ├── memlayout.h
│   ├── riscv.h
│   ├── syscall.h
│   ├── dev/
│   │   ├── console.h
│   │   ├── plic.h
│   │   └── uart.h
│   ├── mem/
│   │   ├── pmem.h
│   │   └── vmem.h
│   ├── proc/
│   │   ├── cpu.h
│   │   ├── proc.h
│   │   └── trapframe.h
│   └── lib/
│       ├── lock.h
│       ├── print.h
│       └── string.h
├── kernel/
│   ├── Makefile
│   ├── kernel.ld
│   ├── boot/
│   │   ├── Makefile
│   │   ├── entry.S
│   │   ├── main.c
│   │   └── start.c
│   ├── dev/
│   │   ├── Makefile
│   │   ├── console.c
│   │   ├── plic.c
│   │   ├── timer.c
│   │   └── uart.c
│   ├── lib/
│   │   ├── Makefile
│   │   ├── print.c
│   │   ├── spinlock.c
│   │   └── string.c
│   ├── mem/
│   │   ├── Makefile
│   │   ├── pmem.c
│   │   └── vmem.c
│   ├── proc/
│   │   ├── Makefile
│   │   ├── initcode.S
│   │   ├── proc.c
│   │   ├── swtch.S
│   │   ├── syscall.c
│   │   └── sysproc.c
│   └── trap/
│       ├── Makefile
│       ├── trap.S
│       ├── trampoline.S
│       └── trap_kernel.c
└── (其他文档/二进制/源码)
```

说明：上面的树状结构为简化视图，实际仓库中每个目录下还有一些辅助的头文件、编译产物和 `Makefile` 依赖文件。
