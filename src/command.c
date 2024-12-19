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

/*
 * This file implements a basic command interpreter for GDB 'monitor' commands.
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
#include "jtagtap.h"

#if CONFIG_BMDA == 0
#include "jtag_scan.h"
#endif

#ifdef ENABLE_RTT
#include "rtt.h"
#include "hex_utils.h"
#endif

#ifdef PLATFORM_HAS_TRACESWO
#include "serialno.h"
#include "swo.h"
#include "usb.h"
#endif

static bool cmd_version(target_s *target, int argc, const char **argv);
static bool cmd_help(target_s *target, int argc, const char **argv);

static bool cmd_jtag_scan(target_s *target, int argc, const char **argv);
static bool cmd_swd_scan(target_s *target, int argc, const char **argv);
static bool cmd_rvswd_scan(target_s *target, int argc, const char **argv);
static bool cmd_auto_scan(target_s *target, int argc, const char **argv);
static bool cmd_frequency(target_s *target, int argc, const char **argv);
static bool cmd_targets(target_s *target, int argc, const char **argv);
static bool cmd_morse(target_s *target, int argc, const char **argv);
static bool cmd_halt_timeout(target_s *target, int argc, const char **argv);
static bool cmd_connect_reset(target_s *target, int argc, const char **argv);
static bool cmd_reset(target_s *target, int argc, const char **argv);
static bool cmd_tdi_low_reset(target_s *target, int argc, const char **argv);
#ifdef PLATFORM_HAS_POWER_SWITCH
static bool cmd_target_power(target_s *target, int argc, const char **argv);
#endif
#ifdef PLATFORM_HAS_BATTERY
static bool cmd_target_battery(target_s *t, int argc, const char **argv);
#endif
#ifdef PLATFORM_HAS_TRACESWO
static bool cmd_swo(target_s *target, int argc, const char **argv);
#endif
static bool cmd_heapinfo(target_s *target, int argc, const char **argv);
#ifdef ENABLE_RTT
static bool cmd_rtt(target_s *target, int argc, const char **argv);
#endif
#if defined(PLATFORM_HAS_DEBUG) && CONFIG_BMDA == 0
static bool cmd_debug_bmp(target_s *target, int argc, const char **argv);
#endif
#if CONFIG_BMDA == 1
static bool cmd_shutdown_bmda(target_s *target, int argc, const char **argv);
#endif

#ifdef _MSC_VER
#define strtok_r strtok_s
#endif

const command_s cmd_list[] = {
	{"version", cmd_version, "Display firmware version info"},
	{"help", cmd_help, "Display help for monitor commands"},
	{"jtag_scan", cmd_jtag_scan, "Scan JTAG chain for devices"},
	{"swd_scan", cmd_swd_scan, "Scan SWD interface for devices: [TARGET_ID]"},
	{"swdp_scan", cmd_swd_scan, "Deprecated: use swd_scan instead"},
	{"rvswd_scan", cmd_rvswd_scan, "Scan RVSWD for devices"},
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
#ifdef PLATFORM_HAS_BATTERY
	{"battery", cmd_target_battery, "Reads the battery state"},
#endif
#ifdef ENABLE_RTT
	{"rtt", cmd_rtt,
		"[enable|disable|status|channel [0..15 ...]|ident [STR]|cblock|ram [RAM_START RAM_END]|poll [MAXMS MINMS "
		"MAXERR]]"},
#endif
#ifdef PLATFORM_HAS_TRACESWO
#if SWO_ENCODING == 1
	{"swo", cmd_swo, "Start SWO capture, Manchester mode: <enable|disable> [decode [CHANNEL_NR ...]]"},
#elif SWO_ENCODING == 2
	{"swo", cmd_swo, "Start SWO capture, UART mode: <enable|disable> [BAUDRATE] [decode [CHANNEL_NR ...]]"},
#elif SWO_ENCODING == 3
	{"swo", cmd_swo, "Start SWO capture: <enable|disable> [manchester|uart] [BAUDRATE] [decode [CHANNEL_NR ...]]"},
#endif
	{"traceswo", cmd_swo, "Deprecated: use swo instead"},
#endif
	{"heapinfo", cmd_heapinfo, "Set semihosting heapinfo: HEAP_BASE HEAP_LIMIT STACK_BASE STACK_LIMIT"},
#if defined(PLATFORM_HAS_DEBUG) && CONFIG_BMDA == 0
	{"debug_bmp", cmd_debug_bmp, "Output BMP \"debug\" strings to the second vcom: [enable|disable]"},
#endif
#if CONFIG_BMDA == 1
	{"shutdown_bmda", cmd_shutdown_bmda, "Tell the BMDA server to shut down when the GDB connection closes"},
#endif
	{NULL, NULL, NULL},
};

#ifdef PLATFORM_HAS_CUSTOM_COMMANDS
extern const command_s platform_cmd_list[];
#endif

bool connect_assert_nrst;
#if defined(PLATFORM_HAS_DEBUG) && CONFIG_BMDA == 0
bool debug_bmp;
#endif
unsigned cortexm_wait_timeout = 2000; /* Timeout to wait for Cortex to react on halt command. */

