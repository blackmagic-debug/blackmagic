/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2019 - 2021 Uwe Bonnes
 *                            (bon@elektron.ikp.physik.tu-darmstadt.de)
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
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "version.h"
#include "target_internal.h"
#include "cortexm.h"
#include "command.h"

#include "cl_utils.h"
#include "bmp_hosted.h"

#ifndef O_BINARY
#define O_BINARY 0
#endif
#if defined(_WIN32) || defined(__CYGWIN__)
# include <windows.h>
#else
# include <sys/mman.h>
#endif

static void cl_target_printf(struct target_controller *tc,
                              const char *fmt, va_list ap)
{
	(void)tc;

	vprintf(fmt, ap);
}

static struct target_controller cl_controller = {
	.printf = cl_target_printf,
};

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
		DEBUG_WARN("Open file %s failed: %s\n", file, strerror(errno));
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
		DEBUG_WARN("Map file %s failed: %s\n", file, strerror(errno));
		CloseHandle(map->hFile);
		return -1;
	}
	map->data = MapViewOfFile(map->hMapFile, FILE_MAP_READ, 0, 0, 0);
	if (!map->data) {
		DEBUG_WARN("Could not create file mapping object (%s).\n",
			   strerror(errno));
        CloseHandle(map->hMapFile);
		return -1;
    }
