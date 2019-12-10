/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2019
 * Written by Uwe Bonnes (bon@elektron.ikp.physik.tu-darmstadt.de)
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
 */

/* This file allows pc-hosted BMP platforms to erase or read/verify/flash a
 * binary file from the command line.
 */

#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "general.h"
#include "target.h"
#include "target_internal.h"

#include "cl_utils.h"

#ifndef O_BINARY
#define O_BINARY 0
#endif
#if defined(_WIN32) || defined(__CYGWIN__)
#else
# include <sys/mman.h>
#endif

struct mmap_data {
	void *data;
	size_t size;
#if defined(_WIN32) || defined(__CYGWIN__)
	HANDLE hFile;
	HANDLE hMapFile;
#else
	int fd;
#endif
};

static int bmp_mmap(char *file, struct mmap_data *map)
{
#if defined(_WIN32) || defined(__CYGWIN__)
	map->hFile = CreateFile(file, GENERIC_WRITE | GENERIC_READ, FILE_SHARE_READ,
							NULL, OPEN_ALWAYS, 0, NULL);
	if (map->hFile == INVALID_HANDLE_VALUE) {
		DEBUG("Open file %s failed: %s\n", file, strerror(errno));
		return -1;
	}
	map->size = GetFileSize(map->hFile, NULL);
    map->hMapFile = CreateFileMapping(
		map->hFile,
		NULL,              /* default security       */
		PAGE_READONLY ,    /* read only access       */
		0,                 /* max. object size high  */
		0,                 /* max. object size low   */
		NULL);             /* name of mapping object */

    if (map->hMapFile == NULL || map->hMapFile == INVALID_HANDLE_VALUE) {
		DEBUG("Map file %s failed: %s\n", file, strerror(errno));
		CloseHandle(map->hFile);
		return -1;
	}
	map->data = MapViewOfFile(map->hMapFile, FILE_MAP_READ, 0, 0, 0);
	if (!map->data) {
		printf("Could not create file mapping object (%s).\n",
			   strerror(errno));
        CloseHandle(map->hMapFile);
		return -1;
    }
#else
	map->fd = open(file, O_RDONLY | O_BINARY);
	if (map->fd < 0) {
		DEBUG("Open file %s failed: %s\n", file, strerror(errno));
		return -1;
	}
	struct stat stat;
	if (fstat(map->fd, &stat))
		return -1;
	map->size = stat.st_size;
	map->data = mmap(NULL, stat.st_size, PROT_READ, MAP_PRIVATE, map->fd, 0);
#endif
	return 0;
}

static void bmp_munmap(struct mmap_data *map)
{
#if defined(_WIN32) || defined(__CYGWIN__)
	UnmapViewOfFile(map->data);
	CloseHandle(map->hMapFile);
	CloseHandle(map->hFile);
#else
	munmap(map->data, map->size);
#endif
}

static void cl_help(char **argv, BMP_CL_OPTIONS_t *opt)
{
	printf("%s\n\n", opt->opt_idstring);
	printf("Usage: %s [options]\n", argv[0]);
	printf("\t-h\t\t: This help.\n");
	printf("\t-v[1|2]\t\t: Increasing verbosity\n");
	printf("\t-s \"string\"\t: Use dongle with (partial) "
		  "serial number \"string\"\n");
	printf("\t-c \"string\"\t: Use ftdi dongle with type \"string\"\n");
	printf("\t-n\t\t:  Exit immediate if no device found\n");
	printf("\tRun mode related options:\n");
	printf("\t-t\t\t: Scan SWD, with no target found scan jtag and exit\n");
	printf("\t-E\t\t: Erase flash until flash end or for given size\n");
	printf("\t-V\t\t: Verify flash against binary file\n");
	printf("\t-r\t\t: Read flash and write to binary file\n");
	printf("\t-R\t\t: Reset device\n");
	printf("\t\tDefault mode is starting the debug server\n");
	printf("\tFlash operation modifiers options:\n");
	printf("\t-a <num>\t: Start flash operation at flash address <num>\n"
		"\t\t\tDefault start is 0x08000000\n");
	printf("\t-S <num>\t: Read <num> bytes. Default is until read fails.\n");
	printf("\t-j\t\t: Use JTAG. SWD is default.\n");
	printf("\t <file>\t\t: Use (binary) file <file> for flash operation\n"
		   "\t\t\tGiven <file> writes to flash if neither -r or -V is given\n");
	exit(0);
}

