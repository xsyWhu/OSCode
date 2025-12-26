#ifndef __PRINT_H__
#define __PRINT_H__

#include "common.h"

void print_init(void);
void printf(const char* fmt, ...);
void panic(const char* warning);//内核panic处理
void assert(bool condition, const char* warning);//断言失败处理
void clear_screen(void);//清屏
void goto_xy(int row, int col);//光标移动
void set_color(int fg, int bg);//设置颜色
void reset_color(void);//重置颜色

#endif


