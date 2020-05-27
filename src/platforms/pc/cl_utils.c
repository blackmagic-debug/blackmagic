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

#include "general.h"
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "target.h"
#include "target_internal.h"

#include "cl_utils.h"

#ifndef O_BINARY
#define O_BINARY 0
#endif
#if defined(_WIN32) || defined(__CYGWIN__)
# include <windows.h>
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
int cl_debuglevel;
static struct mmap_data map; /* Portable way way to nullify the struct!*/


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
	printf("\t-d \"path\"\t: Use serial device at \"path\"\n");
	printf("\t-P <num>\t: Use device found as <num>");
	printf("\t-s \"string\"\t: Use dongle with (partial) "
		  "serial number \"string\"\n");
	printf("\t-c \"string\"\t: Use ftdi dongle with type \"string\"\n");
	printf("\t-n\t\t: Exit immediate if no device found\n");
	printf("\tRun mode related options:\n");
	printf("\t-C\t\t: Connect under reset\n");
	printf("\t-t\t\t: Scan SWD, with no target found scan jtag and exit\n");
	printf("\t-E\t\t: Erase flash until flash end or for given size\n");
	printf("\t-V\t\t: Verify flash against binary file\n");
	printf("\t-r\t\t: Read flash and write to binary file\n");
	printf("\t-p\t\t: Supplies power to the target (where applicable)\n");
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
	while((c = getopt(argc, argv, "Ehv::d:s:I:c:CnN:tVta:S:jpP:rR")) != -1) {
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
				cl_debuglevel = strtol(optarg, NULL, 0);
			else
				cl_debuglevel = -1;
			break;
		case 'j':
			opt->opt_usejtag = true;
			break;
		case 'C':
			opt->opt_connect_under_reset = true;
			break;
		case 'n':
			opt->opt_no_wait = true;
			break;
		case 'd':
			if (optarg)
				opt->opt_device = optarg;
			break;
		case 's':
			if (optarg)
				opt->opt_serial = optarg;
			break;
		case 'I':
			if (optarg)
				opt->opt_ident_string = optarg;
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
		case 'p':
			opt->opt_tpwr = true;
			break;
		case 'a':
			if (optarg)
				opt->opt_flash_start = strtol(optarg, NULL, 0);
			break;
		case 'N':
			if (optarg)
				opt->opt_target_dev = strtol(optarg, NULL, 0);
			break;
		case 'P':
			if (optarg)
				opt->opt_position = atoi(optarg);
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

static void display_target(int i, target *t, void *context)
{
	(void)context;
	DEBUG("*** %2d   %c  %s %s\n", i, target_attached(t)?'*':' ',
		  target_driver_name(t),
		  (target_core_name(t)) ? target_core_name(t): "");
}

int cl_execute(BMP_CL_OPTIONS_t *opt)
{
	int res = -1;
	int num_targets;
#if defined(PLATFORM_HAS_POWER_SWITCH)
	if (opt->opt_tpwr) {
		printf("Powering up device");
		platform_target_set_power(true);
		platform_delay(500);
	}
#endif
	if (opt->opt_connect_under_reset)
		printf("Connecting under reset\n");
	connect_assert_srst = opt->opt_connect_under_reset;
	platform_srst_set_val(opt->opt_connect_under_reset);
	if (opt->opt_mode == BMP_MODE_TEST)
		printf("Running in Test Mode\n");
	printf("Target voltage: %s Volt\n", platform_target_voltage());
	if (opt->opt_usejtag) {
		num_targets = jtag_scan(NULL);
	} else {
		num_targets = adiv5_swdp_scan();
	}
	if (!num_targets) {
		DEBUG("No target found\n");
		return res;
	} else {
		target_foreach(display_target, NULL);
	}
	if (opt->opt_target_dev > num_targets) {
		DEBUG("Given target nummer %d not available\n", opt->opt_target_dev);
		return res;
	}
	target *t = target_attach_n(opt->opt_target_dev, NULL);
	if (!t) {
		DEBUG("Can not attach to target %d\n", opt->opt_target_dev);
		goto target_detach;
	}
	if (opt->opt_mode == BMP_MODE_TEST) {
		char map [1024], *p = map;
		if (target_mem_map(t, map, sizeof(map))) {
			while (*p && (*p == '<')) {
				unsigned int start, size;
				char *res;
				int match;
				match = strncmp(p, "<memory-map>", strlen("<memory-map>"));
				if (!match) {
					p  += strlen("<memory-map>");
					continue;
				}
				match = strncmp(p, "<memory type=\"flash\" ", strlen("<memory type=\"flash\" "));
				if (!match) {
					unsigned int blocksize;
					if (sscanf(p, "<memory type=\"flash\" start=\"%x\" length=\"%x\">"
							  "<property name=\"blocksize\">%x</property></memory>",
							  &start, &size, &blocksize))
						printf("Flash Start: 0x%08x, length %#9x, blocksize %#8x\n",
							   start, size, blocksize);
					res = strstr(p, "</memory>");
					p = res + strlen("</memory>");
					continue;
				}
				match = strncmp(p, "<memory type=\"ram\" ", strlen("<memory type=\"ram\" "));
				if (!match) {
					if (sscanf(p, "<memory type=\"ram\" start=\"%x\" length=\"%x\"/",
							   &start, &size))
						printf("Ram   Start: 0x%08x, length %#9x\n", start, size);
					res = strstr(p, "/>");
					p = res + strlen("/>");
					continue;
				}
				break;
			}
		}
		goto target_detach;
	}
	int read_file = -1;
	if ((opt->opt_mode == BMP_MODE_FLASH_WRITE) ||
		(opt->opt_mode == BMP_MODE_FLASH_VERIFY)) {
		int mmap_res = bmp_mmap(opt->opt_flash_file, &map);
		if (mmap_res) {
			DEBUG("Can not map file: %s. Aborting!\n", strerror(errno));
			goto target_detach;
		}
	} else if (opt->opt_mode == BMP_MODE_FLASH_READ) {
		/* Open as binary */
		read_file = open(opt->opt_flash_file, O_TRUNC | O_CREAT | O_RDWR | O_BINARY,
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
		uint32_t start_time = platform_time_ms();
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
		uint32_t end_time = platform_time_ms();
		printf("Flash Write succeeded for %d bytes, %8.3f kiB/s\n",
			   (int)map.size, (((map.size * 1.0)/(end_time - start_time))));
	} else {
#define WORKSIZE 1024
		uint8_t *data = alloca(WORKSIZE);
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
		uint32_t start_time = platform_time_ms();
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
		uint32_t end_time = platform_time_ms();
		if (read_file != -1)
			close(read_file);
		printf("Read/Verified succeeded for %d bytes, %8.3f kiB/s\n",
			   bytes_read, (((bytes_read * 1.0)/(end_time - start_time))));
	}
  free_map:
	if (map.size)
		bmp_munmap(&map);
  target_detach:
	if (t)
		target_detach(t);
	return res;
}
