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
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <getopt.h>
#include "version.h"
#include "target_internal.h"
#include "cortexm.h"
#include "command.h"

#include "cli.h"
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
	PRINT_INFO(
		"\n"
		"Usage: %s [-h | -l | [-vBITMASK] [-d PATH | -P NUMBER | -s SERIAL | -c TYPE]\n"
		"\t[-n NUMBER] [-j | -A] [-C] [-t | -T] [-e] [-p] [-R[h]] [-H] [-M STRING ...]\n"
		"\t[-f | -m] [-E | -w | -V | -r] [-a ADDR] [-S number] [file]]\n"
		"\n"
		"The default is to start a debug server at localhost:2000\n\n"
		"Single-shot and verbosity options [-h | -l | -vBITMASK]:\n"
		"\t-h, --help       Show the version and this help, then exit\n"
		"\t-l, --list       List available supported probes\n"
		"\t-v, --verbose    Set the output verbosity level based on some combination of:\n"
		"\t                   1 = INFO, 2 = GDB, 4 = TARGET, 8 = PROBE, 16 = WIRE\n"
		"\n"
		"Probe selection arguments [-d PATH | -P NUMBER | -s SERIAL | -c TYPE]:\n"
		"\t-d, --device     Use a serial device at the given path (Deprecated!)\n"
		"\t-P, --probe      Use the <number>th debug probe found while scanning the\n"
		"\t                   system, see the output from list for the order\n"
		"\t-s, --serial     Select the debug probe with the given serial number\n"
		"\t-c, --ftdi-type  Select the FTDI-based debug probe with of the given\n"
		"\t                   type (cable)\n"
		"\n"
		"General configuration options: [-n NUMBER] [-j] [-C] [-t | -T] [-e] [-p] [-R[h]]\n"
		"\t\t[-H] [-M STRING ...]\n"
		"\t-n, --number     Select the target device at the given position in the\n"
		"\t                   scan chain (use the -t option to get a scan chain listing)\n"
		"\t-j, --jtag       Use JTAG instead of SWD\n"
		"\t-A, --auto-scan  Automatic scanning - try JTAG first, then SWD\n"
		"\t-C, --hw-reset   Connect to target under hardware reset\n"
		"\t-t, --list-chain Perform a chain scan and display information about the\n"
		"\t                   conected devices\n"
		"\t-T, --timing     Perform continues read- or write-back of a value to allow\n"
		"\t                   measurement of protocol timing. Aborted by ^C\n"
		"\t-e, --ext-res    Assume external resistors for FTDI devices, that is having the\n"
		"\t                   FTDI chip connected through resistors to TMS, TDI and TDO\n"
		"\t-p, --power      Power the target from the probe (if possible)\n"
		"\t-R, --reset      Reset the device. If followed by 'h', this will be done using\n"
		"\t                   the hardware reset line instead of over the debug link\n"
		"\t-H, --high-level Do not use the high level command API (bmp-remote)\n"
		"\t-M, --monitor    Run target-specific monitor commands. This option\n"
		"\t                   can be repeated for as many commands you wish to run.\n"
		"\t                   If the command contains spaces, use quotes around the\n"
		"\t                   complete command\n"
		"\n"
		"SWD-specific configuration options [-f FREQUENCY | -m TARGET]:\n"
		"\t-f, --freq       Set an operating frequency for SWD\n"
		"\t-m, --mult-drop  Use the given target ID for selection in SWD multi-drop\n"
		"\n"
		"Flash operation selection options [-E | -w | -V | -r]:\n"
		"\t-E, --erase      Erase the target device Flash\n"
		"\t-w, --write      Write the specified binary file to the target device\n"
		"\t                   Flash (the default)\n"
		"\t-V, --verify     Verify the target device Flash against the specified\n"
		"\t                   binary file\n"
		"\t-r, --read       Read the target device Flash\n"
		"\n"
		"Flash operation modifiers options: [-a ADDR] [-S number] [FILE]\n"
		"\t-a, --addr       Start address for the given Flash operation (defaults to\n"
		"\t                   the start of Flash)\n"
		"\t-S, --byte-count Number of bytes to work on in the Flash operation (default\n"
		"\t                   is till the operation fails or is complete)\n"
		"\t<file>           Binary file to use in Flash operations\n",
		argv[0]
	);
	exit(0);
}