#else
	map->fd = open(file, O_RDONLY | O_BINARY);
	if (map->fd < 0) {
		DEBUG_WARN("Open file %s failed: %s\n", file, strerror(errno));
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

static void cl_help(char **argv)
{
	bmp_ident(NULL);
	DEBUG_WARN("Usage: %s [options]\n", argv[0]);
	DEBUG_WARN("\t-h\t\t: This help.\n");
	DEBUG_WARN("\t-v[bitmask]\t: Increasing verbosity. Bitmask:\n");
	DEBUG_WARN("\t\t\t  1 = INFO, 2 = GDB, 4 = TARGET, 8 = PROBE, 16 = WIRE\n");
	DEBUG_WARN("\t-l\t\t: List available probes\n");
	DEBUG_WARN("Probe selection arguments:\n");
	DEBUG_WARN("\t-d \"path\"\t: Use serial BMP device at <path>");
#if HOSTED_BMP_ONLY == 1 && defined(__APPLE__)
	DEBUG_WARN("\n");
#else
	DEBUG_WARN(". Deprecated!\n");
#endif
	DEBUG_WARN("\t-P <pos>\t: Use debugger found at position <pos>\n");
	DEBUG_WARN("\t-n <num>\t: Use target device found at position <num>\n");
	DEBUG_WARN("\t-s \"serial\"\t: Use dongle with (partial) "
		  "serial number \"serial\"\n");
	DEBUG_WARN("\t-c \"string\"\t: Use ftdi dongle with type \"string\"\n");
	DEBUG_WARN("\t\t Use \"list\" to list available cables\n");
	DEBUG_WARN("Run mode related options:\n");
	DEBUG_WARN("\tDefault mode is to start the debug server at :2000\n");
	DEBUG_WARN("\t-j\t\t: Use JTAG. SWD is default.\n");
	DEBUG_WARN("\t-f\t\t: Set minimum high and low times of SWJ waveform.\n");
	DEBUG_WARN("\t-C\t\t: Connect under reset\n");
	DEBUG_WARN("\t-t\t\t: Scan SWD or JTAG and display information about \n"
			   "\t\t\t  connected devices\n");
	DEBUG_WARN("\t-T\t\t: Continious read/write-back some value to allow\n"
			   "\t\t\t  timing insection of SWJ. Abort with ^C\n");
	DEBUG_WARN("\t-e\t\t: Assume \"resistor SWD connection\" on FTDI: TDI\n"
               "\t\t\t  connected to TMS, TDO to TDI with eventual resistor\n");
	DEBUG_WARN("\t-E\t\t: Erase flash until flash end or for given size\n");
	DEBUG_WARN("\t-w\t\t: Write binary file to target flash (default).\n");
	DEBUG_WARN("\t-V\t\t: Verify flash against binary file. Can be combined\n"
	           "\t\t\t  with -w to verify right after programming.\n");
	DEBUG_WARN("\t-r\t\t: Read flash and write to binary file\n");
	DEBUG_WARN("\t-p\t\t: Supplies power to the target (where applicable)\n");
	DEBUG_WARN("\t-R\t\t: Reset device\n");
	DEBUG_WARN("\t-H\t\t: Do not use high level commands (BMP-Remote)\n");
	DEBUG_WARN("\t-m <target>\t: Use (target)id for SWD multi-drop.\n");
	DEBUG_WARN("\t-M <string>\t: Run target specific monitor commands. Quote multi\n");
	DEBUG_WARN("\t\t\t  word strings. Run \"-M help\" for help.\n");
	DEBUG_WARN("Flash operation modifiers options:\n");
	DEBUG_WARN("\tDefault action with given file is to write to flash\n");
	DEBUG_WARN("\t-a <addr>\t: Start flash operation at flash address <addr>\n"
		"\t\t\t  Default start is start of flash in memory map\n");
	DEBUG_WARN("\t-S <num>\t: Read <num> bytes. Default is until read fails.\n");
	DEBUG_WARN("\t <file>\t\t: Use (binary) file <file> for flash operation\n");
	exit(0);
}

void cl_init(BMP_CL_OPTIONS_t *opt, int argc, char **argv)
{
	int c;
	opt->opt_target_dev = 1;
	opt->opt_flash_size = 0xffffffff;
	opt->opt_flash_start = 0xffffffff;
	opt->opt_max_swj_frequency = 4000000;
	while((c = getopt(argc, argv, "eEhHv:d:f:s:I:c:Cln:m:M:wVtTa:S:jpP:rR")) != -1) {
		switch(c) {
		case 'c':
			if (optarg)
				opt->opt_cable = optarg;
			break;
		case 'h':
			cl_debuglevel = 3;
			cl_help(argv);
			break;
		case 'H':
			opt->opt_no_hl = true;
			break;
		case 'v':
			if (optarg)
				cl_debuglevel = strtol(optarg, NULL, 0) & (BMP_DEBUG_MAX - 1);
			break;
		case 'j':
			opt->opt_usejtag = true;
			break;
		case 'l':
			opt->opt_list_only = true;
			cl_debuglevel |= BMP_DEBUG_STDOUT;
			break;
		case 'C':
			opt->opt_connect_under_reset = true;
			break;
		case 'e':
			opt->external_resistor_swd = true;
			break;
		case 'd':
			if (optarg)
				opt->opt_device = optarg;
			break;
		case 'f':
			if (optarg) {
				char *p;
				uint32_t frequency = strtol(optarg, &p, 10);
				switch(*p) {
				case 'k':
					frequency *= 1000;
					break;
				case 'M':
					frequency *= 1000*1000;
					break;
				}
				opt->opt_max_swj_frequency = frequency;
			}
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
			cl_debuglevel |= BMP_DEBUG_INFO | BMP_DEBUG_STDOUT;
			break;
		case 'T':
			opt->opt_mode = BMP_MODE_SWJ_TEST;
			break;
		case 'w':
			if (opt->opt_mode == BMP_MODE_FLASH_VERIFY)
				opt->opt_mode = BMP_MODE_FLASH_WRITE_VERIFY;
			else
				opt->opt_mode = BMP_MODE_FLASH_WRITE;
			break;
		case 'V':
			if (opt->opt_mode == BMP_MODE_FLASH_WRITE)
				opt->opt_mode = BMP_MODE_FLASH_WRITE_VERIFY;
			else
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
		case 'n':
			if (optarg)
				opt->opt_target_dev = strtol(optarg, NULL, 0);
			break;
		case 'm':
			if (optarg)
				opt->opt_targetid = strtol(optarg, NULL, 0);
			break;
		case 'M':
			opt->opt_mode = BMP_MODE_MONITOR;
			if (optarg)
				opt->opt_monitor = optarg;
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
								  (opt->opt_mode == BMP_MODE_SWJ_TEST) ||
								  (opt->opt_mode == BMP_MODE_RESET))) {
		DEBUG_WARN("Ignoring filename in reset/test mode\n");
		opt->opt_flash_file = NULL;
	}
}

static void display_target(int i, target *t, void *context)
{
	(void)context;
	if (!strcmp(target_driver_name(t), "ARM Cortex-M")) {
		DEBUG_INFO("***%2d%sUnknown %s Designer %3x Partno %3x %s\n",
			  i, target_attached(t)?" * ":" ",
			  target_driver_name(t),
			  target_designer(t),
			  target_idcode(t),
			  (target_core_name(t)) ? target_core_name(t): "");
	} else {
		DEBUG_INFO("*** %2d   %c  %s %s\n", i, target_attached(t)?'*':' ',
			  target_driver_name(t),
			  (target_core_name(t)) ? target_core_name(t): "");
	}
}

int cl_execute(BMP_CL_OPTIONS_t *opt)
{
	int res = -1;
	int num_targets;
	if (opt->opt_tpwr) {
		platform_target_set_power(true);
		platform_delay(500);
	}
	if (opt->opt_connect_under_reset)
		DEBUG_INFO("Connecting under reset\n");
	connect_assert_srst = opt->opt_connect_under_reset;
	platform_srst_set_val(opt->opt_connect_under_reset);
	if (opt->opt_mode == BMP_MODE_TEST)
		DEBUG_INFO("Running in Test Mode\n");
	DEBUG_INFO("Target voltage: %s Volt\n", platform_target_voltage());
	if (opt->opt_usejtag) {
		num_targets = platform_jtag_scan(NULL);
	} else {
		num_targets = platform_adiv5_swdp_scan(opt->opt_targetid);
		if (!num_targets) {
			DEBUG_INFO("Scan SWD failed, trying JTAG!\n");
			num_targets = platform_jtag_scan(NULL);
		}
	}
	if (!num_targets) {
		DEBUG_WARN("No target found\n");
		return res;
	} else {
		num_targets = target_foreach(display_target, &num_targets);
	}
	if (opt->opt_target_dev > num_targets) {
		DEBUG_WARN("Given target nummer %d not available max %d\n",
				   opt->opt_target_dev, num_targets);
		return res;
	}
	target *t = target_attach_n(opt->opt_target_dev, &cl_controller);

	if (!t) {
		DEBUG_WARN("Can not attach to target %d\n", opt->opt_target_dev);
		goto target_detach;
	}
	/* List each defined RAM */
	int n_ram = 0;
	for (struct target_ram *r = t->ram; r; r = r->next)
		n_ram++;
	for (int n = n_ram; n >= 0; n --) {
		struct target_ram *r = t->ram;
		for (int i = 1; r; r = r->next, i++)
			if (i == n)
				DEBUG_INFO("RAM   Start: 0x%08" PRIx32 " length = 0x%" PRIx32 "\n",
					   r->start, (uint32_t)r->length);
	}
	/* Always scan memory map to find lowest flash */
	/* List each defined Flash */
	uint32_t lowest_flash_start = 0xffffffff;
	uint32_t lowest_flash_size = 0;
	int n_flash = 0;
	for (struct target_flash *f = t->flash; f; f = f->next)
		n_flash++;
	for (int n = n_flash; n >= 0; n --) {
		struct target_flash *f = t->flash;
		for (int i = 1; f; f = f->next, i++)
			if (i == n) {
				DEBUG_INFO("Flash Start: 0x%08" PRIx32 " length = 0x%" PRIx32
						   " blocksize 0x%" PRIx32 "\n",
						   f->start, (uint32_t)f->length, (uint32_t)f->blocksize);
				if (f->start < lowest_flash_start) {
					lowest_flash_start = f->start;
					lowest_flash_size = f->length;
				}
			}
	}
	if (opt->opt_flash_start == 0xffffffff)
		opt->opt_flash_start = lowest_flash_start;
	if ((opt->opt_flash_size == 0xffffffff) &&
	    (opt->opt_mode != BMP_MODE_FLASH_WRITE) &&
	    (opt->opt_mode != BMP_MODE_FLASH_VERIFY) &&
	    (opt->opt_mode != BMP_MODE_FLASH_WRITE_VERIFY))
		opt->opt_flash_size = lowest_flash_size;
	if (opt->opt_mode == BMP_MODE_SWJ_TEST) {
		switch (t->core[0]) {
		case 'M':
			DEBUG_WARN("Continious read/write-back DEMCR. Abort with ^C\n");
			while(1) {
				uint32_t demcr;
				target_mem_read(t, &demcr, CORTEXM_DEMCR, 4);
				target_mem_write32(t, CORTEXM_DEMCR, demcr);
				platform_delay(1); /* To allow trigger*/
			}
		default:
			DEBUG_WARN("No test for this core type yet\n");
		}
	} else if (opt->opt_mode == BMP_MODE_MONITOR) {
		command_process(t, opt->opt_monitor);
	}
	if ((opt->opt_mode == BMP_MODE_TEST) ||
		(opt->opt_mode == BMP_MODE_SWJ_TEST))
		goto target_detach;
	int read_file = -1;
	if ((opt->opt_mode == BMP_MODE_FLASH_WRITE) ||
	    (opt->opt_mode == BMP_MODE_FLASH_VERIFY) ||
	    (opt->opt_mode == BMP_MODE_FLASH_WRITE_VERIFY)) {
		int mmap_res = bmp_mmap(opt->opt_flash_file, &map);
		if (mmap_res) {
			DEBUG_WARN("Can not map file: %s. Aborting!\n", strerror(errno));
			goto target_detach;
		}
	} else if (opt->opt_mode == BMP_MODE_FLASH_READ) {
		/* Open as binary */
		read_file = open(opt->opt_flash_file, O_TRUNC | O_CREAT | O_RDWR | O_BINARY,
						 S_IRUSR | S_IWUSR);
		if (read_file == -1) {
			DEBUG_WARN("Error opening flashfile %s for read: %s\n",
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
		DEBUG_INFO("Erase %zu bytes at 0x%08" PRIx32 "\n", opt->opt_flash_size,
			  opt->opt_flash_start);
		unsigned int erased = target_flash_erase(t, opt->opt_flash_start,
												 opt->opt_flash_size);
		if (erased) {
			DEBUG_WARN("Erased failed!\n");
			goto free_map;
		}
		target_reset(t);
	} else if ((opt->opt_mode == BMP_MODE_FLASH_WRITE) ||
	           (opt->opt_mode == BMP_MODE_FLASH_WRITE_VERIFY)) {
		DEBUG_INFO("Erase    %zu bytes at 0x%08" PRIx32 "\n", map.size,
			  opt->opt_flash_start);
		uint32_t start_time = platform_time_ms();
		unsigned int erased = target_flash_erase(t, opt->opt_flash_start,
												 map.size);
		if (erased) {
			DEBUG_WARN("Erased failed!\n");
			goto free_map;
		} else {
			DEBUG_INFO("Flashing %zu bytes at 0x%08" PRIx32 "\n",
				  map.size, opt->opt_flash_start);
			unsigned int flashed = target_flash_write(t, opt->opt_flash_start,
													  map.data, map.size);
			/* Buffered write cares for padding*/
			if (flashed) {
				DEBUG_WARN("Flashing failed!\n");
			} else {
				DEBUG_INFO("Success!\n");
				res = 0;
			}
		}
		target_flash_done(t);
		uint32_t end_time = platform_time_ms();
		DEBUG_WARN("Flash Write succeeded for %d bytes, %8.3f kiB/s\n",
			   (int)map.size, (((map.size * 1.0)/(end_time - start_time))));
		if (opt->opt_mode != BMP_MODE_FLASH_WRITE_VERIFY) {
			target_reset(t);
			goto free_map;
		}
	}
	if ((opt->opt_mode == BMP_MODE_FLASH_READ) ||
	    (opt->opt_mode == BMP_MODE_FLASH_VERIFY) ||
	    (opt->opt_mode == BMP_MODE_FLASH_WRITE_VERIFY)) {
#define WORKSIZE 1024
		uint8_t *data = alloca(WORKSIZE);
		if (!data) {
			DEBUG_WARN("Can not malloc memory for flash read/verify "
					   "operation\n");
			return res;
		}
		if (opt->opt_mode == BMP_MODE_FLASH_READ)
			DEBUG_INFO("Reading flash from 0x%08" PRIx32 " for %zu"
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
					DEBUG_INFO("Reached end of flash at size %" PRId32 "\n",
						   flash_src - opt->opt_flash_start);
					break;
				} else {
					DEBUG_WARN("Read failed at flash address 0x%08" PRIx32 "\n",
						   flash_src);
					break;
				}
			} else {
				bytes_read += worksize;
			}
			if ((opt->opt_mode == BMP_MODE_FLASH_VERIFY) ||
			    (opt->opt_mode == BMP_MODE_FLASH_WRITE_VERIFY)) {
				int difference = memcmp(data, flash, worksize);
				if (difference){
					DEBUG_WARN("Verify failed at flash region 0x%08"
							   PRIx32 "\n", flash_src);
					return -1;
				}
				flash += worksize;
			} else if (read_file != -1) {
				int written = write(read_file, data, worksize);
				if (written < worksize) {
					DEBUG_WARN("Read failed at flash region 0x%08" PRIx32 "\n",
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
		DEBUG_WARN("Read/Verify succeeded for %d bytes, %8.3f kiB/s\n",
		           bytes_read,
		           (((bytes_read * 1.0)/(end_time - start_time))));
		if (opt->opt_mode == BMP_MODE_FLASH_WRITE_VERIFY)
			target_reset(t);
	}
  free_map:
	if (map.size)
		bmp_munmap(&map);
  target_detach:
	if (t)
		target_detach(t);
	target_list_free();
	return res;
}
