/* kernel/kernel.ld */
OUTPUT_ARCH( "riscv" )
ENTRY( _entry )

SECTIONS
{
  . = 0x80000000;

  .text : {
    *(.text .text.*)
    . = ALIGN(0x1000);

    /* trampoline area (not used here, but keep if present) */
    _trampoline = .;
    KEEP(*(.trampsec))
    . = ALIGN(0x1000);
  }

  .rodata : {
    *(.rodata .rodata.*)
  }

  .data : {
    . = ALIGN(16);
    *(.sdata .sdata.*)
    . = ALIGN(16);
    *(.data .data.*)
  }

  /* bss */
  .bss : {
    . = ALIGN(16);
    *(.sbss .sbss.*)
    . = ALIGN(16);
    *(.bss .bss.*)
    . = ALIGN(16);
  }
  /* 导出 bss 的起止符号用于汇编清零 */
  PROVIDE(edata = ADDR(.bss));
  PROVIDE(end = .);

  /DISCARD/ : {
    *(.eh_frame)
  }
}
