/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2020-2021 Uwe Bonnes (bon@elektron.ikp.physik.tu-darmstadt.de)
 * Copyright (C) 2022-2023 1BitSquared <info@1bitsquared.com>
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

/* Implements core platform-specific functionality for BMDA */

#if defined(_WIN32) || defined(__CYGWIN__)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "general.h"
#include "platform.h"
#include "jtagtap.h"
#include "swd.h"
#include "target.h"
#include "target_internal.h"
#include "adiv5.h"
#include "riscv_debug.h"
#include "timing.h"
#include "cli.h"
#include "gdb_if.h"
#include "gdb_packet.h"
#include <signal.h>

#ifdef ENABLE_RTT
#include "rtt_if.h"
#endif

#include "bmp_remote.h"
#include "bmp_hosted.h"
#if HOSTED_BMP_ONLY == 0
#include "stlinkv2.h"
#include "ftdi_bmp.h"
#include "jlink.h"
#include "cmsis_dap.h"
#endif

#ifdef ENABLE_GPIOD
#include "bmda_gpiod.h"
#endif

bmda_probe_s bmda_probe_info;

#ifndef ENABLE_GPIOD
jtag_proc_s jtag_proc;
swd_proc_s swd_proc;
#endif

static uint32_t max_frequency = 4000000U;

static bmda_cli_options_s cl_opts;

void gdb_ident(char *p, int count)
{
	snprintf(p, count, "%s (%s), %s", bmda_probe_info.manufacturer, bmda_probe_info.product, bmda_probe_info.version);
}

static void exit_function(void)
{
#if HOSTED_BMP_ONLY == 0
	if (bmda_probe_info.type == PROBE_TYPE_STLINK_V2)
		stlink_deinit();
#endif

	libusb_exit_function(&bmda_probe_info);

	switch (bmda_probe_info.type) {
#if HOSTED_BMP_ONLY == 0
	case PROBE_TYPE_CMSIS_DAP:
		dap_exit_function();
		break;
#endif

	default:
		break;
	}

#ifdef ENABLE_RTT
	rtt_if_exit();
#endif
#if HOSTED_BMP_ONLY == 0
	if (bmda_probe_info.libusb_ctx)
		libusb_exit(bmda_probe_info.libusb_ctx);
#endif
	fflush(stdout);
}

/* SIGTERM handler. */
static void sigterm_handler(int sig)
{
	(void)sig;
	exit(0);
}

void platform_init(int argc, char **argv)
{
#if defined(_WIN32) || defined(__CYGWIN__)
	SetConsoleOutputCP(CP_UTF8);
	if (setvbuf(stdout, NULL, _IONBF, 0) < 0) {
		int err = errno;
		fprintf(stderr, "%s: %s returns %s\n", __func__, "setvbuf()", strerror(err));
	}
#endif
	cl_init(&cl_opts, argc, argv);
	atexit(exit_function);
	signal(SIGTERM, sigterm_handler);
	signal(SIGINT, sigterm_handler);

	if (cl_opts.opt_device)
		bmda_probe_info.type = PROBE_TYPE_BMP;
	else if (cl_opts.opt_gpio_map)
		bmda_probe_info.type = PROBE_TYPE_GPIOD;
	else if (find_debuggers(&cl_opts, &bmda_probe_info))
		exit(1);

	if (cl_opts.opt_list_only)
		exit(0);

	bmp_ident(&bmda_probe_info);

	switch (bmda_probe_info.type) {
	case PROBE_TYPE_BMP:
		if (!serial_open(&cl_opts, bmda_probe_info.serial) || !remote_init(cl_opts.opt_tpwr))
			exit(1);
		break;

#if HOSTED_BMP_ONLY == 0
	case PROBE_TYPE_STLINK_V2:
		if (!stlink_init())
			exit(1);
		break;

	case PROBE_TYPE_CMSIS_DAP:
		if (!dap_init())
			exit(1);
		break;

	case PROBE_TYPE_FTDI:
		if (!ftdi_bmp_init(&cl_opts))
			exit(1);
		break;

	case PROBE_TYPE_JLINK:
		if (!jlink_init())
			exit(1);
		break;
#endif

#ifdef ENABLE_GPIOD
	case PROBE_TYPE_GPIOD:
		if (!bmda_gpiod_init(&cl_opts))
			exit(1);
		break;
#endif

	default:
		exit(1);
	}

	if (cl_opts.opt_max_frequency)
		max_frequency = cl_opts.opt_max_frequency;

	if (cl_opts.opt_mode != BMP_MODE_DEBUG)
		exit(cl_execute(&cl_opts));
	else {
		gdb_if_init();

#ifdef ENABLE_RTT
		rtt_if_init();
#endif
	}
}

