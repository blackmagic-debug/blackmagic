/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 * Copyright (C) 2021 Uwe Bonnes
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

/* This file implements a basic command interpreter for GDB 'monitor'
 * commands.
 */

#include "general.h"
#include "exception.h"
#include "command.h"
#include "gdb_packet.h"
#include "target.h"
#include "target_internal.h"
#include "morse.h"
#include "version.h"
#include "serialno.h"

#ifdef ENABLE_RTT
#include "rtt.h"
#endif

#ifdef PLATFORM_HAS_TRACESWO
#	include "traceswo.h"
#endif

#include <alloca.h>

static bool cmd_version(target *t, int argc, char **argv);
static bool cmd_help(target *t, int argc, char **argv);

static bool cmd_jtag_scan(target *t, int argc, char **argv);
static bool cmd_swdp_scan(target *t, int argc, char **argv);
static bool cmd_auto_scan(target *t, int argc, char **argv);
static bool cmd_frequency(target *t, int argc, char **argv);
static bool cmd_targets(target *t, int argc, char **argv);
static bool cmd_morse(target *t, int argc, char **argv);
static bool cmd_halt_timeout(target *t, int argc, const char **argv);
static bool cmd_connect_reset(target *t, int argc, const char **argv);
static bool cmd_reset(target *t, int argc, const char **argv);
#ifdef PLATFORM_HAS_POWER_SWITCH
static bool cmd_target_power(target *t, int argc, const char **argv);
#endif
#ifdef PLATFORM_HAS_TRACESWO
static bool cmd_traceswo(target *t, int argc, const char **argv);
#endif
static bool cmd_heapinfo(target *t, int argc, const char **argv);
#ifdef ENABLE_RTT
static bool cmd_rtt(target *t, int argc, const char **argv);
#endif
#if defined(PLATFORM_HAS_DEBUG) && (PC_HOSTED == 0)
static bool cmd_debug_bmp(target *t, int argc, const char **argv);
#endif

const struct command_s cmd_list[] = {
	{"version", (cmd_handler)cmd_version, "Display firmware version info"},
	{"help", (cmd_handler)cmd_help, "Display help for monitor commands"},
	{"jtag_scan", (cmd_handler)cmd_jtag_scan, "Scan JTAG chain for devices" },
	{"swdp_scan", (cmd_handler)cmd_swdp_scan, "Scan SW-DP for devices" },
	{"auto_scan", (cmd_handler)cmd_auto_scan, "Automatically scan all chain types for devices"},
	{"frequency", (cmd_handler)cmd_frequency, "set minimum high and low times" },
	{"targets", (cmd_handler)cmd_targets, "Display list of available targets" },
	{"morse", (cmd_handler)cmd_morse, "Display morse error message" },
	{"halt_timeout", (cmd_handler)cmd_halt_timeout, "Timeout (ms) to wait until Cortex-M is halted: (Default 2000)" },
	{"connect_rst", (cmd_handler)cmd_connect_reset, "Configure connect under reset: (enable|disable)" },
	{"reset", (cmd_handler)cmd_reset, "Pulse the nRST line - disconnects target" },
#ifdef PLATFORM_HAS_POWER_SWITCH
	{"tpwr", (cmd_handler)cmd_target_power, "Supplies power to the target: (enable|disable)"},
#endif
#ifdef ENABLE_RTT
	{"rtt", (cmd_handler)cmd_rtt, "enable|disable|status|channel 0..15|ident (str)|cblock|poll maxms minms maxerr" },
#endif
#ifdef PLATFORM_HAS_TRACESWO
#if defined TRACESWO_PROTOCOL && TRACESWO_PROTOCOL == 2
	{"traceswo", (cmd_handler)cmd_traceswo, "Start trace capture, NRZ mode: (baudrate) (decode channel ...)" },
#else
	{"traceswo", (cmd_handler)cmd_traceswo, "Start trace capture, Manchester mode: (decode channel ...)" },
#endif
#endif
	{"heapinfo", (cmd_handler)cmd_heapinfo, "Set semihosting heapinfo" },
#if defined(PLATFORM_HAS_DEBUG) && (PC_HOSTED == 0)
	{"debug_bmp", (cmd_handler)cmd_debug_bmp, "Output BMP \"debug\" strings to the second vcom: (enable|disable)"},
#endif
	{NULL, NULL, NULL}
};

