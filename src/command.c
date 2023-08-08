/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011 Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 * Copyright (C) 2021 Uwe Bonnes (bon@elektron.ikp.physik.tu-darmstadt.de)
 * Copyright (C) 2023 1BitSquared <info@1bitsquared.com>
 * Modified by Rachel Mant <git@dragonmux.network>
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
#include "platform.h"
#include "exception.h"
#include "command.h"
#include "gdb_packet.h"
#include "target.h"
#include "target_internal.h"
#include "morse.h"
#include "version.h"
#include "serialno.h"
#include "jtagtap.h"
#include "jtag_scan.h"

#ifdef ENABLE_RTT
#include "rtt.h"
#endif

#ifdef PLATFORM_HAS_TRACESWO
#include "traceswo.h"
#endif

#if defined(_WIN32)
#include <malloc.h>
#else
#include <alloca.h>
#endif

static bool cmd_version(target_s *t, int argc, const char **argv);
static bool cmd_help(target_s *t, int argc, const char **argv);

static bool cmd_jtag_scan(target_s *target, int argc, const char **argv);
static bool cmd_swd_scan(target_s *t, int argc, const char **argv);
static bool cmd_auto_scan(target_s *t, int argc, const char **argv);
static bool cmd_frequency(target_s *t, int argc, const char **argv);
static bool cmd_targets(target_s *t, int argc, const char **argv);
static bool cmd_morse(target_s *t, int argc, const char **argv);
static bool cmd_halt_timeout(target_s *t, int argc, const char **argv);
static bool cmd_connect_reset(target_s *t, int argc, const char **argv);
static bool cmd_reset(target_s *t, int argc, const char **argv);
static bool cmd_tdi_low_reset(target_s *t, int argc, const char **argv);
#ifdef PLATFORM_HAS_POWER_SWITCH
static bool cmd_target_power(target_s *t, int argc, const char **argv);
#endif
#ifdef PLATFORM_HAS_TRACESWO
static bool cmd_traceswo(target_s *t, int argc, const char **argv);
#endif
static bool cmd_heapinfo(target_s *t, int argc, const char **argv);
#ifdef ENABLE_RTT
static bool cmd_rtt(target_s *t, int argc, const char **argv);
#endif
#if defined(PLATFORM_HAS_DEBUG) && PC_HOSTED == 0
static bool cmd_debug_bmp(target_s *t, int argc, const char **argv);
#endif
#if PC_HOSTED == 1
static bool cmd_shutdown_bmda(target_s *t, int argc, const char **argv);
#endif

const command_s cmd_list[] = {
	{"version", cmd_version, "Display firmware version info"},
	{"help", cmd_help, "Display help for monitor commands"},
	{"jtag_scan", cmd_jtag_scan, "Scan JTAG chain for devices"},
	{"swd_scan", cmd_swd_scan, "Scan SWD interface for devices: [TARGET_ID]"},
	{"swdp_scan", cmd_swd_scan, "Deprecated: use swd_scan instead"},
	{"auto_scan", cmd_auto_scan, "Automatically scan all chain types for devices"},
	{"frequency", cmd_frequency, "set minimum high and low times: [FREQ]"},
	{"targets", cmd_targets, "Display list of available targets"},
	{"morse", cmd_morse, "Display morse error message"},
	{"halt_timeout", cmd_halt_timeout, "Timeout to wait until Cortex-M is halted: [TIMEOUT, default 2000ms]"},
	{"connect_rst", cmd_connect_reset, "Configure connect under reset: [enable|disable]"},
	{"reset", cmd_reset, "Pulse the nRST line - disconnects target: [PULSE_LEN, default 0ms]"},
	{"tdi_low_reset", cmd_tdi_low_reset,
		"Pulse nRST with TDI set low to attempt to wake certain targets up (eg LPC82x)"},
#ifdef PLATFORM_HAS_POWER_SWITCH
	{"tpwr", cmd_target_power, "Supplies power to the target: [enable|disable]"},
#endif
#ifdef ENABLE_RTT
	{"rtt", cmd_rtt,
		"[enable|disable|status|channel [0..15 ...]|ident [STR]|cblock|ram [RAM_START RAM_END]|poll [MAXMS MINMS "
		"MAXERR]]"},
#endif
#ifdef PLATFORM_HAS_TRACESWO
#if defined TRACESWO_PROTOCOL && TRACESWO_PROTOCOL == 2
	{"traceswo", cmd_traceswo, "Start trace capture, NRZ mode: [BAUDRATE] [decode [CHANNEL_NR ...]]"},
#else
	{"traceswo", cmd_traceswo, "Start trace capture, Manchester mode: [decode [CHANNEL_NR ...]]"},
#endif
#endif
	{"heapinfo", cmd_heapinfo, "Set semihosting heapinfo: HEAPINFO HEAP_BASE HEAP_LIMIT STACK_BASE STACK_LIMIT"},
#if defined(PLATFORM_HAS_DEBUG) && PC_HOSTED == 0
	{"debug_bmp", cmd_debug_bmp, "Output BMP \"debug\" strings to the second vcom: [enable|disable]"},
#endif
#if PC_HOSTED == 1
	{"shutdown_bmda", cmd_shutdown_bmda, "Tell the BMDA server to shut down when the GDB connection closes"},
#endif
	{NULL, NULL, NULL},
};

