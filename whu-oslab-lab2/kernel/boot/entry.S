/* kernel/boot/entry.S */
    .section .text
    .global _entry
_entry:
        # 把栈指向 CPU_stack + ((hartid+1) * 4096)
        la  sp, CPU_stack
        li  a0, 4096
        csrr a1, mhartid
        addi a1, a1, 1
        mul a0, a0, a1
        add sp, sp, a0

        /* 为 C 代码和锁归属设置 tp=mhartid */
        csrr a0, mhartid
        mv  tp, a0

        /* 清零 BSS: 使用 linker script 导出的 edata/end */
        la  a0, edata    /* a0 = start of bss */
        la  a1, end      /* a1 = end of bss */
1:
        bge a0, a1, 2f
        sd  zero, 0(a0)
        addi a0, a0, 8
        j   1b
2:
        /* 跳转到 C 的 start */
        call start
spin:
        j spin
