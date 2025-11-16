# 定义头文件搜索路径
INLCUDES := ../../include

# 自动查找当前目录下的所有 .c 和 .S 源文件
# 并将它们的目标 .o 文件名存入 target 变量
target = $(shell ls *.c *.S 2>/dev/null | awk '{gsub(/\.c|\.S/, ".o"); print $0}')

# 伪目标，防止存在同名文件
.PHONY: clean

# 包含项目根目录下的通用配置 (编译器、编译选项等)
include ../../common.mk

# 规则：如何从 .S (汇编) 文件生成 .o 文件
%.o: %.S
	$(CC) $(CFLAGS) -I $(INLCUDES) -c $< -o $@

# 规则：如何从 .c (C语言) 文件生成 .o 文件
%.o: %.c
	$(CC) $(CFLAGS) -I $(INLCUDES) -c $< -o $@

# build 规则：编译所有在 target 变量中定义的目标文件
build: $(target)

# clean 规则：删除所有生成的 .o 和 .d (依赖) 文件
clean:
	rm -f *.o *.d