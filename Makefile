targets = matrix.elf matrix.bin

all: $(targets)

%.elf: %.c
	avr-gcc -Wall -Os -mmcu=attiny13a $< -o $@

%.bin: %.elf
	avr-objcopy -I elf32-avr -O binary $< $@
