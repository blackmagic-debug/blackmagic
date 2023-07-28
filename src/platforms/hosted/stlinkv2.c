/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2019-2020 Uwe Bonnes <bon@elektron.ikp.physik.tu-darmstadt.de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.	 If not, see <http://www.gnu.org/licenses/>.
 */

/* Much code and ideas shamelessly taken form
 * https://github.com/texane/stlink.git
 * git://git.code.sf.net/p/openocd/code
 * https://github.com/pavelrevak/pystlink
 * https://github.com/pavelrevak/pystlink
 *
 * with some contribution.
 */

#include "general.h"
#include "gdb_if.h"
#include "adiv5.h"
#include "bmp_hosted.h"
#include "stlinkv2.h"
#include "stlinkv2_protocol.h"
#include "exception.h"
#include "cortexm.h"
#include "buffer_utils.h"

#include <assert.h>
#include <unistd.h>
#include <signal.h>
#include <ctype.h>
#include <sys/time.h>

#include "cli.h"

#ifdef _MSC_VER
#include <intrin.h>
#endif

typedef enum transport_mode {
	STLINK_MODE_SWD = 0,
	STLINK_MODE_JTAG
} transport_mode_e;

typedef struct stlink {
	libusb_context *libusb_ctx;
	uint16_t vid;
	uint16_t pid;
	bool nrst;
	uint8_t dap_select;
	uint8_t ep_tx;
	uint8_t ver_hw;     /* 20, 21 or 31 deciphered from USB PID.*/
	uint8_t ver_stlink; /* 2 or 3  from API.*/
	uint8_t ver_api;
	uint8_t ver_jtag;
	uint8_t ver_mass;
	uint8_t ver_swim;
	uint8_t ver_bridge;
	uint16_t block_size;
	bool ap_error;
} stlink_s;

#define STLINK_ERROR_DP_FAULT (-2)
#define STLINK_ERROR_AP_FAULT (-3)

#define STLINK_V2_CPU_CLOCK_FREQ      (72U * 1000U * 1000U)
#define STLINK_V2_JTAG_MUL_FACTOR     2U
#define STLINK_V2_MAX_JTAG_CLOCK_FREQ (9U * 1000U * 1000U)
#define STLINK_V2_MIN_JTAG_CLOCK_FREQ (281250U)
#define STLINK_V2_SWD_MUL_FACTOR      20U
#define STLINK_V2_MAX_SWD_CLOCK_FREQ  (3600U * 1000U)
#define STLINK_V2_MIN_SWD_CLOCK_FREQ  (4505U)

static stlink_s stlink;

static uint32_t stlink_v2_divisor;
static unsigned int stlink_v3_freq[2];

static int stlink_usb_get_rw_status(bool verbose);

static stlink_mem_command_s stlink_memory_access(
	const uint8_t operation, const uint32_t address, const uint16_t length, const uint8_t apsel)
{
	stlink_mem_command_s command = {
		.command = STLINK_DEBUG_COMMAND,
		.operation = operation,
		.apsel = apsel,
	};
	write_le4(command.address, 0, address);
	write_le2(command.length, 0, length);
	return command;
}

/*
 * Converts an ST-Link status code held in the first byte of a response to
 * readable error
 */