bool connect_assert_nrst;
#if defined(PLATFORM_HAS_DEBUG) && (PC_HOSTED == 0)
bool debug_bmp;
#endif
unsigned cortexm_wait_timeout = 2000; /* Timeout to wait for Cortex to react on halt command. */

int command_process(target *t, char *cmd)
{
	const struct command_s *c;
	int argc = 1;
	const char **argv;
	const char *part;

	/* Initial estimate for argc */
	for(char *s = cmd; *s; s++)
		if((*s == ' ') || (*s == '\t')) argc++;

	/* This needs replacing with something more sensible.
	 * It should be pinging -Wvla among other things, and it failing is straight-up UB
	 */
	argv = alloca(sizeof(const char *) * argc);

	/* Tokenize cmd to find argv */
	argc = 0;
	for (part = strtok(cmd, " \t"); part; part = strtok(NULL, " \t"))
		argv[argc++] = part;

	/* Look for match and call handler */
	for(c = cmd_list; c->cmd; c++) {
		/* Accept a partial match as GDB does.
		 * So 'mon ver' will match 'monitor version'
		 */
		if ((argc == 0) || !strncmp(argv[0], c->cmd, strlen(argv[0])))
			return !c->handler(t, argc, argv);
	}

	if (!t)
		return -1;

	return target_command(t, argc, argv);
}

#define BOARD_IDENT "Black Magic Probe" PLATFORM_IDENT FIRMWARE_VERSION

bool cmd_version(target *t, int argc, char **argv)
{
	(void)t;
	(void)argc;
	(void)argv;
#if PC_HOSTED == 1
	char ident[256];
	gdb_ident(ident, sizeof(ident));
	DEBUG_WARN("%s\n", ident);
#else
	gdb_out(BOARD_IDENT);
	gdb_outf(", Hardware Version %d\n", platform_hwversion());
	gdb_out("Copyright (C) 2022 Black Magic Debug Project\n");
	gdb_out("License GPLv3+: GNU GPL version 3 or later "
		"<http://gnu.org/licenses/gpl.html>\n\n");
#endif

	return true;
}

bool cmd_help(target *t, int argc, char **argv)
{
	(void)argc;
	(void)argv;
	const struct command_s *c;

	if (!t || t->tc->destroy_callback) {
		gdb_out("General commands:\n");
		for(c = cmd_list; c->cmd; c++)
			gdb_outf("\t%s -- %s\n", c->cmd, c->help);
	}
	if (!t)
		return -1;

	target_command_help(t);

	return true;
}

static bool cmd_jtag_scan(target *t, int argc, char **argv)
{
	(void)t;
	uint8_t irlens[argc];

	if (platform_target_voltage())
		gdb_outf("Target voltage: %s\n", platform_target_voltage());

	if (argc > 1) {
		/* Accept a list of IR lengths on command line */
		for (int i = 1; i < argc; i++)
			irlens[i-1] = atoi(argv[i]);
		irlens[argc-1] = 0;
	}

	if (connect_assert_nrst)
		platform_nrst_set_val(true); /* will be deasserted after attach */

	int devs = -1;
	volatile struct exception e;
	TRY_CATCH(e, EXCEPTION_ALL) {
#if PC_HOSTED == 1
		devs = platform_jtag_scan(argc > 1 ? irlens : NULL);
#else
		devs = jtag_scan(argc > 1 ? irlens : NULL);
#endif
	}
	switch (e.type) {
	case EXCEPTION_TIMEOUT:
		gdb_outf("Timeout during scan. Is target stuck in WFI?\n");
		break;
	case EXCEPTION_ERROR:
		gdb_outf("Exception: %s\n", e.msg);
		break;
	}

	if (devs <= 0) {
		platform_nrst_set_val(false);
		gdb_out("JTAG device scan failed!\n");
		return false;
	}

	cmd_targets(NULL, 0, NULL);
	morse(NULL, false);
	return true;
}

