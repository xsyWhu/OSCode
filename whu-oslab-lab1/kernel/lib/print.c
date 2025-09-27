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

/* 输出无符号数（base 可为 10 或 16） */
static void print_number(unsigned long num, int base, int sign)
{
    char buf[32];
    int i = 0;
    if (num == 0) {
        print_putc('0');
        return;
    }
    if (sign && (long)num < 0) {
        print_putc('-');
        num = (unsigned long)(-(long)num);
    }
    while (num) {
        int d = num % base;
        buf[i++] = "0123456789abcdef"[d];
        num /= base;
    }
    while (i--) print_putc(buf[i]);
}

/* 简单 printf（支持 %s %d %x %p %c %%） */
void printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    spinlock_acquire(&print_lk);

    const char *p = fmt;
    while (p && *p) {
        if (*p != '%') {
            print_putc(*p++);
            continue;
        }
        p++;
        if (*p == 's') {
            char *s = va_arg(ap, char*);
            if (!s) s = "(null)";
            while (*s) print_putc(*s++);
        } else if (*p == 'd') {
            int d = va_arg(ap, int);
            print_number((unsigned long)d, 10, 1);
        } else if (*p == 'x' || *p == 'p') {
            unsigned long x = va_arg(ap, unsigned long);
            print_number(x, 16, 0);
        } else if (*p == 'c') {
            int c = va_arg(ap, int);
            print_putc((char)c);
        } else if (*p == '%') {
            print_putc('%');
        } else {
            /* 未知格式，原样输出 */
            print_putc('%');
            if (*p) print_putc(*p);
        }
        if (*p) p++;
    }

    spinlock_release(&print_lk);

    va_end(ap);
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
