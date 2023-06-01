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

/* Handle different BMP pc-hosted platforms/
 */

#include "general.h"
#include "jtagtap.h"
#include "swd.h"
#include "target.h"
#include "target_internal.h"
#include "adiv5.h"
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

bmp_info_s info;

jtag_proc_s jtag_proc;
swd_proc_s swd_proc;

static bmda_cli_options_s cl_opts;

void gdb_ident(char *p, int count)
{
	snprintf(p, count, "%s (%s), %s", info.manufacturer, info.product, info.version);
}

static void exit_function(void)
{
	libusb_exit_function(&info);

	switch (info.bmp_type) {
#if HOSTED_BMP_ONLY == 0
	case BMP_TYPE_CMSIS_DAP:
		dap_exit_function();
		break;
#endif

	default:
		break;
	}

#ifdef ENABLE_RTT
	rtt_if_exit();
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
	cl_init(&cl_opts, argc, argv);
	atexit(exit_function);
	signal(SIGTERM, sigterm_handler);
	signal(SIGINT, sigterm_handler);

	if (cl_opts.opt_device)
		info.bmp_type = BMP_TYPE_BMP;
	else if (find_debuggers(&cl_opts, &info))
		exit(-1);

	bmp_ident(&info);

	switch (info.bmp_type) {
	case BMP_TYPE_BMP:
		if (!serial_open(&cl_opts, info.serial) || !remote_init(cl_opts.opt_tpwr))
			exit(-1);
		break;

#if HOSTED_BMP_ONLY == 0
	case BMP_TYPE_STLINKV2:
		if (!stlink_init())
			exit(-1);
		break;

	case BMP_TYPE_CMSIS_DAP:
		if (!dap_init(&info))
			exit(-1);
		break;

	case BMP_TYPE_LIBFTDI:
		if (!ftdi_bmp_init(&cl_opts))
			exit(-1);
		break;

	case BMP_TYPE_JLINK:
		if (!jlink_init())
			exit(-1);
		break;
#endif

	default:
		exit(-1);
	}

	if (cl_opts.opt_mode != BMP_MODE_DEBUG)
		exit(cl_execute(&cl_opts));
	else {
		gdb_if_init();

#ifdef ENABLE_RTT
		rtt_if_init();
#endif
	}
}

uint32_t platform_adiv5_swdp_scan(uint32_t targetid)
{
	info.is_jtag = false;
	platform_max_frequency_set(cl_opts.opt_max_swj_frequency);

	switch (info.bmp_type) {
	case BMP_TYPE_BMP:
	case BMP_TYPE_LIBFTDI:
	case BMP_TYPE_CMSIS_DAP:
		return adiv5_swdp_scan(targetid);
		break;

#if HOSTED_BMP_ONLY == 0
	case BMP_TYPE_STLINKV2:
		return stlink_swd_scan();

	case BMP_TYPE_JLINK:
		return jlink_swdp_scan(&info);
#endif

	default:
		return 0;
	}
}

bool platform_swdptap_init(adiv5_debug_port_s *dp)
{
#if HOSTED_BMP_ONLY == 1
	(void)dp;
#endif
	switch (info.bmp_type) {
	case BMP_TYPE_BMP:
		return remote_swdptap_init();

#if HOSTED_BMP_ONLY == 0
	case BMP_TYPE_CMSIS_DAP:
		return dap_swd_init(dp);

	case BMP_TYPE_STLINKV2:
	case BMP_TYPE_JLINK:
		return 0;

	case BMP_TYPE_LIBFTDI:
		return ftdi_swd_init();
#endif

	default:
		return false;
	}
}

void platform_add_jtag_dev(uint32_t i, const jtag_dev_s *jtag_dev)
{
	if (info.bmp_type == BMP_TYPE_BMP)
		remote_add_jtag_dev(i, jtag_dev);
}

uint32_t platform_jtag_scan(void)
{
	info.is_jtag = true;

	platform_max_frequency_set(cl_opts.opt_max_swj_frequency);

	switch (info.bmp_type) {
	case BMP_TYPE_BMP:
	case BMP_TYPE_LIBFTDI:
	case BMP_TYPE_JLINK:
	case BMP_TYPE_CMSIS_DAP:
		return jtag_scan();

#if HOSTED_BMP_ONLY == 0
	case BMP_TYPE_STLINKV2:
		return stlink_jtag_scan();
#endif

	default:
		return 0;
	}
}

