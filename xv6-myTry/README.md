## 概要
    该项目为lab1的任务，通过参考 xv6 的启动机制，理解并实现最小操作系统的引导过程，最终在QEMU 中输出"Hello 05"。
项目包括 kernel.ld(链接文件)、entry.S、uart.c、main.c以及Makefile五个主要文件

## 运行流程
打开VS连接WSL，在配置好的环境中打开VS终端，
输入make run，输出：
"
S
P
B
Hello 05
"

## 调试流程
  1. 打开一个终端，执行make qemu-gdb；
  2. 打开另外一个终端，执行 riscv64-unknown-elf-gdb kernel.elf 或者 gdb-multiarch kernel.elf（多终端）；
  3. 连接到QEMU —— target remote :1234
  4. 设置断点、运行等：
  b _start
  b main    
  c
  si //单步执行
  info registers //查看寄存器
  x/16x 0x80000000 //查看内存

## 文件结构
.
├── entry.S # 启动汇编
├── kernel.ld # 链接脚本
├── uart.c # 串口驱动
├── main.c # 主函数
└── Makefile # 构建脚本

## 思考题
1. 启动栈的设计：
o 你如何确定栈的大小？考虑哪些因素？
o 如果栈溢出会发生什么？如何检测栈溢出？
2. BSS 段清零：
o 写一个全局变量，不清零 BSS 会有什么现象？
o 哪些情况下可以省略 BSS 清零？
3. 与 xv6 的对比：
o 你的实现比 xv6 简化了哪些部分？
o 这些简化在什么情况下会成为问题？
4. 错误处理：
o 如果 UART 初始化失败，系统应该如何处理？
o 如何设计一个最小的错误显示机制？