bool cmd_swdp_scan(target *t, int argc, char **argv)
{
	(void)t;
	volatile uint32_t targetid = 0;
	if (argc > 1)
		targetid  = strtol(argv[1], NULL, 0);
	if (platform_target_voltage())
		gdb_outf("Target voltage: %s\n", platform_target_voltage());

	if (connect_assert_nrst)
		platform_nrst_set_val(true); /* will be deasserted after attach */

	int devs = -1;
	volatile struct exception e;
	TRY_CATCH(e, EXCEPTION_ALL) {
#if PC_HOSTED == 1
		devs = platform_adiv5_swdp_scan(targetid);
#else
		devs = adiv5_swdp_scan(targetid);
#endif
		}
	switch (e.type) {
	case EXCEPTION_TIMEOUT:
		gdb_outf("Timeout during scan. Is target stuck in WFI?\n");
		break;
	case EXCEPTION_ERROR:
		gdb_outf("Exception: %s\n", e.msg);
		break;
	}

	if(devs <= 0) {
		platform_nrst_set_val(false);
		gdb_out("SW-DP scan failed!\n");
		return false;
	}

	cmd_targets(NULL, 0, NULL);
	morse(NULL, false);
	return true;
}

bool cmd_auto_scan(target *t, int argc, char **argv)
{
	(void)t;
	(void)argc;
	(void)argv;

	if (platform_target_voltage())
		gdb_outf("Target voltage: %s\n", platform_target_voltage());
	if (connect_assert_nrst)
		platform_nrst_set_val(true); /* will be deasserted after attach */

	int devs = -1;
	volatile struct exception e;
	TRY_CATCH(e, EXCEPTION_ALL) {
#if PC_HOSTED == 1
		devs = platform_jtag_scan(NULL);
#else
		devs = jtag_scan(NULL);
#endif
		if (devs > 0)
			break;
		gdb_out("JTAG scan found no devices, trying SWD!\n");

#if PC_HOSTED == 1
		devs = platform_adiv5_swdp_scan(0);
#else
		devs = adiv5_swdp_scan(0);
#endif
		if (devs > 0)
			break;

		platform_nrst_set_val(false);
		gdb_out("SW-DP scan failed!\n");
		return false;
	}
	switch (e.type) {
	case EXCEPTION_TIMEOUT:
		gdb_outf("Timeout during scan. Is target stuck in WFI?\n");
		break;
	case EXCEPTION_ERROR:
		gdb_outf("Exception: %s\n", e.msg);
		break;
	}
	if (devs <= 0) {
		platform_nrst_set_val(false);
		gdb_out("auto scan failed!\n");
		return false;
	}

	cmd_targets(NULL, 0, NULL);
	morse(NULL, false);
	return true;
}

bool cmd_frequency(target *t, int argc, char **argv)
{
	(void)t;
	if (argc == 2) {
		char *p;
		uint32_t frequency = strtol(argv[1], &p, 10);
		switch (*p) {
		case 'k':
			frequency *= 1000;
			break;
		case 'M':
			frequency *= 1000*1000;
			break;
		}
		platform_max_frequency_set(frequency);
	}
	uint32_t freq = platform_max_frequency_get();
	if (freq == FREQ_FIXED)
		gdb_outf("SWJ freq fixed\n");
	else
		gdb_outf("Max SWJ freq %08" PRIx32 "\n", freq);
	return true;

}

static void display_target(int i, target *t, void *context)
{
	(void)context;
	if (!strcmp(target_driver_name(t), "ARM Cortex-M")) {
		gdb_outf("***%2d%sUnknown %s Designer 0x%03x Partno 0x%03x %s\n",
				 i, target_attached(t)?" * ":" ",
				 target_driver_name(t),
				 target_designer(t),
				 target_idcode(t),
				 (target_core_name(t)) ? target_core_name(t): "");
	} else {
		gdb_outf("%2d   %c  %s %s\n", i, target_attached(t)?'*':' ',
				 target_driver_name(t),
				 (target_core_name(t)) ? target_core_name(t): "");
	}
}

