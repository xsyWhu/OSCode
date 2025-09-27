CROSS = riscv64-unknown-elf-
CC = $(CROSS)gcc
OBJCOPY = $(CROSS)objcopy

CFLAGS = -Wall -O2 -nostdlib -fno-builtin -ffreestanding -fno-pic -mcmodel=medany -march=rv64im -mabi=lp64
LDFLAGS = -T kernel.ld

OBJS = entry.o main.o uart.o

all: kernel.elf kernel.bin

kernel.elf: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

kernel.bin: kernel.elf
	$(OBJCOPY) -O binary $< $@

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.S
	$(CC) $(CFLAGS) -c -o $@ $<

# 普通运行
run: kernel.bin
	qemu-system-riscv64 -machine virt -nographic -kernel kernel.bin

# 启动 QEMU 并等待 GDB 连接
qemu-gdb: kernel.bin
	qemu-system-riscv64 -machine virt -nographic -kernel kernel.bin -s -S

clean:
	rm -f *.o kernel.elf kernel.bin
