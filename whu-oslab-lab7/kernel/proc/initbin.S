    .section .rodata
    .globl _binary_init_bin_start
    .globl _binary_init_bin_end
_binary_init_bin_start:
    .incbin "../../user/init.bin"
_binary_init_bin_end:
