# Toolchain Configurations
CC      = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy
SIZE    = arm-none-eabi-size

# Target Microarchitecture Flags (Corrected Syntax)
MCU_FLAGS = -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard

# Compilation Environment Settings
CFLAGS  = $(MCU_FLAGS) -O2 -Wall -Wextra -ffunction-sections -fdata-sections -nostdlib
LDFLAGS = $(MCU_FLAGS) -T linker.ld -Wl,--gc-sections -Wl,--print-memory-usage

# Targeted Artifact Generation Configurations
TARGET = wolf3d_skeleton
SRCS   = main.c startup_stm32f411xe.s display.c
OBJS   = main.o startup_stm32f411xe.o display.o

all: $(TARGET).elf $(TARGET).bin

%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

%.o: %.s
	$(CC) -c $(CFLAGS) $< -o $@

$(TARGET).elf: $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@
	$(SIZE) $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f $(OBJS) $(TARGET).elf $(TARGET).bin