bool platform_jtagtap_init(void)
{
	switch (info.bmp_type) {
	case BMP_TYPE_BMP:
		return remote_jtagtap_init();

#if HOSTED_BMP_ONLY == 0
	case BMP_TYPE_STLINKV2:
		return 0;

	case BMP_TYPE_LIBFTDI:
		return ftdi_jtag_init();

	case BMP_TYPE_JLINK:
		return jlink_jtagtap_init(&info);

	case BMP_TYPE_CMSIS_DAP:
		return dap_jtag_init();
#endif

	default:
		return false;
	}
}

void platform_adiv5_dp_defaults(adiv5_debug_port_s *dp)
{
	switch (info.bmp_type) {
	case BMP_TYPE_BMP:
		if (cl_opts.opt_no_hl) {
			DEBUG_WARN("Not using HL commands\n");
			return;
		}
		return remote_adiv5_dp_defaults(dp);

#if HOSTED_BMP_ONLY == 0
	case BMP_TYPE_STLINKV2:
		return stlink_adiv5_dp_defaults(dp);

	case BMP_TYPE_CMSIS_DAP:
		return dap_adiv5_dp_defaults(dp);
#endif

	default:
		break;
	}
}

void platform_jtag_dp_init(adiv5_debug_port_s *dp)
{
#if HOSTED_BMP_ONLY == 0
	switch (info.bmp_type) {
	case BMP_TYPE_STLINKV2:
		stlink_jtag_dp_init(dp);
		break;
	case BMP_TYPE_CMSIS_DAP:
		dap_jtag_dp_init(dp);
		break;
	default:
		break;
	}
#else
	(void)dp;
#endif
}

char *platform_ident(void)
{
	switch (info.bmp_type) {
	case BMP_TYPE_NONE:
		return "None";

	case BMP_TYPE_BMP:
		return "BMP";

	case BMP_TYPE_STLINKV2:
		return "ST-Link v2";

	case BMP_TYPE_LIBFTDI:
		return "libFTDI";

	case BMP_TYPE_CMSIS_DAP:
		return "CMSIS-DAP";

	case BMP_TYPE_JLINK:
		return "J-Link";

	default:
		return NULL;
	}
}

const char *platform_target_voltage(void)
{
	switch (info.bmp_type) {
	case BMP_TYPE_BMP:
		return remote_target_voltage();

#if HOSTED_BMP_ONLY == 0
	case BMP_TYPE_STLINKV2:
		return stlink_target_voltage();

	case BMP_TYPE_LIBFTDI:
		return libftdi_target_voltage();

	case BMP_TYPE_JLINK:
		return jlink_target_voltage();
#endif

	default:
		return NULL;
	}
}

void platform_nrst_set_val(bool assert)
{
	switch (info.bmp_type) {
	case BMP_TYPE_BMP:
		return remote_nrst_set_val(assert);

#if HOSTED_BMP_ONLY == 0
	case BMP_TYPE_STLINKV2:
		return stlink_nrst_set_val(assert);

	case BMP_TYPE_JLINK:
		return jlink_nrst_set_val(assert);

	case BMP_TYPE_LIBFTDI:
		return libftdi_nrst_set_val(assert);

	case BMP_TYPE_CMSIS_DAP:
		return dap_nrst_set_val(assert);
#endif

	default:
		break;
	}
}

bool platform_nrst_get_val(void)
{
	switch (info.bmp_type) {
	case BMP_TYPE_BMP:
		return remote_nrst_get_val();

#if HOSTED_BMP_ONLY == 0
	case BMP_TYPE_STLINKV2:
		return stlink_nrst_get_val();

	case BMP_TYPE_JLINK:
		return jlink_nrst_get_val();

	case BMP_TYPE_LIBFTDI:
		return libftdi_nrst_get_val();
#endif

	default:
		return false;
	}
}