static const struct option long_options[] = {
	{"help", no_argument, NULL, 'h'},
	{"list", no_argument, NULL, 'l'},
	{"verbose", required_argument, NULL, 'v'},
	{"device", required_argument, NULL, 'd'},
	{"probe", required_argument, NULL, 'P'},
	{"serial", required_argument, NULL, 's'},
	{"ftdi-type", required_argument, NULL, 'c'},
	{"number", required_argument, NULL, 'n'},
	{"jtag", no_argument, NULL, 'j'},
	{"auto-scan", no_argument, NULL, 'A'},
	{"hw-reset", no_argument, NULL, 'C'},
	{"list-chain", no_argument, NULL, 't'},
	{"timing", no_argument, NULL, 'T'},
	{"ext-res", no_argument, NULL, 'e'},
	{"power", no_argument, NULL, 'p'},
	{"reset", optional_argument, NULL, 'R'},
	{"high-level", no_argument, NULL, 'H'},
	{"monitor", required_argument, NULL, 'M'},
	{"freq", required_argument, NULL, 'f'},
	{"multi-drop", required_argument, NULL, 'm'},
	{"erase", no_argument, NULL, 'E'},
	{"write", no_argument, NULL, 'W'},
	{"verify", no_argument, NULL, 'V'},
	{"read", no_argument, NULL, 'r'},
	{"addr", required_argument, NULL, 'a'},
	{"byte-count", required_argument, NULL, 'S'},
	{NULL, 0, NULL, 0}
} ;

