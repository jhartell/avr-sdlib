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
* FAT filesystem user functions, interaction with the filesystem should be
* done trough the functions in this file 
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "main.h"
#include "comms.h"
#include "sd.h"
#include "fat_fs.h"
#include "fat_func.h"
#include "fat_misc.h"



/*******************************************************************
* fopen
*
* Mode flags:
* "r"		read: Open file for input operations. The file must exist.
* "w"		write: Create an empty file for output operations. If a file with the same name already exists, its contents are discarded and the file is treated as a new empty file.
* "a"		append: Open file for output at the end of a file. Output operations always write data at the end of the file, expanding it. Repositioning operations (fseek, fsetpos, rewind) are ignored. The file is created if it does not exist.
*
* "r+"	read/update: Open a file for update (both for input and output). The file must exist.
* "w+"	write/update: Create an empty file and open it for update (both for input and output). If a file with the same name already exists its contents are discarded and the file is treated as a new empty file.
* "a+"	append/update: Open a file for update (both for input and output) with all output operations writing data at the end of the file. Repositioning operations (fseek, fsetpos, rewind) affects the next input operations, but output operations move the position back to the end of file. The file is created if it does not exist.
*
*/
fat_handle * fat_fopen(sdcard_t * sdcard, const char * filename, const char * mode)
{
	fat_handle *handle;
	dir_short_t *dir;
	
	handle = malloc(sizeof(fat_handle));	
	if(!handle)
	{
		return NULL;
	}
	
	// Set data
	handle->is_file = 1;
	handle->is_dir = 0;
	handle->flags = 0;
	memset(handle->filename, 0x00, 255);
	handle->cluster = 0;
	handle->filesize = 0;
	handle->ptr = 0;
	handle->datacluster = 0;
	handle->sdcard = sdcard;
	
	//
	// Check flags
	//
	if(strcmp(mode, "r") == 0)
	{
		handle->flags |= FILE_READ;
	}
	else if(strcmp(mode, "w") == 0)
	{
		handle->flags |= FILE_WRITE | FILE_CREATE | FILE_TRUNCATE;
	}
	else if(strcmp(mode, "a") == 0)
	{
		handle->flags |= FILE_WRITE | FILE_APPEND | FILE_CREATE;
	}
	if(strcmp(mode, "r+") == 0)
	{
		handle->flags |= FILE_READ | FILE_WRITE;
	}
	if(strcmp(mode, "w+") == 0)
	{
		handle->flags |= FILE_READ | FILE_WRITE | FILE_CREATE | FILE_TRUNCATE;
	}
	if(strcmp(mode, "a+") == 0)
	{
		handle->flags |= FILE_READ | FILE_WRITE | FILE_APPEND | FILE_CREATE;
	}
	
	//
	// Parse path and look for the file/dir in the correct (sub)directory
	// At the moment only the root directory is supported
	//
	handle->cluster = sdcard->rootdir_begin_cluster;
	strcpy(handle->filename, filename);
	
	
	// Check for existing file
	dir = fat_find_lfn(sdcard, handle->cluster, handle->filename);
	
	// File does not exist
	if(dir == NULL)
	{
		// Create the file
		if(handle->flags & FILE_CREATE)
		{
			if(!fat_create_file(sdcard, handle->cluster, handle->filename))
			{
				free(handle);
				return NULL;
			}
			
			// Re-search for the file
			dir = fat_find_lfn(sdcard, handle->cluster, handle->filename);
			if(dir == NULL)
			{
				free(handle);
				return NULL;
			}
		}
		// Do not create file
		else
		{
			free(handle);
			return NULL;
		}
	}
	// File exists
	else
	{
	}
	
	// Truncate the file to zero length
	if(handle->flags & FILE_TRUNCATE)
	{
		if(!fat_truncate_file(sdcard, handle->cluster, handle->filename))
		{
			free(handle);
			return NULL;
		}
		
		// Re-search for the file to update data
		dir = fat_find_lfn(sdcard, handle->cluster, handle->filename);
		if(dir == NULL)
		{
			free(handle);
			return NULL;
		}
	}

	// Set data
	handle->datacluster = (((uint32_t)dir->DIR_FstClusHI << 16) | dir->DIR_FstClusLO);
	handle->filesize = dir->DIR_FileSize;
	handle->ptr = 0;
	
	
	// Put the pointer to the end of the file, fseek can not be used
	if(handle->flags & FILE_APPEND)
	{
		handle->ptr = handle->filesize;
	}
	
	return handle;
}


/*******************************************************************
* fseek
*
*/
int8_t fat_fseek(fat_handle *handle, int32_t offset, int8_t origin)
{
	// Ignore fseek if opened as append
	if(handle->flags & FILE_APPEND)
	{
		return 0;
	}
	
	if(!handle->is_file)
	{
		return 0;
	}
	
	switch(origin)
	{
		// Beginning of file
		case SEEK_SET:
			if(offset >= 0 && offset <= handle->filesize)
			{
				handle->ptr = offset;
				return 1;
			}
			
			return 0;
		break;
		
		// Current position
		case SEEK_CUR:
			if((handle->ptr + offset) >= 0 && (handle->ptr + offset) <= handle->filesize)
			{
				handle->ptr += offset;
				return 1;
			}
			
			return 0;
		break;
		
		// End
		case SEEK_END:
			if((handle->filesize + offset) >= 0 && (handle->filesize + offset) <= handle->filesize)
			{
				handle->ptr = handle->filesize + offset;
				return 1;
			}
			
			return 0;
		break;
	}
	
	return 0;
}