void cl_init(BMP_CL_OPTIONS_t *opt, int argc, char **argv)
{
	int c;
	opt->opt_target_dev = 1;
	opt->opt_flash_start = 0x08000000;
	opt->opt_flash_size = 16 * 1024 *1024;
	while((c = getopt(argc, argv, "Ehv::s:c:nN:tVta:S:jrR")) != -1) {
		switch(c) {
		case 'c':
			if (optarg)
				opt->opt_cable = optarg;
			break;
		case 'h':
			cl_help(argv, opt);
			break;
		case 'v':
			if (optarg)
				opt->opt_debuglevel = strtol(optarg, NULL, 0);
			break;
		case 'j':
			opt->opt_usejtag = true;
			break;
		case 'n':
			opt->opt_no_wait = true;
			break;
		case 's':
			if (optarg)
				opt->opt_serial = optarg;
			break;
		case 'E':
			opt->opt_mode = BMP_MODE_FLASH_ERASE;
			break;
		case 't':
			opt->opt_mode = BMP_MODE_TEST;
			break;
		case 'V':
			opt->opt_mode = BMP_MODE_FLASH_VERIFY;
			break;
		case 'r':
			opt->opt_mode = BMP_MODE_FLASH_READ;
			break;
		case 'R':
			opt->opt_mode = BMP_MODE_RESET;
			break;
		case 'a':
			if (optarg)
				opt->opt_flash_start = strtol(optarg, NULL, 0);
			break;
		case 'N':
			if (optarg)
				opt->opt_target_dev = strtol(optarg, NULL, 0);
			break;
		case 'S':
			if (optarg) {
				char *endptr;
				opt->opt_flash_size = strtol(optarg, &endptr, 0);
				if (endptr) {
					switch(endptr[0]) {
					case 'k':
					case 'K':
						opt->opt_flash_size *= 1024;
						break;
					case 'm':
					case 'M':
						opt->opt_flash_size *= 1024 * 1024;
						break;
					}
				}
			}
		}
	}
	if ((optind) &&  argv[optind]) {
		if (opt->opt_mode == BMP_MODE_DEBUG)
			opt->opt_mode = BMP_MODE_FLASH_WRITE;
		opt->opt_flash_file = argv[optind];
	}
	/* Checks */
	if ((opt->opt_flash_file) && ((opt->opt_mode == BMP_MODE_TEST ) ||
								  (opt->opt_mode == BMP_MODE_RESET))) {
		printf("Ignoring filename in reset/test mode\n");
		opt->opt_flash_file = NULL;
	}
}

