# 实验报告（实验 1：RISC-V 引导与裸机启动）

## 一、实验目的

通过参考 xv6 的启动机制，理解并实现最小操作系统的引导过程，最终在 QEMU 中输出 `"Hello 05"`。  

具体目标：
1. 掌握 RISC-V 裸机启动流程。  
2. 学会编写启动汇编、链接脚本。  
3. 理解 BSS 段清零、栈初始化的重要性。  
4. 实现最小串口驱动并输出字符串。  
5. 熟悉 QEMU + GDB 调试方法。  

## 二、实验环境

  硬件：x86_64 主机  
  软件：  
    QEMU (支持 RISC-V virt)  
    RISC-V GNU 工具链 (`riscv64-unknown-elf-gcc`)  
    GDB (`gdb-multiarch`)  
  系统：Ubuntu 24.04  

## 三、系统设计部分

### 1. 架构设计说明
本实验的目标是基于 RISC-V 架构，完成一个简化的操作系统内核启动过程。系统整体结构参考 xv6，主要模块包括：
- **boot**：引导代码，负责栈初始化、BSS 段清零、跳转到 C 语言入口。
- **lib**：基础库，提供 printf、spinlock 等实现。
- **dev**：外设驱动，如 UART 串口输出。
- **proc**：进程与 CPU 抽象，提供 `mycpu` / `mycpuid`。
- **kernel.ld**：链接脚本，规定内存布局并导出符号。

### 2. 关键数据结构
- `struct cpu`：表示每个硬件线程（hart）的基本状态。
- `spinlock_t`：自旋锁结构体，包含 `locked` 和 `cpuid`，用于多核间同步。
- 全局 `panicked`：标记内核是否崩溃，避免多核同时输出干扰。

### 3. 与 xv6 对比
- xv6 在 `start.c` 中会为每个核打印 `hartid`；本实验实现中仅让 hart0 打印 `Hello 05`，避免输出乱序。
- 自旋锁实现与 xv6 相同，均基于 RISC-V 原子指令 `__sync_lock_test_and_set`。
- 链接脚本更精简，仅包含 `.text`、`.data`、`.bss` 三个主要段。

### 4. 设计决策理由
- **只让 hart0 打印**：确保输出一致性，避免多核 UART 打印交错。
- **BSS 清零**：保证全局变量（如 `panicked`、自旋锁状态）正确初始化。
- **使用自旋锁保护 printf**：为后续多核并行做准备。

## 四、实验过程部分

### 1. 实现步骤记录
1. **环境搭建**  
   - 安装 QEMU、交叉编译工具链（`riscv64-unknown-elf-gcc`）。  
   - 使用 `git` 初始化仓库并整理目录结构。
2. **修改 entry.S**  
   - 添加 BSS 清零循环，确保全局变量初始化。
3. **编写 kernel.ld**  
   - 导出 `edata`、`end` 符号供汇编清零使用。
4. **补全 print.c**  
   - 定义 `panicked` 全局变量。  
   - 实现 `panic`、`printf`、`assert`，并加入自旋锁保护。
5. **实现 spinlock.c**  
   - 编写 `acquire/release`，保证多核同步。  
6. **实现 proc.c / cpu.h**  
   - 定义 `struct cpu` 和 `mycpuid`，封装 `tp` 寄存器读取。
7. **修改 start.c**  
   - 初始化 UART 并调用 `printf("Hello 05")`。

### 2. 遇到的问题与解决方案
- **问题 1：找不到交叉编译器 `riscv64-linux-gnu-gcc`**  
  - 解决：安装 `gcc-riscv64-unknown-elf` 并修改 Makefile。  
- **问题 2：spinlock.c 中 `cpu` 字段不存在**  
  - 解决：检查 `struct spinlock` 定义，改为 `cpuid`。  
- **问题 3：`panic` 声明和实现不一致**  
  - 解决：统一函数签名为 `void panic(const char *s)`。  
- **问题 4：多核同时打印导致输出乱序**  
  - 解决：只允许 hart0 打印，或者使用 spinlock 保护 `printf`。  

### 3. 源码理解总结
- **启动流程**：QEMU 加载 kernel → `_entry` 设置栈 → 清零 BSS → `start()` → 初始化 UART → `printf("Hello 05")`。
- **内核模块划分**：boot 负责硬件初始化，lib 负责基本功能，proc 提供 CPU 抽象。


## 五、测试验证部分

### 1. 功能测试结果
运行：vscode终端里面：输入make qemu
输出![alt text](QQ_1758524566630.png)

### 2. 异常测试部分
1. 我发现将entry.S 中的bss段清零去掉似乎也是正常输出，并未出现乱码情况——询问AI得知这是因为QEMU 的 ELF loader 自动帮我清零了 .bss 段。但从 OS 启动的正确性 来说，清零 .bss 还是必须的，否则一旦换加载方式（裸 bin / 真机）就会立即出问题。


## 六、 实验总结

    通过本实验，我掌握了 RISC-V 裸机启动流程，学会了如何从 _start 设置栈、清零 BSS，再跳转到 C 函数，并通过串口打印输出验证结果。使用 QEMU + GDB，可以精确调试每一步。
最终成功实现最小 OS 输出 "Hello 05"。
    其中遇到了一些问题，比如我没注意到entry.S文件名后缀应该是大写的'S'，而非小写，导致在make run 中一直报错——最后通过多次询问ChatGpt解决问题。
    另外，编译过程中还遇到“编译器在生成对字符串常量 "Hello 05\n" 的访问时，尝试用 RISC-V 的 auipc+addi 模式，结果因为地址太远而失败”这类错误，因为是在裸机中，我们把程序加载在
0x80000000，而默认编译选项假设 .rodata 可能在更远的地方。故而我在链接脚本中将.rodata 紧跟 .text，地址更近，也避免 relocation 溢出。
    除此之外，在进行调试的时候也出现了一些问题如“(gdb) c The program is not being run.”，最后发现是由于输入“target remote :1234”未连接成功
