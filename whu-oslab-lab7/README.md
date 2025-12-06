# Lab5：进程管理与调度

## 1. 环境与依赖

- Ubuntu/WSL2 + `riscv64-linux-gnu-*` 交叉编译工具链  
- QEMU (virt, riscv64)  
- 需已完成前置实验（内存管理、中断等）

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
kernel/
  boot/main.c          # 启动 & 测试驱动
  proc/proc.c          # 进程管理与调度
  proc/swtch.S         # 上下文切换汇编
  trap/trap_kernel.c   # 中断/异常处理
include/
  proc/proc.h          # 进程/CPU 结构定义
  lib/...              # 工具库、锁、字符串等
Report.md              # 实验报告
README.md              # 本文件
```

## 5. 常见问题

1. **`exit_process` 报告“noreturn function does return”**  
   - 已在实现中通过死循环/`panic` 保证不返回；若自己修改需保持 `noreturn` 语义。
2. **QEMU 无输出或只显示 Hart1 idle**  
   - Hart1 设计为 idle，调度仅在 Hart0 进行；请查看 Hart0 的日志。
3. **编译时 `unused-but-set-variable`**  
   - 使用 `-Werror` 时必须移除未使用变量或将其 `__attribute__((unused))`。

## 6. 后续扩展建议

- 实现 `sleep/wakeup`，让 `wait_process` 不再忙等。
- 支持优先级/多级反馈队列等调度算法。
- 引入用户态地址空间与系统调用，完成从 kernel thread 到 user process 的过渡。
