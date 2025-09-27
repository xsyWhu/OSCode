# LAB-1——添加printf以及扩展功能

## 组织结构
## 代码组织结构

whu-oslab-lab1  
├── include  
│   ├── uart.h  
│   ├── lib  
│   │   ├── print.h  
│   │   └── lock.h  
│   ├── proc  
│   │   ├── cpu.h  
│   │   └── proc.h  
│   ├── common.h  
│   ├── memlayout.h  
│   └── riscv.h  
├── kernel  
│   ├── boot  
│   │   ├── main.c     
│   │   ├── start.c    
│   │   ├── entry.S  
│   │   └── Makefile  
│   ├── dev  
│   │   ├── uart.c  
│   │   └── Makefile  
│   ├── lib  
│   │   ├── print.c    
│   │   ├── spinlock.c  
│   │   └── Makefile    
│   ├── proc  
│   │   ├── pro.c     
│   │   └── Makefile  
│   ├── Makefile  
│   └── kernel.ld  
├── picture  
│   └── *.png  
├── Makefile  
├── common.mk  
├── README.md  
└── Report.md

## 第四章实验说明

本章主要实现：

- 内核最小输出环境（UART、BSS 清零、多核栈）。
- `printf`：支持 %d/%x/%s/%c/%%，并发安全。
- 清屏与控制台功能：`clear_screen()`、`goto_xy()`、`set_color()`、`reset_color()`。
- 综合演示：在 `start.c` 展示输出、定位、颜色控制和清屏。

运行方法：

bash：
make qemu

预期效果：

- 打印 "Hello OS"
- 展示 printf 功能、光标定位、清屏效果、彩色输出
