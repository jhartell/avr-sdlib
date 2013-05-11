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
* SD Card specific functions
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/eeprom.h>
#include "main.h"
#include "sd.h"
#include "comms.h"


/*******************************************************************
* Check if there is an SD card in the slot
*
*/
uint8_t sd_inserted(void)
{
	if(!(PINB & (1 << SD_CDET)))
		return 1;
		
	return 0;
}

/*******************************************************************
* Check if the SD card is write protected
*
*/
uint8_t sd_write_protected(void)
{
	if((PINB & (1 << SD_WP)))
		return 1;
		
	return 0;
}


/*******************************************************************
* Reset SD card structure to default values
*
* @param sdcard		SD card structure
*/
void sd_init_info(sdcard_t *sdcard)
{
	sdcard->inited = 0;
	sdcard->init_attempted = 0;
	sdcard->write_protected = 0;
	
	sdcard->byteaddressing = 0;
	sdcard->fattype = 0;
	sdcard->blocksize = 0;
	sdcard->fsinfo_sector = 0;
	
	sdcard->partition_start = 0;
	sdcard->partition_sectors = 0;
	
	sdcard->fat_begin_sector = 0;
	sdcard->fat_sectors = 0;
	sdcard->rootdir_begin_sector = 0;
	sdcard->rootdir_sectors = 0;
	sdcard->data_begin_sector = 0; 
	sdcard->data_sectors = 0;
	sdcard->data_clusters = 0;
	sdcard->sectors_per_cluster = 0;
	
	sdcard->loaded_sector = -1;
}

/*******************************************************************
* Free memory allocated to a SD card structure
*
* @param sdcard		SD card structure
*/
void sd_free_info(sdcard_t *sdcard)
{
	free(sdcard->buffer);
	free(sdcard);
}


/*******************************************************************
* Initialize the SD card
* @return uint8_t		1 on success, 0 on failure
*
* @param sdcard		SD card structure
*/
uint8_t sd_init(sdcard_t *sdcard)
{
	if(!sd_inserted())
	{
		return 0;
	}
	
	uint8_t response;
	uint8_t i;


	// Clock in a minimum of 74 "warm up" pulses
	// with the CS line high (not set)
	sd_cs_high();
		
	for(i=0; i<10; i++)
	{
		spi_byte(0xFF);
	}
	
	// Set write protect flag, a mechanical feature of the slot
	sdcard->write_protected = sd_write_protected();
	
	
	//
	// 1. Send GO_IDLE_STATE (0)
	// The card should respond with 0x01
	//
	i = 10;
	do
	{
		response = sd_send_cmd_r1(sdcard, GO_IDLE_STATE, 0);	
	} while((response != 0x01) && i--);
	
	if(response != 0x01)
	{
		return 0;
	}
	
	//
	// 2. Send SEND_IF_COND (8)
	//
	response = sd_send_cmd_r3(sdcard, SEND_IF_COND, 0x000001AA);
	
	// No response or invalid command
	// SD v1 or MMC?
	if(response != 0x01)
	{
		return 0;
	}
	
	// Command accepted, check supported voltage
	// Voltage not supported, abort
	if(sdcard->buffer[3] != 0x01 || sdcard->buffer[4] != 0xAA)
	{
		return 0;
	}


	//
	// 3. Send CMD55 + ACMD41
	//
	i = 50;	
	do
	{
		response = sd_send_cmd_r1(sdcard, APP_CMD, 0);
		response = sd_send_cmd_r1(sdcard, SD_SEND_OP_COND, 0x40000000);
	} while((response != 0x00) && i--);
	
	// Failed
	if(i == 0)
	{
		return 0;
	}
	
	
	//
	// 4. Send CMD58
	//
	response = sd_send_cmd_r3(sdcard, READ_OCR, 0);
	// Failed
	if(response != 0x00)
	{
		return 0;
	}
	
	// Check if we should use block address insted of byte address
	if(sdcard->buffer[1] & 0x40)
	{
		// Use block addresses
		sdcard->byteaddressing = 0;
	}
	else
	{
		// Use byte addresses
		sdcard->byteaddressing = 1;
	}
	
	//
	// Set 512 byte block size
	//
	response = sd_send_cmd_r1(sdcard, SET_BLOCKLEN, 512);	
	if(response != 0x00)
	{
		return 0;
	}
	
	//
	// 5. Parse CSD and CID registers
	//
	response = sd_parse_csd(sdcard);
	if(!response)
	{
		return 0;
	}
	
	response = sd_parse_cid(sdcard);
	if(!response)
	{
		return 0;
	}
	
	// Init succeeded
	sdcard->inited = 1;
	return 1;
}



/*******************************************************************
* Read and parse the CSD register
* @return uint8_t		1 on success, 0 on failure
*
* @param sdcard		SD card structure
*/
uint8_t sd_parse_csd(sdcard_t *sdcard)
{
	uint8_t response;
	uint8_t i;
	
	
	// Get the data
	sd_cs_low();
	response = sd_send_cmd_raw(sdcard, SEND_CSD, 0);
	
	printf("[CSD] ");
	
	if(response != 0x00)
	{
		printf("Failed.\n");
		sd_cs_high();
		return 0;
	}
	
	// Receive the data
	sd_receive_datablock(sdcard, 16);
		
	for(i=0; i < 16; i++)
	{
		printf("%02X ", sdcard->buffer[i]);
	}
	//printf("\n");
	
	sd_cs_high();

		
	//
	// Parse the contents
	//
	uint16_t tmp;
	uint16_t mult;
	uint32_t csize;
	uint32_t sectors;
	
	sdcard->blocksize = 0;
	
	// CSD Version
	tmp = ((sdcard->buffer[0] >> 6) & 0x03);
	
	//
	// CSD V1.0
	//
	if(tmp == 0)
	{
		printf(" (CSD V1.0)\n");
		
		// READ_BLK_LEN [83:80]
		tmp = sdcard->buffer[5] & 0x0F;
		// Block size = 2^READ_BLK_LEN
		sdcard->blocksize = (1 << tmp);
		
		// C_SIZE_MULT [49:47]
		tmp = ((sdcard->buffer[9] & 0x03) << 1) | (sdcard->buffer[10] >> 7);
		// MULT = 2^C_SIZE_MULT+2
		mult = (1 << (tmp + 2));
			
		// C_SIZE
		csize = ((sdcard->buffer[6] & 0x03) << 10) | (sdcard->buffer[7] << 2) | ((sdcard->buffer[8] >> 6) & 0x03);
		
		// Number of 512 byte sectors
		sectors = (csize + 1) * mult;
		
		// Fix sector count if block size is not 512
		if(sdcard->blocksize != 512)
		{
			sectors = (sectors * (sdcard->blocksize / 512));
			sdcard->blocksize = 512;
		}
	}
	//
	// CSD V2.0
	//
	else if(tmp == 1)
	{	
		printf(" (CSD V2.0)\n");
		
		// Block size is always 512 bytes for V2.0
		sdcard->blocksize = 512;
		
		// C_SIZE parameter
		csize = ((uint32_t)(sdcard->buffer[7] & 0x3F) << 16) | (uint32_t)(sdcard->buffer[8] << 8) | sdcard->buffer[9];
		sectors = (csize + 1) * 1024;
	}
	else
	{
		printf(" (Invalid CSD version)\n");
		return 0;
	}
	
	if(sdcard->blocksize == 0 || sectors == 0)
	{
		return 0;
	}
	
	// Diagnostics
	printf("Block size:    %d\n", sdcard->blocksize);
	printf("Sectors:       %ld\n", sectors);
	
	return 1;
}

/*******************************************************************
* Read and parse the CID register
* @return uint8_t		1 on success, 0 on failure
*
* @param sdcard		SD card structure
*/
uint8_t sd_parse_cid(sdcard_t *sdcard)
{
	uint8_t response;
	uint8_t i;
	
	
	// Get the data
	sd_cs_low();
	response = sd_send_cmd_raw(sdcard, SEND_CID, 0);
	
	printf("[CID] ");
	
	if(response != 0x00)
	{
		printf("Failed.\n");
		sd_cs_high();
		return 0;
	}
	
	// Receive the data
	sd_receive_datablock(sdcard, 16);
		
	for(i=0; i < 16; i++)
	{
		printf("%02X ", sdcard->buffer[i]);
	}
	printf("\n");
	
	sd_cs_high();

	
	//
	// Parse the contents
	//
//	uint16_t tmp;

	// Manufacturer ID 127:120 8-bit binary number
	uint8_t manufactid = sdcard->buffer[0];
	
	// OEM ID 119:104 2 char ascii
	char appid[3];
	appid[0] = sdcard->buffer[1];
	appid[1] = sdcard->buffer[2];
	appid[2] = '\0';
	
	// Product name 103:64 5 character ascii
	unsigned char productname[6];
	productname[0] = sdcard->buffer[3];
	productname[1] = sdcard->buffer[4];
	productname[2] = sdcard->buffer[5];
	productname[3] = sdcard->buffer[6];
	productname[4] = sdcard->buffer[7];
	productname[5] = '\0';
	
	// Product rev, 63:56 2 BCD digits 
	unsigned char productrev[2];
	productrev[0] = (sdcard->buffer[8] >> 4);
	productrev[1] = (sdcard->buffer[8] & 0x0F);
	
	// Serial number 55:24
	uint32_t serialnum = 0;
	serialnum |= sdcard->buffer[9];
	serialnum <<= 8;
	serialnum |= sdcard->buffer[10];
	serialnum <<= 8;
	serialnum |= sdcard->buffer[11];
	serialnum <<= 8;
	serialnum |= sdcard->buffer[12];
		
	// Reserved 23:20
	
	// Manufacturing date 19:8
	uint8_t year = ((sdcard->buffer[13] & 0x0F) << 4) | ((sdcard->buffer[14] & 0xF0) >> 4);
	uint8_t month = sdcard->buffer[14] & 0x0F;
	
	// CRC 7:1 + stop bit 0
	
	// Diagnostics
	printf("Man ID:        %d\n", manufactid);
	printf("App ID:        %s\n", appid);
	printf("Prod name:     %s\n", productname);
	printf("Prod rev:      %d.%d\n", productrev[0], productrev[1]);
	printf("Ser:           %lu\n", serialnum);
	printf("MDate:         %02d 20%02d\n", month, year);
	
	return 1;
}


/*******************************************************************
* Send a command that only responds with one byte of data
* and return the response (R1)
*
* @param sdcard		SD card structure
* @param cmd			Command
* @param arg			Argument
*/
uint8_t sd_send_cmd_r1(sdcard_t *sdcard, uint8_t cmd, uint32_t arg)
{
	uint8_t response;
	
	// Select card
	sd_cs_low();
	
	// Send the command
	response = sd_send_cmd_raw(sdcard, cmd, arg);
	
	// Clock out one extra byte
	spi_byte(0xFF);
	
	// Deselect card
	sd_cs_high();
	
	// Debug
	printf("Send R1: %02d: %02X\n", cmd, response);

	return response;
}

/*******************************************************************
* Send a command that responds with two bytes (R2)
*
* @param sdcard		SD card structure
* @param cmd			Command
* @param arg			Argument
*/
uint16_t sd_send_cmd_r2(sdcard_t *sdcard, uint8_t cmd, uint32_t arg)
{
	uint16_t response;
	
	// Select card
	sd_cs_low();
	
	// Send the command
	response = sd_send_cmd_raw(sdcard, cmd, arg);
	response <<= 8;
	response |= spi_byte(0xFF);
		
	// Clock out one extra byte
	spi_byte(0xFF);
	
	// Deselect card
	sd_cs_high();
	
	// Debug
	printf("Send R2: %02d: %02X %02X\n", cmd, (response >> 8), (response & 0xFF));

	return response;
}


