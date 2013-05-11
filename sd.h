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
#ifndef _SD_H_
#define _SD_H_

/*
* SD Card Response bit field - First byte
*/
#define SD_RESP_IDLE			0x01
#define SD_RESP_ERASE_RST	0x02
#define SD_RESP_ILL_CMD		0x04
#define SD_RESP_CRC_ERR		0x08
#define SD_RESP_ERASE_SEQ	0x10
#define SD_RESP_ADDR_ERR	0x20
#define SD_RESP_PARAM_ERR	0x40

/*
* SD Card Response bit field - Second byte (R2)
*/
#define SD_RESP2_LOCKED		0x01
#define SD_RESP2_WPE_SKIP	0x02
#define SD_RESP2_UNSP_ERR	0x04
#define SD_RESP2_CONT_ERR	0x08
#define SD_RESP2_ECC_FAIL	0x10
#define SD_RESP2_WP_VIOL	0x20
#define SD_RESP2_E_PARAM	0x40
#define SD_RESP2_OUT_RANG	0x80

/*
* SD Card commands
*/
#define GO_IDLE_STATE			0 	// R1 Resets the SD Memory Card
#define SEND_OP_COND				1 	// R1 Sends host capacity support information COND and activates the card's initialization process.
#define SEND_IF_COND				8 	// R7 Sends SD Memory Card interface condition that includes host supply voltage information and asks the
											// accessed card whether card can operate in supplied voltage range.

#define SEND_CSD					9 	// R1 Asks the selected card to send its card- specific data (CSD)
#define SEND_CID					10 // R1 Asks the selected card to send its card identification (CID)

#define SEND_STATUS				13 // R2 Asks the selected card to send its status register.

#define APP_CMD					55	// R1 Defines to the card that the next com- mand is an application specific command rather than a standard command
#define SD_SEND_OP_COND			41 // R1 Sends host capacity support ND information and activates the card's initialization process. 

#define READ_OCR					58 // R3 Reads the OCR register of a card. CCS bit is assigned to OCR[30].

/* Data commands */
#define SET_BLOCKLEN				16	// R1 Sets a block length (in bytes) for all following block commands (read and write) of a Standard Capacity Card.
											// Block length of the read and write commands are fixed to 512 bytes in a High Capacity Card. The length of
											// LOCK_UNLOCK command is set by this command in both capacity cards.

#define READ_SINGLE_BLOCK		17 // R1 Reads a block of the size selected by the SET_BLOCKLEN command
#define READ_MULTIPLE_BLOCK	18 // R1 Continuously transfers data blocks from card to host until interrupted by a STOP_TRANSMISSION command.

#define WRITE_SINGLE_BLOCK		24 // R1 Writes a block of the size selected by the SET_BLOCKLEN command.
#define WRITE_MULTIPLE_BLOCK	25 // R1 Continuously writes blocks of data until ’Stop Tran’ token is sent (instead ’Start Block’).


/* SD Card structure */
typedef struct
{
	int8_t inited;						// Card is initialized and ready to go
	int8_t init_attempted;			// One or more initialization attempts have been made
	int8_t write_protected;			// Card is write protected (the lock tab)
	
	uint8_t byteaddressing;			// Use byte addressing (smaller cards)
	uint8_t fattype;					// 16 or 32
	uint16_t blocksize;				// Block size, always 512 bytes
	
	uint32_t fsinfo_sector;			// Sector containing FSInfo structure
	
	uint32_t partition_start;		// Start of first partition
	uint32_t partition_sectors;	// Sectors in first partition
	
	uint32_t fat_begin_sector;			// FAT table first sector 
	uint32_t fat_sectors; 				// Number of sectors occupied by one FAT (there are usually two)
	uint32_t rootdir_begin_sector;	// Root directory first sector
	uint32_t rootdir_begin_cluster;	// Root directory first cluster (FAT32)
	uint32_t rootdir_sectors;			// Sectors for root directory, 0 for FAT32
	
	uint32_t data_begin_sector;	// Data area first sector, after rootdir for FAT16
	uint32_t data_sectors;			// Total number of data sectors on the partition
	uint32_t data_clusters;			// Total number of data clusters on the partition
	  
	uint8_t  sectors_per_cluster;	// Sectors per cluster
	
	int32_t loaded_sector;			// Sector currently loaded into buffer
	char buffer[512];					// Sector sized buffer used for read/write operations
} sdcard_t;



/*
* Function declarations
*/
uint8_t sd_inserted(void);
uint8_t sd_write_protected(void);

void sd_init_info(sdcard_t *sdcard);
void sd_free_info(sdcard_t *sdcard);

uint8_t sd_init(sdcard_t *sdcard);
uint8_t sd_parse_csd(sdcard_t *sdcard);
uint8_t sd_parse_cid(sdcard_t *sdcard);

uint8_t sd_send_cmd_r1(sdcard_t *sdcard, uint8_t cmd, uint32_t arg);
uint16_t sd_send_cmd_r2(sdcard_t *sdcard, uint8_t cmd, uint32_t arg);
uint8_t sd_send_cmd_r3(sdcard_t *sdcard, uint8_t cmd, uint32_t arg);
uint8_t sd_send_cmd_raw(sdcard_t *sdcard, uint8_t cmd, uint32_t arg);
uint8_t sd_receive_datablock(sdcard_t *sdcard, uint16_t bytes);

uint8_t sd_read_block(sdcard_t *sdcard, uint32_t blockaddr, uint8_t debug);
uint8_t sd_write_block(sdcard_t *sdcard, uint32_t blockaddr, uint8_t debug);


#endif