int stlink_usb_error_check(uint8_t *const data, const bool verbose)
{
	switch (data[0]) {
	case STLINK_DEBUG_ERR_OK:
		return STLINK_ERROR_OK;
	case STLINK_DEBUG_ERR_FAULT:
		if (verbose)
			DEBUG_ERROR("SWD fault response (0x%x)\n", STLINK_DEBUG_ERR_FAULT);
		return STLINK_ERROR_FAIL;
	case STLINK_JTAG_UNKNOWN_JTAG_CHAIN:
		if (verbose)
			DEBUG_ERROR("Unknown JTAG chain\n");
		return STLINK_ERROR_FAIL;
	case STLINK_NO_DEVICE_CONNECTED:
		if (verbose)
			DEBUG_WARN("No device connected\n");
		return STLINK_ERROR_FAIL;
	case STLINK_JTAG_COMMAND_ERROR:
		if (verbose)
			DEBUG_ERROR("Command error\n");
		return STLINK_ERROR_FAIL;
	case STLINK_JTAG_GET_IDCODE_ERROR:
		if (verbose)
			DEBUG_ERROR("Failure reading IDCODE\n");
		return STLINK_ERROR_FAIL;
	case STLINK_JTAG_DBG_POWER_ERROR:
		if (verbose)
			DEBUG_ERROR("Failure powering DBG\n");
		return STLINK_ERROR_WAIT;
	case STLINK_SWD_AP_WAIT:
		if (verbose)
			DEBUG_WARN("Wait status SWD_AP_WAIT (0x%x)\n", STLINK_SWD_AP_WAIT);
		return STLINK_ERROR_WAIT;
	case STLINK_SWD_DP_WAIT:
		if (verbose)
			DEBUG_WARN("Wait status SWD_DP_WAIT (0x%x)\n", STLINK_SWD_DP_WAIT);
		return STLINK_ERROR_WAIT;
	case STLINK_JTAG_WRITE_ERROR:
		if (verbose)
			DEBUG_ERROR("Write error\n");
		return STLINK_ERROR_FAIL;
	case STLINK_JTAG_WRITE_VERIF_ERROR:
		if (verbose)
			DEBUG_ERROR("Write verify error, ignoring\n");
		return STLINK_ERROR_OK;
	case STLINK_SWD_AP_FAULT:
		/* git://git.ac6.fr/openocd commit 657e3e885b9ee10
			 * returns STLINK_ERROR_OK with the comment:
			 * Change in error status when reading outside RAM.
			 * This fix allows CDT plugin to visualize memory.
			 */
		stlink.ap_error = true;
		if (verbose)
			DEBUG_ERROR("STLINK_SWD_AP_FAULT\n");
		return STLINK_ERROR_AP_FAULT;
	case STLINK_SWD_AP_ERROR:
		if (verbose)
			DEBUG_ERROR("STLINK_SWD_AP_ERROR\n");
		return STLINK_ERROR_FAIL;
	case STLINK_SWD_AP_PARITY_ERROR:
		if (verbose)
			DEBUG_ERROR("STLINK_SWD_AP_PARITY_ERROR\n");
		return STLINK_ERROR_FAIL;
	case STLINK_SWD_DP_FAULT:
		if (verbose)
			DEBUG_ERROR("STLINK_SWD_DP_FAULT\n");
		return STLINK_ERROR_FAIL;
	case STLINK_SWD_DP_ERROR:
		if (verbose)
			DEBUG_ERROR("STLINK_SWD_DP_ERROR\n");
		raise_exception(EXCEPTION_ERROR, "STLINK_SWD_DP_ERROR");
		return STLINK_ERROR_FAIL;
	case STLINK_SWD_DP_PARITY_ERROR:
		if (verbose)
			DEBUG_ERROR("STLINK_SWD_DP_PARITY_ERROR\n");
		return STLINK_ERROR_FAIL;
	case STLINK_SWD_AP_WDATA_ERROR:
		if (verbose)
			DEBUG_ERROR("STLINK_SWD_AP_WDATA_ERROR\n");
		return STLINK_ERROR_FAIL;
	case STLINK_SWD_AP_STICKY_ERROR:
		if (verbose)
			DEBUG_ERROR("STLINK_SWD_AP_STICKY_ERROR\n");
		stlink.ap_error = true;
		return STLINK_ERROR_FAIL;
	case STLINK_SWD_AP_STICKYORUN_ERROR:
		if (verbose)
			DEBUG_ERROR("STLINK_SWD_AP_STICKYORUN_ERROR\n");
		return STLINK_ERROR_FAIL;
	case STLINK_BAD_AP_ERROR:
		/* ADIV5 probe 256 APs, most of them are non exisitant.*/
		return STLINK_ERROR_FAIL;
	case STLINK_TOO_MANY_AP_ERROR:
		/* TI TM4C duplicates AP. Error happens at AP9.*/
		if (verbose)
			DEBUG_ERROR("STLINK_TOO_MANY_AP_ERROR\n");
		return STLINK_ERROR_FAIL;
	case STLINK_JTAG_UNKNOWN_CMD:
		if (verbose)
			DEBUG_ERROR("STLINK_JTAG_UNKNOWN_CMD\n");
		return STLINK_ERROR_FAIL;
	default:
		if (verbose)
			DEBUG_ERROR("unknown/unexpected ST-Link status code 0x%x\n", data[0]);
		return STLINK_ERROR_FAIL;
	}
}

int stlink_send_recv_retry(
	const void *const req_buffer, const size_t req_len, void *const rx_buffer, const size_t rx_len)
{
	uint32_t start = platform_time_ms();
	int res;
	int first_res = STLINK_ERROR_OK;
	while (true) {
		bmda_usb_transfer(info.usb_link, req_buffer, req_len, rx_buffer, rx_len, BMDA_USB_NO_TIMEOUT);
		res = stlink_usb_error_check(rx_buffer, false);
		if (res == STLINK_ERROR_OK)
			return res;
		if (res == STLINK_ERROR_AP_FAULT && first_res == STLINK_ERROR_WAIT) {
			/*
			 * ST-Link v3 while AP is busy answers once with ERROR_WAIT, then
			 * with AP_FAULT and finally with ERROR_OK and the pending result.
			 * Interpret AP_FAULT as AP_WAIT in this case.
			 */
			stlink.ap_error = false;
			res = STLINK_ERROR_WAIT;
		}
		if (first_res == STLINK_ERROR_OK)
			first_res = res;
		uint32_t now = platform_time_ms();
		if (now - start > cortexm_wait_timeout || res != STLINK_ERROR_WAIT) {
			DEBUG_ERROR("send_recv_retry failed.\n");
			return res;
		}
	}
	return res;
}

