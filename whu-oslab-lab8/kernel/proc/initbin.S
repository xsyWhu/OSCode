    .section .rodata
    .globl _binary_init_elf_start
    .globl _binary_init_elf_end
    .globl _binary_logread_elf_start
    .globl _binary_logread_elf_end
    .globl _binary_nice_elf_start
    .globl _binary_nice_elf_end
    .globl _binary_elfdemo_elf_start
    .globl _binary_elfdemo_elf_end
    .globl _binary_msgdemo_elf_start
    .globl _binary_msgdemo_elf_end
    .globl _binary_cowtest_elf_start
    .globl _binary_cowtest_elf_end
    .globl _binary_rtdemo_elf_start
    .globl _binary_rtdemo_elf_end
_binary_init_elf_start:
    .incbin "../../user/init.elf"
_binary_init_elf_end:

_binary_logread_elf_start:
    .incbin "../../user/logread.elf"
_binary_logread_elf_end:

_binary_nice_elf_start:
    .incbin "../../user/nice.elf"
_binary_nice_elf_end:

_binary_elfdemo_elf_start:
    .incbin "../../user/elfdemo.elf"
_binary_elfdemo_elf_end:

_binary_msgdemo_elf_start:
    .incbin "../../user/msgdemo.elf"
_binary_msgdemo_elf_end:

_binary_cowtest_elf_start:
    .incbin "../../user/cowtest.elf"
_binary_cowtest_elf_end:

_binary_rtdemo_elf_start:
    .incbin "../../user/rtdemo.elf"
_binary_rtdemo_elf_end:
