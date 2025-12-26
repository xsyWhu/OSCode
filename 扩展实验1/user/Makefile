include ../common.mk

INCLUDES := ../include

COMMON_OBJS := crt0.o usys.o ulib.o
USER_PROGS := init nice logread elfdemo msgdemo
USER_ELFS := $(USER_PROGS:%=%.elf)
USER_BINS := $(USER_PROGS:%=%.bin)
.SECONDARY: $(USER_ELFS)

.PHONY: build clean

%.o: %.c
	$(CC) $(CFLAGS) -I$(INCLUDES) -c $< -o $@

%.o: %.S
	$(CC) $(CFLAGS) -I$(INCLUDES) -c $< -o $@

build: $(USER_BINS) $(USER_ELFS)

%.elf: $(COMMON_OBJS) %.o init.ld
	$(LD) -T init.ld -o $@ $(COMMON_OBJS) $*.o

%.bin: %.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f $(COMMON_OBJS) $(USER_PROGS:%=%.o) $(USER_ELFS) $(USER_BINS) *.d