static int stlink_read_retry(
	const void *const req_buffer, const size_t req_len, void *const rx_buffer, const size_t rx_len)
{
	uint32_t start = platform_time_ms();
	int res;
	while (true) {
		bmda_usb_transfer(info.usb_link, req_buffer, req_len, rx_buffer, rx_len, BMDA_USB_NO_TIMEOUT);
		res = stlink_usb_get_rw_status(false);
		if (res == STLINK_ERROR_OK)
			return res;
		uint32_t now = platform_time_ms();
		if (now - start > 1000U || res != STLINK_ERROR_WAIT) {
			DEBUG_ERROR("stlink_read_retry failed.\n");
			stlink_usb_get_rw_status(true);
			return res;
		}
	}
	return res;
}

static int stlink_write_retry(
	const void *const req_buffer, const size_t req_len, const void *const tx_buffer, const size_t tx_len)
{
	uint32_t start = platform_time_ms();
	int res;
	usb_link_s *link = info.usb_link;
	while (true) {
		bmda_usb_transfer(link, req_buffer, req_len, NULL, 0, BMDA_USB_NO_TIMEOUT);
		bmda_usb_transfer(link, tx_buffer, tx_len, NULL, 0, BMDA_USB_NO_TIMEOUT);
		res = stlink_usb_get_rw_status(false);
		if (res == STLINK_ERROR_OK)
			return res;
		uint32_t now = platform_time_ms();
		if (now - start > 1000U || res != STLINK_ERROR_WAIT) {
			stlink_usb_get_rw_status(true);
			return res;
		}
	}
	return res;
}

int stlink_simple_query(const uint8_t command, const uint8_t operation, void *const rx_buffer, const size_t rx_len)
{
	const stlink_simple_command_s request = {
		.command = command,
		.operation = operation,
	};
	return bmda_usb_transfer(info.usb_link, &request, sizeof(request), rx_buffer, rx_len, BMDA_USB_NO_TIMEOUT);
}

int stlink_simple_request(
	const uint8_t command, const uint8_t operation, const uint8_t param, void *const rx_buffer, const size_t rx_len)
{
	const stlink_simple_request_s request = {
		.command = command,
		.operation = operation,
		.param = param,
	};
	return bmda_usb_transfer(info.usb_link, &request, sizeof(request), rx_buffer, rx_len, BMDA_USB_NO_TIMEOUT);
}

/*
 * Version data is at 0x080103f8 with STLINKV3 bootloader flashed with
 * STLinkUpgrade_v3[3|5].jar
 */
static void stlink_version(void)
{
	if (stlink.ver_hw == 30) {
		uint8_t data[12];
		int size = stlink_simple_query(STLINK_APIV3_GET_VERSION_EX, 0, data, sizeof(data));
		if (size == -1)
			DEBUG_WARN("[!] stlink_send_recv STLINK_APIV3_GET_VERSION_EX\n");

		stlink.ver_stlink = data[0];
		stlink.ver_swim = data[1];
		stlink.ver_jtag = data[2];
		stlink.ver_mass = data[3];
		stlink.ver_bridge = data[4];
		stlink.block_size = 512;
		stlink.vid = (data[3] << 9U) | data[8];
		stlink.pid = (data[5] << 11U) | data[10];
	} else {
		uint8_t data[6];
		int size = stlink_simple_query(STLINK_GET_VERSION, 0, data, sizeof(data));
		if (size == -1)
			DEBUG_WARN("[!] stlink_send_recv STLINK_GET_VERSION_EX\n");
		stlink.vid = (data[3] << 8U) | data[2];
		stlink.pid = (data[5] << 8U) | data[4];
		uint16_t version = (data[0] << 8U) | data[1]; /* Big endian here!*/
		stlink.block_size = 64;
		stlink.ver_stlink = (version >> 12U) & 0x0fU;
		stlink.ver_jtag = (version >> 6U) & 0x3fU;
		if (stlink.pid == PRODUCT_ID_STLINKV21_MSD || stlink.pid == PRODUCT_ID_STLINKV21)
			stlink.ver_mass = version & 0x3fU;
		else
			stlink.ver_swim = version & 0x3fU;
	}
	DEBUG_INFO("ST-Link firmware version: V%hhuJ%hhu", stlink.ver_stlink, stlink.ver_jtag);
	if (stlink.ver_hw == 30U)
		DEBUG_INFO("M%hhuB%hhuS%hhu", stlink.ver_mass, stlink.ver_bridge, stlink.ver_swim);
	else if (stlink.ver_hw == 20U)
		DEBUG_INFO("S%hhu", stlink.ver_swim);
	else if (stlink.ver_hw == 21U)
		DEBUG_INFO("M%hhu", stlink.ver_mass);
	DEBUG_INFO("\n");
}