bool connect_assert_nrst;
#if defined(PLATFORM_HAS_DEBUG) && PC_HOSTED == 0
bool debug_bmp;
#endif
unsigned cortexm_wait_timeout = 2000; /* Timeout to wait for Cortex to react on halt command. */

int command_process(target_s *const t, char *const cmd_buffer)
{
	/* Initial estimate for argc */
	size_t argc = 1;
	for (size_t i = 0; i < strlen(cmd_buffer); ++i) {
		if (cmd_buffer[i] == ' ' || cmd_buffer[i] == '\t')
			++argc;
	}

	/* This needs replacing with something more sensible.
	 * It should be pinging -Wvla among other things, and it failing is straight-up UB
	 */
	const char **const argv = alloca(sizeof(const char *) * argc);

	/* Tokenize cmd_buffer to find argv */
	argc = 0;
	/* Reentrant strtok needs a state pointer to the unprocessed part */
	char *token_state = NULL;
	for (const char *part = strtok_r(cmd_buffer, " \t", &token_state); part; part = strtok_r(NULL, " \t", &token_state))
		argv[argc++] = part;

	/* Look for match and call handler */
	for (const command_s *cmd = cmd_list; cmd->cmd; ++cmd) {
		/* Accept a partial match as GDB does.
		 * So 'mon ver' will match 'monitor version'
		 */
		if ((argc == 0) || !strncmp(argv[0], cmd->cmd, strlen(argv[0])))
			return !cmd->handler(t, argc, argv);
	}

	if (!t)
		return -1;
	return target_command(t, argc, argv);
}

#define BOARD_IDENT "Black Magic Probe " PLATFORM_IDENT "" FIRMWARE_VERSION

bool cmd_version(target_s *t, int argc, const char **argv)
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

bool cmd_help(target_s *t, int argc, const char **argv)
{
	(void)argc;
	(void)argv;

	if (!t || t->tc->destroy_callback) {
		gdb_out("General commands:\n");
		for (const command_s *cmd = cmd_list; cmd->cmd; cmd++)
			gdb_outf("\t%s -- %s\n", cmd->cmd, cmd->help);
		if (!t)
			return true;
	}

	target_command_help(t);
	return true;
}

