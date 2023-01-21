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
#include "exception.h"
#include "jtag_devs.h"
#include "target.h"
#include "cortexm.h"
#include "target_internal.h"

#include <assert.h>
#include <unistd.h>
#include <signal.h>
#include <ctype.h>
#include <sys/time.h>

#include "cli.h"

#define STLINK_SWIM_ERR_OK             0x00U
#define STLINK_SWIM_BUSY               0x01U
#define STLINK_DEBUG_ERR_OK            0x80U
#define STLINK_DEBUG_ERR_FAULT         0x81U
#define STLINK_JTAG_UNKNOWN_JTAG_CHAIN 0x04U
#define STLINK_NO_DEVICE_CONNECTED     0x05U
#define STLINK_JTAG_COMMAND_ERROR      0x08U
#define STLINK_JTAG_COMMAND_ERROR      0x08U
#define STLINK_JTAG_GET_IDCODE_ERROR   0x09U
#define STLINK_JTAG_DBG_POWER_ERROR    0x0bU
#define STLINK_SWD_AP_WAIT             0x10U
#define STLINK_SWD_AP_FAULT            0x11U
#define STLINK_SWD_AP_ERROR            0x12U
#define STLINK_SWD_AP_PARITY_ERROR     0x13U
#define STLINK_JTAG_WRITE_ERROR        0x0cU
#define STLINK_JTAG_WRITE_VERIF_ERROR  0x0dU
#define STLINK_SWD_DP_WAIT             0x14U
#define STLINK_SWD_DP_FAULT            0x15U
#define STLINK_SWD_DP_ERROR            0x16U
#define STLINK_SWD_DP_PARITY_ERROR     0x17U

#define STLINK_SWD_AP_WDATA_ERROR      0x18U
#define STLINK_SWD_AP_STICKY_ERROR     0x19U
#define STLINK_SWD_AP_STICKYORUN_ERROR 0x1aU
#define STLINK_BAD_AP_ERROR            0x1dU
#define STLINK_TOO_MANY_AP_ERROR       0x29U
#define STLINK_JTAG_UNKNOWN_CMD        0x42U

#define STLINK_CORE_RUNNING      0x80U
#define STLINK_CORE_HALTED       0x81U
#define STLINK_CORE_STAT_UNKNOWN (-1)

#define STLINK_GET_VERSION        0xf1U
#define STLINK_DEBUG_COMMAND      0xf2U
#define STLINK_DFU_COMMAND        0xf3U
#define STLINK_SWIM_COMMAND       0xf4U
#define STLINK_GET_CURRENT_MODE   0xf5U
#define STLINK_GET_TARGET_VOLTAGE 0xf7U

#define STLINK_DEV_DFU_MODE        0x00U
#define STLINK_DEV_MASS_MODE       0x01U
#define STLINK_DEV_DEBUG_MODE      0x02U
#define STLINK_DEV_SWIM_MODE       0x03U
#define STLINK_DEV_BOOTLOADER_MODE 0x04U
#define STLINK_DEV_UNKNOWN_MODE    (-1)

#define STLINK_DFU_EXIT 0x07U

#define STLINK_SWIM_ENTER          0x00U
#define STLINK_SWIM_EXIT           0x01U
#define STLINK_SWIM_READ_CAP       0x02U
#define STLINK_SWIM_SPEED          0x03U
#define STLINK_SWIM_ENTER_SEQ      0x04U
#define STLINK_SWIM_GEN_RST        0x05U
#define STLINK_SWIM_RESET          0x06U
#define STLINK_SWIM_ASSERT_RESET   0x07U
#define STLINK_SWIM_DEASSERT_RESET 0x08U
#define STLINK_SWIM_READSTATUS     0x09U
#define STLINK_SWIM_WRITEMEM       0x0aU
#define STLINK_SWIM_READMEM        0x0bU
#define STLINK_SWIM_READBUF        0x0cU

#define STLINK_DEBUG_GETSTATUS           0x01U
#define STLINK_DEBUG_FORCEDEBUG          0x02U
#define STLINK_DEBUG_APIV1_RESETSYS      0x03U
#define STLINK_DEBUG_APIV1_READALLREGS   0x04U
#define STLINK_DEBUG_APIV1_READREG       0x05U
#define STLINK_DEBUG_APIV1_WRITEREG      0x06U
#define STLINK_DEBUG_READMEM_32BIT       0x07U
#define STLINK_DEBUG_WRITEMEM_32BIT      0x08U
#define STLINK_DEBUG_RUNCORE             0x09U
#define STLINK_DEBUG_STEPCORE            0x0aU
#define STLINK_DEBUG_APIV1_SETFP         0x0bU
#define STLINK_DEBUG_READMEM_8BIT        0x0cU
#define STLINK_DEBUG_WRITEMEM_8BIT       0x0dU
#define STLINK_DEBUG_APIV1_CLEARFP       0x0eU
#define STLINK_DEBUG_APIV1_WRITEDEBUGREG 0x0fU
#define STLINK_DEBUG_APIV1_SETWATCHPOINT 0x10U

#define STLINK_DEBUG_ENTER_JTAG_RESET    0x00U
#define STLINK_DEBUG_ENTER_SWD_NO_RESET  0xa3U
#define STLINK_DEBUG_ENTER_JTAG_NO_RESET 0xa4U

#define STLINK_DEBUG_APIV1_ENTER 0x20U
#define STLINK_DEBUG_EXIT        0x21U
#define STLINK_DEBUG_READCOREID  0x22U

#define STLINK_DEBUG_APIV2_ENTER         0x30U
#define STLINK_DEBUG_APIV2_READ_IDCODES  0x31U
#define STLINK_DEBUG_APIV2_RESETSYS      0x32U
#define STLINK_DEBUG_APIV2_READREG       0x33U
#define STLINK_DEBUG_APIV2_WRITEREG      0x34U
#define STLINK_DEBUG_APIV2_WRITEDEBUGREG 0x35U
#define STLINK_DEBUG_APIV2_READDEBUGREG  0x36U