bool stlink_leave_state(void)
{
	uint8_t data[2];
	stlink_simple_query(STLINK_GET_CURRENT_MODE, 0, data, sizeof(data));
	if (data[0] == STLINK_DEV_DFU_MODE) {
		DEBUG_INFO("Leaving DFU Mode\n");
		stlink_simple_query(STLINK_DFU_COMMAND, STLINK_DFU_EXIT, NULL, 0);
		return true;
	}
	if (data[0] == STLINK_DEV_SWIM_MODE) {
		DEBUG_INFO("Leaving SWIM Mode\n");
		stlink_simple_query(STLINK_SWIM_COMMAND, STLINK_SWIM_EXIT, NULL, 0);
	} else if (data[0] == STLINK_DEV_DEBUG_MODE) {
		DEBUG_INFO("Leaving DEBUG Mode\n");
		stlink_simple_query(STLINK_DEBUG_COMMAND, STLINK_DEBUG_EXIT, NULL, 0);
	} else if (data[0] == STLINK_DEV_BOOTLOADER_MODE)
		DEBUG_INFO("Leaving BOOTLOADER Mode\n");
	else if (data[0] == STLINK_DEV_MASS_MODE)
		DEBUG_INFO("Leaving MASS Mode\n");
	else
		DEBUG_INFO("Unknown Mode %02x\n", data[0]);
	return false;
}

const char *stlink_target_voltage(void)
{
	uint8_t data[8];
	stlink_simple_query(STLINK_GET_TARGET_VOLTAGE, 0, data, sizeof(data));
	uint16_t adc[2];
	adc[0] = data[0] | (data[1] << 8U); /* Calibration value? */
	adc[1] = data[4] | (data[5] << 8U); /* Measured value?*/
	float result = 0.0F;
	if (adc[0])
		result = 2.0F * (float)adc[1] * 1.2F / (float)adc[0];
	static char res[6];
	sprintf(res, "%4.2fV", result);
	return res;
}

static void stlink_reset_adaptor(void)
{
	uint8_t data[2];
	stlink_simple_query(STLINK_DEBUG_COMMAND, STLINK_DEBUG_APIV2_RESETSYS, data, sizeof(data));
}

bool stlink_init(void)
{
	usb_link_s *link = calloc(1, sizeof(usb_link_s));
	if (!link)
		return false;
	info.usb_link = link;
	link->context = info.libusb_ctx;
	int result = libusb_open(info.libusb_dev, &link->device_handle);
	if (result != LIBUSB_SUCCESS) {
		DEBUG_ERROR("libusb_open() failed (%d): %s\n", result, libusb_error_name(result));
		DEBUG_WARN("Are you sure the permissions on the device are set correctly?\n");
		return false;
	}
	if (info.vid != VENDOR_ID_STLINK)
		return true;
	switch (info.pid) {
	case PRODUCT_ID_STLINKV2:
		stlink.ver_hw = 20U;
		link->ep_tx = 2U;
		stlink.ep_tx = 2U;
		break;
	case PRODUCT_ID_STLINKV21:
	case PRODUCT_ID_STLINKV21_MSD:
		stlink.ver_hw = 21U;
		link->ep_tx = 1U;
		stlink.ep_tx = 1U;
		break;
	case PRODUCT_ID_STLINKV3_BL:
	case PRODUCT_ID_STLINKV3:
	case PRODUCT_ID_STLINKV3E:
	case PRODUCT_ID_STLINKV3_NO_MSD:
		stlink.ver_hw = 30U;
		link->ep_tx = 1U;
		stlink.ep_tx = 1U;
		break;
	default:
		DEBUG_INFO("Unhandled STM32 device\n");
	}
	link->ep_rx = 1U;
	int config;
	result = libusb_get_configuration(link->device_handle, &config);
	if (result) {
		DEBUG_ERROR("ST-Link libusb_get_configuration failed %d: %s\n", result, libusb_strerror(result));
		return false;
	}
	if (config != 1) {
		result = libusb_set_configuration(link->device_handle, 0);
		if (result) {
			DEBUG_ERROR("ST-Link libusb_set_configuration failed %d: %s\n", result, libusb_strerror(result));
			return false;
		}
	}
	result = libusb_claim_interface(link->device_handle, 0);
	if (result) {
		DEBUG_ERROR("ST-Link libusb_claim_interface failed %s\n", libusb_strerror(result));
		return false;
	}
	stlink_version();
	if ((stlink.ver_stlink < 3U && stlink.ver_jtag < 32U) || (stlink.ver_stlink == 3U && stlink.ver_jtag < 3U)) {
		/* Maybe the adapter is in some strange state. Try to reset */
		result = libusb_reset_device(link->device_handle);
		DEBUG_WARN("Trying ST-Link reset\n");
		if (result == LIBUSB_ERROR_BUSY) { /* Try again */
			platform_delay(50);
			result = libusb_reset_device(link->device_handle);
		}
		if (result != LIBUSB_SUCCESS) {
			DEBUG_ERROR("ST-Link libusb_reset_device failed\n");
			return false;
		}
		stlink_version();
	}
	if ((stlink.ver_stlink < 3U && stlink.ver_jtag < 32U) || (stlink.ver_stlink == 3U && stlink.ver_jtag < 3U)) {
		DEBUG_WARN("Please update the firmware on your ST-Link\n");
		return false;
	}
	if (stlink_leave_state()) {
		DEBUG_WARN("ST-Link board was in DFU mode. Restart\n");
		return false;
	}
	stlink_reset_adaptor();
	return true;
}

