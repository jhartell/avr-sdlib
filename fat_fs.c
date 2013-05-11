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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/eeprom.h>
#include "main.h"
#include "comms.h"
#include "sd.h"
#include "fat_fs.h"
#include "fat_func.h"
#include "fat_misc.h"


/*******************************************************************
* Read the master boot record and set partition info in sdcard
* structure
*/
uint8_t read_mbr(sdcard_t *sdcard)
{
	if(!sdcard->inited)
	{
		return 0;
	}
	
	// Read MBR
	if(!sd_read_block(sdcard, 0, 0))
	{
		return 0;
	}
	
	mbr_t *mbr;
	mbr = (mbr_t *)sdcard->buffer;
	
	// Verify MBR/bootsector signature
	if(mbr->signature != 0xAA55)
	{
		printf("MBR: Invalid signature\n");
		return 0;
	}
	
	// Is this a boot sector?
	if(mbr->bootcode[0] == 0xEB || mbr->bootcode[0] == 0xE9) // Probably
	{
		printf("MBR: No partition table, MBR is a boot sector\n");
		
		sdcard->partition_start = 0;
		// Estimation, not used atm
		sdcard->partition_sectors = 0;
		
		return 1;
	}
	
	// Not a bootsector, probably MBR...
		
	// Read first partition info
	partition_entry_t *partition;
	partition = (partition_entry_t *)(mbr->partition1);
	
	// Check partition type, must be FAT
	if(partition->partition_type != 0x06 && partition->partition_type != 0x0B && partition->partition_type != 0x0C && partition->partition_type != 0x0E)
	{
		printf("MBR: First partition is not FAT\n");
		return 0;
	}

	// Set partition info
	sdcard->partition_start = partition->firstsector;
	sdcard->partition_sectors = partition->sectors;
	
	// Debug	
	printf("MBR OK\n");
	printf("Part. start:   %lu\n", sdcard->partition_start);
	printf("Part. sectors: %lu\n", sdcard->partition_sectors);

	return 1;
}