/*******************************************************************
* ftell
*
*/
int32_t fat_ftell(fat_handle *handle)
{
	return handle->ptr;
}

/*******************************************************************
* fclose
*/
int8_t fat_fclose(fat_handle *handle)
{
	free(handle);
	return 1;
}

/*******************************************************************
* fread
*/
uint32_t fat_fread(void * buffer, uint32_t size, uint32_t count, fat_handle *handle)
{
	uint32_t bytes;
	uint32_t bytesread;

	// Invalid buffer
	if(buffer == NULL)
	{
		return 0;
	}
	
	// No read access
	if(!(handle->flags & FILE_READ))
	{
		return 0;
	}
	
	bytes = size * count;
	
	if(bytes == 0)
		return 0;
	
	// Read the file data
	bytesread = fat_read_file(handle->sdcard, handle->cluster, handle->filename, buffer, handle->ptr, bytes);
	
	handle->ptr += bytesread;
	
	return bytesread;
}

/*******************************************************************
* fwrite
*/
uint32_t fat_fwrite(void * buffer, uint32_t size, uint32_t count, fat_handle *handle)
{
	uint32_t bytes;
	uint32_t byteswritten;

	// Invalid buffer
	if(buffer == NULL)
	{
		return 0;
	}
	
	// No write access
	if(!(handle->flags & FILE_WRITE))
	{
		return 0;
	}
	
	bytes = size * count;
	
	if(bytes == 0)
		return 0;
	
	// Read the file data
	byteswritten = fat_write_file(handle->sdcard, handle->cluster, handle->filename, buffer, handle->ptr, bytes);
	
	handle->ptr += byteswritten;
	
	return byteswritten;
}



/*******************************************************************
* opendir
*
*/
fat_handle * fat_opendir(sdcard_t * sdcard, const char * path)
{
	fat_handle *handle;
	dir_short_t *dir;
	char dirname[64];
	
	handle = malloc(sizeof(fat_handle));	
	if(!handle)
	{
		return NULL;
	}
	
	// Set data
	handle->is_file = 0;
	handle->is_dir = 1;
	handle->flags = 0;
	memset(handle->filename, 0x00, 255);
	handle->cluster = 0;
	handle->datacluster = 0;
	handle->filesize = 0;
	handle->ptr = 0;
	handle->sdcard = sdcard;
	

	// The supplied path is the root directory
	if(!get_path_part(path, dirname, 1))
	{
		strcpy(handle->filename, path);
		handle->cluster = sdcard->rootdir_begin_cluster;
		handle->datacluster = sdcard->rootdir_begin_cluster;
		return handle;
	}
	
	// Search the path until we reach the last directory
	uint8_t level = 1;
	uint32_t cluster = sdcard->rootdir_begin_cluster;
	
	while(1)
	{
		if(get_path_part(path, dirname, level))
		{
			// Check if directory exists
			dir = fat_find_lfn(sdcard, cluster, dirname);
			if(dir == NULL)
			{
				free(handle);
				return NULL;
			}
			
			// Is this a directory?
			if(!(dir->DIR_Attr & ATTR_DIRECTORY))
			{
				free(handle);
				return NULL;
			}
			
			// This is the last directory
			if(!get_path_part(path, dirname, level+1))
			{
				printf("%s found at %ld\n", dirname, cluster);
				
				// Set data
				strcpy(handle->filename, dirname);
				handle->cluster = cluster;
				handle->datacluster = (((uint32_t)dir->DIR_FstClusHI << 16) | dir->DIR_FstClusLO);
				return handle;
			}
			
			// Set parent cluster to the current directory
			cluster = (((uint32_t)dir->DIR_FstClusHI << 16) | dir->DIR_FstClusLO);
		}
		else
		{
			break;
		}
		
		level++;
	}

	return NULL;	
}

/*******************************************************************
* closedir
*/
int8_t fat_closedir(fat_handle *handle)
{
	free(handle);
	return 1;
}

/*******************************************************************
* readdir
*/
int8_t fat_readdir(fat_handle *handle, char *filename)
{
	dir_short_t *dir;
	lfn_cache *lfn = malloc(sizeof(lfn_cache));
	
	// Valid handle
	if(!handle->is_dir)
	{
		free(lfn);
		return 0;
	}
	
	dir = fat_find_next_file(handle->sdcard, handle, lfn);
	if(dir == NULL)
	{
		free(lfn);
		return 0;
	}
	
	// Copy long or shortname
	if(lfn_cache_get(lfn, filename))
	{
	}
	else
	{
		strncpy(filename, (const char *)dir->DIR_Name, 11);
		filename[11] = 0x00;
	}
	
	free(lfn);
	return 1;
}