void stlink_nrst_set_val(bool assert)
{
	uint8_t data[2];
	stlink_simple_request(STLINK_DEBUG_COMMAND, STLINK_DEBUG_APIV2_DRIVE_NRST,
		assert ? STLINK_DEBUG_APIV2_DRIVE_NRST_LOW : STLINK_DEBUG_APIV2_DRIVE_NRST_HIGH, data, sizeof(data));
	stlink.nrst = assert;
	stlink_usb_error_check(data, true);
}

bool stlink_nrst_get_val(void)
{
	return stlink.nrst;
}

int stlink_hwversion(void)
{
	return stlink.ver_stlink;
}

uint32_t stlink_dp_error(adiv5_debug_port_s *dp, const bool protocol_recovery)
{
	if ((dp->version >= 2 && dp->fault) || protocol_recovery) {
		/*
		 * Note that on DPv2+ devices, during a protocol error condition
		 * the target becomes deselected during line reset. Once reset,
		 * we must then re-select the target to bring the device back
		 * into the expected state.
		 */
		stlink_reset_adaptor();
		if (dp->version >= 2)
			adiv5_dp_write(dp, ADIV5_DP_TARGETSEL, dp->targetsel);
		adiv5_dp_read(dp, ADIV5_DP_DPIDR);
	}
	const uint32_t err = adiv5_dp_read(dp, ADIV5_DP_CTRLSTAT) &
		(ADIV5_DP_CTRLSTAT_STICKYORUN | ADIV5_DP_CTRLSTAT_STICKYCMP | ADIV5_DP_CTRLSTAT_STICKYERR |
			ADIV5_DP_CTRLSTAT_WDATAERR);
	uint32_t clr = 0;

	if (err & ADIV5_DP_CTRLSTAT_STICKYORUN)
		clr |= ADIV5_DP_ABORT_ORUNERRCLR;
	if (err & ADIV5_DP_CTRLSTAT_STICKYCMP)
		clr |= ADIV5_DP_ABORT_STKCMPCLR;
	if (err & ADIV5_DP_CTRLSTAT_STICKYERR)
		clr |= ADIV5_DP_ABORT_STKERRCLR;
	if (err & ADIV5_DP_CTRLSTAT_WDATAERR)
		clr |= ADIV5_DP_ABORT_WDERRCLR;

	if (clr)
		adiv5_dp_write(dp, ADIV5_DP_ABORT, clr);
	dp->fault = 0;
	stlink.ap_error = false;
	return err;
}

void stlink_dp_abort(adiv5_debug_port_s *dp, uint32_t abort)
{
	adiv5_dp_write(dp, ADIV5_DP_ABORT, abort);
}

static int stlink_read_dp_register(const uint16_t apsel, const uint16_t address, uint32_t *const reg)
{
	stlink_adiv5_reg_read_s request = {
		.command = STLINK_DEBUG_COMMAND,
		.operation = STLINK_DEBUG_APIV2_READ_DAP_REG,
	};
	write_le2(request.apsel, 0, apsel);
	write_le2(request.address, 0, address);
	if (apsel == STLINK_DEBUG_PORT && stlink.dap_select)
		request.address[0] = ((stlink.dap_select & 0xfU) << 4U) | (address & 0xfU);

	uint8_t data[8];
	int result = stlink_send_recv_retry(&request, sizeof(request), data, sizeof(data));
	if (result == STLINK_ERROR_OK)
		*reg = read_le4(data, 4U);
	else
		DEBUG_ERROR("%s error %d\n", __func__, result);
	return result;
}