/*******************************************************************
* Read the boot sector of a FAT partition
*/
uint8_t fat_read_bootsector(sdcard_t *sdcard)
{
	if(!sdcard->inited)
	{
		printf("BS: Card not inited\n");
		return 0;
	}
	
	if(!sd_read_block(sdcard, sdcard->partition_start, 0))
	{
		printf("BS: Read failed\n");
		return 0;
	}
	
	// Boot sector structure
	bootsector_t *bs;
	bs = (bootsector_t *)sdcard->buffer;
	
	// Invalid boot sector signature
	if(bs->Signature != 0xAA55)
	{
		printf("BS: Invalid signature\n");
		return 0;
	}
	
	// Check sector size
	if(bs->BytsPerSec != 512)
	{
		printf("Bytes per sector is not 512\n");
		return 0;
	}
	
	//
	// Determine FAT type (from MS specs)
	//
	uint16_t rootdirsectors = ((bs->RootEntCnt * 32) + (bs->BytsPerSec - 1)) / bs->BytsPerSec;
	uint16_t fatsize = (bs->FATSz16 != 0 ? bs->FATSz16 : bs->FATSz32);
	uint32_t totalsectors = (bs->TotSec16 != 0 ? bs->TotSec16 : bs->TotSec32);
	uint32_t datasectors = totalsectors - (bs->ResvdSecCnt + (bs->NumFATs * fatsize) + rootdirsectors);
	uint32_t totalclusters = datasectors / bs->SecPerClus;


	// FAT12, exit
	if(totalclusters < 4085)
	{
		printf("FAT12, exiting\n");
		return 0;
	}
	// FAT16
	// Reserved - FAT - FAT copy - Root Dir - Data area
	else if(totalclusters < 65525)
	{
		printf("FAT16 detected\n");
		sdcard->fattype = FAT16;
		
		// FSinfo sector
		sdcard->fsinfo_sector = 1;
		
		// FAT table first sector
		sdcard->fat_begin_sector = sdcard->partition_start + bs->ResvdSecCnt;
		// FAT sectors (for one FAT only)
		//sdcard->fat_sectors = (bs->NumFATs * bs->FATSz16);
		sdcard->fat_sectors = bs->FATSz16;
		
		// Root directory first sector
		sdcard->rootdir_begin_sector = sdcard->partition_start + bs->ResvdSecCnt + (bs->NumFATs * bs->FATSz16);;
		// Root directory first cluster, 0 for FAT16
		sdcard->rootdir_begin_cluster = 0;
		// Root dir sectors
		sdcard->rootdir_sectors = rootdirsectors;
		
		// Data area first sector
		sdcard->data_begin_sector = sdcard->rootdir_begin_sector + rootdirsectors;
		// Total data sectors
		sdcard->data_sectors = datasectors;
		// Total data clusters
		sdcard->data_clusters = totalclusters;
		// Sectors per cluster
		sdcard->sectors_per_cluster = bs->SecPerClus;
	}
	// FAT32
	// Reserved - FAT - FAT copy - Data area
	else
	{
		printf("FAT32 detected\n");
		sdcard->fattype = FAT32;
		
		// FSinfo sector
		sdcard->fsinfo_sector = bs->FSInfo;
		
		// FAT table first sector
		sdcard->fat_begin_sector = sdcard->partition_start + bs->ResvdSecCnt;
		// FAT sectors (for one FAT only)
		//sdcard->fat_sectors = (bs->NumFATs * bs->FATSz32);
		sdcard->fat_sectors = bs->FATSz32;
		
		// Root directory first sector
		sdcard->rootdir_begin_sector = 0;
		// Root directory first cluster
		sdcard->rootdir_begin_cluster = bs->RootClus;
		// Root dir sectors (not used for FAT32)
		sdcard->rootdir_sectors = 0;
		
		// Data area first sector (same as root dir for FAT32)
		sdcard->data_begin_sector = sdcard->partition_start + bs->ResvdSecCnt + (bs->NumFATs * bs->FATSz32);
		// Total data sectors
		sdcard->data_sectors = datasectors;
		// Total data clusters
		sdcard->data_clusters = totalclusters;
		// Sectors per cluster
		sdcard->sectors_per_cluster = bs->SecPerClus;
	
		// Calculate root dir separately for FAT32
		sdcard->rootdir_begin_sector = fat_get_cluster_sector(sdcard, sdcard->rootdir_begin_cluster);
	}

	
	
	
	
	// Debug	
	printf("Bootsector OK\n");
	printf("OEMName:       %s\n", bs->OEMName);
	printf("fat_begin:     %lu\n", sdcard->fat_begin_sector);
	printf("fat_sectors:   %lu\n", sdcard->fat_sectors);
	printf("rootdir_begin:   %lu\n", sdcard->rootdir_begin_sector);
	printf("rootdir_sectors: %lu\n", sdcard->rootdir_sectors);
	printf("data_begin:    %lu\n", sdcard->data_begin_sector);
	printf("data_sectors:  %lu\n", sdcard->data_sectors);
	
	printf("secperclus:    %d\n",sdcard->sectors_per_cluster);
	printf("data_clusters: %lu\n", sdcard->data_clusters);
	
	
	// LFN -> SFN test
	/*
	char sfn[12];
	
	lfn_to_sfn("This is my file.txt", sfn, 1); sfn[11] = 0x00; printf("%s\n", sfn);
	lfn_to_sfn("This is another file.txt", sfn, 23); sfn[11] = 0x00; printf("%s\n", sfn);
	lfn_to_sfn("ms.log", sfn, 3); sfn[11] = 0x00; printf("%s\n", sfn);
	lfn_to_sfn("dataf", sfn, 0); sfn[11] = 0x00; printf("%s\n", sfn);
	lfn_to_sfn("Zip32.zip", sfn, 0); sfn[11] = 0x00; printf("%s\n", sfn);
	
	lfn_to_sfn("MYFILEISLOG", sfn, 0); sfn[11] = 0x00; printf("%s\n", sfn);
	lfn_to_sfn("MYFILEISLOG", sfn, 1); sfn[11] = 0x00; printf("%s\n", sfn);
	*/
	
	// Read FAT table (for fun)
	//sd_read_block(sdcard, sdcard->fat_begin_sector, 1);
	
	//return 0;
	// List all files
	//fat_find_file(sdcard, 0);
	
	//printf("-------------------- CREATE FILE ----------------------------\n");	
	//dir_short_t *dir = fat_create_file(sdcard, "HEP     TXT");
	

	//printf("-------------------- FIND FILE ----------------------------\n");
	// Find a file
	//dir_short_t *dir = fat_find_file(sdcard, "HEJ     TXT");
	//dir_short_t *dir = fat_find_file(sdcard, "TURMIO~1MP3");
	
	//if(dir != NULL)
	//{
		//uint32_t clus = (((uint32_t)dir->DIR_FstClusHI << 16) | dir->DIR_FstClusLO);
		//printf("%s (at %ld) %ld bytes\n", dir->DIR_Name, clus, dir->DIR_FileSize);
		//fat_read_file(sdcard, dir);
		
		// Append
	//	fat_append_file(sdcard, dir, "Hello hejsan");
	//}
	
	
	
	//fat_read_file(sdcard, dir);
	
	//uint32_t test = fat_get_next_free_cluster(sdcard, sdcard->rootdir_begin_cluster);
	//printf("Next free is: %ld\n", test);
	
	//dir_short_t *dir = fat_create_file(sdcard, "HEJ     TXT");
	
	//fat_read_sector(sdcard, 2, 3);
	
	/*
	printf("-------------------- PATH TEST ----------------------------\n");
	char path[] = "var/log/run/test";
	char output[32];
	
	if(get_path_part(path, output, 0)) printf("0: %s\n", output);
	if(get_path_part(path, output, 1)) printf("1: %s\n", output);
	if(get_path_part(path, output, 2)) printf("2: %s\n", output);
	if(get_path_part(path, output, 3)) printf("3: %s\n", output);
	if(get_path_part(path, output, 4)) printf("4: %s\n", output);
	if(get_path_part(path, output, 5)) printf("5: %s\n", output);
	*/
	
	/*
	printf("-------------------- FOPEN TEST ----------------------------\n");	
	fat_handle *handle;
	handle = fat_fopen(sdcard, "sd.h", "r");
	
	if(handle == NULL)
	{
		printf("File not found\n");
		return 0;
	}
	
	printf("File found\n");
	printf("Size: %lu\n", handle->filesize);

	char buf[16];
	uint32_t br = 0;
	uint8_t i;
	
	while((br = fat_fread(buf, 16, 1, handle)) != 0)
	{	
		for(i=0; i<br; i++)
		{
			printf("%02X ", buf[i]);
		}
		//tot += 16;
		printf("(%ld)\n", fat_ftell(handle));
	}
		
	fat_fclose(handle);
	
	return 0;
	*/
	
	
	/*
	printf("-------------------- FWRITE TEST ----------------------------\n");	
	fat_handle *handle;
	handle = fat_fopen(sdcard, "sd.h", "a+");
	
	if(handle == NULL)
	{
		printf("Unable to open file\n");
		return 0;
	}
	
	fat_fseek(handle, 2, SEEK_SET);
	
	printf("Size: %lu\n", handle->filesize);

	char buf[16];
	strcpy(buf, "lol");
	
	uint32_t bw = 0;
	
	bw = fat_fwrite(buf, strlen(buf), 1, handle);
	printf("Wrote: %lu bytes\n", bw);
		
	fat_fclose(handle);
	
	return 0;
	*/
	
	
	/*
	printf("-------------------- OPENDIR TEST ----------------------------\n");	
	fat_handle *handle;
	fat_handle *file;
	char filename[255];
	char buf[16];
	uint8_t i;
	
	handle = fat_opendir(sdcard, "/");
	
	if(handle == NULL)
	{
		//free(entry);
		printf("Dir not found\n");
		return 0;
	}
	
	printf("Directory found at %ld\n", handle->startcluster);
	
	while(fat_readdir(handle, filename))
	{
		printf("[ITEM] %s\n", filename);
		if(strcmp(filename, "addthis_sharingtool.tgz") == 0)
		{
			printf("Found the file\n");
			
			file = fat_fopen(sdcard, filename, "r");
			if(file)
			{
				fat_fread(buf, 16, 1, handle);
				for(i=0; i<16; i++)
				{
					printf("%02X ", buf[i]);
				}
				printf("\n");
			}
			fat_fclose(file);
			
			break;
		}
	}
	
	fat_closedir(handle);
	
	return 0;
	*/
	
	//fat_create_file(sdcard, sdcard->rootdir_begin_cluster, "This is a long filename for testing purposes, spanning multiple entries.txt");
	//fat_create_file(sdcard, sdcard->rootdir_begin_cluster, "Another file.txt");
	
	//fat_create_file(sdcard, sdcard->rootdir_begin_cluster, "Another file.txt");
	//fat_truncate_file(sdcard, sdcard->rootdir_begin_cluster, "Another file.txt");
	//fat_truncate_file(sdcard, sdcard->rootdir_begin_cluster, "TorchStash.tsf");
	//fat_truncate_file(sdcard, sdcard->rootdir_begin_cluster, "Lanot.tgz");
	
	//fat_print_cluster_stats(sdcard);
	
	
	
	
	
	// Read write test
	
	//sd_read_block(sdcard, sdcard->data_begin_sector, 1);
	//sd_write_block(sdcard, sdcard->data_begin_sector, 1);
	//sd_read_block(sdcard, sdcard->data_begin_sector+1, 1);
	//sd_read_block(sdcard, sdcard->data_begin_sector, 1);
	
	printf("-- All done --\n");
	
	return 1;
}


