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
#include <windows.h>
#else
#include <sys/mman.h>
#endif

typedef struct option getopt_option_s;

static void cl_target_printf(target_controller_s *tc, const char *fmt, va_list ap)
{
	(void)tc;

	vprintf(fmt, ap);
}

static target_controller_s cl_controller = {
	.printf = cl_target_printf,
};

typedef struct mmap_data {
	void *data;
	size_t size;
#if defined(_WIN32) || defined(__CYGWIN__)
	HANDLE hFile;
	HANDLE hMapFile;
#else
	size_t real_size;
	int fd;
#endif
} mmap_data_s;

static bool bmp_mmap(char *file, mmap_data_s *map)
{
#if defined(_WIN32) || defined(__CYGWIN__)
	map->hFile = CreateFile(file, GENERIC_WRITE | GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
	if (map->hFile == INVALID_HANDLE_VALUE) {
		DEBUG_ERROR("Open file %s failed: %s\n", file, strerror(errno));
		return false;
	}
	map->size = GetFileSize(map->hFile, NULL);
	map->hMapFile = CreateFileMapping(map->hFile, NULL, /* default security       */
		PAGE_READONLY,                                  /* read only access       */
		0,                                              /* max. object size high  */
		0,                                              /* max. object size low   */
		NULL);                                          /* name of mapping object */

	if (map->hMapFile == NULL || map->hMapFile == INVALID_HANDLE_VALUE) {
		DEBUG_ERROR("Map file %s failed: %s\n", file, strerror(errno));
		CloseHandle(map->hFile);
		return false;
	}
	map->data = MapViewOfFile(map->hMapFile, FILE_MAP_READ, 0, 0, 0);
	if (!map->data) {
		DEBUG_ERROR("Could not create file mapping object (%s).\n", strerror(errno));
		CloseHandle(map->hMapFile);
		return false;
	}
#else
	map->fd = open(file, O_RDONLY | O_BINARY);
	if (map->fd < 0) {
		DEBUG_ERROR("Open file %s failed: %s\n", file, strerror(errno));
		return false;
	}
	struct stat stat = {0};
	if (fstat(map->fd, &stat))
		return false;
	map->real_size = stat.st_size;
	map->size = stat.st_size;
	map->data = mmap(NULL, map->real_size, PROT_READ, MAP_PRIVATE, map->fd, 0);
#endif
	return true;
}

static void bmp_munmap(mmap_data_s *map)
{
#if defined(_WIN32) || defined(__CYGWIN__)
	UnmapViewOfFile(map->data);
	CloseHandle(map->hMapFile);
	CloseHandle(map->hFile);
#else
	/* Use the untainted 'real_size' here, 'size' may have been bounded to the flash size and we want to unmap the whole file */
	munmap(map->data, map->real_size);
#endif
}

static void cl_help(char **argv)
{
	bmp_ident(NULL);
	DEBUG_INFO("\n"
			   "Usage: %s [-h | -l | [-v BITMASK] [-O] [-d PATH | -P NUMBER | -s SERIAL | -c TYPE]\n"
			   "\t[-n NUMBER] [-j | -A] [-C] [-t | -T] [-e] [-p] [-R[h]] [-H] [-M STRING ...]\n"
			   "\t[-f | -m] [-E | -w | -V | -r] [-a ADDR] [-S number] [file]]\n"
			   "\n"
			   "The default is to start a debug server at localhost:2000\n\n"
			   "Single-shot and verbosity options [-h | -l | -v BITMASK]:\n"
			   "\t-h, --help       Show the version and this help, then exit\n"
			   "\t-l, --list       List available supported probes\n"
			   "\t-v, --verbose    Set the output verbosity level based on some combination of:\n"
			   "\t                   1 = INFO, 2 = GDB, 4 = TARGET, 8 = PROTO, 16 = PROBE, 32 = WIRE\n"
			   "\t-O, --no-stdout  Don't use stdout for debugging output, making it available\n"
			   "\t                   for use by RTT, Semihosting, or other target output\n"
			   "\n"
			   "Probe selection arguments [-d PATH | -P NUMBER | -s SERIAL | -c TYPE]:\n"
			   "\t-d, --device     Use a serial device at the given path\n"
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
			   "\t-F, --fast-poll  Poll the target for execution status at maximum speed at\n"
			   "\t                  the expense of increased CPU and USB resource utilisation.\n"
			   "\t-t, --list-chain Perform a chain scan and display information about the\n"
			   "\t                   connected devices\n"
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
		argv[0]);
	exit(0);
}

static const getopt_option_s long_options[] = {
	{"help", no_argument, NULL, 'h'},
	{"list", no_argument, NULL, 'l'},
	{"verbose", required_argument, NULL, 'v'},
	{"no-stdout", no_argument, NULL, 'O'},
	{"device", required_argument, NULL, 'd'},
	{"probe", required_argument, NULL, 'P'},
	{"serial", required_argument, NULL, 's'},
	{"ftdi-type", required_argument, NULL, 'c'},
	{"fast-poll", no_argument, NULL, 'F'},
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
	{NULL, 0, NULL, 0},
};

void cl_init(bmda_cli_options_s *opt, int argc, char **argv)
{
	opt->opt_target_dev = 1;
	opt->opt_flash_size = 0xffffffff;
	opt->opt_flash_start = 0xffffffff;
	opt->opt_max_swj_frequency = 4000000;
	opt->opt_scanmode = BMP_SCAN_SWD;
	opt->opt_mode = BMP_MODE_DEBUG;
	while (true) {
		const int option = getopt_long(argc, argv, "eEFhHv:Od:f:s:I:c:Cln:m:M:wVtTa:S:jApP:rR::", long_options, NULL);
		if (option == -1)
			break;

		switch (option) {
		case 'c':
			if (optarg) {
				char *opt_pointer = optarg;
				while (opt_pointer && *opt_pointer == ' ')
					opt_pointer++;
				opt->opt_cable = opt_pointer;
			}
			break;
		case 'h':
			bmda_debug_flags |= BMD_DEBUG_INFO;
			cl_help(argv);
			break;
		case 'H':
			opt->opt_no_hl = true;
			break;
		case 'v':
			if (optarg) {
				const char *end = optarg + strlen(optarg);
				char *valid = NULL;
				const uint16_t level = (strtoul(optarg, &valid, 10) << BMD_DEBUG_LEVEL_SHIFT) & BMD_DEBUG_LEVEL_MASK;
				if (valid != end) {
					DEBUG_ERROR("Value after verbosity flag was not a valid positive integer, got '%s'\n", optarg);
					exit(1);
				}
				bmda_debug_flags |= level;
			}
			break;
		case 'O':
			bmda_debug_flags |= BMD_DEBUG_USE_STDERR;
			break;
		case 'j':
			opt->opt_scanmode = BMP_SCAN_JTAG;
			break;
		case 'A':
			opt->opt_scanmode = BMP_SCAN_AUTO;
			break;
		case 'l':
			opt->opt_list_only = true;
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
		case 'F':
			opt->fast_poll = true;
			break;
		case 'f':
			if (optarg) {
				char *p;
				uint32_t frequency = strtol(optarg, &p, 10);
				switch (*p) {
				case 'k':
					frequency *= 1000U;
					break;
				case 'M':
					frequency *= 1000U * 1000U;
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
			bmda_debug_flags |= BMD_DEBUG_INFO;
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
				opt->opt_position = strtol(optarg, NULL, 0);
			break;
		case 'S':
			if (optarg) {
				char *endptr;
				opt->opt_flash_size = strtol(optarg, &endptr, 0);
				if (endptr) {
					switch (endptr[0]) {
					case 'k':
					case 'K':
						opt->opt_flash_size *= 1024U;
						break;
					case 'm':
					case 'M':
						opt->opt_flash_size *= 1024U * 1024U;
						break;
					}
				}
			}
		}
	}
	if (optind && argv[optind]) {
		if (opt->opt_mode == BMP_MODE_DEBUG)
			opt->opt_mode = BMP_MODE_FLASH_WRITE;
		opt->opt_flash_file = argv[optind];
	} else if (opt->opt_mode == BMP_MODE_DEBUG && opt->opt_monitor)
		opt->opt_mode = BMP_MODE_MONITOR; // To avoid DEBUG mode

	/* Checks */
	if (opt->opt_flash_file &&
		(opt->opt_mode == BMP_MODE_TEST || opt->opt_mode == BMP_MODE_SWJ_TEST || opt->opt_mode == BMP_MODE_RESET ||
			opt->opt_mode == BMP_MODE_RESET_HW)) {
		DEBUG_WARN("Ignoring filename in reset/test mode\n");
		opt->opt_flash_file = NULL;
	}
}

static void display_target(size_t idx, target_s *target, void *context)
{
	(void)context;
	const char *const core_name = target_core_name(target);
	if (strcmp(target_driver_name(target), "ARM Cortex-M") == 0)
		DEBUG_INFO("*** %2zu %c Unknown %s Designer %x Part ID %x %s\n", idx, target_attached(target) ? '*' : ' ',
			target_driver_name(target), target_designer(target), target_part_id(target), core_name ? core_name : "");
	else
		DEBUG_INFO("*** %2zu %c %s %s\n", idx, target_attached(target) ? '*' : ' ', target_driver_name(target),
			core_name ? core_name : "");
}

bool scan_for_targets(const bmda_cli_options_s *const opt)
{
	if (opt->opt_scanmode == BMP_SCAN_JTAG)
		return bmda_jtag_scan();
	if (opt->opt_scanmode == BMP_SCAN_SWD)
		return bmda_swd_scan(opt->opt_targetid);
	if (bmda_jtag_scan())
		return true;
	DEBUG_WARN("JTAG scan found no devices, trying SWD.\n");
	if (bmda_swd_scan(opt->opt_targetid))
		return true;
	DEBUG_ERROR("SW-DP scan failed!\n");
	return false;
}

int cl_execute(bmda_cli_options_s *opt)
{
	if (opt->opt_mode == BMP_MODE_RESET_HW) {
		platform_nrst_set_val(true);
		platform_delay(1);
		platform_nrst_set_val(false);
		return 0;
	}
	if (opt->opt_connect_under_reset)
		DEBUG_INFO("Connecting under reset\n");
	connect_assert_nrst = opt->opt_connect_under_reset;
	platform_nrst_set_val(opt->opt_connect_under_reset);
	if (opt->opt_mode == BMP_MODE_TEST)
		DEBUG_INFO("Running in Test Mode\n");
	DEBUG_INFO("Target voltage: %s Volt\n", platform_target_voltage());

	if (!scan_for_targets(opt)) {
		DEBUG_ERROR("No target found\n");
		return -1;
	}

	const size_t num_targets = target_foreach(display_target, NULL);
	if (opt->opt_target_dev > num_targets) {
		DEBUG_ERROR("Given target number %" PRIu32 " not available max %zu\n", opt->opt_target_dev, num_targets);
		return -1;
	}
	target_s *target = target_attach_n(opt->opt_target_dev, &cl_controller);

	int res = 0;
	int read_file = -1;
	if (!target) {
		DEBUG_ERROR("Can not attach to target %" PRIu32 "\n", opt->opt_target_dev);
		res = -1;
		goto target_detach;
	}

	/* List each defined RAM region */
	size_t ram_regions = 0;
	for (target_ram_s *ram = target->ram; ram; ram = ram->next)
		++ram_regions;

	for (size_t region = 0; region < ram_regions; ++region) {
		target_ram_s *ram = target->ram;
		for (size_t i = ram_regions - 1U; ram; ram = ram->next, --i) {
			if (region == i) {
				DEBUG_INFO("RAM   Start: 0x%08" PRIx32 " length = 0x%zx\n", ram->start, ram->length);
				break;
			}
		}
	}

	/* Always scan memory map to find lowest flash */
	/* List each defined Flash region */
	uint32_t lowest_flash_start = 0xffffffffU;
	size_t lowest_flash_size = 0;

	size_t flash_regions = 0;
	for (target_flash_s *flash = target->flash; flash; flash = flash->next) {
		++flash_regions;
		if (flash->start < lowest_flash_start) {
			lowest_flash_start = flash->start;
			lowest_flash_size = flash->length;
		}
	}

	for (size_t region = 0; region < flash_regions; ++region) {
		target_flash_s *flash = target->flash;
		for (size_t i = flash_regions - 1U; flash; flash = flash->next, --i) {
			if (region == i) {
				DEBUG_INFO("Flash Start: 0x%08" PRIx32 " length = 0x%zx blocksize 0x%zx\n", flash->start, flash->length,
					flash->blocksize);
				break;
			}
		}
	}

	if (opt->opt_flash_start == 0xffffffffU)
		opt->opt_flash_start = lowest_flash_start;
	if (opt->opt_flash_size == 0xffffffffU && opt->opt_mode != BMP_MODE_FLASH_WRITE &&
		opt->opt_mode != BMP_MODE_FLASH_VERIFY && opt->opt_mode != BMP_MODE_FLASH_WRITE_VERIFY)
		opt->opt_flash_size = lowest_flash_size;
	if (opt->opt_mode == BMP_MODE_SWJ_TEST) {
		/*
		 * XXX: This is bugprone - any core, even if it's not just a Cortex-M* that
		 * matches on the first letter triggers this
		 */
		if (target->core[0] == 'M') {
			DEBUG_WARN("Continuous read/write-back DEMCR. Abort with ^C\n");
			while (true) {
				uint32_t demcr;
				target_mem_read(target, &demcr, CORTEXM_DEMCR, 4);
				target_mem_write32(target, CORTEXM_DEMCR, demcr);
				platform_delay(1); /* To allow trigger */
			}
		} else
			DEBUG_ERROR("No test for this core type yet\n");
	}
	if (opt->opt_mode == BMP_MODE_TEST || opt->opt_mode == BMP_MODE_SWJ_TEST)
		goto target_detach;

	mmap_data_s map = {0};
	if (opt->opt_mode == BMP_MODE_FLASH_WRITE || opt->opt_mode == BMP_MODE_FLASH_VERIFY ||
		opt->opt_mode == BMP_MODE_FLASH_WRITE_VERIFY) {
		if (!bmp_mmap(opt->opt_flash_file, &map)) {
			DEBUG_ERROR("Can not map file: %s. Aborting!\n", strerror(errno));
			res = -1;
			goto target_detach;
		}
	} else if (opt->opt_mode == BMP_MODE_FLASH_READ) {
		/* Open as binary */
		read_file = open(opt->opt_flash_file, O_TRUNC | O_CREAT | O_RDWR | O_BINARY, S_IRUSR | S_IWUSR);
		if (read_file == -1) {
			DEBUG_ERROR("Error opening flashfile %s for read: %s\n", opt->opt_flash_file, strerror(errno));
			res = -1;
			goto target_detach;
		}
	}
	if (opt->opt_flash_size < map.size)
		/* restrict to size given on command line */
		map.size = opt->opt_flash_size;
	if (opt->opt_monitor) {
		res = command_process(target, opt->opt_monitor);
		if (res)
			DEBUG_ERROR("Command \"%s\" failed\n", opt->opt_monitor);
	}
	if (opt->opt_mode == BMP_MODE_RESET)
		target_reset(target);
	else if (opt->opt_mode == BMP_MODE_FLASH_ERASE) {
		DEBUG_INFO("Erase %zu bytes at 0x%08" PRIx32 "\n", opt->opt_flash_size, opt->opt_flash_start);
		if (!target_flash_erase(target, opt->opt_flash_start, opt->opt_flash_size)) {
			DEBUG_ERROR("Flash erase failed!\n");
			res = -1;
			goto free_map;
		}
		target_reset(target);
	} else if (opt->opt_mode == BMP_MODE_FLASH_WRITE || opt->opt_mode == BMP_MODE_FLASH_WRITE_VERIFY) {
		DEBUG_INFO("Erasing %zu bytes at 0x%08" PRIx32 "\n", map.size, opt->opt_flash_start);
		const uint32_t start_time = platform_time_ms();
		if (!target_flash_erase(target, opt->opt_flash_start, map.size)) {
			DEBUG_ERROR("Flash erase failed!\n");
			res = -1;
			goto free_map;
		}
		DEBUG_INFO("Flashing %zu bytes at 0x%08" PRIx32 "\n", map.size, opt->opt_flash_start);
		/* Buffered write cares for padding*/
		if (!target_flash_write(target, opt->opt_flash_start, map.data, map.size) || !target_flash_complete(target)) {
			DEBUG_ERROR("Flashing failed!\n");
			res = -1;
			goto free_map;
		}
		DEBUG_INFO("Success!\n");
		const uint32_t end_time = platform_time_ms();
		DEBUG_WARN(
			"Flash Write succeeded for %zu bytes, %8.3fkiB/s\n", map.size, (double)map.size / (end_time - start_time));
		if (opt->opt_mode != BMP_MODE_FLASH_WRITE_VERIFY) {
			target_reset(target);
			goto free_map;
		}
	}
	if (opt->opt_mode == BMP_MODE_FLASH_READ || opt->opt_mode == BMP_MODE_FLASH_VERIFY ||
		opt->opt_mode == BMP_MODE_FLASH_WRITE_VERIFY) {
#define WORKSIZE 0x1000U
		uint8_t data[WORKSIZE];
		if (opt->opt_mode == BMP_MODE_FLASH_READ)
			DEBUG_INFO("Reading flash from 0x%08" PRIx32 " for %zu bytes to %s\n", opt->opt_flash_start,
				opt->opt_flash_size, opt->opt_flash_file);
		const uint32_t flash_src = opt->opt_flash_start;
		const size_t size = opt->opt_mode == BMP_MODE_FLASH_READ ? opt->opt_flash_size : map.size;
		size_t bytes_read = 0;
		uint8_t *flash = (uint8_t *)map.data;
		const uint32_t start_time = platform_time_ms();
		for (size_t offset = 0; offset < size; offset += WORKSIZE) {
			const size_t worksize = MIN(size - offset, WORKSIZE);
			int n_read = target_mem_read(target, data, flash_src + offset, worksize);
			if (n_read) {
				if (opt->opt_flash_size == 0) /* we reached end of flash */
					DEBUG_INFO("Reached end of flash at size %" PRIu32 "\n", flash_src - opt->opt_flash_start);
				else
					DEBUG_ERROR("Read failed at flash address 0x%08" PRIx32 "\n", flash_src);
				break;
			}
			bytes_read += worksize;
			if (opt->opt_mode == BMP_MODE_FLASH_VERIFY || opt->opt_mode == BMP_MODE_FLASH_WRITE_VERIFY) {
				if (memcmp(data, flash + offset, worksize) != 0) {
					DEBUG_ERROR("Verify failed at flash region 0x%08" PRIx32 "\n", flash_src);
					res = -1;
					goto free_map;
				}
			} else if (read_file != -1) {
				const ssize_t written = write(read_file, data, worksize);
				if (written < 0) {
					const int error = errno;
					DEBUG_ERROR("Write to %s failed (%d): %s\n", opt->opt_flash_file, error, strerror(error));
					res = -1;
					goto free_map;
				}
				if ((size_t)written < worksize) {
					DEBUG_ERROR("Read failed at flash region 0x%08" PRIx32 "\n", flash_src);
					res = -1;
					goto free_map;
				}
			}
		}
		const uint32_t end_time = platform_time_ms();
		if (read_file != -1)
			close(read_file);
		DEBUG_WARN("Read/Verify succeeded for %zu bytes, %8.3fkiB/s\n", bytes_read,
			(double)bytes_read / (end_time - start_time));
		if (opt->opt_mode == BMP_MODE_FLASH_WRITE_VERIFY)
			target_reset(target);
	}
free_map:
	if (map.size)
		bmp_munmap(&map);
target_detach:
	if (read_file != -1)
		close(read_file);
	if (target)
		target_detach(target);
	target_list_free();
	return res;
}