bool cmd_targets(target *t, int argc, char **argv)
{
	(void)t;
	(void)argc;
	(void)argv;
	gdb_out("Available Targets:\n");
	gdb_out("No. Att Driver\n");
	if (!target_foreach(display_target, NULL)) {
		gdb_out("No usable targets found.\n");
		return false;
	}

	return true;
}

bool cmd_morse(target *t, int argc, char **argv)
{
	(void)t;
	(void)argc;
	(void)argv;
	if (morse_msg) {
		gdb_outf("%s\n", morse_msg);
		DEBUG_WARN("%s\n", morse_msg);
	}
	return true;
}

bool parse_enable_or_disable(const char *s, bool *out) {
	if (strlen(s) == 0) {
		gdb_outf("'enable' or 'disable' argument must be provided\n");
		return false;
	} else if (!strncmp(s, "enable", strlen(s))) {
		*out = true;
		return true;
	} else if (!strncmp(s, "disable", strlen(s))) {
		*out = false;
		return true;
	} else {
		gdb_outf("Argument '%s' not recognized as 'enable' or 'disable'\n", s);
		return false;
	}
}

static bool cmd_connect_reset(target *t, int argc, const char **argv)
{
	(void)t;
	bool print_status = false;
	if (argc == 1) {
		print_status = true;
	} else if (argc == 2) {
		if (parse_enable_or_disable(argv[1], &connect_assert_nrst)) {
			print_status = true;
		}
	} else {
		gdb_outf("Unrecognized command format\n");
	}

	if (print_status) {
		gdb_outf("Assert nRST during connect: %s\n",
			 connect_assert_nrst ? "enabled" : "disabled");
	}
	return true;
}

static bool cmd_halt_timeout(target *t, int argc, const char **argv)
{
	(void)t;
	if (argc > 1)
		cortexm_wait_timeout = atol(argv[1]);
	gdb_outf("Cortex-M timeout to wait for device haltes: %d\n",
				 cortexm_wait_timeout);
	return true;
}

static bool cmd_reset(target *t, int argc, const char **argv)
{
	(void)t;
	(void)argc;
	(void)argv;
	target_list_free();
	platform_nrst_set_val(true);
	platform_nrst_set_val(false);
	return true;
}

#ifdef PLATFORM_HAS_POWER_SWITCH
static bool cmd_target_power(target *t, int argc, const char **argv)
{
	(void)t;
	if (argc == 1) {
		gdb_outf("Target Power: %s\n",
			 platform_target_get_power() ? "enabled" : "disabled");
	} else if (argc == 2) {
		bool want_enable = false;
		if (parse_enable_or_disable(argv[1], &want_enable)) {
			if (want_enable
				&& !platform_target_get_power()
				&& platform_target_voltage_sense() > POWER_CONFLICT_THRESHOLD) {
				/* want to enable target power, but VREF > 0.5V sensed -> cancel */
				gdb_outf("Target already powered (%s)\n", platform_target_voltage());
			} else {
				platform_target_set_power(want_enable);
				gdb_outf("%s target power\n", want_enable ? "Enabling" : "Disabling");
			}
		}
	} else {
		gdb_outf("Unrecognized command format\n");
	}
	return true;
}
#endif

#ifdef ENABLE_RTT
const char* onoroffstr[2] = {"off", "on"};
static const char* onoroff(bool bval) {
	return bval ? onoroffstr[1] : onoroffstr[0];
}