/*******************************************************************
* Update next free cluster in FSinfo sector
*
*/
uint8_t fat_update_fsinfo(sdcard_t * sdcard)
{
	return 0;
}


/*******************************************************************
* Print cluster usage
* This is extremely slow :)
*/
uint8_t fat_print_cluster_stats(sdcard_t * sdcard)
{
	uint32_t free_clusters = 0;
	uint32_t used_clusters = 0;
	
	uint16_t *fat16val;
	uint32_t *fat32val;
	uint32_t value;
	uint32_t cluster = 2;
	uint32_t sector = 0;
	uint32_t offset = 0;
	
	
	while(1)
	{
		// FAT16
		if(sdcard->fattype == FAT16)
		{
			// Sector that contains the fat entry
			sector = ((cluster * 2) / sdcard->blocksize);
			// Offset from the start of the sector to the value we want
			offset = ((cluster * 2) % sdcard->blocksize);
		}
		// FAT32
		else
		{
			// Sector that contains the fat entry
			sector = ((cluster * 4) / sdcard->blocksize);
			// Offset from the start of the sector to the value we want
			offset = ((cluster * 4) % sdcard->blocksize);
		}
	
		// Out of data clusters
		if(cluster > sdcard->data_clusters + 1)
		{
			break;
		}
		
		// Out of FAT sectors
		if(sector > sdcard->fat_sectors)
		{
			break;
		}
	
		// Read the sector
		if(!sd_read_block(sdcard, (sdcard->fat_begin_sector + sector), 0))
		{
			return 0;
		}
	
		// Get the value
		if(sdcard->fattype == FAT16)
		{
			fat16val = (uint16_t *)&sdcard->buffer[offset];
			value = *fat16val;
		}
		else
		{
			fat32val = (uint32_t *)&sdcard->buffer[offset];
			value = *fat32val;
		}

		if(value == 0)
		{
			free_clusters++;
		}
		else
		{
			used_clusters++;
		}
	
		cluster++;
	}
	
	printf("Used clusters: %ld\n", used_clusters);
	printf("Free clusters: %ld\n", free_clusters);
	
	return 0;
}


/*******************************************************************
* Read a sector based on a cluster number, and a sector offset
* from that cluster. Use the FAT table to follow the cluster chain.
*
* @param cluster		The cluster number, 2-x
* @param sector		Sector offset from cluster start
*
*/
uint8_t fat_read_sector(sdcard_t *sdcard, uint32_t cluster, uint32_t sector)
{
	uint32_t cluster_offset;
	uint32_t target_sector;
	uint32_t i;
	
	if(sdcard->fattype == FAT16)
	{
		if(cluster >= 0xFFF8)
			return 0;
	}
	else
	{
		cluster = cluster & 0x0FFFFFFF;
		if(cluster >= 0x0FFFFFF8)
			return 0;
	}
	
	// Special case, FAT16 root directory
	if(sdcard->fattype == FAT16 && cluster == 0)
	{
		if(sector >= sdcard->rootdir_sectors) return 0;
		target_sector = sdcard->rootdir_begin_sector + sector;
	}
	// All other cases
	else
	{
		// Check how many clusters we need to search ahead
		cluster_offset = (sector / sdcard->sectors_per_cluster);
		
		for(i=0; i<cluster_offset; i++)
		{
			cluster = fat_get_next_cluster(sdcard, cluster);
		}
		
		// End of chain reached, provided sector is not within
		// the cluster chain
		if(cluster == 0xFFFFFFFF)
		{
			return 0;
		}
		
		// Calculate which sector to read
		target_sector = fat_get_cluster_sector(sdcard, cluster);
		target_sector += (sector - (cluster_offset * sdcard->sectors_per_cluster));
	}
	
	// Read the sector
	if(!sd_read_block(sdcard, target_sector, 0))
	{
		return 0;
	}

	return 1;
}


