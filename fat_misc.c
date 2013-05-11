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
* Misc functions related to the filesystem handling
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
#include "fat_misc.h"


/*******************************************************************
* Reset LFN cache
*/
uint8_t lfn_cache_reset(lfn_cache *cache)
{
	memset(cache->filename, 0x00, (20*13));
	cache->strings = 0;
	cache->checksum = 0;
	
	return 1;
}

/*******************************************************************
* Add an entry to the LFN cache
*/
uint8_t lfn_cache_add(lfn_cache *cache, dir_short_t *dir)
{
	dir_long_t *ldir;
	ldir = (dir_long_t *)dir;
	uint8_t num;
	uint8_t checksum;
	
	num = (ldir->LDIR_Ord & 0x0F) - 1;
	checksum = ldir->LDIR_Chksum;
	
	// Verify checksum
	if(cache->checksum == 0)
	{
		cache->checksum = checksum;
	}
	
	// Incorrect checksum
	if(cache->checksum != checksum)
	{
		return 0;
	}

	cache->filename[num][0] = ldir->LDIR_Name1[0];
	cache->filename[num][1] = ldir->LDIR_Name1[2];
	cache->filename[num][2] = ldir->LDIR_Name1[4];
	cache->filename[num][3] = ldir->LDIR_Name1[6];
	cache->filename[num][4] = ldir->LDIR_Name1[8];
	
	cache->filename[num][5] = ldir->LDIR_Name2[0];
	cache->filename[num][6] = ldir->LDIR_Name2[2];
	cache->filename[num][7] = ldir->LDIR_Name2[4];
	cache->filename[num][8] = ldir->LDIR_Name2[6];
	cache->filename[num][9] = ldir->LDIR_Name2[8];
	cache->filename[num][10] = ldir->LDIR_Name2[10];
	
	cache->filename[num][11] = ldir->LDIR_Name3[0];
	cache->filename[num][12] = ldir->LDIR_Name3[2];
	
	cache->strings++;
	
	return 1;
}

/*******************************************************************
* Get the LFN from the cache
*/
uint8_t lfn_cache_get(lfn_cache *cache, char *output)
{
	if(cache->strings == 0)
	{
		output[0] = 0x00;
		return 0;
	}
	
	memcpy(output, cache->filename, (cache->strings * 13));
	
	return 1;
}

/*******************************************************************
* Compare a filename with the cached LFN
*/
uint8_t lfn_cache_compare(lfn_cache *cache, const char *filename)
{
	uint8_t i;
	uint8_t len;
	char lfn[255];
	
	if(!cache->strings) return 0;
	if(!lfn_cache_get(cache, lfn)) return 0;
	
	len = strlen(filename);
	
	for(i=0; i<len; i++)
	{
		if(lfn[i] != filename[i])
		{
			return 0;
		}
	}
	
	return 1;
}

/*******************************************************************
* Build a LFN cache from a filename string
*/
uint8_t lfn_cache_from_string(lfn_cache *cache, const char *filename, uint8_t chksum)
{
	uint8_t entry, entries, len, i, offset;
	lfn_cache_reset(cache);
	cache->checksum = chksum;
	
	len = strlen(filename);
	entries = ((len + 12) / 13);
	offset = 0;
	
	for(entry = 0; entry < entries; entry++)
	{
		for(i = 0; i < 13; i++)
		{
			offset = ((entry * 13) + i);
			
			if(offset >= len)
			{
				cache->filename[entry][i] = ((offset == len) ? 0x00 : 0xFF);
			}
			else
			{
				cache->filename[entry][i] = filename[offset];
			}
		}
		
		cache->strings++;
	}
	
	return 1;
}


/*******************************************************************
* Compute checksum for a FAT shortname
* shortname is assumed to be 11 bytes long
*/
uint8_t sfn_checksum(char *shortname)
{
	uint8_t sum;
	uint8_t i;
	
	i = 11;
	sum = 0;
	
	while(i > 0)
	{
		sum = ((sum & 1) ? 0x80 : 0) + (sum >> 1) + *shortname++;
		i--;
	}
	
	return sum;
}


/*******************************************************************
* Check if two shortnames match
*/
uint8_t sfn_compare(const char *name1, const char *name2)
{
	uint16_t i;
	
	for(i = 0; i < 11; i++)
	{
		if(name1[i] != name2[i])
		{
			return 0;
		}
	}
	
	return 1;
}