int command_process(target_s *const target, char *const cmd_buffer)
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
			return !cmd->handler(target, argc, argv);
	}

#ifdef PLATFORM_HAS_CUSTOM_COMMANDS
	for (const command_s *cmd = platform_cmd_list; cmd->cmd; ++cmd) {
		if (!strncmp(argv[0], cmd->cmd, strlen(argv[0])))
			return !cmd->handler(target, argc, argv);
	}
#endif

	if (!target)
		return -1;
	return target_command(target, argc, argv);
}

bool cmd_version(target_s *target, int argc, const char **argv)
{
	(void)target;
	(void)argc;
	(void)argv;
#if CONFIG_BMDA == 1
	gdb_out("Black Magic Debug App " FIRMWARE_VERSION "\n");
	bmda_display_probe();
#else
#ifndef PLATFORM_IDENT_DYNAMIC
	gdb_out(BOARD_IDENT);
#else
	gdb_outf(BOARD_IDENT, platform_ident());
#endif
	gdb_outf(", Hardware Version %d\n", platform_hwversion());
#endif
	gdb_out("Copyright (C) 2010-2024 Black Magic Debug Project\n");
	gdb_out("License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>\n\n");

	return true;
}

bool cmd_help(target_s *target, int argc, const char **argv)
{
	(void)argc;
	(void)argv;

	if (!target || target->tc->destroy_callback) {
		gdb_out("General commands:\n");
		for (const command_s *cmd = cmd_list; cmd->cmd; cmd++)
			gdb_outf("\t%s -- %s\n", cmd->cmd, cmd->help);
#ifdef PLATFORM_HAS_CUSTOM_COMMANDS
		gdb_out("Platform commands:\n");
		for (const command_s *cmd = platform_cmd_list; cmd->cmd; ++cmd)
			gdb_outf("\t%s -- %s\n", cmd->cmd, cmd->help);
#endif
		if (!target)
			return true;
	}

	target_command_help(target);
	return true;
}