/*******************************************************************
* Write a sector based on a cluster number, and a sector offset
* from that cluster. Use the FAT table to follow the cluster chain.
*
* @param sdcard		SD Card structure
* @param cluster		The cluster number
* @param sector		Sector offset from cluster start, can be > sectors per cluster
* @param allocate		Allow new clusters to be allocated on the fly
*
*/
uint8_t fat_write_sector(sdcard_t *sdcard, uint32_t cluster, uint32_t sector, uint8_t allocate)
{
	uint32_t cluster_offset;
	uint32_t target_sector;
	uint32_t lastcluster;
	uint32_t nextcluster;
	uint32_t i;
	
	if(sdcard->fattype == FAT16)
	{
		if(cluster >= 0xFFF8)
			return 0;
	}
	else
	{
		cluster = cluster & 0x0FFFFFFF;
		if(cluster >= 0x0FFFFFF8)
			return 0;
	}
	
	// Copy the sd buffer to a temporary variable
	// in case we need to perform a FAT table lookup which will
	// overwrite the buffer
	char *tmpbuffer = malloc(sdcard->blocksize);
	if(tmpbuffer == NULL)
	{
		return 0;
	}
	
	memcpy(tmpbuffer, sdcard->buffer, sdcard->blocksize);
	
	
	// Special case, FAT16 root directory
	if(sdcard->fattype == FAT16 && cluster == 0)
	{
		if(sector >= sdcard->rootdir_sectors) return 0;
		target_sector = sdcard->rootdir_begin_sector + sector;
	}
	// All other cases
	else
	{
		// Check how many clusters we need to search ahead
		cluster_offset = (sector / sdcard->sectors_per_cluster);
		
		for(i=0; i<cluster_offset; i++)
		{
			// The last allocated cluster
			lastcluster = cluster;
			
			// Get the next cluster in the chain
			cluster = fat_get_next_cluster(sdcard, lastcluster);
			
			// End of chain
			if(cluster == 0xFFFFFFFF)
			{
				// Attempt to allocate another cluster
				if(allocate)
				{
					nextcluster = fat_allocate_cluster(sdcard, lastcluster);
					
					// No free cluster found
					if(nextcluster == 0)
					{
						free(tmpbuffer);
						return 0;
					}
					
					cluster = nextcluster;
				}
				else
				{
					free(tmpbuffer);
					return 0;
				}
			}
		}
		
		
		// Calculate which sector to write
		target_sector = fat_get_cluster_sector(sdcard, cluster);
		target_sector += (sector - (cluster_offset * sdcard->sectors_per_cluster));
	}
	
	// Invalidate cached buffer
	sdcard->loaded_sector = -1;
	// Copy back the temporary buffer
	memcpy(sdcard->buffer, tmpbuffer, sdcard->blocksize);
	free(tmpbuffer);
	
	// Write the sector
	if(!sd_write_block(sdcard, target_sector, 0))
	{
		return 0;
	}

	return 1;
}


/*******************************************************************
* Check if a cluster is the end of the chain
*/
/*
uint8_t fat_is_last_cluster(sdcard_t *sdcard, uint32_t cluster)
{
	if(sdcard->fattype == FAT16)
	{
		if(cluster == 0xFFFF)
			return 1;
	}
	else
	{
		if(cluster == 0xFFFFFFFF)
			return 1;
	}
	
	return 0;
}
*/

/*******************************************************************
* Get the first sector of a cluster
* Cluster #2 is the first cluster in the data area (after reserved + fats + root dir)
*/
uint32_t fat_get_cluster_sector(sdcard_t *sdcard, uint32_t cluster_number)
{
	if(cluster_number < 2)
	{
		cluster_number = 2;
	}
	
	return (sdcard->data_begin_sector + ((cluster_number - 2) * sdcard->sectors_per_cluster));
}


/*******************************************************************
* Free a cluster chain
*
* @param sdcard			SD Card structure
* @param startcluster	The first cluster of the chain
*
*/
uint8_t fat_free_cluster_chain(sdcard_t *sdcard, uint32_t startcluster, uint8_t cleardata)
{
	uint32_t cluster;
	uint32_t nextcluster;
	uint32_t sector;
	
	nextcluster = startcluster;
	
	while(nextcluster != 0xFFFFFFFF && nextcluster != 0)
	{
		cluster = nextcluster;
		
		// Find next
		nextcluster = fat_get_next_cluster(sdcard, cluster);
		
		// Free cluster
		fat_set_next_cluster(sdcard, cluster, 0);
		
		// Clear the actual data on the drive
		if(cleardata)
		{
			sdcard->loaded_sector = -1;
			memset(sdcard->buffer, 0x00, sdcard->blocksize);
			
			for(sector = 0; sector < sdcard->sectors_per_cluster; sector++)
			{
				if(!fat_write_sector(sdcard, cluster, sector, 0))
				{
					// TODO: Abort?
				}
			} 
		}
	}
	
	return 1;
}


/*******************************************************************
* Get the next cluster from the FAT table
*
* @param sdcard			SD card structure
* @param cluster			The cluster to look up
*
*/
uint32_t fat_get_next_cluster(sdcard_t *sdcard, uint32_t cluster)
{
	uint16_t *fat16val;
	uint32_t *fat32val;
	uint32_t value;
	uint32_t sector = 0;
	uint32_t offset = 0;

	// Check cluster
	if(sdcard->fattype == FAT16)
	{
		if(cluster > 0xFFF8)
			return 0xFFFFFFFF;
	}
	else
	{
		cluster = cluster & 0x0FFFFFFF;
		
		if(cluster >= 0x0FFFFFF8)
			return 0xFFFFFFFF;
	}
	
	// FAT16
	if(sdcard->fattype == FAT16)
	{
		// Sector that contains the fat entry
		sector = ((cluster * 2) / sdcard->blocksize);
		// Offset from the start of the sector to the value we want
		offset = ((cluster * 2) % sdcard->blocksize);
	}
	// FAT32
	else
	{
		// Sector that contains the fat entry
		sector = ((cluster * 4) / sdcard->blocksize);
		// Offset from the start of the sector to the value we want
		offset = ((cluster * 4) % sdcard->blocksize);
	}

	// Out of data clusters
	if(cluster > sdcard->data_clusters + 1) { return 0; }
	// Out of FAT sectors
	if(sector > sdcard->fat_sectors) { return 0; }
	// Read the sector within the FAT table
	if(!sd_read_block(sdcard, (sdcard->fat_begin_sector + sector), 0)) { return 0; }
	
	// Get the value
	if(sdcard->fattype == FAT16)
	{
		fat16val = (uint16_t *)&sdcard->buffer[offset];
		value = *fat16val;
		
		value = value & 0xFFFF;
		
		if(value >= 0xFFF8)
		{
			value = 0xFFFFFFFF;
		}
	}
	else
	{
		fat32val = (uint32_t *)&sdcard->buffer[offset];
		value = *fat32val;
		
		value = value & 0x0FFFFFFF;
		
		if(value >= 0x0FFFFFF8)
		{
			value = 0xFFFFFFFF;
		}
	}
	
	return value;
}