/*******************************************************************
* Send a command that responds with 5 bytes of data (R3 or R7)
* The response is placed in sdcard->buffer[], including the first byte
* which is also returned from the function
*
* @param sdcard		SD card structure
* @param cmd			Command
* @param arg			Argument
*/
uint8_t sd_send_cmd_r3(sdcard_t *sdcard, uint8_t cmd, uint32_t arg)
{
	uint8_t response;
	uint8_t i;
	
	// Select card
	sd_cs_low();
	
	// Send the command
	response = sd_send_cmd_raw(sdcard, cmd, arg);
	sdcard->buffer[0] = response;
	
	for(i=1; i<5; i++)
	{
		sdcard->buffer[i] = spi_byte(0xFF);
	}
	
	// Clock out one extra byte
	spi_byte(0xFF);
	
	// Deselect card
	sd_cs_high();
	
	// Debug
	printf("Send R3: %02d: ", cmd);

	for(i = 0; i < 5; i++)
	{
		printf("%02X ", sdcard->buffer[i]);
	}
	
	printf("\n");
	
	return response;
}

/*******************************************************************
* Low level send command function, returns the first received
* valid byte, 0xFF on failure
*
* CS must be asserted externally
*
* @param sdcard		SD card structure
* @param cmd			Command
* @param arg			Argument
*/
uint8_t sd_send_cmd_raw(sdcard_t *sdcard, uint8_t cmd, uint32_t arg)
{
	uint8_t attempts = 100;
	uint8_t response;

	// Send command
	spi_byte(cmd | 0x40);
	// Argument, 4 bytes
	spi_byte(arg >> 24);
	spi_byte(arg >> 16);
	spi_byte(arg >> 8);
	spi_byte(arg >> 0);

	// Send CRC
	if(cmd == SEND_IF_COND)
	{
		spi_byte(0x87);
	}
	else
	{
		spi_byte(0x95);
	}

	// Clock out data until bit 7 goes low
	while((response = spi_byte(0xFF)) & 0x80)
	{
		// Command failed
		if(!attempts--)
		{
			return 0xFF;
		}
	}
	
	// Return the status byte
	return response;
}

/*******************************************************************
* Receive a number of bytes from the SD card
* Ignore everything before the start data token (0xFE)
*
* @param sdcard	SD card structure
* @param bytes		Number of bytes to receive
*/
uint8_t sd_receive_datablock(sdcard_t *sdcard, uint16_t bytes)
{
	uint16_t attempts = 0xFFFF;
	uint8_t response;
	uint16_t i = 0;
	
	while((response = spi_byte(0xFF)) != 0xFE)
	{
		if(!attempts--)
		{
			printf(" read_block: Wait for response failed. ");
			return 0xFF;
		}
	}
	
	// Receive the data
	for(i=0; i < bytes; i++)
	{
		sdcard->buffer[i] = spi_byte(0xFF);
	}
	
	// Receive CRC 16-bit
	spi_byte(0xFF);
	spi_byte(0xFF);
	
	// Receive one extra byte
	spi_byte(0xFF);
	
	return 0;
}

/*******************************************************************
* Send a number of bytes to the SD card
* check response and wait for the card to become ready
*
* @param sdcard	SD card structure
* @param bytes		Number of bytes to write
*/
uint8_t sd_send_datablock(sdcard_t *sdcard, uint16_t bytes)
{
	uint16_t attempts = 0xFFFF;
	uint8_t response;
	uint16_t i = 0;
	
	// Send start block token
	spi_byte(0xFE);
	
	// Send the data
	for(i=0; i < bytes; i++)
	{
		spi_byte(sdcard->buffer[i]);
	}
	
	// Send dummy CRC 16-bit
	spi_byte(0xFF);
	spi_byte(0xFF);
	
	// Send one extra byte and receive response
	response = spi_byte(0xFF);
	
	// Check response
	// 0x05 Data accepted
	// 0x0B Data rejected CRC error
	// 0x0D Data rejected write error
	if((response & 0x1F) != 0x05)
	{
		return 0xFF;
	}
	
	// Wait for the card to finish the write operation
	while((response = spi_byte(0xFF)) == 0x00)
	{
		if(!attempts--)
		{
			printf(" send_block: Wait for done failed. ");
			return 0xFF;
		}
	}
	
	return 0;
}

