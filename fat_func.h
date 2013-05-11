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
#ifndef _FAT_FUNC_H_
#define _FAT_FUNC_H_


#ifndef SEEK_SET
	#define SEEK_SET 0
#endif

#ifndef SEEK_CUR
	#define SEEK_CUR 1
#endif

#ifndef SEEK_END
	#define SEEK_END 2
#endif

/* fopen flags */
#define FILE_READ			(1 << 1)
#define FILE_WRITE		(1 << 2)
#define FILE_APPEND		(1 << 3)
#define FILE_CREATE		(1 << 4)
#define FILE_TRUNCATE	(1 << 5)

/*
* Function declarations
*/
fat_handle * fat_fopen(sdcard_t * sdcard, const char * filename, const char * mode);
int8_t fat_fseek(fat_handle *handle, int32_t offset, int8_t origin);
int32_t fat_ftell(fat_handle *handle);
int8_t fat_fclose(fat_handle *handle);

uint32_t fat_fread(void * buffer, uint32_t size, uint32_t count, fat_handle *handle);
uint32_t fat_fwrite(void * buffer, uint32_t size, uint32_t count, fat_handle *handle);


fat_handle * fat_opendir(sdcard_t * sdcard, const char * path);
int8_t fat_closedir(fat_handle *handle);
int8_t fat_readdir(fat_handle *handle, char *filename);

#endif