/*******************************************************************
* Set the next cluster in the FAT table
*
* @param sdcard			SD card structure
* @param cluster			The cluster to read/write
* @param nextcluster		The value to set
*
*/
uint8_t fat_set_next_cluster(sdcard_t *sdcard, uint32_t cluster, uint32_t nextcluster)
{
	uint16_t *fat16val;
	uint32_t *fat32val;
	uint32_t sector = 0;
	uint32_t offset = 0;

	// Sanity check
	if(sdcard->fattype == FAT16)
	{
		if(cluster > 0xFFF8)
			return 0;
	}
	else
	{
		cluster = cluster & 0x0FFFFFFF;
		
		if(cluster >= 0x0FFFFFF8)
			return 0;
	}
	
	
	// FAT16
	if(sdcard->fattype == FAT16)
	{
		// Sector that contains the fat entry
		sector = ((cluster * 2) / sdcard->blocksize);
		// Offset from the start of the sector to the value we want
		offset = ((cluster * 2) % sdcard->blocksize);
	}
	// FAT32
	else
	{
		// Sector that contains the fat entry
		sector = ((cluster * 4) / sdcard->blocksize);
		// Offset from the start of the sector to the value we want
		offset = ((cluster * 4) % sdcard->blocksize);
	}

	// Out of data clusters
	if(cluster > sdcard->data_clusters + 1) { return 0; }
	// Out of FAT sectors
	if(sector > sdcard->fat_sectors) { return 0; }
	// Read the sector within the FAT table
	if(!sd_read_block(sdcard, (sdcard->fat_begin_sector + sector), 0)) { return 0; }
	
	// Set the new value
	if(sdcard->fattype == FAT16)
	{
		fat16val = (uint16_t *)&sdcard->buffer[offset];
		*fat16val = (uint16_t)nextcluster;
	}
	else
	{
		fat32val = (uint32_t *)&sdcard->buffer[offset];
		*fat32val = nextcluster;
	}
	
	// Write back
	if(!sd_write_block(sdcard, (sdcard->fat_begin_sector + sector), 0))
	{
		return 0;
	}
	
	return 1;
}


/*******************************************************************
* Get the next free cluster
*
* @param sdcard		SD Card structure
* @param cluster		Cluster to start looking from
*
*/
uint32_t fat_get_next_free_cluster(sdcard_t *sdcard, uint32_t cluster)
{
	uint16_t *fat16val;
	uint32_t *fat32val;
	uint32_t value;
	uint32_t sector = 0;
	uint32_t offset = 0;
	
	// Sanity check
	if(sdcard->fattype == FAT16)
	{
		if(cluster > 0xFFF8)
			return 0;
	}
	else
	{
		cluster = cluster & 0x0FFFFFFF;
		
		if(cluster >= 0x0FFFFFF8)
			return 0;
	}
	
	
	while(1)
	{
		// FAT16
		if(sdcard->fattype == FAT16)
		{
			// Sector that contains the fat entry
			sector = ((cluster * 2) / sdcard->blocksize);
			// Offset from the start of the sector to the value we want
			offset = ((cluster * 2) % sdcard->blocksize);
		}
		// FAT32
		else
		{
			// Sector that contains the fat entry
			sector = ((cluster * 4) / sdcard->blocksize);
			// Offset from the start of the sector to the value we want
			offset = ((cluster * 4) % sdcard->blocksize);
		}
	
		// Out of data clusters
		if(cluster > sdcard->data_clusters + 1) { break; }
		// Out of FAT sectors
		if(sector > sdcard->fat_sectors) { return 0; }
	
		// Read the sector
		if(!sd_read_block(sdcard, (sdcard->fat_begin_sector + sector), 1))
		{
			return 0;
		}
	
		//printf("Sector:        %ld\n", sector);
		//printf("Offset:        %ld\n", offset);
	
		// Get the value
		if(sdcard->fattype == FAT16)
		{
			fat16val = (uint16_t *)&sdcard->buffer[offset];
			value = *fat16val;
		}
		else
		{
			fat32val = (uint32_t *)&sdcard->buffer[offset];
			value = *fat32val;
		}
		
		//printf("Table val:     %ld\n", value);
		
		// We have found a free entry
		if(value == 0)
		{
			printf("Found free cluster: %ld\n", cluster);
			printf("Sector:        %ld\n", sector);
			printf("Offset:        %ld\n", offset);
			printf("Table val:     %ld\n", value);
			return cluster;
		}
	
		cluster++;
	}
	
	return 0;
}


/*******************************************************************
* Allocate a new cluster for a chain
*
* @param sdcard		SD Card structure
* @param cluster		Cluster to allocate for
*
*/
uint32_t fat_allocate_cluster(sdcard_t *sdcard, uint32_t cluster)
{
	uint32_t nextcluster;
	
	nextcluster = fat_get_next_free_cluster(sdcard, cluster);
	if(!nextcluster)
	{
		return 0;
	}
	
	if(!fat_set_next_cluster(sdcard, cluster, nextcluster))
	{
		return 0;
	}
	
	// Mark the new cluster as end of chain
	if(!fat_set_next_cluster(sdcard, nextcluster, 0xFFFFFFFF))
	{
		return 0;
	}
	
	return nextcluster;
}



