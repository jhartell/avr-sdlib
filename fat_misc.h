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
#ifndef _FAT_MISC_H_
#define _FAT_MISC_H_

uint8_t lfn_cache_reset(lfn_cache *cache);
uint8_t lfn_cache_add(lfn_cache *cache, dir_short_t *dir);
uint8_t lfn_cache_get(lfn_cache *cache, char *output);
uint8_t lfn_cache_compare(lfn_cache *cache, const char *filename);
uint8_t lfn_cache_from_string(lfn_cache *cache, const char *filename, uint8_t chksum);

uint8_t sfn_checksum(char *shortname);
uint8_t sfn_compare(const char *name1, const char *name2);
uint8_t lfn_to_sfn(const char *lfn, char *sfn, uint16_t tailnum);

uint8_t get_path_part(const char * path, char * output, uint8_t part);

uint8_t fat_is_last_entry(dir_short_t *entry);
uint8_t fat_is_free_entry(dir_short_t *entry);
uint8_t fat_is_lfn_entry(dir_short_t *entry);
uint8_t fat_is_sfn_entry(dir_short_t *entry);

#endif