bool bmda_swd_scan(const uint32_t targetid)
{
	bmda_probe_info.is_jtag = false;
	platform_max_frequency_set(max_frequency);

	switch (bmda_probe_info.type) {
	case PROBE_TYPE_BMP:
	case PROBE_TYPE_FTDI:
	case PROBE_TYPE_CMSIS_DAP:
	case PROBE_TYPE_JLINK:
#ifdef ENABLE_GPIOD
	case PROBE_TYPE_GPIOD:
#endif
		return adiv5_swd_scan(targetid);

#if HOSTED_BMP_ONLY == 0
	case PROBE_TYPE_STLINK_V2:
		return stlink_swd_scan();
#endif

	default:
		return false;
	}
}

bool bmda_swd_dp_init(adiv5_debug_port_s *dp)
{
#if HOSTED_BMP_ONLY == 1
	(void)dp;
#endif
	switch (bmda_probe_info.type) {
	case PROBE_TYPE_BMP:
		return remote_swd_init();

#if HOSTED_BMP_ONLY == 0
	case PROBE_TYPE_CMSIS_DAP:
		return dap_swd_init(dp);

	case PROBE_TYPE_STLINK_V2:
		return false;

	case PROBE_TYPE_JLINK:
		return jlink_swd_init(dp);

	case PROBE_TYPE_FTDI:
		return ftdi_swd_init();
#endif

#ifdef ENABLE_GPIOD
	case PROBE_TYPE_GPIOD:
		return bmda_gpiod_swd_init();
#endif

	default:
		return false;
	}
}

void bmda_add_jtag_dev(const uint32_t dev_index, const jtag_dev_s *const jtag_dev)
{
	if (bmda_probe_info.type == PROBE_TYPE_BMP)
		remote_add_jtag_dev(dev_index, jtag_dev);
}

bool bmda_jtag_scan(void)
{
	bmda_probe_info.is_jtag = true;
	platform_max_frequency_set(max_frequency);

	switch (bmda_probe_info.type) {
	case PROBE_TYPE_BMP:
	case PROBE_TYPE_FTDI:
	case PROBE_TYPE_JLINK:
	case PROBE_TYPE_CMSIS_DAP:
#ifdef ENABLE_GPIOD
	case PROBE_TYPE_GPIOD:
#endif
		return jtag_scan();

#if HOSTED_BMP_ONLY == 0
	case PROBE_TYPE_STLINK_V2:
		return stlink_jtag_scan();
#endif

	default:
		return false;
	}
}

bool bmda_jtag_init(void)
{
	switch (bmda_probe_info.type) {
	case PROBE_TYPE_BMP:
		return remote_jtag_init();

#if HOSTED_BMP_ONLY == 0
	case PROBE_TYPE_STLINK_V2:
		return false;

	case PROBE_TYPE_FTDI:
		return ftdi_jtag_init();

	case PROBE_TYPE_JLINK:
		return jlink_jtag_init();

	case PROBE_TYPE_CMSIS_DAP:
		return dap_jtag_init();
#endif

#ifdef ENABLE_GPIOD
	case PROBE_TYPE_GPIOD:
		return bmda_gpiod_jtag_init();
#endif

	default:
		return false;
	}
}