/*******************************************************************
* Find a specific file or directory by longname
*
* @param sdcard			SD Card structure
* @param startcluster	The cluster to start searching from
* @param filename			Longname of the file/dir to find
*
*/
dir_short_t * fat_find_lfn(sdcard_t *sdcard, uint32_t startcluster, const char * filename)
{
	uint32_t cluster;
	uint32_t sector;
	uint32_t entry;
	dir_short_t *dir;
	lfn_cache lfn;

	lfn_cache_reset(&lfn);
	
	// Start cluster
	cluster = startcluster;
	sector = 0;
	
	while(1)
	{
		printf("Reading cluster: %ld -> %ld\n", cluster, sector);
		if(!fat_read_sector(sdcard, cluster, sector))
		{
			return 0;
		}
				
		//
		// Loop directory entries in this sector
		//
		for(entry = 0; entry < 16; entry++)
		{
			dir = (dir_short_t *)(sdcard->buffer + (entry * 32));
			
			// Last entry
			if(fat_is_last_entry(dir)) { return 0; }			
			// Free entry
			if(fat_is_free_entry(dir)) { lfn_cache_reset(&lfn); continue; }
			
			// Long name entry
			if(fat_is_lfn_entry(dir))
			{
				lfn_cache_add(&lfn, dir);
				continue;
			}
			
			// Directory or file entry
			if(fat_is_sfn_entry(dir))
			{
				// Compare
				if(lfn.strings > 0)
				{
					if(lfn_cache_compare(&lfn, filename))
					{
						return dir;
					}
				}
				
				lfn_cache_reset(&lfn);				
				continue;
			}
		}

		// Increment sector
		sector++;
	}
	
	//printf("End of function\n");
	return 0;
}


/*******************************************************************
* Find a specific file or directory by shortname
* Used to know what the next free tail number is when creating files
*
* @param sdcard			SD Card structure
* @param startcluster	The cluster to start searching from
* @param filename			Shortname of the file/dir to find
*
*/
dir_short_t * fat_find_sfn(sdcard_t *sdcard, uint32_t startcluster, const char * filename)
{
	uint32_t cluster;
	uint32_t sector;
	uint32_t entry;
	dir_short_t *dir;
	
	// Start cluster
	cluster = startcluster;
	sector = 0;
	
	while(1)
	{
		printf("Reading cluster: %ld -> %ld\n", cluster, sector);
		if(!fat_read_sector(sdcard, cluster, sector))
		{
			return 0;
		}
				
		//
		// Loop directory entries in this sector
		//
		for(entry = 0; entry < 16; entry++)
		{
			dir = (dir_short_t *)(sdcard->buffer + (entry * 32));
			
			// Last entry
			if(fat_is_last_entry(dir)) { return 0; }			
			// Free entry
			if(fat_is_free_entry(dir)) { continue; }
			// Long name entry
			if(fat_is_lfn_entry(dir)) { continue; }
			
			// Directory or file entry
			if(fat_is_sfn_entry(dir))
			{
				if(sfn_compare((char*)dir->DIR_Name, filename))
				{
					return dir;
				}
		
				continue;
			}
		}

		// Increment sector
		sector++;
	}
	
	return 0;
}


/*******************************************************************
* Find the first free entry that can contain "entries" number
* of consecutive free entries
* TODO: check rootdir entry limit for FAT16
*
* @param sdcard			SD Card structure
* @param startcluster	Cluster to start looking from
* @param entries			Number of entries needed
*
* @return dir_short_t * or 0 if none found
*/
fat_entry * fat_find_free_entry(sdcard_t *sdcard, uint32_t startcluster, uint8_t entries)
{
	uint32_t sector;
	uint8_t entry;
	uint8_t count;
	fat_entry * firstentry;
	dir_short_t *dir;
	
	if(entries < 1)
	{
		return NULL;
	}
	
	firstentry = malloc(sizeof(fat_entry));
	if(firstentry == NULL)
	{
		return NULL;
	}
	
	count = 0;
	firstentry->cluster = startcluster;
	firstentry->sector = 0;
	firstentry->entry = 0;
	
	// Start cluster
	sector = 0;
	
	while(1)
	{
		if(!fat_read_sector(sdcard, startcluster, sector))
		{
			return 0;
		}
		
		printf("Reading cluster: %ld -> %ld\n", startcluster, sector);
		
		//
		// Loop directory entries in this sector
		//
		for(entry = 0; entry < 16; entry++)
		{
			dir = (dir_short_t *)(sdcard->buffer + (entry * 32));
			
			// Free entry
			if(fat_is_last_entry(dir) || fat_is_free_entry(dir))
			{
				// First valid
				if(count == 0)
				{
					firstentry->sector = sector;
					firstentry->entry = entry;
				}
				
				count++;
				
				if(count == entries)
				{
					return firstentry;
				}
			}
			// Entry is not free, invalidate
			else
			{
				count = 0;
			}
		}
		
		// Increment sector
		sector++;
	}
	
	printf("End of function\n");
	return NULL;
}



/*******************************************************************
* Find the next file or directory based on a byte offset from the
* cluster start
* Used by readdir
*
* @param sdcard			SD Card structure
* @param handle			FAT handle
* @param lfn				Long file name cache (optional)
*
*/
dir_short_t * fat_find_next_file(sdcard_t *sdcard, fat_handle *handle, lfn_cache *lfn)
{
	uint32_t cluster;
	uint32_t sector;
	uint32_t offset;
	dir_short_t *dir;
	
	if(lfn != NULL)
	{
		lfn_cache_reset(lfn);
	}
	
	// Start cluster/sector
	cluster = handle->datacluster;
	sector = (handle->ptr / sdcard->blocksize);
	offset = (handle->ptr % sdcard->blocksize);
	
	while(1)
	{
		printf("Reading cluster: %ld -> %ld\n", cluster, sector);
		if(!fat_read_sector(sdcard, cluster, sector))
		{
			return 0;
		}
				
		//
		// Loop directory entries in this sector
		//
		for(; offset < sdcard->blocksize; offset += 32)
		{
			dir = (dir_short_t *)(sdcard->buffer + offset);
			
			// Last entry
			if(fat_is_last_entry(dir))
			{
				if(lfn != NULL)
				{
					lfn_cache_reset(lfn);
				}
				
				return 0;
			}
			
			// Increment offset
			handle->ptr += 32;
			
			// Free entry
			if(fat_is_free_entry(dir))
			{
				if(lfn != NULL)
				{
					lfn_cache_reset(lfn);
				}
				
				continue;
			}
			
			//
			// Long name entry
			//
			if(fat_is_lfn_entry(dir))
			{
				if(lfn != NULL)
				{
					lfn_cache_add(lfn, dir);
				}
				
				continue;
			}
			
			//
			// Directory or file entry
			//
			if(fat_is_sfn_entry(dir))
			{
				return dir;
			}
		}
		
		// Reset offset
		offset = 0;

		// Increment sector
		sector++;
	}
	
	//printf("End of function\n");
	return 0;
}