#define STLINK_DEBUG_APIV2_READALLREGS     0x3aU
#define STLINK_DEBUG_APIV2_GETLASTRWSTATUS 0x3bU
#define STLINK_DEBUG_APIV2_DRIVE_NRST      0x3cU

#define STLINK_DEBUG_APIV2_GETLASTRWSTATUS2 0x3eU

#define STLINK_DEBUG_APIV2_START_TRACE_RX 0x40U
#define STLINK_DEBUG_APIV2_STOP_TRACE_RX  0x41U
#define STLINK_DEBUG_APIV2_GET_TRACE_NB   0x42U
#define STLINK_DEBUG_APIV2_SWD_SET_FREQ   0x43U
#define STLINK_DEBUG_APIV2_JTAG_SET_FREQ  0x44U
#define STLINK_DEBUG_APIV2_READ_DAP_REG   0x45U
#define STLINK_DEBUG_APIV2_WRITE_DAP_REG  0x46U
#define STLINK_DEBUG_APIV2_READMEM_16BIT  0x47U
#define STLINK_DEBUG_APIV2_WRITEMEM_16BIT 0x48U

#define STLINK_DEBUG_APIV2_INIT_AP      0x4bU
#define STLINK_DEBUG_APIV2_CLOSE_AP_DBG 0x4cU

#define STLINK_APIV3_SET_COM_FREQ 0x61U
#define STLINK_APIV3_GET_COM_FREQ 0x62U

#define STLINK_APIV3_GET_VERSION_EX 0xfbU

#define STLINK_DEBUG_APIV2_DRIVE_NRST_LOW   0x00U
#define STLINK_DEBUG_APIV2_DRIVE_NRST_HIGH  0x01U
#define STLINK_DEBUG_APIV2_DRIVE_NRST_PULSE 0x02U

#define STLINK_TRACE_SIZE   4096U
#define STLINK_TRACE_MAX_HZ 2000000U

#define STLINK_V3_MAX_FREQ_NB 10U

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

stlink_s stlink;

static int stlink_usb_get_rw_status(bool verbose);

int debug_level = 0;

#define STLINK_ERROR_DP_FAULT (-2)
#define STLINK_ERROR_AP_FAULT (-3)

/**
    Converts an STLINK status code held in the first byte of a response to
	readable error
*/
static int stlink_usb_error_check(uint8_t *data, bool verbose)
{
	switch (data[0]) {
	case STLINK_DEBUG_ERR_OK:
		return STLINK_ERROR_OK;
	case STLINK_DEBUG_ERR_FAULT:
		if (verbose)
			DEBUG_WARN("SWD fault response (0x%x)\n", STLINK_DEBUG_ERR_FAULT);
		return STLINK_ERROR_FAIL;
	case STLINK_JTAG_UNKNOWN_JTAG_CHAIN:
		if (verbose)
			DEBUG_WARN("Unknown JTAG chain\n");
		return STLINK_ERROR_FAIL;
	case STLINK_NO_DEVICE_CONNECTED:
		if (verbose)
			DEBUG_WARN("No device connected\n");
		return STLINK_ERROR_FAIL;
	case STLINK_JTAG_COMMAND_ERROR:
		if (verbose)
			DEBUG_WARN("Command error\n");
		return STLINK_ERROR_FAIL;
	case STLINK_JTAG_GET_IDCODE_ERROR:
		if (verbose)
			DEBUG_WARN("Failure reading IDCODE\n");
		return STLINK_ERROR_FAIL;
	case STLINK_JTAG_DBG_POWER_ERROR:
		if (verbose)
			DEBUG_WARN("Failure powering DBG\n");
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
			DEBUG_WARN("Write error\n");
		return STLINK_ERROR_FAIL;
	case STLINK_JTAG_WRITE_VERIF_ERROR:
		if (verbose)
			DEBUG_WARN("Write verify error, ignoring\n");
		return STLINK_ERROR_OK;
	case STLINK_SWD_AP_FAULT:
		/* git://git.ac6.fr/openocd commit 657e3e885b9ee10
			 * returns STLINK_ERROR_OK with the comment:
			 * Change in error status when reading outside RAM.
			 * This fix allows CDT plugin to visualize memory.
			 */
		stlink.ap_error = true;
		if (verbose)
			DEBUG_WARN("STLINK_SWD_AP_FAULT\n");
		return STLINK_ERROR_AP_FAULT;
	case STLINK_SWD_AP_ERROR:
		if (verbose)
			DEBUG_WARN("STLINK_SWD_AP_ERROR\n");
		return STLINK_ERROR_FAIL;
	case STLINK_SWD_AP_PARITY_ERROR:
		if (verbose)
			DEBUG_WARN("STLINK_SWD_AP_PARITY_ERROR\n");
		return STLINK_ERROR_FAIL;
	case STLINK_SWD_DP_FAULT:
		if (verbose)
			DEBUG_WARN("STLINK_SWD_DP_FAULT\n");
		return STLINK_ERROR_FAIL;
	case STLINK_SWD_DP_ERROR:
		if (verbose)
			DEBUG_WARN("STLINK_SWD_DP_ERROR\n");
		raise_exception(EXCEPTION_ERROR, "STLINK_SWD_DP_ERROR");
		return STLINK_ERROR_FAIL;
	case STLINK_SWD_DP_PARITY_ERROR:
		if (verbose)
			DEBUG_WARN("STLINK_SWD_DP_PARITY_ERROR\n");
		return STLINK_ERROR_FAIL;
	case STLINK_SWD_AP_WDATA_ERROR:
		if (verbose)
			DEBUG_WARN("STLINK_SWD_AP_WDATA_ERROR\n");
		return STLINK_ERROR_FAIL;
	case STLINK_SWD_AP_STICKY_ERROR:
		if (verbose)
			DEBUG_WARN("STLINK_SWD_AP_STICKY_ERROR\n");
		stlink.ap_error = true;
		return STLINK_ERROR_FAIL;
	case STLINK_SWD_AP_STICKYORUN_ERROR:
		if (verbose)
			DEBUG_WARN("STLINK_SWD_AP_STICKYORUN_ERROR\n");
		return STLINK_ERROR_FAIL;
	case STLINK_BAD_AP_ERROR:
		/* ADIV5 probe 256 APs, most of them are non exisitant.*/
		return STLINK_ERROR_FAIL;
	case STLINK_TOO_MANY_AP_ERROR:
		/* TI TM4C duplicates AP. Error happens at AP9.*/
		if (verbose)
			DEBUG_WARN("STLINK_TOO_MANY_AP_ERROR\n");
		return STLINK_ERROR_FAIL;
	case STLINK_JTAG_UNKNOWN_CMD:
		if (verbose)
			DEBUG_WARN("STLINK_JTAG_UNKNOWN_CMD\n");
		return STLINK_ERROR_FAIL;
	default:
		if (verbose)
			DEBUG_WARN("unknown/unexpected ST-Link status code 0x%x\n", data[0]);
		return STLINK_ERROR_FAIL;
	}
}