int cl_execute(BMP_CL_OPTIONS_t *opt)
{
	int res = -1;
	int num_targets;
	if (opt->opt_mode == BMP_MODE_TEST) {
		printf("Running in Test Mode\n");
		num_targets = adiv5_swdp_scan();
		if (num_targets == 0)
			num_targets = jtag_scan(NULL);
		if (num_targets)
			return 0;
		else
			return res;
	}
	if (opt->opt_usejtag) {
		num_targets = jtag_scan(NULL);
	} else {
		num_targets = adiv5_swdp_scan();
	}
	if (!num_targets) {
		DEBUG("No target found\n");
		return res;
	}
	if (opt->opt_target_dev > num_targets) {
		DEBUG("Given target nummer %d not available\n", opt->opt_target_dev);
		return res;
	}
	struct target_controller tc = {NULL};
	target *t = target_attach_n(opt->opt_target_dev, &tc);
	if (!t) {
		DEBUG("Can not attach to target %d\n", opt->opt_target_dev);
		goto target_detach;
	}
	int read_file = -1;
	struct mmap_data map = {0};
	if ((opt->opt_mode == BMP_MODE_FLASH_WRITE) ||
		(opt->opt_mode == BMP_MODE_FLASH_VERIFY)) {
		int mmap_res = bmp_mmap(opt->opt_flash_file, &map);
		if (mmap_res) {
			DEBUG("Can not map file: %s. Aborting!\n", strerror(errno));
			goto target_detach;
		}
	} else if (opt->opt_mode == BMP_MODE_FLASH_READ) {
		/* Open as binary */
		read_file = open(opt->opt_flash_file, O_CREAT | O_RDWR | O_BINARY,
						 S_IRUSR | S_IWUSR);
		if (read_file == -1) {
			printf("Error opening flashfile %s for read: %s\n",
				   opt->opt_flash_file, strerror(errno));
			return res;
		}
	}
	if (opt->opt_flash_size < map.size)
		/* restrict to size given on command line */
		map.size = opt->opt_flash_size;
	if (opt->opt_mode == BMP_MODE_RESET) {
		target_reset(t);
	} else if (opt->opt_mode == BMP_MODE_FLASH_ERASE) {
		DEBUG("Erase %zu bytes at 0x%08" PRIx32 "\n", opt->opt_flash_size,
			  opt->opt_flash_start);
		unsigned int erased = target_flash_erase(t, opt->opt_flash_start,
												 opt->opt_flash_size);
		if (erased) {
			DEBUG("Erased failed!\n");
			goto free_map;
		}
		target_reset(t);
	} else if (opt->opt_mode == BMP_MODE_FLASH_WRITE) {
		DEBUG("Erase    %zu bytes at 0x%08" PRIx32 "\n", map.size,
			  opt->opt_flash_start);
		unsigned int erased = target_flash_erase(t, opt->opt_flash_start,
												 map.size);
		if (erased) {
			DEBUG("Erased failed!\n");
			goto free_map;
		} else {
			DEBUG("Flashing %zu bytes at 0x%08" PRIx32 "\n",
				  map.size, opt->opt_flash_start);
			unsigned int flashed = target_flash_write(t, opt->opt_flash_start,
													  map.data, map.size);
			/* Buffered write cares for padding*/
			if (flashed) {
				DEBUG("Flashing failed!\n");
			} else {
				DEBUG("Success!\n");
				res = 0;
			}
		}
		target_flash_done(t);
		target_reset(t);
	} else {
#define WORKSIZE 1024
		uint8_t *data = malloc(WORKSIZE);
		if (!data) {
			printf("Can not malloc memory for flash read/verify operation\n");
			return res;
		}
		if (opt->opt_mode == BMP_MODE_FLASH_READ)
			printf("Reading flash from 0x%08" PRIx32 " for %zu"
				   " bytes to %s\n", opt->opt_flash_start,  opt->opt_flash_size,
				   opt->opt_flash_file);
		uint32_t flash_src = opt->opt_flash_start;
		size_t size = (opt->opt_mode == BMP_MODE_FLASH_READ) ? opt->opt_flash_size:
			map.size;
		int bytes_read = 0;
		void *flash = map.data;
		while (size) {
			int worksize = (size > WORKSIZE) ? WORKSIZE : size;
			int n_read = target_mem_read(t, data, flash_src, worksize);
			if (n_read) {
				if (opt->opt_flash_size == 0) {/* we reached end of flash */
					printf("Reached end of flash at size %" PRId32 "\n",
						   flash_src - opt->opt_flash_start);
					break;
				} else {
					printf("Read failed at flash address 0x%08" PRIx32 "\n",
						   flash_src);
					break;
				}
			} else {
				bytes_read += worksize;
			}
			if (opt->opt_mode == BMP_MODE_FLASH_VERIFY) {
				int difference = memcmp(data, flash, worksize);
				if (difference){
					printf("Verify failed at flash region 0x%08" PRIx32 "\n",
						   flash_src);
					return -1;
				}
				flash += worksize;
			} else if (read_file != -1) {
				int written = write(read_file, data, worksize);
				if (written < worksize) {
					printf("Read failed at flash region 0x%08" PRIx32 "\n",
						   flash_src);
					return -1;
				}
			}
			flash_src += worksize;
			size -= worksize;
			if (size <= 0)
				res = 0;
		}
		if (read_file != -1)
			close(read_file);
		printf("Read/Verifed succeeded for %d bytes\n", bytes_read);
	}
  free_map:
	if (map.size)
		bmp_munmap(&map);
  target_detach:
	if (t)
		target_detach(t);
	return res;
}
