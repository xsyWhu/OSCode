include common.mk

KERN = kernel
USER = user
KERNEL_ELF = kernel-qemu
CPUNUM = 2
FS_IMG = fs.img
FS_SIZE_MB ?= 8
MKFS = python3 tools/mkfs.py

.PHONY: clean $(KERN) $(USER)

$(KERN): $(USER)
	$(MAKE) build --directory=$@

$(USER):
	$(MAKE) build --directory=$@

# QEMU相关配置
QEMU     =  qemu-system-riscv64
QEMUOPTS =  -machine virt -bios none -kernel $(KERNEL_ELF) 
QEMUOPTS += -m 128M -smp $(CPUNUM) -nographic
QEMUOPTS += -global virtio-mmio.force-legacy=false
QEMUOPTS += -drive file=$(FS_IMG),if=none,format=raw,id=hd0 -device virtio-blk-device,drive=hd0,bus=virtio-mmio-bus.0

# 调试
GDBPORT = $(shell expr `id -u` % 5000 + 25000)
QEMUGDB = $(shell if $(QEMU) -help | grep -q '^-gdb'; \
	then echo "-gdb tcp::$(GDBPORT)"; \
	else echo "-s -p $(GDBPORT)"; fi)

build: $(KERN)

$(FS_IMG): tools/mkfs.py
	$(MKFS) $(FS_IMG) $(FS_SIZE_MB)

# qemu运行
qemu: $(KERN) $(FS_IMG)
	$(QEMU) $(QEMUOPTS)

.gdbinit: .gdbinit.tmpl-riscv
	sed "s/:1234/:$(GDBPORT)/" < $^ > $@

qemu-gdb: $(KERN) .gdbinit
	$(QEMU) $(QEMUOPTS) -S $(QEMUGDB)

clean:
	$(MAKE) --directory=$(KERN) clean
	$(MAKE) --directory=$(USER) clean
	rm -f $(KERNEL_ELF) .gdbinit $(FS_IMG)