void platform_max_frequency_set(uint32_t freq)
{
	if (!freq)
		return;

	switch (info.bmp_type) {
	case BMP_TYPE_BMP:
		remote_max_frequency_set(freq);
		break;

#if HOSTED_BMP_ONLY == 0
	case BMP_TYPE_CMSIS_DAP:
		dap_swj_clock(freq);
		break;

	case BMP_TYPE_LIBFTDI:
		libftdi_max_frequency_set(freq);
		break;

	case BMP_TYPE_STLINKV2:
		stlink_max_frequency_set(freq);
		break;

	case BMP_TYPE_JLINK:
		jlink_max_frequency_set(freq);
		break;
#endif

	default:
		DEBUG_WARN("Setting max SWD/JTAG frequency not yet implemented\n");
		break;
	}

	const uint32_t actual_freq = platform_max_frequency_get();
	if (actual_freq == FREQ_FIXED)
		DEBUG_INFO("Device has fixed frequency for %s\n", (info.is_jtag) ? "JTAG" : "SWD");
	else {
		const uint16_t freq_mhz = actual_freq / 1000000U;
		const uint16_t freq_khz = (actual_freq / 1000U) - (freq_mhz * 1000U);
		DEBUG_INFO("Speed set to %u.%03uMHz for %s\n", freq_mhz, freq_khz, info.is_jtag ? "JTAG" : "SWD");
	}
}

uint32_t platform_max_frequency_get(void)
{
	switch (info.bmp_type) {
	case BMP_TYPE_BMP:
		return remote_max_frequency_get();

#if HOSTED_BMP_ONLY == 0
	case BMP_TYPE_CMSIS_DAP:
		return dap_swj_clock(0);

	case BMP_TYPE_LIBFTDI:
		return libftdi_max_frequency_get();

	case BMP_TYPE_STLINKV2:
		return stlink_max_frequency_get();

	case BMP_TYPE_JLINK:
		return jlink_max_frequency_get();
#endif

	default:
		DEBUG_WARN("Reading max SWJ frequency not yet implemented\n");
		return 0;
	}
}

void platform_target_set_power(const bool power)
{
	switch (info.bmp_type) {
	case BMP_TYPE_BMP:
		if (remote_target_set_power(power))
			DEBUG_INFO("Powering up device!\n");
		else
			DEBUG_ERROR("Powering up device unimplemented or failed\n");
		break;

	default:
		break;
	}
}

bool platform_target_get_power(void)
{
	switch (info.bmp_type) {
	case BMP_TYPE_BMP:
		return remote_target_get_power();

	default:
		return false;
	}
}

uint32_t platform_target_voltage_sense(void)
{
	uint32_t targetVoltage = 0;

	switch (info.bmp_type) {
	case BMP_TYPE_BMP: {
		const char *const result = remote_target_voltage();
		if (result != NULL) {
			uint32_t units = 0;
			uint32_t tenths = 0;
			sscanf(result, "%" PRIu32 ".%" PRIu32, &units, &tenths);
			targetVoltage = (units * 10U) + tenths;
		}
		break;
	}

	default:
		break;
	}

	return targetVoltage;
}