static int stlink_write_dp_register(const uint16_t apsel, const uint16_t address, const uint32_t value)
{
	if (apsel == STLINK_DEBUG_PORT && address == 8U) {
		stlink.dap_select = value;
		DEBUG_PROBE("Caching SELECT 0x%02" PRIx32 "\n", value);
		return STLINK_ERROR_OK;
	}

	stlink_adiv5_reg_write_s request = {
		.command = STLINK_DEBUG_COMMAND,
		.operation = STLINK_DEBUG_APIV2_WRITE_DAP_REG,
	};
	write_le2(request.apsel, 0, apsel);
	write_le2(request.address, 0, address);
	write_le4(request.value, 0, value);
	uint8_t data[2];
	stlink_send_recv_retry(&request, sizeof(request), data, sizeof(data));
	return stlink_usb_error_check(data, true);
}

uint32_t stlink_raw_access(adiv5_debug_port_s *dp, uint8_t rnw, uint16_t addr, uint32_t value)
{
	uint32_t response = 0;
	const int result = rnw ? stlink_read_dp_register(addr < 0x100U ? STLINK_DEBUG_PORT : 0U, addr, &response) :
							 stlink_write_dp_register(addr < 0x100U ? STLINK_DEBUG_PORT : 0U, addr, value);

	if (result == STLINK_ERROR_WAIT) {
		DEBUG_ERROR("SWD access resulted in wait, aborting\n");
		dp->fault = SWDP_ACK_WAIT;
		return 0;
	}

	if (result == STLINK_ERROR_DP_FAULT || result == STLINK_ERROR_AP_FAULT) {
		DEBUG_ERROR("SWD access resulted in fault\n");
		dp->fault = SWDP_ACK_FAULT;
		return 0;
	}

	if (result == STLINK_ERROR_FAIL)
		raise_exception(EXCEPTION_ERROR, "SWD invalid ACK");
	return response;
}

static bool stlink_ap_setup(const uint8_t ap)
{
	if (ap > 7)
		return false;
	const stlink_simple_request_s command = {
		STLINK_DEBUG_COMMAND,
		STLINK_DEBUG_APIV2_INIT_AP,
		ap,
	};
	uint8_t data[2];
	DEBUG_PROBE("%s: AP %u\n", __func__, ap);
	stlink_send_recv_retry(&command, sizeof(command), data, sizeof(data));
	int res = stlink_usb_error_check(data, true);
	if (res && stlink.ver_hw == 30)
		DEBUG_WARN("ST-Link v3 only connects to STM8/32!\n");
	return !res;
}

static void stlink_ap_cleanup(const uint8_t ap)
{
	uint8_t data[2];
	stlink_simple_request(STLINK_DEBUG_COMMAND, STLINK_DEBUG_APIV2_CLOSE_AP_DBG, ap, data, sizeof(data));
	DEBUG_PROBE("%s: AP %u\n", __func__, ap);
	stlink_usb_error_check(data, true);
}

static int stlink_usb_get_rw_status(bool verbose)
{
	uint8_t data[12];
	stlink_simple_query(STLINK_DEBUG_COMMAND, STLINK_DEBUG_APIV2_GETLASTRWSTATUS2, data, sizeof(data));
	return stlink_usb_error_check(data, verbose);
}

static void stlink_mem_read(adiv5_access_port_s *ap, void *dest, uint32_t src, size_t len)
{
	if (len == 0)
		return;
	if (len > stlink.block_size) {
		DEBUG_WARN("Too large!\n");
		return;
	}

	uint8_t type;
	if ((src & 1U) || (len & 1U))
		type = STLINK_DEBUG_READMEM_8BIT;
	else if ((src & 3U) || (len & 3U))
		type = STLINK_DEBUG_APIV2_READMEM_16BIT;
	else
		type = STLINK_DEBUG_READMEM_32BIT;

	/* Build the command packet and perform the access */
	stlink_mem_command_s command = stlink_memory_access(type, src, len, ap->apsel);
	int res = 0;
	if (len > 1)
		res = stlink_read_retry(&command, sizeof(command), dest, len);
	else {
		/*
		 * Due to an artefact of how the ST-Link protocol works (minimum read size is 2),
		 * a single byte read must be done into a 2 byte buffer
		 */
		uint8_t buffer[2];
		res = stlink_read_retry(&command, sizeof(command), buffer, sizeof(buffer));
		/* But we only want and need to keep a single byte from this */
		memcpy(dest, buffer, 1);
	}
	if (res != STLINK_ERROR_OK) {
		/* FIXME: What is the right measure when failing?
		 *
		 * E.g. TM4C129 gets here when NRF probe reads 0x10000010
		 * Approach taken:
		 * Fill the memory with some fixed pattern so hopefully
		 * the caller notices the error*/
		DEBUG_ERROR("stlink_mem_read from  %" PRIx32 " to %p, len %zu failed\n", src, dest, len);
		memset(dest, 0xff, len);
	}
	DEBUG_PROBE("stlink_mem_read from %" PRIx32 " to %p, len %zu\n", src, dest, len);
}

