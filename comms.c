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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <avr/io.h>
#include "main.h"
#include "comms.h"


void sd_cs_low(void) { PORTB &= ~(1 << SD_CS); }
void sd_cs_high(void) { PORTB |= (1 << SD_CS); }

void leds_cs_low(void) { PORTB &= ~(1 << CS_LEDS); }
void leds_cs_high(void) { PORTB |= (1 << CS_LEDS); }

void lcd_cs_low(void) { PORTB &= ~(1 << CS_LCD); }
void lcd_cs_high(void) { PORTB |= (1 << CS_LCD); }


void led_on(uint8_t led)
{
	led_status |= (1 << led);
	set_leds(led_status);
}

void led_off(uint8_t led)
{
	led_status &= ~(1 << led);
	set_leds(led_status);
}

/*******************************************************************
* Set leds bitfield
*
* @param b	Value
*/
void set_leds(uint8_t b)
{
	led_status = b;
	
	leds_cs_low();
	spi_byte(b);
	leds_cs_high();
}

/*******************************************************************
* Init SPI
*
* @param fast	1 = 1MHz, 0 = 250kHz
*/
void spi_init(uint8_t fast)
{
	if(fast)
	{
		// 1/16 = 1MHz
		SPCR = (1 << SPE) | (1 << MSTR) | (1 << SPR0);
		SPSR = 0x00;
	}
	else
	{
		// 1/64 = 250kHz
		SPCR = (1 << SPE) | (1 << MSTR) | (1 << SPR1);
		SPSR = 0x00;
	}
}

/*******************************************************************
* Send/Read a byte over SPI
*/
uint8_t spi_byte(uint8_t b)
{
	SPDR = b;
	while(!(SPSR & (1 << SPIF)));
	return SPDR;
}


/*******************************************************************
* Initialize USART
*/
void usart_init(void)
{
	UBRR1H = 0;
	UBRR1L = 51; // 19200
	UCSR1B = (1 << RXEN1) | (1 << TXEN1);
	UCSR1C = (1 << UCSZ11) | (1 << UCSZ10);
}

/*******************************************************************
* Stream handler for printf(), set in main.c
*/
int usart_printf(char c, FILE *stream)
{
	usart_byte(c);
	return 0;
}

/*******************************************************************
* Send a byte over usart
*/
void usart_byte(uint8_t c)
{
	while(!(UCSR1A & (1 << UDRE1)));
	UDR1 = c;
}
