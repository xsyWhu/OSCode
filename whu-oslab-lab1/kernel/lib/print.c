/* kernel/lib/print.c */
#include <stdarg.h>
#include <stddef.h>
#include "lib/print.h"
#include "dev/uart.h"
#include "lib/lock.h"

/* 全局 panic 标志（需要在 BSS 清零后为 0） */
int panicked = 0;

/* 用于保护打印的自旋锁 */
static spinlock_t print_lk;

/* 低级单字符打印 */
static void print_putc(char c) {
    /* 在 panic 状态下仍尽量输出 */
    if (!panicked) {
        uart_putc_sync((int)c);
    }
}

/* 初始化打印子系统（只要幂等即可） */
void print_init(void)
{
    uart_init();
    spinlock_init(&print_lk, "print");
}

/* 简单 puts */
void puts(const char *s)
{
    if (!s) return;
    spinlock_acquire(&print_lk);
    while (*s) {
        print_putc(*s++);
    }
    spinlock_release(&print_lk);
}

/* panic 与 assert 实现 */
void panic(const char *s)
{
    panicked = 1;
    /* 在 panic 中尽量不依赖锁（锁可能不安全），直接逐字符写串口 */
    if (s) {
        const char *p = "panic: ";
        while (*p) uart_putc_sync(*p++);
        while (*s) uart_putc_sync(*s++);
        uart_putc_sync('\n');
    } else {
        const char *p = "panic\n";
        while (*p) uart_putc_sync(*p++);
    }
    /* 停在这里 */
    for (;;)
        ;
}

void assert(bool condition, const char* warning)
{
    if (!condition) panic(warning);
}
/*
// 输出无符号数（base 可为 10 或 16)
static void print_number(unsigned long num, int base, int sign)
{
    //
}
// 简单 printf（支持 %s %d %x %p %c %%)
void printf(const char *fmt, ...)
{
    //
} 
*/