static bool cmd_jtag_scan(target_s *target, int argc, const char **argv)
{
	(void)target;
	(void)argc;
	(void)argv;

	if (platform_target_voltage())
		gdb_outf("Target voltage: %s\n", platform_target_voltage());

	if (connect_assert_nrst)
		platform_nrst_set_val(true); /* will be deasserted after attach */

	bool scan_result = false;
	volatile exception_s e;
	TRY_CATCH (e, EXCEPTION_ALL) {
#if PC_HOSTED == 1
		scan_result = bmda_jtag_scan();
#else
		scan_result = jtag_scan();
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

	if (!scan_result) {
		platform_target_clk_output_enable(false);
		platform_nrst_set_val(false);
		gdb_out("JTAG device scan failed!\n");
		return false;
	}

	cmd_targets(NULL, 0, NULL);
	platform_target_clk_output_enable(false);
	morse(NULL, false);
	return true;
}

bool cmd_swd_scan(target_s *t, int argc, const char **argv)
{
	(void)t;
	volatile uint32_t targetid = 0;
	if (argc > 1)
		targetid = strtoul(argv[1], NULL, 0);
	if (platform_target_voltage())
		gdb_outf("Target voltage: %s\n", platform_target_voltage());

	if (connect_assert_nrst)
		platform_nrst_set_val(true); /* will be deasserted after attach */

	bool scan_result = false;
	volatile exception_s e;
	TRY_CATCH (e, EXCEPTION_ALL) {
#if PC_HOSTED == 1
		scan_result = bmda_swd_scan(targetid);
#else
		scan_result = adiv5_swd_scan(targetid);
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

	if (!scan_result) {
		platform_target_clk_output_enable(false);
		platform_nrst_set_val(false);
		gdb_out("SW-DP scan failed!\n");
		return false;
	}

	cmd_targets(NULL, 0, NULL);
	platform_target_clk_output_enable(false);
	morse(NULL, false);
	return true;
}

bool cmd_auto_scan(target_s *t, int argc, const char **argv)
{
	(void)t;
	(void)argc;
	(void)argv;

	if (platform_target_voltage())
		gdb_outf("Target voltage: %s\n", platform_target_voltage());
	if (connect_assert_nrst)
		platform_nrst_set_val(true); /* will be deasserted after attach */

	bool scan_result = false;
	volatile exception_s e;
	TRY_CATCH (e, EXCEPTION_ALL) {
#if PC_HOSTED == 1
		scan_result = bmda_jtag_scan();
#else
		scan_result = jtag_scan();
#endif
		if (scan_result)
			break;
		gdb_out("JTAG scan found no devices, trying SWD!\n");

#if PC_HOSTED == 1
		scan_result = bmda_swd_scan(0);
#else
		scan_result = adiv5_swd_scan(0);
#endif
		if (scan_result)
			break;

		gdb_out("SW-DP scan found no devices.\n");
	}
	switch (e.type) {
	case EXCEPTION_TIMEOUT:
		gdb_outf("Timeout during scan. Is target stuck in WFI?\n");
		break;
	case EXCEPTION_ERROR:
		gdb_outf("Exception: %s\n", e.msg);
		break;
	}

	if (!scan_result) {
		platform_target_clk_output_enable(false);
		platform_nrst_set_val(false);
		gdb_out("auto scan failed!\n");
		return false;
	}

	cmd_targets(NULL, 0, NULL);
	platform_target_clk_output_enable(false);
	morse(NULL, false);
	return true;
}

bool cmd_frequency(target_s *t, int argc, const char **argv)
{
	(void)t;
	if (argc == 2) {
		char *multiplier = NULL;
		uint32_t frequency = strtoul(argv[1], &multiplier, 10);
		if (!multiplier) {
			gdb_outf("Frequency must be an integral value possibly followed by 'k' or 'M'");
			return false;
		}
		switch (*multiplier) {
		case 'k':
			frequency *= 1000U;
			break;
		case 'M':
			frequency *= 1000U * 1000U;
			break;
		}
		platform_max_frequency_set(frequency);
	}
	const uint32_t freq = platform_max_frequency_get();
	if (freq == FREQ_FIXED)
		gdb_outf("SWJ freq fixed\n");
	else
		gdb_outf("Current SWJ freq %" PRIu32 "Hz\n", freq);
	return true;
}

static void display_target(size_t idx, target_s *target, void *context)
{
	(void)context;
	const char attached = target_attached(target) ? '*' : ' ';
	const char *const core_name = target_core_name(target);
	if (!strcmp(target_driver_name(target), "ARM Cortex-M"))
		gdb_outf("***%2u %c Unknown %s Designer 0x%x Part ID 0x%x %s\n", (unsigned)idx, attached,
			target_driver_name(target), target_designer(target), target_part_id(target), core_name ? core_name : "");
	else
		gdb_outf("%2u %3c  %s %s\n", (unsigned)idx, attached, target_driver_name(target), core_name ? core_name : "");
}

bool cmd_targets(target_s *target, int argc, const char **argv)
{
	(void)target;
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

bool cmd_morse(target_s *t, int argc, const char **argv)
{
	(void)t;
	(void)argc;
	(void)argv;
	if (morse_msg) {
		gdb_outf("%s\n", morse_msg);
		DEBUG_WARN("%s\n", morse_msg);
	} else
		gdb_out("No message\n");
	return true;
}

bool parse_enable_or_disable(const char *value, bool *out)
{
	const size_t value_len = strlen(value);
	if (value_len && !strncmp(value, "enable", value_len))
		*out = true;
	else if (value_len && !strncmp(value, "disable", value_len))
		*out = false;
	else {
		gdb_out("'enable' or 'disable' argument must be provided\n");
		return false;
	}
	return true;
}

static bool cmd_connect_reset(target_s *t, int argc, const char **argv)
{
	(void)t;
	bool print_status = false;
	if (argc == 1)
		print_status = true;
	else if (argc == 2) {
		if (parse_enable_or_disable(argv[1], &connect_assert_nrst))
			print_status = true;
	} else
		gdb_out("Unrecognized command format\n");

	if (print_status) {
		gdb_outf("Assert nRST during connect: %s\n", connect_assert_nrst ? "enabled" : "disabled");
	}
	return true;
}

static bool cmd_halt_timeout(target_s *t, int argc, const char **argv)
{
	(void)t;
	if (argc > 1)
		cortexm_wait_timeout = strtoul(argv[1], NULL, 0);
	gdb_outf("Cortex-M timeout to wait for device halts: %d\n", cortexm_wait_timeout);
	return true;
}

static bool cmd_reset(target_s *t, int argc, const char **argv)
{
	(void)t;
	uint32_t pulse_len_ms = 0;
	if (argc > 1)
		pulse_len_ms = strtoul(argv[1], NULL, 0);
	target_list_free();
	platform_nrst_set_val(true);
	platform_delay(pulse_len_ms);
	platform_nrst_set_val(false);
	return true;
}

static bool cmd_tdi_low_reset(target_s *t, int argc, const char **argv)
{
	(void)t;
	(void)argc;
	(void)argv;
	jtag_proc.jtagtap_next(true, false);
	cmd_reset(NULL, 0, NULL);
	return true;
}

#ifdef PLATFORM_HAS_POWER_SWITCH
static bool cmd_target_power(target_s *t, int argc, const char **argv)
{
	(void)t;
	if (argc == 1)
		gdb_outf("Target Power: %s\n", platform_target_get_power() ? "enabled" : "disabled");
	else if (argc == 2) {
		bool want_enable = false;
		if (parse_enable_or_disable(argv[1], &want_enable)) {
			if (want_enable && !platform_target_get_power() &&
				platform_target_voltage_sense() > POWER_CONFLICT_THRESHOLD) {
				/* want to enable target power, but VREF > 0.5V sensed -> cancel */
				gdb_outf("Target already powered (%s)\n", platform_target_voltage());
			} else {
				if (!platform_target_set_power(want_enable))
					DEBUG_ERROR("%s target power failed\n", want_enable ? "Enabling" : "Disabling");
				gdb_outf("%s target power\n", want_enable ? "Enabling" : "Disabling");
			}
		}
	} else
		gdb_outf("Unrecognized command format\n");
	return true;
}
#endif

#ifdef ENABLE_RTT
static const char *on_or_off(const bool value)
{
	return value ? "on" : "off";
}

static bool cmd_rtt(target_s *t, int argc, const char **argv)
{
	(void)t;
	const size_t command_len = argc > 1 ? strlen(argv[1]) : 0;
	if (argc == 1 || (argc == 2 && strncmp(argv[1], "enabled", command_len) == 0)) {
		rtt_enabled = true;
		rtt_found = false;
		memset(rtt_channel, 0, sizeof(rtt_channel));
	} else if (argc == 2 && strncmp(argv[1], "disabled", command_len) == 0) {
		rtt_enabled = false;
		rtt_found = false;
	} else if (argc == 2 && strncmp(argv[1], "status", command_len) == 0) {
		gdb_outf("rtt: %s found: %s ident: ", on_or_off(rtt_enabled), rtt_found ? "yes" : "no");
		if (rtt_ident[0] == '\0')
			gdb_out("off");
		else
			gdb_outf("\"%s\"", rtt_ident);
		gdb_outf(" halt: %s", on_or_off(target_mem_access_needs_halt(t)));
		gdb_out(" channels: ");
		if (rtt_auto_channel)
			gdb_out("auto ");
		for (size_t i = 0; i < MAX_RTT_CHAN; i++) {
			if (rtt_channel_enabled[i])
				gdb_outf("%" PRIu32 " ", (uint32_t)i);
		}
		if (rtt_flag_ram)
			gdb_outf("ram: 0x%08" PRIx32 " 0x%08" PRIx32, rtt_ram_start, rtt_ram_end);
		gdb_outf(
			"\nmax poll ms: %u min poll ms: %u max errs: %u\n", rtt_max_poll_ms, rtt_min_poll_ms, rtt_max_poll_errs);
	} else if (argc >= 2 && strncmp(argv[1], "channel", command_len) == 0) {
		/* mon rtt channel switches to auto rtt channel selection
		   mon rtt channel number... selects channels given */
		for (size_t i = 0; i < MAX_RTT_CHAN; i++)
			rtt_channel_enabled[i] = false;
		if (argc == 2)
			rtt_auto_channel = true;
		else {
			rtt_auto_channel = false;
			for (size_t i = 2; i < (size_t)argc; ++i) {
				const uint32_t channel = strtoul(argv[i], NULL, 0);
				if (channel < MAX_RTT_CHAN)
					rtt_channel_enabled[channel] = true;
			}
		}
	} else if (argc == 2 && strncmp(argv[1], "ident", command_len) == 0)
		rtt_ident[0] = '\0';
	else if (argc == 2 && strncmp(argv[1], "poll", command_len) == 0)
		gdb_outf("%u %u %u\n", rtt_max_poll_ms, rtt_min_poll_ms, rtt_max_poll_errs);
	else if (argc == 2 && strncmp(argv[1], "cblock", command_len) == 0) {
		gdb_outf("cbaddr: 0x%x\n", rtt_cbaddr);
		gdb_out("ch ena i/o buffer@      size   head   tail flag\n");
		for (uint32_t i = 0; i < rtt_num_up_chan + rtt_num_down_chan; ++i) {
			gdb_outf("%2" PRIu32 "   %c %s 0x%08" PRIx32 " %6" PRIu32 " %6" PRIu32 " %6" PRIu32 " %4" PRIu32 "\n", i,
				rtt_channel_enabled[i] ? 'y' : 'n', i < rtt_num_up_chan ? "out" : "in ", rtt_channel[i].buf_addr,
				rtt_channel[i].buf_size, rtt_channel[i].head, rtt_channel[i].tail, rtt_channel[i].flag);
		}
	} else if (argc == 3 && strncmp(argv[1], "ident", command_len) == 0) {
		strncpy(rtt_ident, argv[2], sizeof(rtt_ident));
		rtt_ident[sizeof(rtt_ident) - 1U] = '\0';
		for (size_t i = 0; i < sizeof(rtt_ident); i++) {
			if (rtt_ident[i] == '_')
				rtt_ident[i] = ' ';
		}
	} else if (argc == 2 && strncmp(argv[1], "ram", command_len) == 0)
		rtt_flag_ram = false;
	else if (argc == 4 && strncmp(argv[1], "ram", command_len) == 0) {
		const int cnt1 = sscanf(argv[2], "%" SCNx32, &rtt_ram_start);
		const int cnt2 = sscanf(argv[3], "%" SCNx32, &rtt_ram_end);
		rtt_flag_ram = cnt1 == 1 && cnt2 == 1 && rtt_ram_end > rtt_ram_start;
		if (!rtt_flag_ram)
			gdb_out("address?\n");
	} else if (argc == 5 && strncmp(argv[1], "poll", command_len) == 0) {
		/* set polling params */
		rtt_max_poll_ms = strtoul(argv[2], NULL, 0);
		rtt_min_poll_ms = strtoul(argv[3], NULL, 0);
		rtt_max_poll_errs = strtoul(argv[4], NULL, 0);
	} else
		gdb_out("what?\n");
	return true;
}
#endif

#ifdef PLATFORM_HAS_TRACESWO
static bool cmd_traceswo(target_s *t, int argc, const char **argv)
{
	(void)t;
#if TRACESWO_PROTOCOL == 2
	uint32_t baudrate = SWO_DEFAULT_BAUD;
#endif
	uint32_t swo_channelmask = 0; /* swo decoding off */
	uint8_t decode_arg = 1;
#if TRACESWO_PROTOCOL == 2
	/* argument: optional baud rate for async mode */
	if (argc > 1 && argv[1][0] >= '0' && argv[1][0] <= '9') {
		baudrate = strtoul(argv[1], NULL, 0);
		if (baudrate == 0)
			baudrate = SWO_DEFAULT_BAUD;
		decode_arg = 2;
	}
#endif
	/* argument: 'decode' literal */
	if (argc > decode_arg && !strncmp(argv[decode_arg], "decode", strlen(argv[decode_arg]))) {
		swo_channelmask = 0xffffffffU; /* decoding all channels */
		/* arguments: channels to decode */
		if (argc > decode_arg + 1) {
			swo_channelmask = 0U;
			for (size_t i = decode_arg + 1U; i < (size_t)argc; ++i) { /* create bitmask of channels to decode */
				const uint32_t channel = strtoul(argv[i], NULL, 0);
				if (channel < 32U)
					swo_channelmask |= 1U << channel;
			}
		}
	}

#if TRACESWO_PROTOCOL == 2
	gdb_outf("Baudrate: %lu ", baudrate);
#endif
	gdb_outf("Channel mask: ");
	for (size_t i = 0; i < 32U; ++i) {
		const uint32_t bit = (swo_channelmask >> (31U - i)) & 1U;
		gdb_outf("%" PRIu32, bit);
	}
	gdb_outf("\n");

#if TRACESWO_PROTOCOL == 2
	traceswo_init(baudrate, swo_channelmask);
#else
	traceswo_init(swo_channelmask);
#endif

	gdb_outf("Trace enabled for BMP serial %s, USB EP 5\n", serial_no);
	return true;
}
#endif

#if defined(PLATFORM_HAS_DEBUG) && PC_HOSTED == 0
static bool cmd_debug_bmp(target_s *t, int argc, const char **argv)
{
	(void)t;
	if (argc == 2) {
		if (!parse_enable_or_disable(argv[1], &debug_bmp))
			return false;
	} else if (argc > 2) {
		gdb_outf("usage: monitor debug [enable|disable]\n");
		return false;
	}

	gdb_outf("Debug mode is %s\n", debug_bmp ? "enabled" : "disabled");
	return true;
}
#endif

#if PC_HOSTED == 1
static bool cmd_shutdown_bmda(target_s *t, int argc, const char **argv)
{
	(void)t;
	(void)argc;
	(void)argv;
	shutdown_bmda = true;
	return true;
}
#endif

static bool cmd_heapinfo(target_s *t, int argc, const char **argv)
{
	if (t == NULL)
		gdb_out("not attached\n");
	else if (argc == 5) {
		target_addr_t heap_base = strtoul(argv[1], NULL, 16);
		target_addr_t heap_limit = strtoul(argv[2], NULL, 16);
		target_addr_t stack_base = strtoul(argv[3], NULL, 16);
		target_addr_t stack_limit = strtoul(argv[4], NULL, 16);
		gdb_outf("heapinfo heap_base: %p heap_limit: %p stack_base: %p stack_limit: %p\n", heap_base, heap_limit,
			stack_base, stack_limit);
		target_set_heapinfo(t, heap_base, heap_limit, stack_base, stack_limit);
	} else
		gdb_outf("heapinfo heap_base heap_limit stack_base stack_limit\n");
	return true;
}
