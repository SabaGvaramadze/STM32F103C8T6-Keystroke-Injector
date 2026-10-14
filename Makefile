TARGET  = blink
CC      = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy
OPENCM3 = ./libopencm3

CFLAGS  = -mcpu=cortex-m3 -mthumb -Wall -g \
          -ffreestanding \
          -I$(OPENCM3)/include \
          -DSTM32F1 \
		  -O0

LDFLAGS = -T link.ld \
          --specs=nano.specs --specs=nosys.specs \
          -nostartfiles \
          -L$(OPENCM3)/lib


LIBS    =  -lopencm3_stm32f1 -lgcc -lc

all: $(TARGET).bin

$(TARGET).elf: main.c fat32_sd_spi.h link.ld
	$(CC) $(CFLAGS) $(LDFLAGS) startup.s main.c $(LIBS) -o $@

debug: $(TARGET).bin
	openocd -f interface/stlink.cfg \
	        -c "set CPUTAPID 0x2ba01477" \
	        -f target/stm32f1x.cfg

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

flash: $(TARGET).bin
	st-flash write $(TARGET).bin 0x08000000

clean:
	rm -f *.elf *.bin
