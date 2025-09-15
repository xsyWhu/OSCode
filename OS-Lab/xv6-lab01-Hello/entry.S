    .section .text
    .globl _start
_start:
    #调试点1
    li t0,0x10000000
    li t1, 'S'
    sb t1, 0(t0)

    li t1, 0x0D        # '\r'
    sb t1, 0(t0)
    li t1, 0x0A        # '\n'
    sb t1, 0(t0)

    # 设置栈指针,stack_top在ld中定义
    la sp, stack_top

    #调试点2，栈设置完成
    li t1, 'P'
    sb t1, 0(t0)

    li t1, 0x0D        # '\r'
    sb t1, 0(t0)
    li t1, 0x0A        # '\n'
    sb t1, 0(t0)

    # 清零 BSS 段
    la t0, _sbss
    la t1, _ebss
1:
    bge t0, t1, 2f
    sd zero, 0(t0)
    addi t0, t0, 8
    j 1b
2:
    #调试点 3：BSS 清零完成
    li t1, 'B'
    li t2, 0x10000000
    sb t1, 0(t2) #原本地址弄错了，t0修改后不再是基地址

    li t1, 0x0D        # '\r'
    sb t1, 0(t2)
    li t1, 0x0A        # '\n'
    sb t1, 0(t2)

    # 跳转到 C 函数 main
    call main

# 程序结束后死循环
hang:
    j hang