/*******************************************************************
* Read a data block from the card into sdcard->buffer
*
* @param sdcard		SD card structure
* @param blockaddr	The block address to read
* @param debug			Print the read data
*/
uint8_t sd_read_block(sdcard_t *sdcard, uint32_t blockaddr, uint8_t debug)
{
	uint8_t response;
	uint16_t i = 0;
	uint8_t cnt = 0;
	
	// Check if we already have this sector loaded
	if(sdcard->loaded_sector == blockaddr)
	{
		printf("[CACHED DATA] (%ld)\n", blockaddr);
		return 1;		
	}
	
	led_on(4);
	sdcard->loaded_sector = blockaddr;
	
	// Use byte addressing
	if(sdcard->byteaddressing)
	{
		blockaddr = blockaddr * sdcard->blocksize;
	}
	
	sd_cs_low();

	// Send read command block
	response = sd_send_cmd_raw(sdcard, READ_SINGLE_BLOCK, blockaddr);
	
	printf("[READ DATA] (%ld)", blockaddr);
	
	if(response != 0x00)
	{
		printf(" Command failed.\n");
		sd_cs_high();
		led_off(4);
		sdcard->loaded_sector = -1;
		
		return 0;
	}

	response = sd_receive_datablock(sdcard, sdcard->blocksize);
	if(response == 0xFF)
	{
		printf(" Read failed.\n");
		sd_cs_high();
		led_off(4);
		sdcard->loaded_sector = -1;
		
		return 0;
	}
	
	//sdcard->loaded_sector = blockaddr;
	printf(" OK\n");
	
	// Read OK
	if(debug)
	{
		for(i=0; i < sdcard->blocksize; i++)
		{
			printf("%02X ", sdcard->buffer[i]);
			if(cnt == 7) { printf(" "); }
			if(++cnt%16 == 0) { cnt=0; printf("\n"); }
		}
			
		printf("\n");
	}
	
	sd_cs_high();
	
	led_off(4);
	
	return 1;
}

/*******************************************************************
* Write a data block to the card from sdcard->buffer
*
* @param sdcard		SD card structure
* @param blockaddr	The block address to write
* @param debug			Print the written data
*/
uint8_t sd_write_block(sdcard_t *sdcard, uint32_t blockaddr, uint8_t debug)
{
	uint8_t response;
	uint16_t i = 0;
	uint8_t cnt = 0;
	
	led_on(3);
	
	printf("[WRITE DATA] (%ld)", blockaddr);
	
	// Check if SD card is write protected
	if(sdcard->write_protected)
	{
		printf(" Write protected\n");
		led_off(3);
		return 0;
	}
	
	// Use byte addressing
	if(sdcard->byteaddressing)
	{
		blockaddr = blockaddr * sdcard->blocksize;
	}
	
	sd_cs_low();

	// Send write block
	response = sd_send_cmd_raw(sdcard, WRITE_SINGLE_BLOCK, blockaddr);
		
	if(response != 0x00)
	{
		printf(" Command failed.\n");
		sd_cs_high();
		
		led_off(3);
		return 0;
	}

	response = sd_send_datablock(sdcard, sdcard->blocksize);
	if(response == 0xFF)
	{
		printf(" Write failed.\n");
		sd_cs_high();
		
		led_off(3);
		return 0;
	}
	
	printf(" OK\n");
	
	// Write OK
	if(debug)
	{
		for(i=0; i < sdcard->blocksize; i++)
		{
			printf("%02X ", sdcard->buffer[i]);
			if(cnt == 7) { printf(" "); }
			if(++cnt%16 == 0) { cnt=0; printf("\n"); }
		}
			
		printf("\n");
	}
	
	
	sd_cs_high();
	
	led_off(3);
	return 1;
}

