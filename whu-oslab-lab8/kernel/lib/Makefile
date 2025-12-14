# 头文件
INLCUDES := ../../include

# 选出所有后缀为.c或.S的文件,将其名称后缀替换为.o,作为输出目标
# 选出所有后缀为.c或.S的文件,将其名称后缀替换为.o,作为输出目标
sources := $(wildcard *.c *.S)
target := $(sources:.c=.o)
target := $(target:.S=.o)

.PHONY: clean

# 定义了一些通用操作和参数
include ../../common.mk

%.o: %.S
	$(CC) $(CFLAGS) -I $(INLCUDES) -c $<

%.o: %.c
	$(CC) $(CFLAGS) -I $(INLCUDES) -c $<

build: $(target)
	
clean:
	rm -f *.o *.d