static void stlink_mem_write(
	adiv5_access_port_s *const ap, const uint32_t dest, const void *const src, const size_t len, const align_e align)
{
	if (len == 0)
		return;
	const uint8_t *const data = (const uint8_t *)src;
	/* Chunk the write up into stlink.block_size blocks */
	for (size_t offset = 0; offset < len; offset += stlink.block_size) {
		/* Figure out how many bytes are in the block and at what start address */
		const size_t amount = MIN(len - offset, stlink.block_size);
		const uint32_t addr = dest + offset;
		/* Now generate an appropriate access packet */
		stlink_mem_command_s command;
		switch (align) {
		case ALIGN_BYTE:
			command = stlink_memory_access(STLINK_DEBUG_WRITEMEM_8BIT, addr, amount, ap->apsel);
			break;
		case ALIGN_HALFWORD:
			command = stlink_memory_access(STLINK_DEBUG_APIV2_WRITEMEM_16BIT, addr, amount, ap->apsel);
			break;
		case ALIGN_WORD:
		case ALIGN_DWORD:
			command = stlink_memory_access(STLINK_DEBUG_WRITEMEM_32BIT, addr, amount, ap->apsel);
			break;
		}
		/* And perform the block write */
		stlink_write_retry(&command, sizeof(command), data + offset, amount);
	}
}

static void stlink_regs_read(adiv5_access_port_s *ap, void *data)
{
	uint8_t result[88];
	DEBUG_PROBE("%s: AP %u\n", __func__, ap->apsel);
	stlink_simple_request(STLINK_DEBUG_COMMAND, STLINK_DEBUG_APIV2_READALLREGS, ap->apsel, result, sizeof(result));
	stlink_usb_error_check(result, true);
	/* Ignore the first 4 bytes as protocol overhead */
	memcpy(data, result + 4U, sizeof(result) - 4U);
}

static uint32_t stlink_reg_read(adiv5_access_port_s *const ap, const uint8_t reg_num)
{
	uint8_t data[8];
	const stlink_arm_reg_read_s request = {
		.command = STLINK_DEBUG_COMMAND,
		.operation = STLINK_DEBUG_APIV2_READREG,
		.reg_num = reg_num,
		.apsel = ap->apsel,
	};
	bmda_usb_transfer(info.usb_link, &request, sizeof(request), data, sizeof(data), BMDA_USB_NO_TIMEOUT);
	stlink_usb_error_check(data, true);
	const uint32_t result = read_le4(data, 0U);
	DEBUG_PROBE("%s: AP %u, reg %02u val 0x%08" PRIx32 "\n", __func__, ap->apsel, reg_num, result);
	return result;
}

static void stlink_reg_write(adiv5_access_port_s *const ap, const uint8_t reg_num, const uint32_t value)
{
	uint8_t res[2];
	stlink_arm_reg_write_s request = {
		.command = STLINK_DEBUG_COMMAND,
		.operation = STLINK_DEBUG_APIV2_WRITEREG,
		.reg_num = reg_num,
		.apsel = ap->apsel,
	};
	write_le4(request.value, 0U, value);
	bmda_usb_transfer(info.usb_link, &request, sizeof(request), res, sizeof(res), BMDA_USB_NO_TIMEOUT);
	DEBUG_PROBE("%s: AP %u, reg %02u val 0x%08" PRIx32 "\n", __func__, ap->apsel, reg_num, value);
	stlink_usb_error_check(res, true);
}

static void stlink_ap_write(adiv5_access_port_s *ap, uint16_t addr, uint32_t value)
{
	stlink_write_dp_register(ap->apsel, addr, value);
}

static uint32_t stlink_ap_read(adiv5_access_port_s *ap, uint16_t addr)
{
	uint32_t ret = 0;
	stlink_read_dp_register(ap->apsel, addr, &ret);
	return ret;
}

void stlink_adiv5_dp_init(adiv5_debug_port_s *dp)
{
	dp->ap_regs_read = stlink_regs_read;
	dp->ap_reg_read = stlink_reg_read;
	dp->ap_reg_write = stlink_reg_write;
	dp->ap_setup = stlink_ap_setup;
	dp->ap_cleanup = stlink_ap_cleanup;
	dp->ap_write = stlink_ap_write;
	dp->ap_read = stlink_ap_read;
	dp->mem_read = stlink_mem_read;
	dp->mem_write = stlink_mem_write;
}

