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
* TODO
* ************************************************
* Add usage examples
* Remove/decrease debug-statements, the printf-strings waste a lot of memory
* Disable external RAM and verify that it works with the 4kB RAM available in the mega128
* Update Free_Count and Nxt_Free in the FSInfo structure on the partition, whenever necessary
* Add more checks to make sure the SD card is inserted and inited before operations
* Create folders support
* Delete files and folders support
* Rename files and folders support
* fat_fopen: Support full paths, at the moment only files in the root directory are supported
*
* Changelog
* ************************************************
* 12.05.2013
* - Fixed potential memory leak in fat_find_free_entry()
* - Cleaned up additional includes
*
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include "main.h"
#include "sd.h"
#include "fat_fs.h"
#include "comms.h"

/***************************************************************************************
* Setup a stream for printf()
*/
static FILE usart_stdout = FDEV_SETUP_STREAM(usart_printf, NULL, _FDEV_SETUP_WRITE);


/***************************************************************************************
* Print memory usage over RS232
*/
extern int __data_start;
extern int __data_end;

extern int __bss_start;
extern int __bss_end;

extern int __heap_start;
extern int __heap_end;
extern int *__brkval;


void print_mem_usage(void)
{
	uint16_t dataused;
	uint16_t bssused;
	uint16_t heapused;
	uint16_t freeram;
	
	dataused = (uint16_t)&__data_end - (uint16_t)&__data_start;
	bssused = (uint16_t)&__bss_end - (uint16_t)&__bss_start;
	heapused = (uint16_t)__brkval - (uint16_t)&__heap_start;
	
	freeram = (uint16_t)SP - (uint16_t)&__bss_end;
	
	printf(".data %u, .bss: %u, stack %u, free: %u - heap: %u\n",
		dataused, bssused, (RAMEND - SP), freeram, heapused);
}


/***************************************************************************************
* Main
*/
int main(void)
{
	sdcard_t *sdcard;
	uint8_t sdresponse = 0;
	led_status = 0;
	
	// Set stream handler for printf
	stdout = &usart_stdout;
	
	// Delay a bit
	_delay_ms(10);
	
	// *************************************************************
	// Input/output signals
	// *************************************************************
	// SPI PORTB
	PORTB =	(1 << CS_LCD) |
	 			(1 << CS_LEDS) |
	 			(0 << SD_CDET) |
	 			(0 << SD_WP) |
	 			(0 << SPI_MISO) |
	 			(0 << SPI_MOSI) |
	 			(0 << SPI_SCK) |
				(1 << SD_CS);
				
	DDRB =	(1 << CS_LCD) |
	 			(1 << CS_LEDS) |
	 			(0 << SD_CDET) |
	 			(0 << SD_WP) |
	 			(0 << SPI_MISO) |
	 			(1 << SPI_MOSI) |
	 			(1 << SPI_SCK) |
				(1 << SD_CS);

	
	// Switches PORTE
	PORTE = 0x00;
	DDRE = 0x00;
	
	// PORTD and PORTF headers
	PORTD = 0x00;
	DDRD = 0x00;
	PORTF = 0x00;
	DDRF = 0x00;

	// *************************************************************
	// Set up external SRAM (32kB)
	// *************************************************************
	MCUCR = (1 << SRE) | (0 << SRW10); // Enable external SRAM
	XMCRA = 0x00;
	XMCRB = (1 << XMBK) | (1 << XMM0); // Bus-keeper, 7 bits
	
	// *************************************************************
	// Timer1 1mS interrupt
	// *************************************************************
	//TCCR1A = (1 << COM1A1) | (1 << WGM11);
	//TCCR1B = (1 << CS10);
	//OCR1A = 0;
	
	// *************************************************************
	// USART
	// *************************************************************
	usart_init();
	
	
	// *************************************************************
	// SPI
	// *************************************************************
	spi_init(0);
	
	// Reset leds
	set_leds(0x00);
	
	//
	// Init SD card structure
	//
	sdcard = malloc(sizeof(*sdcard));
	
	if(sdcard == NULL)
	{
		return 0;
	}
	
	// Set all vars to 0
	sd_init_info(sdcard);
	
	//
	// Enable interrupts
	//
	sei();
	
	printf("SD Development board inited 1.0\n");
	

	// *************************************************************
	// Main loop
	// *************************************************************
	while(1)
	{
		//
		// SD Card has been inserted, but not initialized yet
		// Attempt to initialize
		//
		if(sd_inserted() && !sdcard->init_attempted && !sdcard->inited)
		{
			// Slow SPI
			spi_init(0);
			printf("-- SD Card init --\n");
			
			// Attempt to init the card
			sdresponse = sd_init(sdcard);
			
			if(sdcard->inited)
			{
				// Fast SPI
				spi_init(1);
			
				printf("-- Init OK --\n");
				
				sdresponse = read_mbr(sdcard);
				
				// MBR read and found a valid partition
				if(sdresponse)
				{
					sdresponse = fat_read_bootsector(sdcard);
				}
				
				if(sdresponse)
				{
					//
					// Use fat_fopen/fat_fread/fat_fwrite and so on to handle files on the card
					//
				}
			}
			else
			{
				printf("-- Init Failed --\n");
			}
			
			// Init has been attempted
			sdcard->init_attempted = 1;
		}
		
		//
		// The SD Card has previously been inserted, and is now removed
		//
		if(!sd_inserted() && sdcard->init_attempted)
		{
			printf("-- SD Card removed from slot --\n");
			sd_init_info(sdcard);
		}
		
		
		// Set led state
		// Card is inserted and inited
		if(sdcard->inited)
		{
			led_on(8);
			led_off(7);
		}
		// Card is inserted but init failed
		else if(sdcard->init_attempted)
		{
			led_on(7);
			led_off(8);
		}
		// No card inserted
		else
		{
			set_leds(0x00);
		}
	}
	
	return(0);
}
