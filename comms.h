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
* Communication functions for SPI and RS232
*/
#ifndef _COMMS_H_
#define _COMMS_H_

void sd_cs_low(void);
void sd_cs_high(void);

void leds_cs_low(void);
void leds_cs_high(void);

void led_on(uint8_t led);
void led_off(uint8_t led);
void set_leds(uint8_t b);

void lcd_cs_low(void);
void lcd_cs_high(void);

void spi_init(uint8_t fast);
uint8_t spi_byte(uint8_t b);

void usart_init(void);
int usart_printf(char c, FILE *stream);
void usart_byte(uint8_t c);

#endif