/*******************************************************************
* Convert a long filename into a shortname and add a tail number
* if supplied
* shortname is assumed to be 11 bytes long
*/
uint8_t lfn_to_sfn(const char *lfn, char *sfn, uint16_t tailnum)
{
	char ext[3];
	char tail[12];
	uint16_t i, j, taillen;
	uint16_t len = strlen(lfn);
	
	// Invalid filename
	if(lfn[0] == '.')
	{
		return 0;
	}
	
	// Clear SFN
	memset(sfn, ' ', 11);
	memset(ext, ' ', 3);
	
	// Find extension
	for(i=0; i<len; i++)
	{
		if(lfn[i] == '.')
		{
			// Copy extension
			ext[0] = lfn[i+1];
			ext[1] = lfn[i+2];
			ext[2] = lfn[i+3];
			
			// Set length so we dont copy extension again into filename
			len = i;
			break;
		}
	}
	
	// Copy filename
	j = 0;
	for(i=0; i < len; i++)
	{
		if(lfn[i] == ' ' || lfn[i] == '.')
		{
			continue;
		}
		
		// convert to uppercase
		sfn[j++] = lfn[i] - ((lfn[i] >= 'a' && lfn[i] <= 'z') ? 32 : 0);
		
		if(j == 8) break;
	}
	
	// Extension
	j = 8;
	for(i=0; i<3; i++)
	{
		sfn[j++] = ext[i] - ((ext[i] >= 'a' && ext[i] <= 'z') ? 32 : 0);
	}

	// Overwrite part of filename with tail
	if(tailnum > 0 && tailnum < 9999)
	{
		sprintf(tail, "~%d", tailnum);
		taillen = strlen(tail);
		
		for(i=0; i<taillen; i++)
		{
			sfn[8-taillen+i] = tail[i];
		}
	}
		
	return 1;
}





/*******************************************************************
* Get part of a path like /myfolder/subfolder/file.txt
* Leading slash is assumed if it is omitted, so all paths
* start from the root directory
*/
uint8_t get_path_part(const char * path, char * output, uint8_t part)
{
	uint8_t level = 0;
	uint8_t i = 0;
	char *p;
	
	p = (char *)path;
	
	// Root directory
	if(part == 0)
	{
		output[0] = '/';
		output[1] = 0x00;
		return 1;
	}
	
	// First slash omitted, increment level
	if(*p != '/')
	{
		level++;
	}
	
	// Search for part
	while(*p != 0)
	{
		if(*p == '/') { level++; p++; }
		if(*p == 0) { break; }
		if(level > part) { break; }
		if(level == part)
		{
			output[i] = *p;
			i++;
		}
		
		p++;
	}
	
	output[i] = 0x00;
	
	if(i == 0)
	{
		return 0;
	}
	
	return 1;
}


/*******************************************************************
* Determine if a directory entry is the last (every following entry is free)
*/
uint8_t fat_is_last_entry(dir_short_t *entry)
{
	if((entry->DIR_Name[0] == ENTRY_BLANK))
		return 1;
	
	return 0;
}

/*******************************************************************
* Determine if a directory entry is free to use
*/
uint8_t fat_is_free_entry(dir_short_t *entry)
{
	if(
		(entry->DIR_Name[0] == ENTRY_BLANK) ||
		(entry->DIR_Name[0] == ENTRY_DELETED)
	)
		return 1;
	
	return 0;
}


/*******************************************************************
* Determine if a directory entry is a valid long filename entry
*/
uint8_t fat_is_lfn_entry(dir_short_t *entry)
{
	if((entry->DIR_Attr & ATTR_LONG_NAME_MASK) == ATTR_LONG_NAME)
		return 1;
	
	return 0;
}

/*******************************************************************
* Determine if a directory entry is a valid short filename entry
* (file or directory)
*/
uint8_t fat_is_sfn_entry(dir_short_t *entry)
{
	if(
		(entry->DIR_Name[0] != ENTRY_BLANK) &&
		(entry->DIR_Name[0] != ENTRY_DELETED) &&
		(entry->DIR_Attr != ATTR_VOLUME_ID) &&
		((entry->DIR_Attr & ATTR_DIRECTORY) || (entry->DIR_Attr & ATTR_ARCHIVE))
	)
		return 1;
	
	return 0;
}