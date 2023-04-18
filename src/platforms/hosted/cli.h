/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2019 - 2021  Uwe Bonnes
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

/* This file implements the interface to command line command for PC-Hosted
 * platforms.
 */
#ifndef PLATFORMS_HOSTED_CLI_H
#define PLATFORMS_HOSTED_CLI_H

#include "cortexm.h"

typedef enum bmda_cli_mode {
	BMP_MODE_DEBUG,
	BMP_MODE_TEST,
	BMP_MODE_RESET,
	BMP_MODE_RESET_HW,
	BMP_MODE_FLASH_ERASE,
	BMP_MODE_FLASH_WRITE,
	BMP_MODE_FLASH_WRITE_VERIFY,
	BMP_MODE_FLASH_READ,
	BMP_MODE_FLASH_VERIFY,
	BMP_MODE_SWJ_TEST,
	BMP_MODE_MONITOR,
} bmda_cli_mode_e;

typedef enum bmp_scan_mode {
	BMP_SCAN_JTAG,
	BMP_SCAN_SWD,
	BMP_SCAN_AUTO
} bmp_scan_mode_e;

typedef struct bmda_cli_options {
	bmda_cli_mode_e opt_mode;
	bmp_scan_mode_e opt_scanmode;
	bool opt_tpwr;
	bool opt_list_only;
	bool opt_connect_under_reset;
	bool external_resistor_swd;
	bool fast_poll;
	bool opt_no_hl;
	char *opt_flash_file;
	char *opt_device;
	char *opt_serial;
	uint32_t opt_targetid;
	char *opt_ident_string;
	size_t opt_position;
	char *opt_cable;
	char *opt_monitor;
	uint32_t opt_target_dev;
	uint32_t opt_flash_start;
	uint32_t opt_max_swj_frequency;
	size_t opt_flash_size;
} bmda_cli_options_s;

void cl_init(bmda_cli_options_s *opt, int argc, char **argv);
int cl_execute(bmda_cli_options_s *opt);
bool serial_open(const bmda_cli_options_s *opt, const char *serial);
void serial_close(void);

#endif /* PLATFORMS_HOSTED_CLI_H */
