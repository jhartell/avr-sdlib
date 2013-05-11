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
* FAT filesystem specific functions
*/
#ifndef _FAT_FS_H_
#define _FAT_FS_H_

// File attributes
#define ENTRY_BLANK		0x00
#define ENTRY_DELETED	0xE5

#define ATTR_READ_ONLY 	0x01
#define ATTR_HIDDEN 		0x02
#define ATTR_SYSTEM 		0x04
#define ATTR_VOLUME_ID 	0x08
#define ATTR_DIRECTORY 	0x10
#define ATTR_ARCHIVE 	0x20

#define ATTR_LONG_NAME 			(ATTR_READ_ONLY | ATTR_HIDDEN | ATTR_SYSTEM | ATTR_VOLUME_ID)
#define ATTR_LONG_NAME_MASK	(ATTR_READ_ONLY | ATTR_HIDDEN | ATTR_SYSTEM | ATTR_VOLUME_ID | ATTR_DIRECTORY | ATTR_ARCHIVE)

// Long dir attributes
#define ATTR_FREE			0xE5
#define ATTR_LAST_LONG	0x40

#define FAT16	16
#define FAT32	32


/* Master boot record structure */
typedef struct
{
	unsigned char bootcode[446];
	unsigned char partition1[16];
	unsigned char partition2[16];
	unsigned char partition3[16];
	unsigned char partition4[16];
	uint16_t signature;
} mbr_t;


/* Partition entry structure (in MBR) */
typedef struct
{
	uint8_t status;
	uint8_t start_head;
	uint8_t start_sect;
	uint8_t start_cyl;
	uint8_t partition_type;
	uint8_t end_head;
	uint8_t end_sect;
	uint8_t end_cyl;
	uint32_t firstsector;
	uint32_t sectors;
} partition_entry_t;


/*
* FAT32 boot sector structure
* First sector of every partition
*/
typedef struct
{
	unsigned char jmpBoot[3];	// Jump instructions to boot code.
	unsigned char OEMName[8];	// OEM Name Identifier. Can be set by a FAT implementation to any desired value.

	uint16_t BytsPerSec; 		// Size of a hardware sector (always 512 bytes)
	uint8_t SecPerClus; 			// Sectors per cluster, 1,2,4,8,16,32,64,128
	uint16_t ResvdSecCnt; 		// Reserved sectors before the first FAT, including boot sector, typically 32
	uint8_t NumFATs;				// Number of copies of the FAT table stored on the disk, mostly 2
	uint16_t RootEntCnt;			// For FAT12 and FAT16 volumes, this field contains the count of 32-byte directory entries in the root directory. For FAT32 volumes, this field must be set to 0
	uint16_t TotSec16;			// This field is the old 16-bit total count of sectors on the volume. This count includes the count of all sectors in all four regions of the volume.
	uint8_t Media;					// This byte provides information about the media being used. 0xF0 for removable
	uint16_t FATSz16;				// This field is the FAT12/FAT16 16-bit count of sectors occupied by one FAT.
	uint16_t SecPerTrk;			// Sectors per track, for media with tracks only (?)
	uint16_t NumHeads;			// Number of heads, for media with tracks only (?)
	uint32_t HiddSec; 			// Hidden sectors before partition containing FAT volume
	uint32_t TotSec32;			// This field is the new 32-bit total count of sectors on the volume. This count includes the count of all sectors in all four regions of the volume.
		
	uint32_t FATSz32;					// Sectors occupied by one FAT
	uint16_t ExtFlags;				// Flags, mirroring?
	uint16_t FSVer;					// Version number of FAT32 volume, must be 0
	uint32_t RootClus;				// This is set to the cluster number of the first cluster of the root directory, usually 2 but not required to be 2
	uint16_t FSInfo; 					// Sector number of FSINFO structure in the reserved area of the FAT32 volume. Usually 1.
	uint16_t BkBootSec;				// If non-zero, indicates the sector number in the reserved area of the volume of a copy of the boot record. Usually 6
	unsigned char Reserved1[12];	// Reserved for future expansion
	uint8_t DrvNum;					// Drive number
	uint8_t Reserved2;				// Reserved
	uint8_t BootSig;					// This is a signature byte that indicates that the following three fields in the boot sector are present.
	uint32_t VolID;					// Volume serial number.
	unsigned char VolLab[11];		// Volume label. This field matches the 11-byte volume label recorded in the root directory. 
	unsigned char FilSysType[8];	// Name of fat, always FAT32
	unsigned char Reserved3[420]; // Not used?
	uint16_t Signature;				// 0x55 0xAA
} bootsector_t;


/*
* FAT32 File system information structure
*/
typedef struct
{
	uint32_t LeadSig; 				// Value 0x41615252. This lead signature is used to validate that this is in fact an FSInfo sector.
	unsigned char Reserved1[480];	// This field is currently reserved for future expansion.
	uint32_t StrucSig;				// Value 0x61417272. Another signature that is more localized in the sector to the location of the fields that are used.
	uint32_t Free_Count;				// Contains the last known free cluster count on the volume.
	uint32_t Nxt_Free;				// This is a hint for the FAT driver. It indicates the cluster number at which the driver should start looking for free clusters.
	unsigned char Reserved2[12];	// This field is currently reserved for future expansion.
	uint32_t TrailSig;				// Value 0xAA550000.
} fsinfo_t;