void cl_init(BMP_CL_OPTIONS_t *opt, int argc, char **argv)
{
	int c;
	opt->opt_target_dev = 1;
	opt->opt_flash_size = 0xffffffff;
	opt->opt_flash_start = 0xffffffff;
	opt->opt_max_swj_frequency = 4000000;
	opt->opt_scanmode = BMP_SCAN_SWD;
	while((c = getopt_long(argc, argv, "eEhHv:d:f:s:I:c:Cln:m:M:wVtTa:S:jApP:rR::", long_options, NULL)) != -1) {
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
			opt->opt_scanmode = BMP_SCAN_JTAG;
			break;
		case 'A':
			opt->opt_scanmode = BMP_SCAN_AUTO;
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
			if ((optarg) && (tolower(optarg[0]) == 'h'))
				opt->opt_mode = BMP_MODE_RESET_HW;
			else
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
	} else if ((opt->opt_mode == BMP_MODE_DEBUG) &&
	           (opt->opt_monitor)) {
		opt->opt_mode = BMP_MODE_MONITOR; // To avoid DEBUG mode
	}

	/* Checks */
	if ((opt->opt_flash_file) && ((opt->opt_mode == BMP_MODE_TEST ) ||
								  (opt->opt_mode == BMP_MODE_SWJ_TEST) ||
								  (opt->opt_mode == BMP_MODE_RESET) ||
								  (opt->opt_mode == BMP_MODE_RESET_HW))) {
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
	int res = 0;
	int num_targets;
	if (opt->opt_tpwr) {
		platform_target_set_power(true);
		platform_delay(500);
	}
	if (opt->opt_mode == BMP_MODE_RESET_HW) {
			platform_nrst_set_val(true);
			platform_delay(1);
			platform_nrst_set_val(false);
			return res;
	}
	if (opt->opt_connect_under_reset)
		DEBUG_INFO("Connecting under reset\n");
	connect_assert_srst = opt->opt_connect_under_reset;
	platform_nrst_set_val(opt->opt_connect_under_reset);
	if (opt->opt_mode == BMP_MODE_TEST)
		DEBUG_INFO("Running in Test Mode\n");
	DEBUG_INFO("Target voltage: %s Volt\n", platform_target_voltage());

	if (opt->opt_scanmode == BMP_SCAN_JTAG)
		num_targets = platform_jtag_scan(NULL);
	else if (opt->opt_scanmode == BMP_SCAN_SWD)
		num_targets = platform_adiv5_swdp_scan(opt->opt_targetid);
	else
	{
		num_targets = platform_jtag_scan(NULL);
		if (num_targets > 0)
			goto found_targets;
		DEBUG_INFO("JTAG scan found no devices, trying SWD.\n");
		num_targets = platform_adiv5_swdp_scan(opt->opt_targetid);
		if (num_targets > 0)
			goto found_targets;
		DEBUG_INFO("SW-DP scan failed!\n");
	}

found_targets:
	if (!num_targets) {
		DEBUG_WARN("No target found\n");
		return -1;
	} else {
		num_targets = target_foreach(display_target, &num_targets);
	}
	if (opt->opt_target_dev > num_targets) {
		DEBUG_WARN("Given target number %d not available max %d\n",
				   opt->opt_target_dev, num_targets);
		return -1;
	}
	target *t = target_attach_n(opt->opt_target_dev, &cl_controller);

	if (!t) {
		DEBUG_WARN("Can not attach to target %d\n", opt->opt_target_dev);
		res = -1;
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
			DEBUG_WARN("Continuous read/write-back DEMCR. Abort with ^C\n");
			while(1) {
				uint32_t demcr;
				target_mem_read(t, &demcr, CORTEXM_DEMCR, 4);
				target_mem_write32(t, CORTEXM_DEMCR, demcr);
				platform_delay(1); /* To allow trigger*/
			}
		default:
			DEBUG_WARN("No test for this core type yet\n");
		}
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
			res = -1;
			goto target_detach;
		}
	} else if (opt->opt_mode == BMP_MODE_FLASH_READ) {
		/* Open as binary */
		read_file = open(opt->opt_flash_file, O_TRUNC | O_CREAT | O_RDWR | O_BINARY,
						 S_IRUSR | S_IWUSR);
		if (read_file == -1) {
			DEBUG_WARN("Error opening flashfile %s for read: %s\n",
					   opt->opt_flash_file, strerror(errno));
			res = -1;
			goto target_detach;
		}
	}
	if (opt->opt_flash_size < map.size)
		/* restrict to size given on command line */
		map.size = opt->opt_flash_size;
	if (opt->opt_monitor) {
		res = command_process(t, opt->opt_monitor);
		if (res)
			DEBUG_WARN("Command \"%s\" failed\n", opt->opt_monitor);
	}
	if (opt->opt_mode == BMP_MODE_RESET) {
		target_reset(t);
	} else if (opt->opt_mode == BMP_MODE_FLASH_ERASE) {
		DEBUG_INFO("Erase %zu bytes at 0x%08" PRIx32 "\n", opt->opt_flash_size,
			  opt->opt_flash_start);
		unsigned int erased = target_flash_erase(t, opt->opt_flash_start,
												 opt->opt_flash_size);
		if (erased) {
			DEBUG_WARN("Erasure failed!\n");
			res = -1;
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
			DEBUG_WARN("Erasure failed!\n");
			res = -1;
			goto free_map;
		} else {
			DEBUG_INFO("Flashing %zu bytes at 0x%08" PRIx32 "\n",
				  map.size, opt->opt_flash_start);
			unsigned int flashed = target_flash_write(t, opt->opt_flash_start,
													  map.data, map.size);
			/* Buffered write cares for padding*/
			if (!flashed)
				flashed = target_flash_done(t);
			if (flashed) {
				DEBUG_WARN("Flashing failed!\n");
				res = -1;
				goto free_map;
			} else {
				DEBUG_INFO("Success!\n");
			}
		}
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
#define WORKSIZE 0x1000
		uint8_t *data = alloca(WORKSIZE);
		if (!data) {
			DEBUG_WARN("Can not malloc memory for flash read/verify "
					   "operation\n");
			res = -1;
			goto free_map;
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
					res = -1;
					goto free_map;
				}
				flash += worksize;
			} else if (read_file != -1) {
				int written = write(read_file, data, worksize);
				if (written < worksize) {
					DEBUG_WARN("Read failed at flash region 0x%08" PRIx32 "\n",
						   flash_src);
					res = -1;
					goto free_map;
				}
			}
			flash_src += worksize;
			size -= worksize;
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