static bool cmd_rtt(target *t, int argc, const char **argv)
{
	(void)t;
	if ((argc == 1) || ((argc == 2) && !strncmp(argv[1], "enabled", strlen(argv[1])))) {
		rtt_enabled = true;
		rtt_found = false;
	}
	else if ((argc == 2) && !strncmp(argv[1], "disabled", strlen(argv[1]))) {
		rtt_enabled = false;
		rtt_found = false;
	}
	else if ((argc == 2) && !strncmp(argv[1], "status", strlen(argv[1]))) {
		gdb_outf("rtt: %s found: %s ident: ",
			onoroff(rtt_enabled), rtt_found ? "yes" : "no");
		if (rtt_ident[0] == '\0')
			gdb_out("off");
		else
			gdb_outf("\"%s\"", rtt_ident);
		gdb_outf(" halt: %s", onoroff(target_no_background_memory_access(t)));
		gdb_out(" channels: ");
		if (rtt_auto_channel) gdb_out("auto ");
		for (uint32_t i = 0; i < MAX_RTT_CHAN; i++)
			if (rtt_channel[i].is_enabled) gdb_outf("%d ", i);
		gdb_outf("\nmax poll ms: %u min poll ms: %u max errs: %u\n",
			rtt_max_poll_ms, rtt_min_poll_ms, rtt_max_poll_errs);
	}
	else if ((argc >= 2) && !strncmp(argv[1], "channel", strlen(argv[1]))) {
		/* mon rtt channel switches to auto rtt channel selection
		   mon rtt channel number... selects channels given */
		for (uint32_t i = 0; i < MAX_RTT_CHAN; i++)
			rtt_channel[i].is_enabled = false;
		if (argc == 2) {
			rtt_auto_channel = true;
		} else {
			rtt_auto_channel = false;
			for (int i = 2; i < argc; i++) {
				int chan = atoi(argv[i]);
				if ((chan >= 0) && (chan < MAX_RTT_CHAN))
					rtt_channel[chan].is_enabled = true;
			}
		}
	}
	else if ((argc == 2) && !strncmp(argv[1], "ident", strlen(argv[1]))) {
		rtt_ident[0] = '\0';
	}
	else if ((argc == 2) && !strncmp(argv[1], "poll", strlen(argv[1])))
		gdb_outf("%u %u %u\n", rtt_max_poll_ms, rtt_min_poll_ms, rtt_max_poll_errs);
	else if ((argc == 2) && !strncmp(argv[1], "cblock", strlen(argv[1]))) {
		gdb_outf("cbaddr: 0x%x\n", rtt_cbaddr);
		gdb_out("ch ena cfg i/o buf@        size head@      tail@      flg\n");
		for (uint32_t i = 0; i < MAX_RTT_CHAN; i++) {
			gdb_outf("%2d   %c   %c %s 0x%08x %5d 0x%08x 0x%08x   %d\n",
			i, rtt_channel[i].is_enabled ? 'y' : 'n', rtt_channel[i].is_configured ? 'y' : 'n',
			rtt_channel[i].is_output ? "out" : "in ", rtt_channel[i].buf_addr, rtt_channel[i].buf_size,
			rtt_channel[i].head_addr, rtt_channel[i].tail_addr, rtt_channel[i].flag);
		}
	}
	else if ((argc == 3) && !strncmp(argv[1], "ident", strlen(argv[1]))) {
		strncpy(rtt_ident, argv[2], sizeof(rtt_ident));
		rtt_ident[sizeof(rtt_ident)-1] = '\0';
		for (uint32_t i = 0; i < sizeof(rtt_ident); i++)
			if (rtt_ident[i] == '_') rtt_ident[i] = ' ';
	}
	else if ((argc == 5) && !strncmp(argv[1], "poll", strlen(argv[1]))) {
		/* set polling params */
		int32_t new_max_poll_ms = atoi(argv[2]);
		int32_t new_min_poll_ms = atoi(argv[3]);
		int32_t new_max_poll_errs = atoi(argv[4]);
		if ((new_max_poll_ms >= 0) && (new_min_poll_ms >= 0) && (new_max_poll_errs >= 0)
			 && (new_max_poll_ms >= new_min_poll_ms)) {
			rtt_max_poll_ms = new_max_poll_ms;
			rtt_min_poll_ms = new_min_poll_ms;
			rtt_max_poll_errs = new_max_poll_errs;
		}
		else gdb_out("how?\n");
	}
	else gdb_out("what?\n");
	return true;
}
#endif

