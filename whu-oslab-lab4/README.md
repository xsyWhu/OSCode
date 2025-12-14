# Lab4 —— RISC-V 中断与时钟管理

完成了 **M/S 模式中断委托、trap 框架、PLIC 外设中断分发、CLINT 时钟中断及自测用例**。
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
   - RISC-V 的交叉工具链：`riscv64-unknown-elf-gcc` 或 `riscv64-linux-gnu-gcc`。  
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

4. **GDB 调试**
   ```sh
   riscv64-unknown-elf-gdb kernel/kernel.elf
   (gdb) target remote :1234
   (gdb) b trap_kernel_handler
   (gdb) c
   ```

---

## 实验功能与验证

1. **定时器与 ticks (`test_timer_interrupt`)**
   - 在 `kernel/boot/main.c` 中实现了 `test_timer_interrupt()`，包含三项子测试：
      - Test 1 — 观察 50 个 tick（串口输出 `T` 表示每个 tick），统计起止 tick；
      - Test 2 — 精度验证（等待 10 tick，比较实际 ticks 数量）；
      - Test 3 — 实时观察 20 个 tick（串口输出 `.` 作为进度）。
   - 注意：`main()` 中对该函数的调用被注释掉（可按需解除注释以运行）。

2. **异常处理 (`test_exception_handling`)**
   - `test_exception_handling()` 在 `main.c` 中实现并在当前 `main()` 默认被调用（其余测试被注释）。
   - 本函数包含三种触发器（请每次只启用一项以便观察对应输出）：
      - `trigger_illegal_instruction()` — 注入无效指令以触发 Illegal Instruction 异常；
      - `trigger_bad_memory_access()` — 访问不可映射地址以触发 Page Fault/Access Fault；
      - `trigger_ecall_from_smode()` — 在 S 模式执行 `ecall` 以触发环境调用异常。
   - 以上任意异常会由 `trap_kernel_handler()` 路径打印 `scause/stval` 等信息并最终 `panic()`，便于验证异常处理链路。

3. **外设中断（UART 等）**
   - UART 中断由驱动通过 `register_interrupt(UART_IRQ, uart_intr)` 注册并由 PLIC 分发；键入字符后应在串口看到回显，证明 Claim/Complete 与中断使能工作正常。

4. **中断开销粗测 (`test_interrupt_overhead`)**
   - `test_interrupt_overhead()` 等待固定数量的 tick（源码中为 20），并用 `r_time()` 计时以输出 tick 周期的平均 cycle 数，作为 trap/处理中断开销的粗略估计。
   - `main()` 中对该函数的调用同样被注释，可按需启用。