/*
* FAT short filename entry structure
*/
typedef struct
{
	uint8_t DIR_Name[11];		// Short file name 11 chars
	uint8_t DIR_Attr;				// Attributes
	uint8_t DIR_NTRes;			// Reserved. Must be set to 0
	uint8_t DIR_CrtTimeTenth;	// Component of the file creation time. Count of tenths of a second. 
	uint16_t DIR_CrtTime;		// Creation time. Granularity is 2 seconds.
	uint16_t DIR_CrtDate;		// Creation date
	uint16_t DIR_LstAccDate;	// Last access date
	uint16_t DIR_FstClusHI;		// High word of first data cluster number for file/directory described by this entry.
	uint16_t DIR_WrtTime;		// Last modification (write) time.
	uint16_t DIR_WrtDate;		// Last modification (write) date.
	uint16_t DIR_FstClusLO;		// Low word of first data cluster number for file/directory described by this entry.
	uint32_t DIR_FileSize;		// 32-bit quantity containing size in bytes of file/directory described by this entry.
} dir_short_t;


/*
* FAT long filename entry structure
*/
typedef struct
{
	uint8_t LDIR_Ord;				// The order of this entry in the sequence of long name directory entries (each containing components of the long file name) associated with the corresponding short name directory entry.
	uint8_t LDIR_Name1[10];		// Contains characters 1 through 5 constituting a portion of the long name.
	uint8_t LDIR_Attr;			// Attributes - must be set to ATTR_LONG_NAME
	uint8_t LDIR_Type;			// Must be set to 0
	uint8_t LDIR_Chksum;			// Checksum of name in the associated short name directory entry at the end of the long name directory entry set.
	uint8_t LDIR_Name2[12];		// Contains characters 6 through 11 constituting a portion of the long name.
	uint16_t LDIR_FstClusLO;	// Must be set to 0
	uint8_t LDIR_Name3[4];		// Contains characters 12 and 13 constituting a portion of the long name.
} dir_long_t;



/*
* FAT file handle for a directory or a file
* Used for fopen/fseek/fread implementation
*/
typedef struct fat_handle_t
{
	uint8_t is_file;			// File flag
	uint8_t is_dir;			// Directory flag
	uint8_t flags;				// Flags to indicate possible operations
	
	char filename[255];		// Filename (LFN)
	uint32_t cluster;			// Cluster where the file/dir entry is located
	
	uint32_t datacluster;	// First data cluster
	uint32_t filesize;		// File size, 0 for directories
	uint32_t ptr;				// Byte pointer for fread/fwrite/fseek or readdir
	
	sdcard_t * sdcard;		// Pointer to sdcard structure
} fat_handle;


/*
* Long filename cache structure
*/
typedef struct lfn_cache_t
{
	char filename[20][13];
	uint8_t strings;
	uint8_t checksum;
} lfn_cache;

/*
* Fat directory entry offset structure
*/
typedef struct fat_entry_t
{
	uint32_t cluster;
	uint32_t sector;
	uint8_t entry;
} fat_entry;


/*
* Function declarations
*/
uint8_t read_mbr(sdcard_t *sdcard);
uint8_t fat_read_bootsector(sdcard_t *sdcard);

uint8_t fat_update_fsinfo(sdcard_t * sdcard);
uint8_t fat_print_cluster_stats(sdcard_t * sdcard);

uint8_t fat_read_sector(sdcard_t *sdcard, uint32_t cluster, uint32_t sector);
uint8_t fat_write_sector(sdcard_t *sdcard, uint32_t cluster, uint32_t sector, uint8_t allocate);

uint32_t fat_get_cluster_sector(sdcard_t *sdcard, uint32_t cluster_number);
uint8_t  fat_free_cluster_chain(sdcard_t *sdcard, uint32_t startcluster, uint8_t cleardata);
uint32_t fat_get_next_cluster(sdcard_t *sdcard, uint32_t cluster);
uint8_t fat_set_next_cluster(sdcard_t *sdcard, uint32_t cluster, uint32_t nextcluster);
uint32_t fat_get_next_free_cluster(sdcard_t *sdcard, uint32_t cluster);
uint32_t fat_allocate_cluster(sdcard_t *sdcard, uint32_t cluster);

dir_short_t * fat_find_lfn(sdcard_t *sdcard, uint32_t startcluster, const char * filename);
dir_short_t * fat_find_sfn(sdcard_t *sdcard, uint32_t startcluster, const char * filename);

dir_short_t * fat_find_next_file(sdcard_t *sdcard, fat_handle *handle, lfn_cache *lfn);

uint8_t fat_create_file(sdcard_t *sdcard, uint32_t startcluster, const char * filename);
uint8_t fat_truncate_file(sdcard_t *sdcard, uint32_t startcluster, const char * filename);

fat_entry * fat_find_free_entry(sdcard_t *sdcard, uint32_t startcluster, uint8_t entries);

uint32_t fat_read_file(sdcard_t * sdcard, uint32_t startcluster, const char * filename, void * buffer, uint32_t start, uint32_t bytes);
uint32_t fat_write_file(sdcard_t * sdcard, uint32_t startcluster, const char * filename, void * buffer, uint32_t start, uint32_t bytes);


#endif