/*******************************************************************
* Create a file if it does not exist and return it
*
* @param sdcard			SD Card structure
* @param startcluster	The cluster to create the file in (rootdir or directory)
* @param filename			Longname of the file to create
*
*/
uint8_t fat_create_file(sdcard_t *sdcard, uint32_t startcluster, const char * filename)
{
	fat_entry * entry;
	lfn_cache lfn;
	dir_short_t *dir;
	dir_long_t *ldir;
	uint16_t i;
	uint8_t entries, entrynum;
	uint32_t cluster;
	char sfn[13];

	if(strlen(filename) == 0)
	{
		return 0;
	}
	
	// Attempt to find the file
	dir = fat_find_lfn(sdcard, startcluster, filename);
	if(dir != NULL)
	{
		printf("File already exists\n");
		return 0;
	}
	
	// Find a free cluster for the file
	cluster = fat_get_next_free_cluster(sdcard, startcluster);
	
	// No free clusters
	if(cluster == 0)
	{
		printf("Unable to get next free cluster\n");
		return 0;
	}	
	
	printf("Creating file in cluster: %ld\n", cluster);

	//
	// Create shortname
	//
	i = 1;
	while(i < 65535)
	{
		lfn_to_sfn(filename, sfn, i);
		
		// This tail number is available
		if(!fat_find_sfn(sdcard, startcluster, sfn))
		{
			break;
		}
		
		i++;
	}
	
	if(i == 65535)
	{
		printf("Unable to find a free SFN\n");
		return 0;
	}
	
	printf("SFN: %s\n", sfn);
	
	// Update the LFN cache
	lfn_cache_from_string(&lfn, filename, sfn_checksum(sfn));
	
	// How many entries we will need to create
	// this file
	entries = lfn.strings + 1;
	
	// Find free entry
	entry = fat_find_free_entry(sdcard, startcluster, entries);
	
	if(entry == NULL)
	{
		printf("No free entries\n");
		return 0;
	}
	
	// Create the file
	printf("Ready to create file\n");
	
	// Create long name entries
	for(entrynum = lfn.strings; entrynum > 0; entrynum--)
	{
		printf("Creating LFN entry: %d\n", entrynum);
		
		if(entry->entry >= 16)
		{
			entry->entry = 0;
			entry->sector++;
		}
		
		if(!fat_read_sector(sdcard, entry->cluster, entry->sector))
		{
			printf("Failed to read sector for LFN\n");
			return 0;
		}
		
		ldir = (dir_long_t *)(sdcard->buffer + (entry->entry * 32));
		
		ldir->LDIR_Ord = (entrynum == lfn.strings ? ((entrynum) | 0x40) : (entrynum));
		ldir->LDIR_Attr = ATTR_LONG_NAME;
		ldir->LDIR_Type = 0x00;
		ldir->LDIR_Chksum = lfn.checksum;
		ldir->LDIR_FstClusLO = 0x00;
		
		memset(ldir->LDIR_Name1, 0x00, 10);
		memset(ldir->LDIR_Name2, 0x00, 12);
		memset(ldir->LDIR_Name3, 0x00, 4);
		
		ldir->LDIR_Name1[0] = lfn.filename[entrynum-1][0];
		ldir->LDIR_Name1[2] = lfn.filename[entrynum-1][1];
		ldir->LDIR_Name1[4] = lfn.filename[entrynum-1][2];
		ldir->LDIR_Name1[6] = lfn.filename[entrynum-1][3];
		ldir->LDIR_Name1[8] = lfn.filename[entrynum-1][4];
		
		ldir->LDIR_Name2[0] = lfn.filename[entrynum-1][5];
		ldir->LDIR_Name2[2] = lfn.filename[entrynum-1][6];
		ldir->LDIR_Name2[4] = lfn.filename[entrynum-1][7];
		ldir->LDIR_Name2[6] = lfn.filename[entrynum-1][8];
		ldir->LDIR_Name2[8] = lfn.filename[entrynum-1][9];
		ldir->LDIR_Name2[10] = lfn.filename[entrynum-1][10];
		
		ldir->LDIR_Name3[0] = lfn.filename[entrynum-1][11];
		ldir->LDIR_Name3[2] = lfn.filename[entrynum-1][12];
		
		if(!fat_write_sector(sdcard, entry->cluster, entry->sector, 0))
		{
			printf("Failed to write sector for LFN\n");
			return 0;
		}
		
		entry->entry++;
	}
	
	// Increment entry
	if(entry->entry >= 16)
	{
		entry->entry = 0;
		entry->sector++;
	}
	
	// Create SFN entry
	if(!fat_read_sector(sdcard, entry->cluster, entry->sector))
	{
		printf("Failed to read sector for SFN\n");
		return 0;
	}
	
	dir = (dir_short_t *)(sdcard->buffer + (entry->entry * 32));
	
	// Set directory parameters and write
	for(i=0; i<11; i++)
		dir->DIR_Name[i] = sfn[i];

	dir->DIR_Attr = ATTR_ARCHIVE;
	dir->DIR_NTRes = 0;
	dir->DIR_CrtTimeTenth = 0x00;
	dir->DIR_CrtTime = 0x00;
	dir->DIR_CrtDate = 0x00;
	dir->DIR_LstAccDate = 0x00;
	dir->DIR_FstClusHI = (uint16_t)((cluster >> 16) & 0xFFFF);
	dir->DIR_WrtTime = 0x00;
	dir->DIR_WrtDate = 0x2011;
	dir->DIR_FstClusLO = (uint16_t)(cluster & 0xFFFF);
	dir->DIR_FileSize = 0x00;
	
	if(!fat_write_sector(sdcard, entry->cluster, entry->sector, 0))
	{
		printf("Failed to write sector for SFN\n");
		return 0;
	}
	
	// Mark cluster as end of chain
	if(!fat_set_next_cluster(sdcard, cluster, 0xFFFFFFFF))
	{
		return 0;
	}
	
	printf("File created\n");		
	return 1;
}