#ifdef PLATFORM_HAS_TRACESWO
static bool cmd_traceswo(target *t, int argc, const char **argv)
{
	char serial_no[DFU_SERIAL_LENGTH];
	(void)t;
#if TRACESWO_PROTOCOL == 2
	uint32_t baudrate = SWO_DEFAULT_BAUD;
#endif
	uint32_t swo_channelmask = 0; /* swo decoding off */
	uint8_t decode_arg = 1;
#if TRACESWO_PROTOCOL == 2
	/* argument: optional baud rate for async mode */
	if ((argc > 1) && (*argv[1] >= '0') && (*argv[1] <= '9')) {
		baudrate = atoi(argv[1]);
		if (baudrate == 0) baudrate = SWO_DEFAULT_BAUD;
		decode_arg = 2;
	}
#endif
	/* argument: 'decode' literal */
	if((argc > decode_arg) &&  !strncmp(argv[decode_arg], "decode", strlen(argv[decode_arg]))) {
		swo_channelmask = 0xFFFFFFFF; /* decoding all channels */
		/* arguments: channels to decode */
		if (argc > decode_arg + 1) {
			swo_channelmask = 0x0;
			for (int i = decode_arg+1; i < argc; i++) { /* create bitmask of channels to decode */
				int channel = atoi(argv[i]);
				if ((channel >= 0) && (channel <= 31))
					swo_channelmask |= (uint32_t)0x1 << channel;
			}
		}
	}
#if defined(PLATFORM_HAS_DEBUG) && (PC_HOSTED == 0) && defined(ENABLE_DEBUG)
	if (debug_bmp) {
#if TRACESWO_PROTOCOL == 2
		gdb_outf("baudrate: %lu ", baudrate);
#endif
		gdb_outf("channel mask: ");
		for (int8_t i=31;i>=0;i--) {
			uint8_t bit = (swo_channelmask >> i) & 0x1;
			gdb_outf("%u", bit);
		}
		gdb_outf("\n");
	}
#endif
#if TRACESWO_PROTOCOL == 2
	traceswo_init(baudrate, swo_channelmask);
#else
	traceswo_init(swo_channelmask);
#endif
	serial_no_read(serial_no);
	gdb_outf("%s:%02X:%02X\n", serial_no, 5, 0x85);
	return true;
}
#endif

#if defined(PLATFORM_HAS_DEBUG) && (PC_HOSTED == 0)
static bool cmd_debug_bmp(target *t, int argc, const char **argv)
{
	(void)t;
	bool print_status = false;
	if (argc == 1) {
		print_status = true;
	} else if (argc == 2) {
		if (parse_enable_or_disable(argv[1], &debug_bmp)) {
			print_status = true;
		}
	} else {
		gdb_outf("Unrecognized command format\n");
	}

	if (print_status) {
		gdb_outf("Debug mode is %s\n",
			 debug_bmp ? "enabled" : "disabled");
	}
	return true;
}
#endif
static bool cmd_heapinfo(target *t, int argc, const char **argv)
{
	if (t == NULL) gdb_out("not attached\n");
	else if (argc == 5) {
		target_addr heap_base = strtoul(argv[1], NULL, 16);
		target_addr heap_limit = strtoul(argv[2], NULL, 16);
		target_addr stack_base = strtoul(argv[3], NULL, 16);
		target_addr stack_limit = strtoul(argv[4], NULL, 16);
		gdb_outf("heapinfo heap_base: %p heap_limit: %p stack_base: %p stack_limit: %p\n",
			heap_base, heap_limit, stack_base, stack_limit);
		target_set_heapinfo(t, heap_base, heap_limit, stack_base, stack_limit);
	} else gdb_outf("heapinfo heap_base heap_limit stack_base stack_limit\n");
	return true;
}
