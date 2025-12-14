# 头文件
INLCUDES := ../../include

target = $(shell ls *.c *.S 2>/dev/null | awk '{gsub(/\.c|\.S/, ".o"); print $0}')

.PHONY: clean

include ../../common.mk

%.o: %.S
	$(CC) $(CFLAGS) -I $(INLCUDES) -c $<

%.o: %.c
	$(CC) $(CFLAGS) -I $(INLCUDES) -c $<

build: $(target)
	
clean:
	rm -f *.o *.d