void bmda_adiv5_dp_init(adiv5_debug_port_s *const dp)
{
	switch (bmda_probe_info.type) {
	case PROBE_TYPE_BMP:
		if (cl_opts.opt_no_hl) {
			DEBUG_WARN("Not using ADIv5 acceleration commands\n");
			break;
		}
		remote_adiv5_dp_init(dp);
		break;

#if HOSTED_BMP_ONLY == 0
	case PROBE_TYPE_STLINK_V2:
		stlink_adiv5_dp_init(dp);
		break;

	case PROBE_TYPE_CMSIS_DAP:
		dap_adiv5_dp_init(dp);
		break;
#endif

	default:
		break;
	}
}

void bmda_jtag_dp_init(adiv5_debug_port_s *dp)
{
#if HOSTED_BMP_ONLY == 0
	switch (bmda_probe_info.type) {
	case PROBE_TYPE_STLINK_V2:
		stlink_jtag_dp_init(dp);
		break;
	case PROBE_TYPE_CMSIS_DAP:
		dap_jtag_dp_init(dp);
		break;
	default:
		break;
	}
#else
	(void)dp;
#endif
}

void bmda_riscv_jtag_dtm_init(riscv_dmi_s *const dmi)
{
	switch (bmda_probe_info.type) {
	case PROBE_TYPE_BMP:
		if (cl_opts.opt_no_hl) {
			DEBUG_WARN("Not using RISC-V Debug acceleration commands\n");
			break;
		}
		remote_riscv_jtag_dtm_init(dmi);
		break;

	default:
		break;
	}
}

char *bmda_adaptor_ident(void)
{
	switch (bmda_probe_info.type) {
	case PROBE_TYPE_NONE:
		return "None";

	case PROBE_TYPE_BMP:
		return "BMP";

	case PROBE_TYPE_STLINK_V2:
		return "ST-Link v2";

	case PROBE_TYPE_FTDI:
		return "FTDI";

	case PROBE_TYPE_CMSIS_DAP:
		return "CMSIS-DAP";

	case PROBE_TYPE_JLINK:
		return "J-Link";

	case PROBE_TYPE_GPIOD:
		return "GPIOD";

	default:
		return NULL;
	}
}

const char *platform_target_voltage(void)
{
	switch (bmda_probe_info.type) {
	case PROBE_TYPE_BMP:
		return remote_target_voltage();

#if HOSTED_BMP_ONLY == 0
	case PROBE_TYPE_STLINK_V2:
		return stlink_target_voltage();

	case PROBE_TYPE_FTDI:
		return ftdi_target_voltage();

	case PROBE_TYPE_JLINK:
		return jlink_target_voltage_string();
#endif

	default:
		return "Unknown";
	}
}

void platform_nrst_set_val(bool assert)
{
	switch (bmda_probe_info.type) {
	case PROBE_TYPE_BMP:
		remote_nrst_set_val(assert);
		break;

#if HOSTED_BMP_ONLY == 0
	case PROBE_TYPE_STLINK_V2:
		stlink_nrst_set_val(assert);
		break;

	case PROBE_TYPE_JLINK:
		jlink_nrst_set_val(assert);
		break;

	case PROBE_TYPE_FTDI:
		libftdi_nrst_set_val(assert);
		break;

	case PROBE_TYPE_CMSIS_DAP:
		dap_nrst_set_val(assert);
		break;
#endif

	default:
		break;
	}
}

bool platform_nrst_get_val(void)
{
	switch (bmda_probe_info.type) {
	case PROBE_TYPE_BMP:
		return remote_nrst_get_val();

#if HOSTED_BMP_ONLY == 0
	case PROBE_TYPE_STLINK_V2:
		return stlink_nrst_get_val();

	case PROBE_TYPE_JLINK:
		return jlink_nrst_get_val();

	case PROBE_TYPE_FTDI:
		return ftdi_nrst_get_val();
#endif

	default:
		return false;
	}
}

