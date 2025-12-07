/* include/dev/console.h */
#ifndef __CONSOLE_H__
#define __CONSOLE_H__

#include "common.h"

/* 初始化控制台（目前仅封装 UART，后续可扩展缓冲/多设备） */
void console_init(void);

/* 单字符输出（带阻塞，同步） */
void console_putc(int c);

/* 批量输出字符串（便于未来切换实现） */
static inline void console_write(const char *s)
{
    if (!s) return;
    while (*s) console_putc((unsigned char)*s++);
}

#endif
