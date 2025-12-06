include common.mk

KERN = kernel
KERNEL_ELF = kernel-qemu
CPUNUM = 2
FS_IMG = fs.img

.PHONY: clean $(KERN)

$(KERN):
	$(MAKE) build --directory=$@

# QEMU相关配置
QEMU     =  qemu-system-riscv64
QEMUOPTS =  -machine virt -bios none -kernel $(KERNEL_ELF) 
QEMUOPTS += -m 128M -smp $(CPUNUM) -nographic
#QEMUOPTS += -drive file=$(FS_IMG),format=raw,id=vd0,if=none
#QEMUOPTS += -device virtio-blk-device,drive=vd0,bus=virtio-mmio-bus
QEMUOPTS += -drive file=$(FS_IMG),format=raw,id=vd0,if=none
QEMUOPTS += -device virtio-blk-device,drive=vd0,bus=virtio-mmio-bus.0

# 调试
GDBPORT = $(shell expr `id -u` % 5000 + 25000)
QEMUGDB = $(shell if $(QEMU) -help | grep -q '^-gdb'; \
	then echo "-gdb tcp::$(GDBPORT)"; \
	else echo "-s -p $(GDBPORT)"; fi)

build: $(FS_IMG) $(KERN)

$(FS_IMG):
	@if [ ! -f $(FS_IMG) ]; then \
		echo "Creating $(FS_IMG) (32MB)"; \
		dd if=/dev/zero of=$(FS_IMG) bs=1M count=32 status=none; \
	fi

# qemu运行
qemu: $(FS_IMG) $(KERN)
	$(QEMU) $(QEMUOPTS)

.gdbinit: .gdbinit.tmpl-riscv
	sed "s/:1234/:$(GDBPORT)/" < $^ > $@

qemu-gdb: $(KERN) .gdbinit
	$(QEMU) $(QEMUOPTS) -S $(QEMUGDB)

clean:
	$(MAKE) --directory=$(KERN) clean
	rm -f $(KERNEL_ELF) .gdbinit
