CC = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy
OBJDUMP = arm-none-eabi-objdump

# Cortex-M55 flags (use cortex-m33 as fallback if m55 not supported)
CFLAGS = -mcpu=cortex-m33 -mthumb -mfloat-abi=soft
CFLAGS += -O0 -g -Wall
CFLAGS += -ffreestanding -nostdlib -nostartfiles

LDFLAGS = -T link.ld -nostdlib -nostartfiles

all: minimal.bin

minimal.elf: main.c link.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ main.c

minimal.bin: minimal.elf
	$(OBJCOPY) -O binary $< $@
	$(OBJDUMP) -d $< > minimal.lst
	@echo "Build complete!"
	@echo "Binary size: $$(ls -la minimal.bin | awk '{print $$5}') bytes"

clean:
	rm -f minimal.elf minimal.bin minimal.lst

flash: minimal.bin
	/home/samuel/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI \
		-c port=SWD -w minimal.bin 0x24100400

.PHONY: all clean flash
