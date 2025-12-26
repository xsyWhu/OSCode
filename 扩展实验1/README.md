# Lab8：系统扩展

## 1. 环境与依赖

- Ubuntu/WSL2 + `riscv64-linux-gnu-*` 交叉编译工具链  
- QEMU (virt, riscv64)  
- 需完成前置实验（内存管理、中断、基础进程管理）

## 2. 编译与运行

```bash
make           # 构建 kernel-qemu
make qemu      # 启动 QEMU (nographic)
```

如需清理：

```bash
make clean
```

## 3. 本次扩展功能

- **多级反馈队列(MLFQ)调度与优先级 syscall**：`kernel/proc/proc.c` 通过 3 级队列、时间片衰减、老化与周期性 boost 实现；用户态提供 `setpriority`/`getpriority`，内核启动时运行 MLFQ demo（`kernel/boot/main.c`）。  
- **用户态与系统调用框架**：在 `usertrap`/`usertrapret` 处理用户态 trap；`kernel/syscall/syscall.c` 注册 fork/exec/wait/文件/IPC 等系统调用。  
- **ELF 装载与内置用户程序**：`kernel/proc/exec.c` 装载内嵌的 `/init`、`/logread`、`/nice`、`/elfdemo`、`/msgdemo`，为每个用户进程创建独立页表和用户栈。  
- **文件系统与设备接口**：初始化 virtio 磁盘、日志与 buffer cache（`kernel/boot/main.c`），提供 open/read/write/mkdir/chdir 等文件类系统调用。  
- **内核日志环形缓冲**：`kernel/lib/klog.c` 维护可控日志级别，`klog` 系统调用将日志复制到用户缓冲，`user/logread` 持续读取。  
- **消息队列 IPC**：`kernel/ipc/msg.c` 提供阻塞式队列（sleep/wakeup），用户态通过 `msgget`/`msgsend`/`msgrecv` 使用，`user/msgdemo` 示例演示父子进程通信。

## 4. 用户态程序

- `/init`：启动时运行，先 fork 出 `/logread`，再执行优先级测试和 `elfdemo`。  
- `/nice`：查询或设置进程优先级。  
- `/msgdemo`：父子进程通过消息队列交换字符串。  
- `/logread`：持续读取内核日志环形缓冲。  
- `/elfdemo`：验证 ELF 装载与数据段映射，打印入口参数与地址信息。