static void stlink_v2_set_frequency(const uint32_t freq)
{
	stlink_v2_set_freq_s request = {
		.command = STLINK_DEBUG_COMMAND,
		.operation = info.is_jtag ? STLINK_DEBUG_APIV2_JTAG_SET_FREQ : STLINK_DEBUG_APIV2_SWD_SET_FREQ,
	};

	if (info.is_jtag) {
		/*
		 * The minimum divisor is /4, so cap freq before computing the divisor.
		 * Additionally, the divisor must be a power of 2 and no more than 256.
		 */
		const uint32_t adjusted_freq =
			MAX(STLINK_V2_MIN_JTAG_CLOCK_FREQ, MIN(freq, STLINK_V2_MAX_JTAG_CLOCK_FREQ) + 1U);
		const uint32_t divisor = STLINK_V2_CPU_CLOCK_FREQ / adjusted_freq;
		/*
		 * divisor is now a value between 4 and 256, but may not be a power of 2,
		 * so do PoT rounding to the nearest higher value.
		 *
		 * This algorithm was derived from the information available from
		 * http://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2
		 * For a worked example of it in action, see https://bmp.godbolt.org/z/Pqhjco8e3
		 */
		stlink_v2_divisor = 1U << ulog2(divisor);
		stlink_v2_divisor /= STLINK_V2_JTAG_MUL_FACTOR;
	} else {
		/* Adjust the clock frequency request to result in the corrector dividor */
		const uint32_t adjusted_freq = MAX(STLINK_V2_MIN_SWD_CLOCK_FREQ, MIN(freq, STLINK_V2_MAX_SWD_CLOCK_FREQ) + 1U);
		const uint32_t divisor = STLINK_V2_CPU_CLOCK_FREQ / adjusted_freq;
		/* Then compute the divisor using the multiplication factor */
		stlink_v2_divisor = divisor / STLINK_V2_SWD_MUL_FACTOR;
	}

	const uint16_t freq_mhz = freq / 1000000U;
	const uint16_t freq_khz = (freq / 1000U) - (freq_mhz * 1000U);
	DEBUG_WARN("Divisor for %u.%03uMHz is %u\n", freq_mhz, freq_khz, stlink_v2_divisor);
	write_le2(request.divisor, 0U, stlink_v2_divisor);
	uint8_t data[2];
	bmda_usb_transfer(info.usb_link, &request, sizeof(request), data, sizeof(data), BMDA_USB_NO_TIMEOUT);
	if (stlink_usb_error_check(data, false))
		DEBUG_ERROR("Set frequency failed!\n");
}

static void stlink_v3_set_frequency(const uint32_t freq)
{
	const uint8_t mode = info.is_jtag ? STLINK_MODE_JTAG : STLINK_MODE_SWD;
	uint8_t data[52];
	stlink_simple_request(STLINK_DEBUG_COMMAND, STLINK_APIV3_GET_COM_FREQ, mode, data, sizeof(data));
	stlink_usb_error_check(data, true);
	uint32_t frequency = 0;
	DEBUG_INFO("Available speed settings: ");
	for (size_t i = 0; i < STLINK_V3_FREQ_ENTRY_COUNT; ++i) {
		const size_t offset = 12U + (i * 4U);
		const uint32_t new_freq = read_le4(data, offset);
		if (!new_freq)
			break;
		frequency = new_freq;
		DEBUG_INFO("%s%u", i ? "/" : "", frequency);
		if (freq / 1000U >= frequency)
			break;
	}
	DEBUG_INFO(" kHz for %s\n", info.is_jtag ? "JTAG" : "SWD");
	stlink_v3_set_freq_s request = {
		.command = STLINK_DEBUG_COMMAND,
		.operation = STLINK_APIV3_SET_COM_FREQ,
		.mode = mode,
	};
	write_le4(request.frequency, 0U, frequency);
	bmda_usb_transfer(info.usb_link, &request, sizeof(request), data, 8U, BMDA_USB_NO_TIMEOUT);
	stlink_usb_error_check(data, true);
	stlink_v3_freq[mode] = frequency * 1000U;
}

void stlink_max_frequency_set(const uint32_t freq)
{
	if (stlink.ver_hw == 30U)
		stlink_v3_set_frequency(freq);
	else
		stlink_v2_set_frequency(freq);
}

uint32_t stlink_max_frequency_get(void)
{
	if (stlink.ver_hw == 30U)
		return stlink_v3_freq[info.is_jtag ? STLINK_MODE_JTAG : STLINK_MODE_SWD];

	if (info.is_jtag)
		return STLINK_V2_CPU_CLOCK_FREQ / (STLINK_V2_JTAG_MUL_FACTOR * stlink_v2_divisor);
	return STLINK_V2_CPU_CLOCK_FREQ / (STLINK_V2_SWD_MUL_FACTOR * (stlink_v2_divisor + 1U));
}
