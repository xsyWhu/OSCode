# Lab4 —— RISC-V 中断与时钟管理

本仓库对应“从零构建操作系统”实验四，完成了 **M/S 模式中断委托、trap 框架、PLIC 外设中断分发、CLINT 时钟中断及自测用例**。本文档提供代码概览、依赖与构建步骤、测试方法与调试提示，方便验收与复现实验结果。

---

## 代码结构

| 目录 | 说明 |
| --- | --- |
| `kernel/boot/` | `start.c` 完成 M-mode 初始化与 `timer_init()`；`main.c` 负责打印、自测、开启中断。 |
| `kernel/trap/` | `trap.S` 提供 S-mode `kernel_vector` 与 M-mode `timer_vector`；`trap_kernel.c` 提供 IRQ 注册表、异常处理和时钟/外设分发逻辑。 |
| `kernel/dev/` | `timer.c` 管理 `ticks` 与 `mscratch`，`plic.c` 封装 PLIC 操作，`uart.c` 完成 16550 驱动与中断回显。 |
| `kernel/lib/` | `print.c`、`lock.c` 等基础设施，支持内核日志和自旋锁。 |
| `kernel/mem/`、`kernel/proc/` | 为后续实验预留的内存与 CPU 结构体定义，本实验使用其中的 `mycpu()/mycpuid()`。 |
| `include/` | 统一头文件（`trap.h`、`timer.h`、`memlayout.h` 等）描述硬件寄存器和接口。 |

---

## 构建与运行

1. **依赖**  
   - 任一可用于裸机 RISC-V 的交叉工具链：`riscv64-unknown-elf-gcc` 或 `riscv64-linux-gnu-gcc`。  
   - QEMU 5.0+，需包含 `qemu-system-riscv64`。  
   - GNU make。

2. **构建**  
   ```sh
   make            # 生成 kernel.bin
   make clean      # 清理产物
   ```

3. **运行**  
   ```sh
   make qemu       # 启动 QEMU 并输出串口日志
   make qemu-gdb   # QEMU 等待 GDB 连入 (TCP 1234)
   ```

4. **GDB 调试**（可选）  
   ```sh
   riscv64-unknown-elf-gdb kernel/kernel.elf
   (gdb) target remote :1234
   (gdb) b trap_kernel_handler
   (gdb) c
   ```

---

## 实验功能与验证

1. **定时器与 ticks**  
   - 在 `main()` 中调用 `test_timer_interrupt()`，串口会输出 3 组测试：50 次 tick 观测、10 tick 精度验证、20 tick 实时监控。  
   - `timer_get_ticks()` 读取受自旋锁保护的计数，验证 M-mode → S-mode 委托与 `timer_update()` 生效。

2. **异常处理**  
   - `test_exception_handling()` 可触发非法指令、越界访存和 S-mode `ecall`，`handle_exception()` 会打印 `scause/stval` 并 `panic`，用于检查异常路径。

3. **外设中断**  
   - UART 中断由 `register_interrupt(UART_IRQ, uart_intr)` 注册。运行 `make qemu` 后在串口敲击键盘即可回显，证明 PLIC Claim/Complete、使能开关正常。

4. **性能观测**  
   - `test_interrupt_overhead()` 在等待 20 个 tick 的同时读取 `r_time()`，输出平均 tick 间隔（约 1e6 cycle），用于演示 trap 处理开销评估方法。

---

## 常见问题与调试建议

- **无中断输出**：确认 `make` 后重新运行，确保 `start.c` 中的 `w_mideleg/mideleg`、`w_sie()` 未被覆盖；可在 `trap_kernel_handler()` 中暂时打印 `scause`。
- **PLIC Claim 恒为 0**：表明没有 pending IRQ，检查 `enable_interrupt(UART_IRQ)` 是否在 `trap_inithart()` 执行，或 UART 是否开启接收中断。
- **时钟停止增长**：可能是 `timer_vector` 未正确写入 `mscratch` 或 `INTERVAL` 过小导致溢出。确认 `timer_init()` 在每个 hart 调用且 `CLINT_MTIMECMP` 正确更新。
- **异常未触发**：确保 `test_exception_handling()` 中只启用一项测试，避免在第一次 panic 后继续执行。

如需提交验收，请提供：
1. `make qemu` 的完整串口日志（包含 tick/exception 测试）。  
2. `Report.md`（本仓库已更新）。  
3. 触发异常或 tick 运行时的截图/录像（可按报告说明生成）。
