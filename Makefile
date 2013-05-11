# FAT16/32 filesystem implementation for AVR Microcontrollers
# Copyright (C) 2013 Johnny Härtell
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
# Device: atmega128
#

# Microcontroller Type
MCU = atmega128

# Avrdude options
AVRDUDE = avrdude
AVRDUDE_PROGRAMMER = usbtiny
AVRDUDE_SPEED = -B 1

# Target file name (without extension).
TARGET = main

# Cpu clock speed definition
MCU_SPEED = -D F_CPU=16000000UL

# Source files
SRC = main.c comms.c sd.c fat_fs.c fat_func.c fat_misc.c

# Object files
OBJ = $(SRC:.c=.o)

# Listing files.
LST = $(SRC:.c=.lst)

# Output format
FORMAT = ihex

# CC
CC = avr-gcc
OBJCOPY = avr-objcopy
OBJDUMP = avr-objdump

# Compiler flags
CFLAGS = -g -Os \
-funsigned-char -funsigned-bitfields -fpack-struct -fshort-enums \
-Wall -Wstrict-prototypes \
-Wa,-adhlns=$(<:.c=.lst) \
-std=gnu99 \
-mmcu=$(MCU) $(MCU_SPEED) -I.

# Linker flags
# 32k external RAM, place the heap in the external, .data + .bss + stack in internal
LDFLAGS = -Wl,-Map=$(TARGET).map,--cref \
-Wl,--defsym=__heap_start=0x801100,--defsym=__heap_end=0x8090ff \
-lm


# Default target
all: begin clean $(TARGET).elf $(TARGET).hex $(TARGET).eep $(TARGET).lss $(TARGET).sym $(TARGET).size end

# Begin messages
begin:
	@$(CC) --version
	
end:
	@echo
	@echo No errors.
	@echo

#
# Compile source files into object files
%.o : %.c 
	@$(CC) -c $(CFLAGS) $< -o $@
	@echo [CC] $<

#
# Link object files into a binary file
.SECONDARY : $(TARGET).elf
.PRECIOUS : $(OBJ)
%.elf : $(OBJ)
	@$(CC) $(CFLAGS) $(LDFLAGS) $(OBJ) --output $@
	@echo [LINK] $(OBJ) : $@

#
# Create extended listing file from ELF output file.
%.lss : %.elf
	$(OBJDUMP) -h -S $< > $@

#
# Create a symbol table from ELF output file.
%.sym: %.elf
	avr-nm -n $< > $@

#
# Create load file for flash
%.hex: %.elf
	$(OBJCOPY) -O $(FORMAT) -j .text -j .data $< $@

#
# Create a load file for eeprom
#@$(OBJCOPY) -j .eeprom --set-section-flags=.eeprom="alloc,load" --change-section-lma .eeprom=0 -O $(FORMAT) $< $@
%.eep: %.elf
	$(OBJCOPY) -O ihex -j .eeprom $< $@

#
# Size diagnostics
%.size: %.elf
	avr-size -C --mcu=$(MCU) $(TARGET).elf > $@


# Program the flash
program: $(TARGET).hex
	$(AVRDUDE) -p $(MCU) -c $(AVRDUDE_PROGRAMMER) $(AVRDUDE_SPEED) -U flash:w:$(TARGET).hex

# Program the eeprom
eeprom: $(TARGET).eep
	$(AVRDUDE) -p $(MCU) -c $(AVRDUDE_PROGRAMMER) $(AVRDUDE_SPEED) -U eeprom:w:$(TARGET).eep

# Dump fuses
dumpfuses:
	$(AVRDUDE) -p $(MCU) -c $(AVRDUDE_PROGRAMMER) $(AVRDUDE_SPEED) -U lfuse:r:lfuse.txt:b
	$(AVRDUDE) -p $(MCU) -c $(AVRDUDE_PROGRAMMER) $(AVRDUDE_SPEED) -U hfuse:r:hfuse.txt:b
	$(AVRDUDE) -p $(MCU) -c $(AVRDUDE_PROGRAMMER) $(AVRDUDE_SPEED) -U efuse:r:efuse.txt:b
	
# Clean project
clean :
	@rm -f $(TARGET).elf
	@rm -f $(TARGET).lss
	@rm -f $(TARGET).hex
	@rm -f $(TARGET).sym
	@rm -f $(TARGET).eep
	@rm -f $(TARGET).map
	@rm -f $(TARGET).size
	@rm -f $(OBJ)
	@rm -f $(LST)
	@rm -f lfuse.txt hfuse.txt efuse.txt