static bool cmd_jtag_scan(target_s *target, int argc, const char **argv)
{
	(void)target;
	(void)argc;
	(void)argv;

	gdb_outf("Target voltage: %s\n", platform_target_voltage());

	if (connect_assert_nrst)
		platform_nrst_set_val(true); /* will be deasserted after attach */

	bool scan_result = false;
	TRY (EXCEPTION_ALL) {
#if CONFIG_BMDA == 1
		scan_result = bmda_jtag_scan();
#else
		scan_result = jtag_scan();
#endif
	}
	CATCH () {
	case EXCEPTION_TIMEOUT:
		gdb_outf("Timeout during scan. Is target stuck in WFI?\n");
		break;
	case EXCEPTION_ERROR:
		gdb_outf("Exception: %s\n", exception_frame.msg);
		break;
	default:
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

bool cmd_swd_scan(target_s *target, int argc, const char **argv)
{
	(void)target;
	volatile uint32_t targetid = 0;
	if (argc > 1)
		targetid = strtoul(argv[1], NULL, 0);
	gdb_outf("Target voltage: %s\n", platform_target_voltage());

	if (connect_assert_nrst)
		platform_nrst_set_val(true); /* will be deasserted after attach */

	bool scan_result = false;
	TRY (EXCEPTION_ALL) {
#if CONFIG_BMDA == 1
		scan_result = bmda_swd_scan(targetid);
#else
		scan_result = adiv5_swd_scan(targetid);
#endif
	}
	CATCH () {
	case EXCEPTION_TIMEOUT:
		gdb_outf("Timeout during scan. Is target stuck in WFI?\n");
		break;
	case EXCEPTION_ERROR:
		gdb_outf("Exception: %s\n", exception_frame.msg);
		break;
	default:
		break;
	}

	if (!scan_result) {
		platform_target_clk_output_enable(false);
		platform_nrst_set_val(false);
		gdb_out("SWD scan failed!\n");
		return false;
	}

	cmd_targets(NULL, 0, NULL);
	platform_target_clk_output_enable(false);
	morse(NULL, false);
	return true;
}

bool cmd_rvswd_scan(target_s *target, int argc, const char **argv)
{
	(void)target;
	(void)argc;
	(void)argv;

	if (platform_target_voltage())
		gdb_outf("Target voltage: %s\n", platform_target_voltage());

	if (connect_assert_nrst)
		platform_nrst_set_val(true); /* will be deasserted after attach */

	bool scan_result = false;
	TRY (EXCEPTION_ALL) {
#if CONFIG_BMDA == 1
		scan_result = bmda_rvswd_scan();
#else
		scan_result = false;
#endif
	}
	CATCH () {
	case EXCEPTION_TIMEOUT:
		gdb_outf("Timeout during scan. Is target stuck in WFI?\n");
		break;
	case EXCEPTION_ERROR:
		gdb_outf("Exception: %s\n", exception_frame.msg);
		break;
	default:
		break;
	}

	if (!scan_result) {
		platform_target_clk_output_enable(false);
		platform_nrst_set_val(false);
		gdb_out("RVSWD scan failed!\n");
		return false;
	}

	cmd_targets(NULL, 0, NULL);
	platform_target_clk_output_enable(false);
	morse(NULL, false);
	return true;
}

bool cmd_auto_scan(target_s *target, int argc, const char **argv)
{
	(void)target;
	(void)argc;
	(void)argv;

	gdb_outf("Target voltage: %s\n", platform_target_voltage());

	if (connect_assert_nrst)
		platform_nrst_set_val(true); /* will be deasserted after attach */

	bool scan_result = false;
	TRY (EXCEPTION_ALL) {
#if CONFIG_BMDA == 1
		scan_result = bmda_jtag_scan();
#else
		scan_result = jtag_scan();
#endif
		if (!scan_result) {
			gdb_out("JTAG scan found no devices, trying SWD!\n");

#if CONFIG_BMDA == 1
			scan_result = bmda_swd_scan(0);
#else
			scan_result = adiv5_swd_scan(0);
#endif
			if (!scan_result) {
				gdb_out("SWD scan found no devices.\n");

#if CONFIG_BMDA == 1
				scan_result = bmda_rvswd_scan();
#else
				scan_result = false;
#endif
				if (!scan_result)
					gdb_out("RVSWD scan found no devices.\n");
			}
		}
	}
	CATCH () {
	case EXCEPTION_TIMEOUT:
		gdb_outf("Timeout during scan. Is target stuck in WFI?\n");
		break;
	case EXCEPTION_ERROR:
		gdb_outf("Exception: %s\n", exception_frame.msg);
		break;
	default:
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

bool cmd_frequency(target_s *target, int argc, const char **argv)
{
	(void)target;
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
		default:
			break;
		}
		platform_max_frequency_set(frequency);
	}
	const uint32_t freq = platform_max_frequency_get();
	if (freq == FREQ_FIXED)
		gdb_outf("Debug iface frequency is fixed.\n");
	else
		gdb_outf("Debug iface frequency set to %" PRIu32 "Hz\n", freq);
	return true;
}

static void display_target(size_t idx, target_s *target, void *context)
{
	(void)context;
	const char attached = target->attached ? '*' : ' ';
	const char *const core_name = target->core;
	if (!strcmp(target->driver, "ARM Cortex-M"))
		gdb_outf("***%2u %c Unknown %s Designer 0x%x Part ID 0x%x %s\n", (unsigned)idx, attached, target->driver,
			target->designer_code, target->part_id, core_name ? core_name : "");
	else
		gdb_outf("%2u %3c  %s %s\n", (unsigned)idx, attached, target->driver, core_name ? core_name : "");
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

bool cmd_morse(target_s *target, int argc, const char **argv)
{
	(void)target;
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

static bool cmd_connect_reset(target_s *target, int argc, const char **argv)
{
	(void)target;
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

static bool cmd_halt_timeout(target_s *target, int argc, const char **argv)
{
	(void)target;
	if (argc > 1)
		cortexm_wait_timeout = strtoul(argv[1], NULL, 0);
	gdb_outf("Cortex-M timeout to wait for device halts: %u\n", cortexm_wait_timeout);
	return true;
}

static bool cmd_reset(target_s *target, int argc, const char **argv)
{
	(void)target;
	uint32_t pulse_len_ms = 0;
	if (argc > 1)
		pulse_len_ms = strtoul(argv[1], NULL, 0);
	target_list_free();
	platform_nrst_set_val(true);
	platform_delay(pulse_len_ms);
	platform_nrst_set_val(false);
	return true;
}

static bool cmd_tdi_low_reset(target_s *target, int argc, const char **argv)
{
	(void)target;
	(void)argc;
	(void)argv;
	if (!jtag_proc.jtagtap_next)
		return false;
	jtag_proc.jtagtap_next(true, false);
	cmd_reset(NULL, 0, NULL);
	return true;
}

#ifdef PLATFORM_HAS_POWER_SWITCH
static bool cmd_target_power(target_s *target, int argc, const char **argv)
{
	(void)target;
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

#ifdef PLATFORM_HAS_BATTERY
static bool cmd_target_battery(target_s *t, int argc, const char **argv)
{
	(void)t;
	(void)argc;
	(void)argv;
	gdb_out(platform_battery_voltage());
	return true;
}
#endif
#ifdef ENABLE_RTT
static const char *on_or_off(const bool value)
{
	return value ? "on" : "off";
}

static bool cmd_rtt(target_s *target, int argc, const char **argv)
{
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
		gdb_outf(" halt: %s", on_or_off(target_mem_access_needs_halt(target)));
		gdb_out(" channels: ");
		if (rtt_auto_channel)
			gdb_out("auto ");
		for (size_t i = 0; i < MAX_RTT_CHAN; i++) {
			if (rtt_channel_enabled[i])
				gdb_outf("%" PRIu32 " ", (uint32_t)i);
		}
		if (rtt_flag_ram)
			gdb_outf("ram: 0x%08" PRIx32 " 0x%08" PRIx32, rtt_ram_start, rtt_ram_end);
		gdb_outf("\nmax poll ms: %" PRIu32 " min poll ms: %" PRIu32 " max errs: %" PRIu32 "\n", rtt_max_poll_ms,
			rtt_min_poll_ms, rtt_max_poll_errs);
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
		gdb_outf("%" PRIu32 " %" PRIu32 " %" PRIu32 "\n", rtt_max_poll_ms, rtt_min_poll_ms, rtt_max_poll_errs);
	else if (argc == 2 && strncmp(argv[1], "cblock", command_len) == 0) {
		gdb_outf("cbaddr: 0x%08" PRIx32 "\n", rtt_cbaddr);
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
		if (read_hex32(argv[2], NULL, &rtt_ram_start, READ_HEX_NO_FOLLOW) &&
			read_hex32(argv[3], NULL, &rtt_ram_end, READ_HEX_NO_FOLLOW)) {
			rtt_flag_ram = rtt_ram_end > rtt_ram_start;
			if (!rtt_flag_ram)
				gdb_out("address?\n");
		}
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
static bool cmd_swo_enable(int argc, const char **argv)
{
	/* Set up which mode we're going to default to */
#if SWO_ENCODING == 1
	const swo_coding_e capture_mode = swo_manchester;
#elif SWO_ENCODING == 2
	const swo_coding_e capture_mode = swo_nrz_uart;
#elif SWO_ENCODING == 3
	swo_coding_e capture_mode = swo_none;
#endif
	/* Set up the presumed baudrate for the stream */
	uint32_t baudrate = SWO_DEFAULT_BAUD;
	/*
	 * Before we can enable SWO data recovery, potentially with decoding,
	 * start with the assumption ITM decoding is off
	 */
	uint32_t itm_stream_mask = 0U;
	uint8_t decode_arg = 1U;
#if SWO_ENCODING == 3
	/* Next, determine which decoding mode to use */
	if (argc > decode_arg) {
		const size_t arg_length = strlen(argv[decode_arg]);
		if (!strncmp(argv[decode_arg], "manchester", arg_length))
			capture_mode = swo_manchester;
		if (!strncmp(argv[decode_arg], "uart", arg_length))
			capture_mode = swo_nrz_uart;
	}
	/* If a mode was given, make sure the rest of the parser skips the mode verb */
	if (capture_mode != swo_none)
		++decode_arg;
	/* Otherwise set a default mode up */
	else
		capture_mode = swo_nrz_uart;
#endif
#if SWO_ENCODING == 2 || SWO_ENCODING == 3
	/* Handle the optional baud rate argument if present */
	if (capture_mode == swo_nrz_uart && argc > decode_arg && argv[decode_arg][0] >= '0' && argv[decode_arg][0] <= '9') {
		baudrate = strtoul(argv[decode_arg], NULL, 0);
		if (baudrate == 0U)
			baudrate = SWO_DEFAULT_BAUD;
		++decode_arg;
	}
#endif
	/* Check if `decode` has been given and if it has, enable ITM decoding */
	if (argc > decode_arg && !strncmp(argv[decode_arg], "decode", strlen(argv[decode_arg]))) {
		/* Check if there are specific ITM streams to enable and build a bitmask of them */
		if (argc > decode_arg + 1) {
			/* For each of the specified streams */
			for (size_t i = decode_arg + 1U; i < (size_t)argc; ++i) {
				/* Figure out which the next one is */
				const uint32_t stream = strtoul(argv[i], NULL, 0);
				/* If it's a valid ITM stream number, set it in the mask */
				if (stream < 32U)
					itm_stream_mask |= 1U << stream;
			}
		} else
			/* Decode all ITM streams if non given */
			itm_stream_mask = 0xffffffffU;
	}

	/* Now enable SWO data recovery */
	swo_init(capture_mode, baudrate, itm_stream_mask);
	/* And show the user what we've done - first the channel mask from MSb to LSb */
	gdb_outf("Channel mask: ");
	for (size_t i = 0; i < 32U; ++i) {
		const char bit = '0' + ((itm_stream_mask >> (31U - i)) & 1U);
		gdb_outf("%c", bit);
	}
	gdb_outf("\n");
	/* Then the connection information for programs that are scraping BMD's output to know what to connect to */
	gdb_outf("Trace enabled for BMP serial %s, USB EP %u\n", serial_no, SWO_ENDPOINT);
	return true;
}

static bool cmd_swo_disable(void)
{
	swo_deinit(true);
	gdb_out("Trace disabled\n");
	return true;
}

static bool cmd_swo(target_s *target, int argc, const char **argv)
{
	(void)target;
	bool enable_swo = false;
	if (argc >= 2 && !parse_enable_or_disable(argv[1], &enable_swo)) {
		gdb_out("Usage: traceswo <enable|disable> [2000000] [decode [0 1 3 31]]\n");
		return false;
	}

	if (enable_swo)
		return cmd_swo_enable(argc - 1, argv + 1);
	return cmd_swo_disable();
}
#endif

#if defined(PLATFORM_HAS_DEBUG) && CONFIG_BMDA == 0
static bool cmd_debug_bmp(target_s *target, int argc, const char **argv)
{
	(void)target;
	if (argc == 2 && !parse_enable_or_disable(argv[1], &debug_bmp))
		return false;
	if (argc > 2) {
		gdb_outf("usage: monitor debug [enable|disable]\n");
		return false;
	}

	gdb_outf("Debug mode is %s\n", debug_bmp ? "enabled" : "disabled");
	return true;
}
#endif

#if CONFIG_BMDA == 1
static bool cmd_shutdown_bmda(target_s *target, int argc, const char **argv)
{
	(void)target;
	(void)argc;
	(void)argv;
	shutdown_bmda = true;
	return true;
}
#endif

/*
 * Heapinfo allows passing up to four uint32_t from host to target.
 * Heapinfo can be used to quickly test a system with different heap and stack values, to see how much heap and stack is needed.
 * - User sets up values for heap and stack using "mon heapinfo"
 * - When the target boots, crt0.S does a heapinfo semihosting call to get these values for heap and stack.
 * - If the target system crashes, increase heap or stack
 * See newlib/libc/sys/arm/crt0.S "Issue Angel SWI to read stack info"
 */
static bool cmd_heapinfo(target_s *target, int argc, const char **argv)
{
	if (target == NULL)
		gdb_out("not attached\n");
	else if (argc == 5) {
		target_addr_t heap_base = strtoul(argv[1], NULL, 16);
		target_addr_t heap_limit = strtoul(argv[2], NULL, 16);
		target_addr_t stack_base = strtoul(argv[3], NULL, 16);
		target_addr_t stack_limit = strtoul(argv[4], NULL, 16);
		gdb_outf("heap_base: %08" PRIx32 " heap_limit: %08" PRIx32 " stack_base: %08" PRIx32 " stack_limit: "
				 "%08" PRIx32 "\n",
			heap_base, heap_limit, stack_base, stack_limit);
		target_set_heapinfo(target, heap_base, heap_limit, stack_base, stack_limit);
	} else
		gdb_outf("%s\n", "Set semihosting heapinfo: HEAP_BASE HEAP_LIMIT STACK_BASE STACK_LIMIT");
	return true;
}
