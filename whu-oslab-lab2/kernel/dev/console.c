/* kernel/dev/console.c */
#include "dev/console.h"
#include "dev/uart.h"

void console_init(void)
{
    /* 目前直接初始化 UART，后续可在此扩展为多后端/缓冲 */
    uart_init();
}

void console_putc(int c)
{
    /* 目前直接走 UART 的同步输出 */
    uart_putc_sync(c);
}