static int stlink_send_recv_retry(uint8_t *txbuf, size_t txsize, uint8_t *rxbuf, size_t rxsize)
{
	uint32_t start = platform_time_ms();
	int res;
	int first_res = STLINK_ERROR_OK;
	usb_link_s *link = info.usb_link;
	while (true) {
		send_recv(link, txbuf, txsize, rxbuf, rxsize);
		res = stlink_usb_error_check(rxbuf, false);
		if (res == STLINK_ERROR_OK)
			return res;
		if (res == STLINK_ERROR_AP_FAULT && first_res == STLINK_ERROR_WAIT) {
			/* ST-Link v3 while AP is busy answers once with ERROR_WAIT, then
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
			DEBUG_WARN("send_recv_retry failed.\n");
			return res;
		}
	}
	return res;
}

static int read_retry(uint8_t *txbuf, size_t txsize, uint8_t *rxbuf, size_t rxsize)
{
	uint32_t start = platform_time_ms();
	int res;
	while (true) {
		send_recv(info.usb_link, txbuf, txsize, rxbuf, rxsize);
		res = stlink_usb_get_rw_status(false);
		if (res == STLINK_ERROR_OK)
			return res;
		uint32_t now = platform_time_ms();
		if (now - start > 1000U || res != STLINK_ERROR_WAIT) {
			DEBUG_WARN("read_retry failed.\n");
			stlink_usb_get_rw_status(true);
			return res;
		}
	}
	return res;
}

static int write_retry(uint8_t *cmdbuf, size_t cmdsize, uint8_t *txbuf, size_t txsize)
{
	uint32_t start = platform_time_ms();
	int res;
	usb_link_s *link = info.usb_link;
	while (true) {
		send_recv(link, cmdbuf, cmdsize, NULL, 0);
		send_recv(link, txbuf, txsize, NULL, 0);
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

/* Version data is at 0x080103f8 with STLINKV3 bootloader flashed with
 * STLinkUpgrade_v3[3|5].jar
 */
static void stlink_version(bmp_info_s *info)
{
	if (stlink.ver_hw == 30) {
		uint8_t cmd[16];
		uint8_t data[12];
		memset(cmd, 0, sizeof(cmd));
		cmd[0] = STLINK_APIV3_GET_VERSION_EX;
		int size = send_recv(info->usb_link, cmd, 16, data, 12);
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
		uint8_t cmd[16];
		uint8_t data[6];
		memset(cmd, 0, sizeof(cmd));
		cmd[0] = STLINK_GET_VERSION;
		int size = send_recv(info->usb_link, cmd, 16, data, 6);
		if (size == -1) {
			DEBUG_WARN("[!] stlink_send_recv STLINK_GET_VERSION_EX\n");
		}
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

static bool stlink_leave_state(bmp_info_s *info)
{
	uint8_t cmd[16];
	uint8_t data[2];
	memset(cmd, 0, sizeof(cmd));
	cmd[0] = STLINK_GET_CURRENT_MODE;
	send_recv(info->usb_link, cmd, 16, data, 2);
	if (data[0] == STLINK_DEV_DFU_MODE) {
		DEBUG_INFO("Leaving DFU Mode\n");
		memset(cmd, 0, sizeof(cmd));
		cmd[0] = STLINK_DFU_COMMAND;
		cmd[1] = STLINK_DFU_EXIT;
		send_recv(info->usb_link, cmd, 16, NULL, 0);
		return true;
	}
	if (data[0] == STLINK_DEV_SWIM_MODE) {
		DEBUG_INFO("Leaving SWIM Mode\n");
		memset(cmd, 0, sizeof(cmd));
		cmd[0] = STLINK_SWIM_COMMAND;
		cmd[1] = STLINK_SWIM_EXIT;
		send_recv(info->usb_link, cmd, 16, NULL, 0);
	} else if (data[0] == STLINK_DEV_DEBUG_MODE) {
		DEBUG_INFO("Leaving DEBUG Mode\n");
		memset(cmd, 0, sizeof(cmd));
		cmd[0] = STLINK_DEBUG_COMMAND;
		cmd[1] = STLINK_DEBUG_EXIT;
		send_recv(info->usb_link, cmd, 16, NULL, 0);
	} else if (data[0] == STLINK_DEV_BOOTLOADER_MODE)
		DEBUG_INFO("Leaving BOOTLOADER Mode\n");
	else if (data[0] == STLINK_DEV_MASS_MODE)
		DEBUG_INFO("Leaving MASS Mode\n");
	else
		DEBUG_INFO("Unknown Mode %02x\n", data[0]);
	return false;
}

const char *stlink_target_voltage(bmp_info_s *info)
{
	uint8_t cmd[16];
	uint8_t data[8];
	memset(cmd, 0, sizeof(cmd));
	cmd[0] = STLINK_GET_TARGET_VOLTAGE;
	send_recv(info->usb_link, cmd, 16, data, 8);
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

static void stlink_resetsys(bmp_info_s *info)
{
	uint8_t cmd[16];
	uint8_t data[2];
	memset(cmd, 0, sizeof(cmd));
	cmd[0] = STLINK_DEBUG_COMMAND;
	cmd[1] = STLINK_DEBUG_APIV2_RESETSYS;
	send_recv(info->usb_link, cmd, 16, data, 2);
}

int stlink_init(bmp_info_s *info)
{
	usb_link_s *sl = calloc(1, sizeof(usb_link_s));
	if (!sl)
		return -1;
	info->usb_link = sl;
	sl->ul_libusb_ctx = info->libusb_ctx;
	libusb_device **devs = NULL;
	const ssize_t cnt = libusb_get_device_list(info->libusb_ctx, &devs);
	if (cnt < 0) {
		DEBUG_WARN("FATAL: ST-Link libusb_get_device_list failed\n");
		return -1;
	}
	bool found = false;
	for (size_t i = 0; devs[i]; ++i) {
		libusb_device *dev = devs[i];
		struct libusb_device_descriptor desc;
		int result = libusb_get_device_descriptor(dev, &desc);
		if (result != LIBUSB_SUCCESS) {
			DEBUG_WARN("libusb_get_device_descriptor failed %s\n", libusb_strerror(result));
			return -1;
		}
		if (desc.idVendor != info->vid || desc.idProduct != info->pid)
			continue;

		result = libusb_open(dev, &sl->ul_libusb_device_handle);
		if (result != LIBUSB_SUCCESS) {
			DEBUG_WARN("Failed to open ST-Link device %04x:%04x - %s\n", desc.idVendor, desc.idProduct,
				libusb_strerror(result));
			DEBUG_WARN("Are you sure the permissions on the device are set correctly?\n");
			continue;
		}
		char serial[64];
		if (desc.iSerialNumber) {
			int result = libusb_get_string_descriptor_ascii(
				sl->ul_libusb_device_handle, desc.iSerialNumber, (uint8_t *)serial, sizeof(serial));
			/* If the call fails and it's not because the device gave us STALL, continue to the next one */
			if (result < 0 && result != LIBUSB_ERROR_PIPE) {
				libusb_close(sl->ul_libusb_device_handle);
				continue;
			}
			if (result <= 0)
				serial[0] = '\0';
		} else
			serial[0] = '\0';
		/* Likewise, if the serial number returned doesn't match the one in info, go to next */
		if (!strstr(serial, info->serial)) {
			libusb_close(sl->ul_libusb_device_handle);
			continue;
		}
		found = true;
		break;
	}
	libusb_free_device_list(devs, cnt);
	if (!found)
		return 1;
	if (info->vid != VENDOR_ID_STLINK)
		return 0;
	switch (info->pid) {
	case PRODUCT_ID_STLINKV2:
		stlink.ver_hw = 20U;
		info->usb_link->ep_tx = 2U;
		stlink.ep_tx = 2U;
		break;
	case PRODUCT_ID_STLINKV21:
	case PRODUCT_ID_STLINKV21_MSD:
		stlink.ver_hw = 21U;
		info->usb_link->ep_tx = 1U;
		stlink.ep_tx = 1U;
		break;
	case PRODUCT_ID_STLINKV3_BL:
	case PRODUCT_ID_STLINKV3:
	case PRODUCT_ID_STLINKV3E:
	case PRODUCT_ID_STLINKV3_NO_MSD:
		stlink.ver_hw = 30U;
		info->usb_link->ep_tx = 1U;
		stlink.ep_tx = 1U;
		break;
	default:
		DEBUG_INFO("Unhandled STM32 device\n");
	}
	info->usb_link->ep_rx = 1U;
	int config;
	int r = libusb_get_configuration(sl->ul_libusb_device_handle, &config);
	if (r) {
		DEBUG_WARN("FATAL: ST-Link libusb_get_configuration failed %d: %s\n", r, libusb_strerror(r));
		return -1;
	}
	if (config != 1) {
		r = libusb_set_configuration(sl->ul_libusb_device_handle, 0);
		if (r) {
			DEBUG_WARN("FATAL: ST-Link libusb_set_configuration failed %d: %s\n", r, libusb_strerror(r));
			return -1;
		}
	}
	r = libusb_claim_interface(sl->ul_libusb_device_handle, 0);
	if (r) {
		DEBUG_WARN("FATAL: ST-Link libusb_claim_interface failed %s\n", libusb_strerror(r));
		return -1;
	}
	sl->req_trans = libusb_alloc_transfer(0);
	sl->rep_trans = libusb_alloc_transfer(0);
	stlink_version(info);
	if ((stlink.ver_stlink < 3U && stlink.ver_jtag < 32U) || (stlink.ver_stlink == 3U && stlink.ver_jtag < 3U)) {
		/* Maybe the adapter is in some strange state. Try to reset */
		int result = libusb_reset_device(sl->ul_libusb_device_handle);
		DEBUG_WARN("INFO: Trying ST-Link reset\n");
		if (result == LIBUSB_ERROR_BUSY) { /* Try again */
			platform_delay(50);
			result = libusb_reset_device(sl->ul_libusb_device_handle);
		}
		if (result != LIBUSB_SUCCESS) {
			DEBUG_WARN("FATAL: ST-Link libusb_reset_device failed\n");
			return -1;
		}
		stlink_version(info);
	}
	if ((stlink.ver_stlink < 3U && stlink.ver_jtag < 32U) || (stlink.ver_stlink == 3U && stlink.ver_jtag < 3U)) {
		DEBUG_WARN("Please update the firmware on your ST-Link\n");
		return -1;
	}
	if (stlink_leave_state(info)) {
		DEBUG_WARN("ST-Link board was in DFU mode. Restart\n");
		return -1;
	}
	stlink_resetsys(info);
	return 0;
}

void stlink_nrst_set_val(bmp_info_s *info, bool assert)
{
	uint8_t cmd[16];
	uint8_t data[2];
	memset(cmd, 0, sizeof(cmd));
	cmd[0] = STLINK_DEBUG_COMMAND;
	cmd[1] = STLINK_DEBUG_APIV2_DRIVE_NRST;
	cmd[2] = assert ? STLINK_DEBUG_APIV2_DRIVE_NRST_LOW : STLINK_DEBUG_APIV2_DRIVE_NRST_HIGH;
	stlink.nrst = assert;
	send_recv(info->usb_link, cmd, 16, data, 2);
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

static int stlink_enter_debug_jtag(bmp_info_s *info)
{
	stlink_leave_state(info);
	uint8_t cmd[16];
	uint8_t data[2];
	memset(cmd, 0, sizeof(cmd));
	cmd[0] = STLINK_DEBUG_COMMAND;
	cmd[1] = STLINK_DEBUG_APIV2_ENTER;
	cmd[2] = STLINK_DEBUG_ENTER_JTAG_NO_RESET;
	send_recv(info->usb_link, cmd, 16, data, 2);
	return stlink_usb_error_check(data, true);
}

// static uint32_t stlink_read_coreid(void)
// {
// 	uint8_t cmd[16] = {STLINK_DEBUG_COMMAND, STLINK_DEBUG_APIV2_READ_IDCODES};
// 	uint8_t data[12];
// 	send_recv(info.usb_link, cmd, 16, data, 12);
// 	uint32_t id =  data[4] | data[5] << 8 | data[6] << 16 | data[7] << 24;
// 	DEBUG_INFO("Read Core ID: 0x%08" PRIx32 "\n", id);
// 	return id;
// }

static size_t stlink_read_idcodes(bmp_info_s *info, uint32_t *idcodes)
{
	uint8_t cmd[16];
	uint8_t data[12];
	memset(cmd, 0, sizeof(cmd));
	cmd[0] = STLINK_DEBUG_COMMAND;
	cmd[1] = STLINK_DEBUG_APIV2_READ_IDCODES;
	send_recv(info->usb_link, cmd, 16, data, 12);
	if (stlink_usb_error_check(data, true))
		return 0;
	idcodes[0] = data[4] | (data[5] << 8U) | (data[6] << 16U) | (data[7] << 24U);
	idcodes[1] = data[8] | (data[9] << 8U) | (data[10] << 16U) | (data[11] << 24U);
	return 2;
}

uint32_t stlink_dp_low_access(adiv5_debug_port_s *dp, uint8_t RnW, uint16_t addr, uint32_t value);

uint32_t stlink_dp_error(adiv5_debug_port_s *dp, const bool protocol_recovery)
{
	/* XXX: This should perform a line reset for protocol recovery.. */
	(void)protocol_recovery;

	uint32_t err = adiv5_dp_read(dp, ADIV5_DP_CTRLSTAT) &
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

	adiv5_dp_write(dp, ADIV5_DP_ABORT, clr);
	dp->fault = 0;
	if (err)
		DEBUG_WARN("stlink_dp_error %u\n", err);
	err |= stlink.ap_error;
	stlink.ap_error = false;
	return err;
}

void stlink_dp_abort(adiv5_debug_port_s *dp, uint32_t abort)
{
	adiv5_dp_write(dp, ADIV5_DP_ABORT, abort);
}

static int stlink_read_dp_register(uint16_t port, uint16_t addr, uint32_t *reg)
{
	uint8_t cmd[16];
	memset(cmd, 0, sizeof(cmd));
	cmd[0] = STLINK_DEBUG_COMMAND;
	cmd[1] = STLINK_DEBUG_APIV2_READ_DAP_REG;
	cmd[2] = port & 0xffU;
	cmd[3] = port >> 8U;
	cmd[4] = addr & 0xffU;
	cmd[5] = addr >> 8U;
	if (port == STLINK_DEBUG_PORT_ACCESS && stlink.dap_select)
		cmd[4] = ((stlink.dap_select & 0xfU) << 4U) | (addr & 0xfU);
	else
		cmd[4] = addr & 0xffU;
	uint8_t data[8];
	int result = stlink_send_recv_retry(cmd, 16, data, 8);
	if (result == STLINK_ERROR_OK)
		*reg = data[4] | (data[5] << 8U) | (data[6] << 16U) | (data[7] << 24U);
	else
		DEBUG_WARN("%s error %d\n", __func__, result);
	return result;
}

static int stlink_write_dp_register(uint16_t port, uint16_t addr, uint32_t val)
{
	if (port == STLINK_DEBUG_PORT_ACCESS && addr == 8U) {
		stlink.dap_select = val;
		DEBUG_PROBE("Caching SELECT 0x%02" PRIx32 "\n", val);
		return STLINK_ERROR_OK;
	}
	uint8_t cmd[16];
	memset(cmd, 0, sizeof(cmd));
	cmd[0] = STLINK_DEBUG_COMMAND;
	cmd[1] = STLINK_DEBUG_APIV2_WRITE_DAP_REG;
	cmd[2] = port & 0xffU;
	cmd[3] = port >> 8U;
	cmd[4] = addr & 0xffU;
	cmd[5] = addr >> 8U;
	cmd[6] = val & 0xffU;
	cmd[7] = (val >> 8U) & 0xffU;
	cmd[8] = (val >> 16U) & 0xffU;
	cmd[9] = (val >> 24U) & 0xffU;
	uint8_t data[2];
	stlink_send_recv_retry(cmd, 16, data, 2);
	return stlink_usb_error_check(data, true);
}

uint32_t stlink_dp_low_access(adiv5_debug_port_s *dp, uint8_t RnW, uint16_t addr, uint32_t value)
{
	uint32_t response = 0;
	int res;
	if (RnW)
		res = stlink_read_dp_register(addr < 0x100U ? STLINK_DEBUG_PORT_ACCESS : 0, addr, &response);
	else
		res = stlink_write_dp_register(addr < 0x100U ? STLINK_DEBUG_PORT_ACCESS : 0, addr, value);

	if (res == STLINK_ERROR_WAIT)
		raise_exception(EXCEPTION_TIMEOUT, "DP ACK timeout");

	if (res == STLINK_ERROR_DP_FAULT) {
		dp->fault = 1;
		return 0;
	}
	if (res == STLINK_ERROR_FAIL)
		raise_exception(EXCEPTION_ERROR, "SWDP invalid ACK");

	return response;
}

static bool stlink_ap_setup(int ap)
{
	if (ap > 7)
		return false;
	uint8_t cmd[16];
	memset(cmd, 0, sizeof(cmd));
	cmd[0] = STLINK_DEBUG_COMMAND;
	cmd[1] = STLINK_DEBUG_APIV2_INIT_AP;
	cmd[2] = ap;
	uint8_t data[2];
	DEBUG_PROBE("Open AP %d\n", ap);
	stlink_send_recv_retry(cmd, 16, data, 2);
	int res = stlink_usb_error_check(data, true);
	if (res && stlink.ver_hw == 30)
		DEBUG_WARN("ST-Link v3 only connects to STM8/32!\n");
	return !res;
}

static void stlink_ap_cleanup(int ap)
{
	uint8_t cmd[16];
	memset(cmd, 0, sizeof(cmd));
	cmd[0] = STLINK_DEBUG_COMMAND;
	cmd[1] = STLINK_DEBUG_APIV2_CLOSE_AP_DBG;
	cmd[2] = ap;
	uint8_t data[2];
	send_recv(info.usb_link, cmd, 16, data, 2);
	DEBUG_PROBE("Close AP %d\n", ap);
	stlink_usb_error_check(data, true);
}

static int stlink_usb_get_rw_status(bool verbose)
{
	uint8_t cmd[16];
	memset(cmd, 0, sizeof(cmd));
	cmd[0] = STLINK_DEBUG_COMMAND;
	cmd[1] = STLINK_DEBUG_APIV2_GETLASTRWSTATUS2;
	uint8_t data[12];
	send_recv(info.usb_link, cmd, 16, data, 12);
	return stlink_usb_error_check(data, verbose);
}

static void stlink_mem_read(adiv5_access_port_s *ap, void *dest, uint32_t src, size_t len)
{
	if (len == 0)
		return;
	size_t read_len = len;
	uint8_t type;
	if ((src & 1U) || (len & 1U)) {
		type = STLINK_DEBUG_READMEM_8BIT;
		if (len > stlink.block_size) {
			DEBUG_WARN("Too large!\n");
			return;
		}
		if (len == 1)
			++read_len; /* Fix read length as in openocd*/
	} else if ((src & 3U) || (len & 3U))
		type = STLINK_DEBUG_APIV2_READMEM_16BIT;
	else
		type = STLINK_DEBUG_READMEM_32BIT;

	uint8_t cmd[16];
	memset(cmd, 0, sizeof(cmd));
	cmd[0] = STLINK_DEBUG_COMMAND;
	cmd[1] = type;
	cmd[2] = src & 0xffU;
	cmd[3] = (src >> 8U) & 0xffU;
	cmd[4] = (src >> 16U) & 0xffU;
	cmd[5] = (src >> 24U) & 0xffU;
	cmd[6] = len & 0xffU;
	cmd[7] = len >> 8U;
	cmd[8] = ap->apsel;
	int res = read_retry(cmd, 16, dest, read_len);
	if (res != STLINK_ERROR_OK) {
		/* FIXME: What is the right measure when failing?
		 *
		 * E.g. TM4C129 gets here when NRF probe reads 0x10000010
		 * Approach taken:
		 * Fill the memory with some fixed pattern so hopefully
		 * the caller notices the error*/
		DEBUG_WARN("stlink_mem_read from  %" PRIx32 " to %p, len %zu failed\n", src, dest, len);
		memset(dest, 0xff, len);
	}
	DEBUG_PROBE("stlink_mem_read from %" PRIx32 " to %p, len %zu\n", src, dest, len);
}

static void stlink_mem_write8(usb_link_s *link, adiv5_access_port_s *ap, uint32_t addr, size_t len, uint8_t *buffer)
{
	while (len) {
		size_t length;
		/* OpenOCD has some note about writemem8*/
		if (len > stlink.block_size)
			length = stlink.block_size;
		else
			length = len;
		uint8_t cmd[16];
		memset(cmd, 0, sizeof(cmd));
		cmd[0] = STLINK_DEBUG_COMMAND;
		cmd[1] = STLINK_DEBUG_WRITEMEM_8BIT;
		cmd[2] = addr & 0xffU;
		cmd[3] = (addr >> 8U) & 0xffU;
		cmd[4] = (addr >> 16U) & 0xffU;
		cmd[5] = (addr >> 24U) & 0xffU;
		cmd[6] = length & 0xffU;
		cmd[7] = length >> 8U;
		cmd[8] = ap->apsel;
		send_recv(link, cmd, 16, NULL, 0);
		send_recv(link, (void *)buffer, length, NULL, 0);
		stlink_usb_get_rw_status(true);
		len -= length;
		addr += length;
	}
}

static void stlink_mem_write16(usb_link_s *link, adiv5_access_port_s *ap, uint32_t addr, size_t len, uint16_t *buffer)
{
	uint8_t cmd[16];
	memset(cmd, 0, sizeof(cmd));
	cmd[0] = STLINK_DEBUG_COMMAND;
	cmd[1] = STLINK_DEBUG_APIV2_WRITEMEM_16BIT;
	cmd[2] = addr & 0xffU;
	cmd[3] = (addr >> 8U) & 0xffU;
	cmd[4] = (addr >> 16U) & 0xffU;
	cmd[5] = (addr >> 24U) & 0xffU;
	cmd[6] = len & 0xffU;
	cmd[7] = len >> 8U;
	cmd[8] = ap->apsel;
	send_recv(link, cmd, 16, NULL, 0);
	send_recv(link, (void *)buffer, len, NULL, 0);
	stlink_usb_get_rw_status(true);
}

static void stlink_mem_write32(adiv5_access_port_s *ap, uint32_t addr, size_t len, uint32_t *buffer)
{
	uint8_t cmd[16];
	memset(cmd, 0, sizeof(cmd));
	cmd[0] = STLINK_DEBUG_COMMAND;
	cmd[1] = STLINK_DEBUG_WRITEMEM_32BIT;
	cmd[2] = addr & 0xffU;
	cmd[3] = (addr >> 8U) & 0xffU;
	cmd[4] = (addr >> 16U) & 0xffU;
	cmd[5] = (addr >> 24U) & 0xffU;
	cmd[6] = len & 0xffU;
	cmd[7] = len >> 8U;
	cmd[8] = ap->apsel;
	write_retry(cmd, 16, (void *)buffer, len);
}

static void stlink_regs_read(adiv5_access_port_s *ap, void *data)
{
	uint8_t cmd[16];
	uint8_t res[88];
	memset(cmd, 0, sizeof(cmd));
	cmd[0] = STLINK_DEBUG_COMMAND;
	cmd[1] = STLINK_DEBUG_APIV2_READALLREGS;
	cmd[2] = ap->apsel;
	DEBUG_PROBE("AP %hhu: Read all core registers\n", ap->apsel);
	send_recv(info.usb_link, cmd, 16, res, 88);
	stlink_usb_error_check(res, true);
	memcpy(data, res + 4U, 84);
}

static uint32_t stlink_reg_read(adiv5_access_port_s *ap, int num)
{
	uint8_t cmd[16];
	uint8_t res[8];
	memset(cmd, 0, sizeof(cmd));
	cmd[0] = STLINK_DEBUG_COMMAND;
	cmd[1] = STLINK_DEBUG_APIV2_READREG;
	cmd[2] = num;
	cmd[3] = ap->apsel;
	send_recv(info.usb_link, cmd, 16, res, 8);
	stlink_usb_error_check(res, true);
	uint32_t ret = (res[0]) | (res[1] << 8U) | (res[2] << 16U) | (res[3] << 24U);
	DEBUG_PROBE("AP %hhu: Read reg %02d val 0x%08" PRIx32 "\n", ap->apsel, num, ret);
	return ret;
}

static void stlink_reg_write(adiv5_access_port_s *ap, int num, uint32_t val)
{
	uint8_t cmd[16];
	uint8_t res[2];
	memset(cmd, 0, sizeof(cmd));
	cmd[0] = STLINK_DEBUG_COMMAND;
	cmd[1] = STLINK_DEBUG_APIV2_WRITEREG;
	cmd[2] = num;
	cmd[3] = val & 0xffU;
	cmd[4] = (val >> 8U) & 0xffU;
	cmd[5] = (val >> 16U) & 0xffU;
	cmd[6] = (val >> 24U) & 0xffU;
	cmd[7] = ap->apsel;
	send_recv(info.usb_link, cmd, 16, res, 2);
	DEBUG_PROBE("AP %hhu: Write reg %02d val 0x%08" PRIx32 "\n", ap->apsel, num, val);
	stlink_usb_error_check(res, true);
}

static void stlink_mem_write(adiv5_access_port_s *ap, uint32_t dest, const void *src, size_t len, align_e align)
{
	if (len == 0)
		return;
	usb_link_s *link = info.usb_link;
	switch (align) {
	case ALIGN_BYTE:
		stlink_mem_write8(link, ap, dest, len, (uint8_t *)src);
		break;
	case ALIGN_HALFWORD:
		stlink_mem_write16(link, ap, dest, len, (uint16_t *)src);
		break;
	case ALIGN_WORD:
	case ALIGN_DWORD:
		stlink_mem_write32(ap, dest, len, (uint32_t *)src);
		break;
	}
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

uint32_t jtag_scan_stlinkv2(bmp_info_s *info, const uint8_t *irlens)
{
	uint32_t idcodes[JTAG_MAX_DEVS + 1U];
	(void)*irlens;
	target_list_free();

	jtag_dev_count = 0;
	memset(jtag_devs, 0, sizeof(jtag_devs));
	if (stlink_enter_debug_jtag(info))
		return 0;
	jtag_dev_count = stlink_read_idcodes(info, idcodes);
	/* Check for known devices and handle accordingly */
	for (uint32_t i = 0; i < jtag_dev_count; ++i)
		jtag_devs[i].jd_idcode = idcodes[i];
	for (uint32_t i = 0; i < jtag_dev_count; ++i) {
		for (size_t j = 0; dev_descr[j].idcode; ++j) {
			if ((jtag_devs[i].jd_idcode & dev_descr[j].idmask) == dev_descr[j].idcode) {
				if (dev_descr[j].handler)
					dev_descr[j].handler(i);
				break;
			}
		}
	}

	return jtag_dev_count;
}

void stlink_jtag_dp_init(adiv5_debug_port_s *dp)
{
	dp->error = stlink_dp_error;
	dp->low_access = stlink_dp_low_access;
	dp->abort = stlink_dp_abort;
}

void stlink_adiv5_dp_defaults(adiv5_debug_port_s *dp)
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

uint32_t stlink_swdp_scan(bmp_info_s *info)
{
	target_list_free();

	stlink_leave_state(info);

	uint8_t cmd[16];
	uint8_t data[2];
	memset(cmd, 0, sizeof(cmd));
	cmd[0] = STLINK_DEBUG_COMMAND;
	cmd[1] = STLINK_DEBUG_APIV2_ENTER;
	cmd[2] = STLINK_DEBUG_ENTER_SWD_NO_RESET;

	stlink_send_recv_retry(cmd, 16, data, 2);

	if (stlink_usb_error_check(data, true))
		return 0;

	adiv5_debug_port_s *dp = calloc(1, sizeof(*dp));
	if (!dp) { /* calloc failed: heap exhaustion */
		DEBUG_WARN("calloc: failed in %s\n", __func__);
		return 0;
	}

	dp->dp_read = firmware_swdp_read;
	dp->error = stlink_dp_error;
	dp->low_access = stlink_dp_low_access;
	dp->abort = stlink_dp_abort;

	adiv5_dp_error(dp);

	adiv5_dp_init(dp, 0);

	return target_list ? 1U : 0U;
}

#define V2_USED_SWD_CYCLES 20U
#define V2_CYCLES_PER_CNT  20U
#define V2_CLOCK_RATE      (72U * 1000U * 1000U)
/* Above values reproduce the known values for V2
#include <stdio.h>

int main(void)
{
    int divs[] = {0, 1,2,3,7,15,31,40,79,158,265,798};
    for (int i = 0; i < (sizeof(divs) /sizeof(divs[0])); i++) {
        float ret = 72.0 * 1000 * 1000 / (20 + 20 * divs[i]);
        printf("%3d: %6.4f MHz\n", divs[i], ret/ 1000000);
    }
    return 0;
}
*/

static uint32_t divisor;
static unsigned int v3_freq[2];

void stlink_max_frequency_set(bmp_info_s *info, uint32_t freq)
{
	uint8_t cmd[16];
	if (stlink.ver_hw == 30U) {
		uint8_t data[52];
		memset(cmd, 0, sizeof(cmd));
		cmd[0] = STLINK_DEBUG_COMMAND;
		cmd[1] = STLINK_APIV3_GET_COM_FREQ;
		cmd[2] = info->is_jtag ? STLINK_MODE_JTAG : STLINK_MODE_SWD;
		send_recv(info->usb_link, cmd, 16, data, 52);
		stlink_usb_error_check(data, true);
		uint32_t frequency = 0;
		DEBUG_INFO("Available speed settings: ");
		for (size_t i = 0; i < STLINK_V3_MAX_FREQ_NB; ++i) {
			const size_t offset = 12U + (i * 4U);
			const uint32_t new_freq =
				data[offset] | (data[offset + 1U] << 8U) | (data[offset + 2U] << 16U) | (data[offset + 3U] << 24U);
			if (!new_freq)
				break;
			frequency = new_freq;
			DEBUG_INFO("%s%u", i ? "/" : "", frequency);
			if (freq / 1000U >= frequency)
				break;
		}
		DEBUG_INFO(" kHz for %s\n", info->is_jtag ? "JTAG" : "SWD");
		cmd[1] = STLINK_APIV3_SET_COM_FREQ;
		/* cmd[0] and cmd[2..3] are kept the same as the previous command */
		cmd[4] = frequency & 0xffU;
		cmd[5] = (frequency >> 8U) & 0xffU;
		cmd[6] = (frequency >> 16U) & 0xffU;
		cmd[7] = (frequency >> 24U) & 0xffU;
		send_recv(info->usb_link, cmd, 16, data, 8);
		stlink_usb_error_check(data, true);
		v3_freq[info->is_jtag ? 1 : 0] = frequency * 1000U;
	} else {
		memset(cmd, 0, sizeof(cmd));
		cmd[0] = STLINK_DEBUG_COMMAND;
		if (info->is_jtag) {
			cmd[1] = STLINK_DEBUG_APIV2_JTAG_SET_FREQ;
			/*  V2_CLOCK_RATE / (4, 8, 16, ... 256)*/
			uint32_t div = (V2_CLOCK_RATE + (2U * freq) - 1U) / (2U * freq);
			if (div & (div - 1U)) { /* Round up */
				uint32_t clz = __builtin_clz(div);
				divisor = 1U << (32U - clz);
			} else
				divisor = div;
			if (divisor < 4U)
				divisor = 4U;
			else if (divisor > 256U)
				divisor = 256U;
		} else {
			cmd[1] = STLINK_DEBUG_APIV2_SWD_SET_FREQ;
			divisor = V2_CLOCK_RATE + freq - 1U;
			divisor /= freq;
			divisor -= V2_USED_SWD_CYCLES;
			/* If the subtraction made the value negative, set it to 0 */
			if (divisor & 0x80000000U)
				divisor = 0;
			divisor /= V2_CYCLES_PER_CNT;
		}
		DEBUG_WARN("Divisor for %6.4f MHz is %u\n", freq / 1000000.0F, divisor);
		cmd[2] = divisor & 0xffU;
		cmd[3] = (divisor >> 8U) & 0xffU;
		uint8_t data[2];
		send_recv(info->usb_link, cmd, 16, data, 2);
		if (stlink_usb_error_check(data, false))
			DEBUG_WARN("Set frequency failed!\n");
	}
}

uint32_t stlink_max_frequency_get(bmp_info_s *info)
{
	if (stlink.ver_hw == 30U)
		return v3_freq[info->is_jtag ? STLINK_MODE_JTAG : STLINK_MODE_SWD];
	const uint32_t result = V2_CLOCK_RATE;
	if (info->is_jtag)
		return result / (2U * divisor);
	return result / (V2_USED_SWD_CYCLES + (V2_CYCLES_PER_CNT * divisor));
}
