include ../common.mk

INCLUDES := ../include

USER_OBJS := crt0.o usys.o ulib.o init.o
USER_ELF := init.elf
USER_BIN := init.bin

.PHONY: build clean

%.o: %.c
	$(CC) $(CFLAGS) -I$(INCLUDES) -c $< -o $@

%.o: %.S
	$(CC) $(CFLAGS) -I$(INCLUDES) -c $< -o $@

build: $(USER_BIN)

$(USER_ELF): $(USER_OBJS) init.ld
	$(LD) -T init.ld -o $@ $(USER_OBJS)

$(USER_BIN): $(USER_ELF)
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f $(USER_OBJS) $(USER_ELF) $(USER_BIN) *.d
