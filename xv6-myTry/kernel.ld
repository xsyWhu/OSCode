OUTPUT_ARCH(riscv)
ENTRY(_start)

SECTIONS
{
    . = 0x80000000;

    .text : {
        *(.text*)
        *(.rodata*)
    }

    .data : {
        *(.data*)
    }

    .bss : {
        _sbss = .;
        *(.bss*)
        _ebss = .;
    }

    . = ALIGN(16);
    stack_bottom = .;
    . = . + 4096;   /* 4KB 栈 */
    stack_top = .;
}
