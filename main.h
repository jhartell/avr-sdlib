/***************************************************************************************
* FAT16/32 filesystem implementation for AVR Microcontrollers
* Copyright (C) 2013 Johnny Härtell
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
* Device: atmega128
*
*/
#ifndef _MAIN_H_
#define _MAIN_H_

/*
* Pin definitions
*/
#define SW1		PE2
#define SW2		PE3
#define SW3		PE4

#define SD_CS		PB0	
#define SPI_SCK	PB1	
#define SPI_MOSI	PB2
#define SPI_MISO	PB3
#define SD_WP		PB4 // write protect
#define SD_CDET	PB5 // card detect
#define CS_LEDS	PB6
#define CS_LCD		PB7


volatile uint8_t led_status;

void print_mem_usage(void);

#endif