void platform_buffer_flush(void)
{
	switch (info.bmp_type) {
#if HOSTED_BMP_ONLY == 0
	case BMP_TYPE_LIBFTDI:
		return libftdi_buffer_flush();
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
	switch (info.bmp_type) {
	case BMP_TYPE_BMP:
		remote_target_clk_output_enable(enable);
		break;

	default:
		break;
	}
}

static void decode_dp_access(const uint8_t addr, const uint8_t rnw)
{
	const char *reg = NULL;
	switch (addr) {
	case 0x00U:
		reg = rnw ? "DPIDR" : "ABORT";
		break;
	case 0x04U:
		reg = "CTRL/STAT";
		break;
	case 0x08U:
		reg = rnw ? "RESEND" : "SELECT";
		break;
	case 0x0cU:
		reg = "RDBUFF";
		break;
	}

	if (reg)
		DEBUG_PROTO("%s: ", reg);
	else
		DEBUG_PROTO("Unknown DP register %02x: ", addr);
}

static void decode_ap_access(const uint8_t ap, const uint8_t addr)
{
	DEBUG_PROTO("AP %u ", ap);

	const char *reg = NULL;
	switch (addr) {
	case 0x00U:
		reg = "CSW";
		break;
	case 0x04U:
		reg = "TAR";
		break;
	case 0x0cU:
		reg = "DRW";
		break;
	case 0x10U:
		reg = "DB0";
		break;
	case 0x14U:
		reg = "DB1";
		break;
	case 0x18U:
		reg = "DB2";
		break;
	case 0x1cU:
		reg = "DB3";
		break;
	case 0xf8U:
		reg = "BASE";
		break;
	case 0xf4U:
		reg = "CFG";
		break;
	case 0xfcU:
		reg = "IDR";
		break;
	}

	if (reg)
		DEBUG_PROTO("%s: ", reg);
	else
		DEBUG_PROTO("Reserved(%02x): ", addr);
}

static void decode_access(const uint16_t addr, const uint8_t rnw)
{
	if (rnw)
		DEBUG_PROTO("Read ");
	else
		DEBUG_PROTO("Write ");

	if (addr < 0x100U)
		decode_dp_access(addr & 0xffU, rnw);
	else
		decode_ap_access(addr >> 8U, addr & 0xffU);
}

void adiv5_dp_write(adiv5_debug_port_s *dp, uint16_t addr, uint32_t value)
{
	decode_access(addr, ADIV5_LOW_WRITE);
	DEBUG_PROTO("0x%08" PRIx32 "\n", value);
	dp->low_access(dp, ADIV5_LOW_WRITE, addr, value);
}

uint32_t adiv5_dp_read(adiv5_debug_port_s *dp, uint16_t addr)
{
	uint32_t ret = dp->dp_read(dp, addr);
	decode_access(addr, ADIV5_LOW_READ);
	DEBUG_PROTO("0x%08" PRIx32 "\n", ret);
	return ret;
}

uint32_t adiv5_dp_error(adiv5_debug_port_s *dp)
{
	uint32_t ret = dp->error(dp, false);
	DEBUG_PROTO("DP Error 0x%08" PRIx32 "\n", ret);
	return ret;
}

uint32_t adiv5_dp_low_access(adiv5_debug_port_s *dp, uint8_t rnw, uint16_t addr, uint32_t value)
{
	uint32_t ret = dp->low_access(dp, rnw, addr, value);
	decode_access(addr, rnw);
	DEBUG_PROTO("0x%08" PRIx32 "\n", rnw ? ret : value);
	return ret;
}

uint32_t adiv5_ap_read(adiv5_access_port_s *ap, uint16_t addr)
{
	uint32_t ret = ap->dp->ap_read(ap, addr);
	decode_access(addr, ADIV5_LOW_READ);
	DEBUG_PROTO("0x%08" PRIx32 "\n", ret);
	return ret;
}

void adiv5_ap_write(adiv5_access_port_s *ap, uint16_t addr, uint32_t value)
{
	decode_access(addr, ADIV5_LOW_WRITE);
	DEBUG_PROTO("0x%08" PRIx32 "\n", value);
	return ap->dp->ap_write(ap, addr, value);
}

void adiv5_mem_read(adiv5_access_port_s *ap, void *dest, uint32_t src, size_t len)
{
	ap->dp->mem_read(ap, dest, src, len);
	DEBUG_PROTO("ap_memread @ %" PRIx32 " len %zu:", src, len);
	const uint8_t *const data = (const uint8_t *)dest;
	for (size_t offset = 0; offset < len; ++offset) {
		if (offset == 16U)
			break;
		DEBUG_PROTO(" %02x", data[offset]);
	}
	if (len > 16U)
		DEBUG_PROTO(" ...");
	DEBUG_PROTO("\n");
}

void adiv5_mem_write_sized(adiv5_access_port_s *ap, uint32_t dest, const void *src, size_t len, align_e align)
{
	DEBUG_PROTO("ap_mem_write_sized @ %" PRIx32 " len %zu, align %d:", dest, len, 1 << align);
	const uint8_t *const data = (const uint8_t *)src;
	for (size_t offset = 0; offset < len; ++offset) {
		if (offset == 16U)
			break;
		DEBUG_PROTO(" %02x", data[offset]);
	}
	if (len > 16U)
		DEBUG_PROTO(" ...");
	DEBUG_PROTO("\n");
	return ap->dp->mem_write(ap, dest, src, len, align);
}

void adiv5_dp_abort(adiv5_debug_port_s *dp, uint32_t abort)
{
	DEBUG_PROTO("Abort: %08" PRIx32 "\n", abort);
	return dp->abort(dp, abort);
}