/*******************************************************************
* Truncate a file to zero size and free the cluster chain
*
* @param sdcard			SD Card structure
* @param startcluster	The cluster to create the file in (rootdir or directory)
* @param filename			Longname of the file to create
*
*/
uint8_t fat_truncate_file(sdcard_t *sdcard, uint32_t startcluster, const char * filename)
{
	dir_short_t *dir;
	uint32_t cluster, filesize;

	if(strlen(filename) == 0)
	{
		return 0;
	}
	
	// Attempt to find the file
	dir = fat_find_lfn(sdcard, startcluster, filename);
	if(dir == NULL)
	{
		printf("File does not exist\n");
		return 0;
	}
	
	// First cluster for file
	cluster = (((uint32_t)dir->DIR_FstClusHI << 16) | dir->DIR_FstClusLO);
	filesize = dir->DIR_FileSize;
	
	// Nothing to do
	if(filesize == 0)
	{
		return 1;
	}
	
	// Update filesize
	dir->DIR_FileSize = 0;
	
	// TODO: Is the last loaded sector always the correct one?
	if(!sd_write_block(sdcard, sdcard->loaded_sector, 0))
	{
		return 0;
	}	
	
	// Free cluster chain and clear data
	fat_free_cluster_chain(sdcard, cluster, 1);
	
	// Mark first cluster as end of chain
	if(!fat_set_next_cluster(sdcard, cluster, 0xFFFFFFFF))
	{
		return 0;
	}
	
	printf("File truncated\n");		
	return 1;
}



/*******************************************************************
* Read data from a file into a buffer
*/
uint32_t fat_read_file(sdcard_t * sdcard, uint32_t startcluster, const char * filename, void * buffer, uint32_t start, uint32_t bytes)
{
	dir_short_t *dir;
	uint32_t cluster;
	uint32_t sector;
	uint32_t filesize;
	uint32_t offset;
	uint32_t bytesread;
	uint32_t bytestoread;
	
	if(strlen(filename) == 0)
		return 0;
	
	if(bytes == 0)
		return 0;
	
	// Attempt to find the file
	dir = fat_find_lfn(sdcard, startcluster, filename);
	if(dir == NULL)
	{
		printf("File does not exist\n");
		return 0;
	}
	
	filesize = dir->DIR_FileSize;
	
	if(filesize == 0 || start >= filesize)
	{
		return 0;
	}
	
	
	//
	// Sector to start reading from, determined by the start offset
	//
	cluster = ((uint32_t)dir->DIR_FstClusHI << 16) | dir->DIR_FstClusLO;
	sector = (start / sdcard->blocksize);
	offset = start - (sector * sdcard->blocksize);
	bytesread = 0;
	
	while(bytesread < bytes)
	{
		// Read the sector
		if(!fat_read_sector(sdcard, cluster, sector))
		{
			return bytesread;
		}
		
		// Bytes to read from this sector
		bytestoread = (bytes - bytesread);
		if(bytestoread > (sdcard->blocksize - offset))
		{
			bytestoread = (sdcard->blocksize - offset);
		}
		
		// Check that we dont read past the file end
		if((start + bytesread + bytestoread) > filesize)
		{
			bytestoread = (filesize - (start - bytesread));
		}
		
		// Copy data to buffer
		if(bytestoread > 0)
		{
			memcpy((buffer + bytesread), (sdcard->buffer + offset), bytestoread);
		}
		
		// Increment counters and pointers
		bytesread += bytestoread;
		offset = 0;
		sector++;
		
		// Bytes to read is 0, which means we are done, or we have
		// hit the end of file
		if(bytestoread == 0)
		{
			break;
		}
	}
	
	return bytesread;	
}


/*******************************************************************
* Write data to a file from a buffer
*/
uint32_t fat_write_file(sdcard_t * sdcard, uint32_t startcluster, const char * filename, void * buffer, uint32_t start, uint32_t bytes)
{
	dir_short_t *dir;
	uint32_t cluster;
	uint32_t sector;
	uint32_t filesize;
	uint32_t offset;
	uint32_t byteswritten;
	uint32_t bytestowrite;
	
	if(strlen(filename) == 0)
		return 0;
	
	if(bytes == 0)
		return 0;
	
	// Attempt to find the file
	dir = fat_find_lfn(sdcard, startcluster, filename);
	if(dir == NULL)
	{
		printf("File does not exist\n");
		return 0;
	}
	
	filesize = dir->DIR_FileSize;
	
	// Cannot start writing past the end of file
	if(start > filesize)
	{
		start = filesize;
	}
	
	//
	// Sector to start writing to from, determined by the start offset
	//
	cluster = ((uint32_t)dir->DIR_FstClusHI << 16) | dir->DIR_FstClusLO;
	sector = (start / sdcard->blocksize);
	offset = start - (sector * sdcard->blocksize);
	byteswritten = 0;
	
	while(byteswritten < bytes)
	{
		// Read the sector
		if(!fat_read_sector(sdcard, cluster, sector))
		{
			return byteswritten;
		}

		// Bytes to write to this sector
		bytestowrite = (bytes - byteswritten);
		if(bytestowrite > (sdcard->blocksize - offset))
		{
			bytestowrite = (sdcard->blocksize - offset);
		}
				
		// Copy and write data
		if(bytestowrite > 0)
		{
			memcpy((sdcard->buffer + offset), (buffer + byteswritten), bytestowrite);
			
			if(!fat_write_sector(sdcard, cluster, sector, 1))
			{
				return byteswritten;
			}
		}
		
		// Increment counter
		byteswritten += bytestowrite;
		offset = 0;
		sector++;
		
		// Bytes to write is 0, which means we are done
		if(bytestowrite == 0)
		{
			break;
		}
	}
	
	// Update file entry
	if(byteswritten > 0)
	{
		dir = fat_find_lfn(sdcard, startcluster, filename);
		if(dir == NULL)
		{
			return 0;
		}
		
		// TODO: Update last access time?
		
		// Update file size
		if(start + byteswritten > filesize)
		{		
			dir->DIR_FileSize = (start + byteswritten);
		}
		
		if(!sd_write_block(sdcard, sdcard->loaded_sector, 0))
		{
			return 0;
		}
	}
	
	return byteswritten;	
}


