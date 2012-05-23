/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
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

/* This file implements a basic command interpreter for GDB 'monitor'
 * commands.
 * 
 * TODO: Add a mechanism for target driver so register new commands.
 */

#include <stdlib.h>
#include <string.h>

#include "general.h"

#include "command.h"
#include "gdb_packet.h"

#include "jtag_scan.h"
#include "target.h"

#include "adiv5.h"

#include <libopencm3/usb/usbd.h>

static void cmd_version(void);
static void cmd_help(void);

static void cmd_jtag_scan(void);
static void cmd_swdp_scan(void);
static void cmd_targets(void);
static void cmd_morse(void);

static void cmd_traceswo(void);

const struct command_s cmd_list[] = {
	{"version", (cmd_handler)cmd_version, "Display firmware version info"},
	{"help", (cmd_handler)cmd_help, "Display help for monitor commands"},
	{"jtag_scan", (cmd_handler)cmd_jtag_scan, "Scan JTAG chain for devices" },
	{"swdp_scan", (cmd_handler)cmd_swdp_scan, "Scan SW-DP for devices" },
	{"targets", (cmd_handler)cmd_targets, "Display list of available targets" },
	{"morse", (cmd_handler)cmd_morse, "Display morse error message" },
	{"traceswo", (cmd_handler)cmd_traceswo, "Start trace capture" },

	{NULL, NULL, NULL}
};


int command_process(char *cmd)
{
	const struct command_s *c;
	int argc = 0;
	const char **argv;

	/* Initial estimate for argc */
	for(char *s = cmd; *s; s++) 
		if((*s == ' ') || (*s == '\t')) argc++;

	argv = alloca(sizeof(const char *) * argc);

	/* Tokenize cmd to find argv */
	for(argc = 0, argv[argc] = strtok(cmd, " \t"); 
		argv[argc]; argv[++argc] = strtok(NULL, " \t"));

	/* Look for match and call handler */
	for(c = cmd_list; c->cmd; c++) {
		/* Accept a partial match as GDB does.
		 * So 'mon ver' will match 'monitor version' 
		 */
		if(!strncmp(argv[0], c->cmd, strlen(argv[0]))) {
			c->handler(argc, argv);
			return 0;
		}
	}

	return -1;
}

void cmd_version(void)
{
	gdb_out("Black Magic Probe (Firmware 1.5" VERSION_SUFFIX ", build " BUILDDATE ")\n");
	gdb_out("Copyright (C) 2011  Black Sphere Technologies Ltd.\n");
	gdb_out("License GPLv3+: GNU GPL version 3 or later "
		"<http://gnu.org/licenses/gpl.html>\n\n");
}

void cmd_help(void)
{
	const struct command_s *c;

	for(c = cmd_list; c->cmd; c++) 
		gdb_outf("%s -- %s\n", c->cmd, c->help);
}

void cmd_jtag_scan(void)
{
	gdb_outf("Target voltage: %s\n", platform_target_voltage());

	int devs = jtag_scan();

	if(devs < 0) {
		gdb_out("JTAG device scan failed!\n");
		return;
	} 
	if(devs == 0) {
		gdb_out("JTAG scan found no devices!\n");
		return;
	} 
	gdb_outf("Device  IR Len  IDCODE      Description\n");
	for(int i = 0; i < jtag_dev_count; i++)
		gdb_outf("%d\t%d\t0x%08lX  %s\n", i, 
			 jtag_devs[i].ir_len, jtag_devs[i].idcode, 
			 jtag_devs[i].descr);
	gdb_out("\n");
	cmd_targets();
}

void cmd_swdp_scan(void)
{
	gdb_outf("Target voltage: %s\n", platform_target_voltage());

	if(adiv5_swdp_scan() < 0) {
		gdb_out("SW-DP scan failed!\n");
		return;
	} 

	gdb_outf("SW-DP detected IDCODE: 0x%08X\n", adiv5_dp_list->idcode);

	cmd_targets();
}

void cmd_targets(void)
{
	struct target_s *t;
	int i;

	if(!target_list) {
		gdb_out("No usable targets found.\n");
		return;
	}
	
	gdb_out("Available Targets:\n");
	gdb_out("No. Att Driver\n");
	for(t = target_list, i = 1; t; t = t->next, i++)
		gdb_outf("%2d   %c  %s\n", i, t==cur_target?'*':' ', 
			 t->driver);
}

void cmd_morse(void)
{
	if(morse_msg) 
		gdb_outf("%s\n", morse_msg);
}

static void cmd_traceswo(void)
{
	extern char serial_no[9];
	traceswo_init();
	gdb_outf("%s:%02X:%02X\n", serial_no, 5, 0x85);
}