void platform_max_frequency_set(const uint32_t freq)
{
	if (!freq)
		return;

	// Remember the frequency we were asked to set,
	// this will be re-set every time a scan is issued.
	max_frequency = freq;

	switch (bmda_probe_info.type) {
	case PROBE_TYPE_BMP:
		remote_max_frequency_set(freq);
		break;

#if HOSTED_BMP_ONLY == 0
	case PROBE_TYPE_CMSIS_DAP:
		dap_max_frequency(freq);
		break;

	case PROBE_TYPE_FTDI:
		ftdi_max_frequency_set(freq);
		break;

	case PROBE_TYPE_STLINK_V2:
		stlink_max_frequency_set(freq);
		break;

	case PROBE_TYPE_JLINK:
		jlink_max_frequency_set(freq);
		break;
#endif

	default:
		DEBUG_WARN("Setting max debug interface frequency not available or not yet implemented\n");
		break;
	}

	const uint32_t actual_freq = platform_max_frequency_get();
	if (actual_freq == FREQ_FIXED)
		DEBUG_INFO("Device has fixed frequency for %s\n", bmda_probe_info.is_jtag ? "JTAG" : "SWD");
	else if (actual_freq != 0) {
		const uint16_t freq_mhz = actual_freq / 1000000U;
		const uint16_t freq_khz = (actual_freq / 1000U) - (freq_mhz * 1000U);
		DEBUG_INFO("Speed set to %u.%03uMHz for %s\n", freq_mhz, freq_khz, bmda_probe_info.is_jtag ? "JTAG" : "SWD");
	}
}

uint32_t platform_max_frequency_get(void)
{
	switch (bmda_probe_info.type) {
	case PROBE_TYPE_BMP:
		return remote_max_frequency_get();

#if HOSTED_BMP_ONLY == 0
	case PROBE_TYPE_CMSIS_DAP:
		return dap_max_frequency(0);

	case PROBE_TYPE_FTDI:
		return libftdi_max_frequency_get();

	case PROBE_TYPE_STLINK_V2:
		return stlink_max_frequency_get();

	case PROBE_TYPE_JLINK:
		return jlink_max_frequency_get();
#endif

	default:
		DEBUG_WARN("Reading max debug interface frequency not available or not yet implemented\n");
		return 0;
	}
}

bool platform_target_set_power(const bool power)
{
	switch (bmda_probe_info.type) {
	case PROBE_TYPE_BMP:
		return remote_target_set_power(power);

#if HOSTED_BMP_ONLY == 0
	case PROBE_TYPE_JLINK:
		return jlink_target_set_power(power);
#endif

	default:
		DEBUG_ERROR("Target power not available or not yet implemented\n");
		return false;
	}
}

bool platform_target_get_power(void)
{
	switch (bmda_probe_info.type) {
	case PROBE_TYPE_BMP:
		return remote_target_get_power();

#if HOSTED_BMP_ONLY == 0
	case PROBE_TYPE_JLINK:
		return jlink_target_get_power();
#endif

	default:
		return false;
	}
}

uint32_t platform_target_voltage_sense(void)
{
	uint32_t target_voltage = 0;

	switch (bmda_probe_info.type) {
	case PROBE_TYPE_BMP: {
		const char *const result = remote_target_voltage();
		if (result != NULL) {
			uint32_t units = 0;
			uint32_t tenths = 0;
			sscanf(result, "%" PRIu32 ".%" PRIu32, &units, &tenths);
			target_voltage = (units * 10U) + tenths;
		}
		break;
	}

#if HOSTED_BMP_ONLY == 0
	case PROBE_TYPE_JLINK:
		target_voltage = jlink_target_voltage_sense();
		break;
#endif

	default:
		break;
	}

	return target_voltage;
}

void platform_buffer_flush(void)
{
	switch (bmda_probe_info.type) {
#if HOSTED_BMP_ONLY == 0
	case PROBE_TYPE_FTDI:
		ftdi_buffer_flush();
		break;
#endif

	default:
		break;
	}
}

void platform_pace_poll(void)
{
	if (!cl_opts.fast_poll)
		platform_delay(8);
}

void platform_target_clk_output_enable(const bool enable)
{
	switch (bmda_probe_info.type) {
	case PROBE_TYPE_BMP:
		remote_target_clk_output_enable(enable);
		break;

	default:
		break;
	}
}
