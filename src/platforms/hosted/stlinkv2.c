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
#include "stlinkv2.h"
#include "exception.h"
#include "jtag_devs.h"
#include "target.h"
#include "cortexm.h"

#include <assert.h>
#include <unistd.h>
#include <signal.h>
#include <ctype.h>
#include <sys/time.h>

#include "cl_utils.h"

#define VENDOR_ID_STLINK		0x483
#define PRODUCT_ID_STLINK_MASK	0xffe0
#define PRODUCT_ID_STLINK_GROUP 0x3740
#define PRODUCT_ID_STLINKV1		0x3744
#define PRODUCT_ID_STLINKV2		0x3748
#define PRODUCT_ID_STLINKV21	0x374b
#define PRODUCT_ID_STLINKV21_MSD 0x3752
#define PRODUCT_ID_STLINKV3		0x374f
#define PRODUCT_ID_STLINKV3E	0x374e

#define STLINK_SWIM_ERR_OK             0x00
#define STLINK_SWIM_BUSY               0x01
#define STLINK_DEBUG_ERR_OK            0x80
#define STLINK_DEBUG_ERR_FAULT         0x81
#define STLINK_JTAG_UNKNOWN_JTAG_CHAIN 0x04
#define STLINK_NO_DEVICE_CONNECTED     0x05
#define STLINK_JTAG_COMMAND_ERROR      0x08
#define STLINK_JTAG_COMMAND_ERROR      0x08
#define STLINK_JTAG_GET_IDCODE_ERROR   0x09
#define STLINK_JTAG_DBG_POWER_ERROR    0x0b
#define STLINK_SWD_AP_WAIT             0x10
#define STLINK_SWD_AP_FAULT            0x11
#define STLINK_SWD_AP_ERROR            0x12
#define STLINK_SWD_AP_PARITY_ERROR     0x13
#define STLINK_JTAG_WRITE_ERROR        0x0c
#define STLINK_JTAG_WRITE_VERIF_ERROR  0x0d
#define STLINK_SWD_DP_WAIT             0x14
#define STLINK_SWD_DP_FAULT            0x15
#define STLINK_SWD_DP_ERROR            0x16
#define STLINK_SWD_DP_PARITY_ERROR     0x17

#define STLINK_SWD_AP_WDATA_ERROR      0x18
#define STLINK_SWD_AP_STICKY_ERROR     0x19
#define STLINK_SWD_AP_STICKYORUN_ERROR 0x1a
#define STLINK_BAD_AP_ERROR            0x1d
#define STLINK_TOO_MANY_AP_ERROR       0x29
#define STLINK_JTAG_UNKNOWN_CMD        0x42

#define STLINK_CORE_RUNNING            0x80
#define STLINK_CORE_HALTED             0x81
#define STLINK_CORE_STAT_UNKNOWN       -1

#define STLINK_GET_VERSION             0xF1
#define STLINK_DEBUG_COMMAND           0xF2
#define STLINK_DFU_COMMAND             0xF3
#define STLINK_SWIM_COMMAND            0xF4
#define STLINK_GET_CURRENT_MODE        0xF5
#define STLINK_GET_TARGET_VOLTAGE      0xF7

#define STLINK_DEV_DFU_MODE            0x00
#define STLINK_DEV_MASS_MODE           0x01
#define STLINK_DEV_DEBUG_MODE          0x02
#define STLINK_DEV_SWIM_MODE           0x03
#define STLINK_DEV_BOOTLOADER_MODE     0x04
#define STLINK_DEV_UNKNOWN_MODE        -1

#define STLINK_DFU_EXIT                0x07

#define STLINK_SWIM_ENTER                  0x00
#define STLINK_SWIM_EXIT                   0x01
#define STLINK_SWIM_READ_CAP               0x02
#define STLINK_SWIM_SPEED                  0x03
#define STLINK_SWIM_ENTER_SEQ              0x04
#define STLINK_SWIM_GEN_RST                0x05
#define STLINK_SWIM_RESET                  0x06
#define STLINK_SWIM_ASSERT_RESET           0x07
#define STLINK_SWIM_DEASSERT_RESET         0x08
#define STLINK_SWIM_READSTATUS             0x09
#define STLINK_SWIM_WRITEMEM               0x0a
#define STLINK_SWIM_READMEM                0x0b
#define STLINK_SWIM_READBUF                0x0c

#define STLINK_DEBUG_GETSTATUS             0x01
#define STLINK_DEBUG_FORCEDEBUG            0x02
#define STLINK_DEBUG_APIV1_RESETSYS        0x03
#define STLINK_DEBUG_APIV1_READALLREGS     0x04
#define STLINK_DEBUG_APIV1_READREG         0x05
#define STLINK_DEBUG_APIV1_WRITEREG        0x06
#define STLINK_DEBUG_READMEM_32BIT         0x07
#define STLINK_DEBUG_WRITEMEM_32BIT        0x08
#define STLINK_DEBUG_RUNCORE               0x09
#define STLINK_DEBUG_STEPCORE              0x0a
#define STLINK_DEBUG_APIV1_SETFP           0x0b
#define STLINK_DEBUG_READMEM_8BIT          0x0c
#define STLINK_DEBUG_WRITEMEM_8BIT         0x0d
#define STLINK_DEBUG_APIV1_CLEARFP         0x0e
#define STLINK_DEBUG_APIV1_WRITEDEBUGREG   0x0f
#define STLINK_DEBUG_APIV1_SETWATCHPOINT   0x10

#define STLINK_DEBUG_ENTER_JTAG_RESET      0x00
#define STLINK_DEBUG_ENTER_SWD_NO_RESET    0xa3
#define STLINK_DEBUG_ENTER_JTAG_NO_RESET   0xa4

#define STLINK_DEBUG_APIV1_ENTER           0x20
#define STLINK_DEBUG_EXIT                  0x21
#define STLINK_DEBUG_READCOREID            0x22

#define STLINK_DEBUG_APIV2_ENTER           0x30
#define STLINK_DEBUG_APIV2_READ_IDCODES    0x31
#define STLINK_DEBUG_APIV2_RESETSYS        0x32
#define STLINK_DEBUG_APIV2_READREG         0x33
#define STLINK_DEBUG_APIV2_WRITEREG        0x34
#define STLINK_DEBUG_APIV2_WRITEDEBUGREG   0x35
#define STLINK_DEBUG_APIV2_READDEBUGREG    0x36

#define STLINK_DEBUG_APIV2_READALLREGS     0x3A
#define STLINK_DEBUG_APIV2_GETLASTRWSTATUS 0x3B
#define STLINK_DEBUG_APIV2_DRIVE_NRST      0x3C

#define STLINK_DEBUG_APIV2_GETLASTRWSTATUS2 0x3E

#define STLINK_DEBUG_APIV2_START_TRACE_RX  0x40
#define STLINK_DEBUG_APIV2_STOP_TRACE_RX   0x41
#define STLINK_DEBUG_APIV2_GET_TRACE_NB    0x42
#define STLINK_DEBUG_APIV2_SWD_SET_FREQ    0x43
#define STLINK_DEBUG_APIV2_JTAG_SET_FREQ   0x44
#define STLINK_DEBUG_APIV2_READ_DAP_REG    0x45
#define STLINK_DEBUG_APIV2_WRITE_DAP_REG   0x46
#define STLINK_DEBUG_APIV2_READMEM_16BIT   0x47
#define STLINK_DEBUG_APIV2_WRITEMEM_16BIT  0x48

#define STLINK_DEBUG_APIV2_INIT_AP         0x4B
#define STLINK_DEBUG_APIV2_CLOSE_AP_DBG    0x4C

#define STLINK_APIV3_SET_COM_FREQ           0x61
#define STLINK_APIV3_GET_COM_FREQ           0x62

#define STLINK_APIV3_GET_VERSION_EX         0xFB

#define STLINK_DEBUG_APIV2_DRIVE_NRST_LOW   0x00
#define STLINK_DEBUG_APIV2_DRIVE_NRST_HIGH  0x01
#define STLINK_DEBUG_APIV2_DRIVE_NRST_PULSE 0x02


#define STLINK_TRACE_SIZE               4096
#define STLINK_TRACE_MAX_HZ             2000000

#define STLINK_V3_MAX_FREQ_NB               10

/** */
enum stlink_mode {
	STLINK_MODE_UNKNOWN = 0,
	STLINK_MODE_DFU,
	STLINK_MODE_MASS,
	STLINK_MODE_DEBUG_JTAG,
	STLINK_MODE_DEBUG_SWD,
	STLINK_MODE_DEBUG_SWIM
};

enum transport_mode_t{
	STLINK_MODE_SWD = 0,
	STLINK_MODE_JTAG
};

typedef struct {
	libusb_context* libusb_ctx;
	uint16_t     vid;
	uint16_t     pid;
	uint8_t      transport_mode;
	bool         srst;
	uint8_t      dap_select;
	uint8_t      ep_tx;
	uint8_t      ver_hw;     /* 20, 21 or 31 deciphered from USB PID.*/
	uint8_t      ver_stlink; /* 2 or 3  from API.*/
	uint8_t      ver_api;
	uint8_t      ver_jtag;
	uint8_t      ver_mass;
	uint8_t      ver_swim;
	uint8_t      ver_bridge;
	uint16_t     block_size;
	bool         ap_error;
} stlink;

stlink Stlink;

static int stlink_usb_get_rw_status(bool verbose);

int debug_level = 0;

#define STLINK_ERROR_DP_FAULT -2

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
				DEBUG_WARN("SWD fault response (0x%x)\n",
						   STLINK_DEBUG_ERR_FAULT);
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
				DEBUG_WARN("wait status SWD_AP_WAIT (0x%x)\n",
						   STLINK_SWD_AP_WAIT);
			return STLINK_ERROR_WAIT;
		case STLINK_SWD_DP_WAIT:
			if (verbose)
				DEBUG_WARN("wait status SWD_DP_WAIT (0x%x)\n",
						   STLINK_SWD_DP_WAIT);
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
			Stlink.ap_error = true;
			if (verbose)
				DEBUG_WARN("STLINK_SWD_AP_FAULT\n");
			return STLINK_ERROR_DP_FAULT;
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
			Stlink.ap_error = true;
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
		case STLINK_JTAG_UNKNOWN_CMD :
			if (verbose)
				DEBUG_WARN("STLINK_JTAG_UNKNOWN_CMD\n");
			return STLINK_ERROR_FAIL;
		default:
			if (verbose)
				DEBUG_WARN("unknown/unexpected STLINK status code 0x%x\n", data[0]);
			return STLINK_ERROR_FAIL;
	}
}

static int stlink_send_recv_retry(uint8_t *txbuf, size_t txsize,
					 uint8_t *rxbuf, size_t rxsize)
{
	uint32_t start = platform_time_ms();
	int res;
	usb_link_t *link = info.usb_link;
	while(1) {
		send_recv(link, txbuf, txsize, rxbuf, rxsize);
		res = stlink_usb_error_check(rxbuf, false);
		if (res == STLINK_ERROR_OK)
			return res;
		uint32_t now = platform_time_ms();
		if (((now - start) > cortexm_wait_timeout) ||
			(res != STLINK_ERROR_WAIT)) {
			DEBUG_WARN("write_retry failed. ");
			return res;
		}
	}
	return res;
}

static int read_retry(uint8_t *txbuf, size_t txsize,
					 uint8_t *rxbuf, size_t rxsize)
{
	uint32_t start = platform_time_ms();
	int res;
	while(1) {
		send_recv(info.usb_link, txbuf, txsize, rxbuf, rxsize);
		res = stlink_usb_get_rw_status(false);
		if (res == STLINK_ERROR_OK)
			return res;
		uint32_t now = platform_time_ms();
		if (((now -start) > 1000) || (res != STLINK_ERROR_WAIT)) {
			DEBUG_WARN("read_retry failed. ");
			stlink_usb_get_rw_status(true);
			return res;
		}
	}
	return res;
}

static int write_retry(uint8_t *cmdbuf, size_t cmdsize,
					 uint8_t *txbuf, size_t txsize)
{
	uint32_t start = platform_time_ms();
	int res;
	usb_link_t *link = info.usb_link;
	while(1) {
		send_recv(link, cmdbuf, cmdsize, NULL, 0);
		send_recv(link, txbuf, txsize, NULL, 0);
		res = stlink_usb_get_rw_status(false);
		if (res == STLINK_ERROR_OK)
			return res;
		uint32_t now = platform_time_ms();
		if (((now - start) > 1000) || (res != STLINK_ERROR_WAIT)) {
			stlink_usb_get_rw_status(true);
			return res;
		}
	}
	return res;
}

static void stlink_version(bmp_info_t *info)
{
	if (Stlink.ver_hw == 30) {
		uint8_t cmd[16] = {STLINK_APIV3_GET_VERSION_EX};
		uint8_t data[12];
		int size = send_recv(info->usb_link, cmd, 16, data, 12);
		if (size == -1) {
			DEBUG_WARN("[!] stlink_send_recv STLINK_APIV3_GET_VERSION_EX\n");
		}
		Stlink.ver_stlink = data[0];
		Stlink.ver_swim  =  data[1];
		Stlink.ver_jtag  =  data[2];
		Stlink.ver_mass  =  data[3];
		Stlink.ver_bridge = data[4];
		Stlink.block_size = 512;
		Stlink.vid = data[3] <<  9 | data[8];
		Stlink.pid = data[5] << 11 | data[10];
	} else {
		uint8_t cmd[16] = {STLINK_GET_VERSION};
		uint8_t data[6];
		int size = send_recv(info->usb_link, cmd, 16, data, 6);
		if (size == -1) {
			DEBUG_WARN("[!] stlink_send_recv STLINK_GET_VERSION_EX\n");
		}
		Stlink.vid = data[3] << 8 | data[2];
		Stlink.pid = data[5] << 8 | data[4];
		int  version = data[0] << 8 | data[1]; /* Big endian here!*/
		Stlink.block_size = 64;
		Stlink.ver_stlink = (version >> 12) & 0x0f;
		Stlink.ver_jtag   = (version >>  6) & 0x3f;
		if ((Stlink.pid == PRODUCT_ID_STLINKV21_MSD) ||
			(Stlink.pid == PRODUCT_ID_STLINKV21)) {
			Stlink.ver_mass = (version >> 0) & 0x3f;
		} else {
			Stlink.ver_swim = (version >> 0) & 0x3f;
		}
	}
	DEBUG_INFO("V%dJ%d",Stlink.ver_stlink, Stlink.ver_jtag);
	if (Stlink.ver_hw == 30) {
		DEBUG_INFO("M%dB%dS%d", Stlink.ver_mass, Stlink.ver_bridge, Stlink.ver_swim);
	} else if (Stlink.ver_hw == 20) {
		DEBUG_INFO("S%d", Stlink.ver_swim);
	} else if (Stlink.ver_hw == 21) {
		DEBUG_INFO("M%d", Stlink.ver_mass);
	}
	DEBUG_INFO("\n");
}

static bool stlink_leave_state(bmp_info_t *info)
{
	uint8_t cmd[16] = {STLINK_GET_CURRENT_MODE};
	uint8_t data[2];
	send_recv(info->usb_link,cmd, 16, data, 2);
	if (data[0] == STLINK_DEV_DFU_MODE) {
		uint8_t dfu_cmd[16] = {STLINK_DFU_COMMAND, STLINK_DFU_EXIT};
		DEBUG_INFO("Leaving DFU Mode\n");
		send_recv(info->usb_link, dfu_cmd, 16, NULL, 0);
		return true;
	} else if (data[0] == STLINK_DEV_SWIM_MODE) {
		uint8_t swim_cmd[16] = {STLINK_SWIM_COMMAND, STLINK_SWIM_EXIT};
		DEBUG_INFO("Leaving SWIM Mode\n");
		send_recv(info->usb_link, swim_cmd, 16, NULL, 0);
	} else if (data[0] == STLINK_DEV_DEBUG_MODE) {
		uint8_t dbg_cmd[16] = {STLINK_DEBUG_COMMAND, STLINK_DEBUG_EXIT};
		DEBUG_INFO("Leaving DEBUG Mode\n");
		send_recv(info->usb_link, dbg_cmd, 16, NULL, 0);
	} else if (data[0] == STLINK_DEV_BOOTLOADER_MODE) {
		DEBUG_INFO("Leaving BOOTLOADER Mode\n");
	} else if (data[0] == STLINK_DEV_MASS_MODE) {
		DEBUG_INFO("Leaving MASS Mode\n");
	} else {
		DEBUG_INFO("Unknown Mode %02x\n", data[0]);
	}
	return false;
}

const char *stlink_target_voltage(bmp_info_t *info)
{
	uint8_t cmd[16] = {STLINK_GET_TARGET_VOLTAGE};
	uint8_t data[8];
	send_recv(info->usb_link, cmd, 16, data, 8);
	uint16_t adc[2];
	adc[0] = data[0] | data[1] << 8; /* Calibration value? */
	adc[1] = data[4] | data[5] << 8; /* Measured value?*/
	float result  = 0.0;
	if (adc[0])
		result = 2.0 * adc[1] * 1.2 / adc[0];
	static char res[6];
	sprintf(res, "%4.2fV", result);
	return res;
}

static void stlink_resetsys(bmp_info_t *info)
{
	uint8_t cmd[16] = {STLINK_DEBUG_COMMAND ,STLINK_DEBUG_APIV2_RESETSYS};
	uint8_t data[2];
	send_recv(info->usb_link, cmd, 16, data, 2);
}

int stlink_init(bmp_info_t *info)
{
	usb_link_t *sl = calloc(1, sizeof(usb_link_t));
	if (!sl)
		return -1;
	info->usb_link = sl;
	sl->ul_libusb_ctx = info->libusb_ctx;
	libusb_device **devs, *dev;
	ssize_t cnt;
	cnt = libusb_get_device_list(info->libusb_ctx, &devs);
	if (cnt < 0) {
		DEBUG_WARN("FATAL: Stlink libusb_get_device_list failed\n");
		return -1;
	}
	int i = 0;
	bool found = false;
	while ((dev = devs[i++]) != NULL) {
		struct libusb_device_descriptor desc;
		int r = libusb_get_device_descriptor(dev, &desc);
		if (r < 0) {
			DEBUG_WARN("libusb_get_device_descriptor failed %s",
					libusb_strerror(r));
			return -1;
		}
		if ((desc.idVendor != info->vid) ||
			(desc.idProduct != info->pid) ||
			(libusb_open(dev, &sl->ul_libusb_device_handle)
			 != LIBUSB_SUCCESS)) {
			continue;
		}
		char serial[64];
		r = libusb_get_string_descriptor_ascii(
			sl->ul_libusb_device_handle, desc.iSerialNumber,
			(uint8_t*)serial,sizeof(serial));
		if (r <= 0 || !strstr(serial, info->serial)) {
			libusb_close(sl->ul_libusb_device_handle);
			continue;
		}
		found = true;
		break;
	}
	libusb_free_device_list(devs, 1);
	if (!found)
		return 0;
	if (info->pid == PRODUCT_ID_STLINKV2) {
		Stlink.ver_hw = 20;
		info->usb_link->ep_tx = 2;
		Stlink.ep_tx = 2;
	} else if ((info->pid == PRODUCT_ID_STLINKV21)||
			   (info->pid == PRODUCT_ID_STLINKV21_MSD)) {
		Stlink.ver_hw = 21;
		info->usb_link->ep_tx = 1;
		Stlink.ep_tx = 1;
	} else if ((info->pid == PRODUCT_ID_STLINKV3) ||
			   (info->pid == PRODUCT_ID_STLINKV3E)) {
		Stlink.ver_hw = 30;
		info->usb_link->ep_tx = 1;
		Stlink.ep_tx = 1;
	}
	info->usb_link->ep_rx = 1;
	int config;
	int r = libusb_get_configuration(sl->ul_libusb_device_handle, &config);
	if (r) {
		DEBUG_WARN("FATAL: Stlink libusb_get_configuration failed %d: %s",
				 r, libusb_strerror(r));
		return -1;
	}
	if (config != 1) {
		r = libusb_set_configuration(sl->ul_libusb_device_handle, 0);
		if (r) {
			DEBUG_WARN("FATAL: Stlinklibusb_set_configuration "
				   "failed %d: %s", r, libusb_strerror(r));
			return -1;
		}
	}
	r = libusb_claim_interface(sl->ul_libusb_device_handle, 0);
	if (r) 	{
		DEBUG_WARN("FATAL: Stlink libusb_claim_interface failed %s\n",
				libusb_strerror(r));
		return -1;
	}
	sl->req_trans = libusb_alloc_transfer(0);
	sl->rep_trans = libusb_alloc_transfer(0);
	stlink_version(info);
	if ((Stlink.ver_stlink < 3 && Stlink.ver_jtag < 32) ||
		(Stlink.ver_stlink == 3 && Stlink.ver_jtag < 3)) {
		/* Maybe the adapter is in some strange state. Try to reset */
        int result = libusb_reset_device(sl->ul_libusb_device_handle);
		DEBUG_WARN("INFO: Trying Stlink reset\n");
		if (result == LIBUSB_ERROR_BUSY) { /* Try again */
			platform_delay(50);
			result = libusb_reset_device(sl->ul_libusb_device_handle);
		}
        if (result != LIBUSB_SUCCESS) {
			DEBUG_WARN("FATAL: Stlink libusb_reset_device failed\n");
			return -1;
		}
		stlink_version(info);
    }
	if ((Stlink.ver_stlink < 3 && Stlink.ver_jtag < 32) ||
		(Stlink.ver_stlink == 3 && Stlink.ver_jtag < 3)) {
		DEBUG_WARN("Please update Firmware\n");
		return -1;
	}
	if (stlink_leave_state(info)) {
		DEBUG_WARN("Stlink board was in DFU mode. Restart\n");
		return -1;
	}
	stlink_resetsys(info);
	return 0;
}

void stlink_srst_set_val(bmp_info_t *info, bool assert)
{
	uint8_t cmd[16] = {STLINK_DEBUG_COMMAND,
					  STLINK_DEBUG_APIV2_DRIVE_NRST,
					  (assert)? STLINK_DEBUG_APIV2_DRIVE_NRST_LOW
					  : STLINK_DEBUG_APIV2_DRIVE_NRST_HIGH};
	uint8_t data[2];
	Stlink.srst = assert;
	send_recv(info->usb_link, cmd, 16, data, 2);
	stlink_usb_error_check(data, true);
}

bool stlink_srst_get_val(void)
{
	return Stlink.srst;
}

static bool stlink_set_freq_divisor(bmp_info_t *info, uint16_t divisor)
{
	uint8_t cmd[16] = {STLINK_DEBUG_COMMAND,
					  STLINK_DEBUG_APIV2_SWD_SET_FREQ,
					  divisor & 0xff, divisor >> 8};
	uint8_t data[2];
	send_recv(info->usb_link, cmd, 16, data, 2);
	if (stlink_usb_error_check(data, false))
		return false;
	return true;
}

static bool stlink3_set_freq_divisor(bmp_info_t *info, uint16_t divisor)
{
	uint8_t cmd[16] = {STLINK_DEBUG_COMMAND,
					  STLINK_APIV3_GET_COM_FREQ,
					  Stlink.transport_mode};
	uint8_t data[52];
	send_recv(info->usb_link, cmd, 16, data, 52);
	stlink_usb_error_check(data, true);
	int size = data[8];
	if (divisor > size)
		divisor = size;
	uint8_t *p = data + 12 + divisor * sizeof(uint32_t);
	uint32_t freq = p[0] | p[1] << 8 | p[2] << 16 | p[3] << 24;
	DEBUG_INFO("Selected %" PRId32 " khz\n", freq);
	cmd[1] = STLINK_APIV3_SET_COM_FREQ;
	cmd[2] = Stlink.transport_mode;
	cmd[3] = 0;
	p = data + 12 + divisor * sizeof(uint32_t);
	cmd[4] = p[0];
	cmd[5] = p[1];
	cmd[6] = p[2];
	cmd[7] = p[3];
	send_recv(info->usb_link, cmd, 16, data, 8);
	return true;
}

int stlink_hwversion(void)
{
	return Stlink.ver_stlink;
}

static int stlink_enter_debug_jtag(bmp_info_t *info)
{
	stlink_leave_state(info);
	Stlink.transport_mode = STLINK_MODE_JTAG;
	if (Stlink.ver_stlink == 3)
		stlink3_set_freq_divisor(info, 4);
	else
		stlink_set_freq_divisor(info, 1);
	uint8_t cmd[16] = {STLINK_DEBUG_COMMAND,
					  STLINK_DEBUG_APIV2_ENTER,
					  STLINK_DEBUG_ENTER_JTAG_NO_RESET};
	uint8_t data[2];
	DEBUG_INFO("Enter JTAG\n");
	send_recv(info->usb_link, cmd, 16, data, 2);
	return stlink_usb_error_check(data, true);
}

static uint32_t stlink_read_coreid(void)
{
	uint8_t cmd[16] = {STLINK_DEBUG_COMMAND, STLINK_DEBUG_APIV2_READ_IDCODES};
	uint8_t data[12];
	send_recv(info.usb_link, cmd, 16, data, 12);
	uint32_t id =  data[4] | data[5] << 8 | data[6] << 16 | data[7] << 24;
	DEBUG_INFO("Read Core ID: 0x%08" PRIx32 "\n", id);
	return id;
}

static int stlink_read_idcodes(bmp_info_t *info, uint32_t *idcodes)
{
	uint8_t cmd[16] = {STLINK_DEBUG_COMMAND,
					   STLINK_DEBUG_APIV2_READ_IDCODES};
	uint8_t data[12];
	send_recv(info->usb_link, cmd, 16, data, 12);
	if (stlink_usb_error_check(data, true))
		return 0;
	uint8_t *p = data + 4;
	idcodes[0] = p[0] | p[1] << 8 | p[2] << 16 | p[3] << 24;
	p += 4;
	idcodes[1] = p[0] | p[1] << 8 | p[2] << 16 | p[3] << 24;
	return 2;
}

uint32_t stlink_dp_low_access(ADIv5_DP_t *dp, uint8_t RnW,
							  uint16_t addr, uint32_t value);

	static uint32_t stlink_dp_read(ADIv5_DP_t *dp, uint16_t addr)
{
	if (addr & ADIV5_APnDP) {
		stlink_dp_low_access(dp, ADIV5_LOW_READ, addr, 0);
		return stlink_dp_low_access(dp, ADIV5_LOW_READ,
		                           ADIV5_DP_RDBUFF, 0);
	} else {
		return stlink_dp_low_access(dp, ADIV5_LOW_READ, addr, 0);
	}
}

uint32_t stlink_dp_error(ADIv5_DP_t *dp)
{
	uint32_t err, clr = 0;

	err = stlink_dp_read(dp, ADIV5_DP_CTRLSTAT) &
		(ADIV5_DP_CTRLSTAT_STICKYORUN | ADIV5_DP_CTRLSTAT_STICKYCMP |
		ADIV5_DP_CTRLSTAT_STICKYERR | ADIV5_DP_CTRLSTAT_WDATAERR);

	if(err & ADIV5_DP_CTRLSTAT_STICKYORUN)
		clr |= ADIV5_DP_ABORT_ORUNERRCLR;
	if(err & ADIV5_DP_CTRLSTAT_STICKYCMP)
		clr |= ADIV5_DP_ABORT_STKCMPCLR;
	if(err & ADIV5_DP_CTRLSTAT_STICKYERR)
		clr |= ADIV5_DP_ABORT_STKERRCLR;
	if(err & ADIV5_DP_CTRLSTAT_WDATAERR)
		clr |= ADIV5_DP_ABORT_WDERRCLR;

	adiv5_dp_write(dp, ADIV5_DP_ABORT, clr);
	dp->fault = 0;
	if (err)
		DEBUG_WARN("stlink_dp_error %d\n", err);
	err |= Stlink.ap_error;
	Stlink.ap_error = false;
	return err;
}

void stlink_dp_abort(ADIv5_DP_t *dp, uint32_t abort)
{
	adiv5_dp_write(dp, ADIV5_DP_ABORT, abort);
}

static int stlink_read_dp_register(uint16_t port, uint16_t addr, uint32_t *reg)
{
	uint8_t cmd[16] = {STLINK_DEBUG_COMMAND,
					  STLINK_DEBUG_APIV2_READ_DAP_REG,
					  port & 0xff, port >> 8,
					  addr & 0xff, addr >> 8};
	if (port == STLINK_DEBUG_PORT_ACCESS  && Stlink.dap_select)
		cmd[4] = ((Stlink.dap_select & 0xf) << 4) | (addr & 0xf);
	else
		cmd[4] = addr & 0xff;
	uint8_t data[8];
	int res = stlink_send_recv_retry(cmd, 16, data, 8);
	if (res == STLINK_ERROR_OK) {
		uint32_t ret = data[4] | data[5] << 8 | data[6] << 16 | data[7] << 24;
		*reg = ret;
	} else {
		DEBUG_WARN("%s error %d\n", __func__, res);
	}
	return res;
}

static int stlink_write_dp_register(uint16_t port, uint16_t addr, uint32_t val)
{
	if (port == STLINK_DEBUG_PORT_ACCESS && addr == 8) {
		Stlink.dap_select = val;
	  DEBUG_PROBE("Caching SELECT 0x%02" PRIx32 "\n", val);
		return STLINK_ERROR_OK;
	} else {
		uint8_t cmd[16] = {
			STLINK_DEBUG_COMMAND, STLINK_DEBUG_APIV2_WRITE_DAP_REG,
			port & 0xff, port >> 8,
			addr & 0xff, addr >> 8,
			val & 0xff, (val >>  8) & 0xff, (val >> 16) & 0xff,
			(val >> 24) & 0xff};
		uint8_t data[2];
		stlink_send_recv_retry(cmd, 16, data, 2);
		return stlink_usb_error_check(data, true);
	}
}

uint32_t stlink_dp_low_access(ADIv5_DP_t *dp, uint8_t RnW,
				      uint16_t addr, uint32_t value)
{
	uint32_t response = 0;
	int res;
	if (RnW) {
		res = stlink_read_dp_register(
			STLINK_DEBUG_PORT_ACCESS, addr, &response);
	} else {
		res = stlink_write_dp_register(STLINK_DEBUG_PORT_ACCESS, addr, value);
	}
	if (res == STLINK_ERROR_WAIT)
		raise_exception(EXCEPTION_TIMEOUT, "DP ACK timeout");

	if(res == STLINK_ERROR_DP_FAULT) {
		dp->fault = 1;
		return 0;
	}
	if(res == STLINK_ERROR_FAIL)
		raise_exception(EXCEPTION_ERROR, "SWDP invalid ACK");

	return response;
}

static bool stlink_ap_setup(int ap)
{
	if (ap > 7)
		return false;
	uint8_t cmd[16] = {
		STLINK_DEBUG_COMMAND,
		STLINK_DEBUG_APIV2_INIT_AP,
		ap,
	};
	uint8_t data[2];
	DEBUG_PROBE("Open AP %d\n", ap);
	stlink_send_recv_retry(cmd, 16, data, 2);
	int res = stlink_usb_error_check(data, true);
	if (res) {
		if (Stlink.ver_hw == 30) {
			DEBUG_WARN("STLINKV3 only connects to STM8/32!\n");
		}
		return false;
	}
	return true;
}

static void stlink_ap_cleanup(int ap)
{
       uint8_t cmd[16] = {
               STLINK_DEBUG_COMMAND,
               STLINK_DEBUG_APIV2_CLOSE_AP_DBG,
               ap,
       };
       uint8_t data[2];
       send_recv(info.usb_link, cmd, 16, data, 2);
	   DEBUG_PROBE("Close AP %d\n", ap);
       stlink_usb_error_check(data, true);
}
static int stlink_usb_get_rw_status(bool verbose)
{
	uint8_t cmd[16] = {
		STLINK_DEBUG_COMMAND,
		STLINK_DEBUG_APIV2_GETLASTRWSTATUS2
	};
	uint8_t data[12];
	send_recv(info.usb_link, cmd, 16, data, 12);
	return stlink_usb_error_check(data, verbose);
}

static void stlink_readmem(ADIv5_AP_t *ap, void *dest, uint32_t src, size_t len)
{
	if (len == 0)
		return;
	size_t read_len = len;
	uint8_t type;
	if (src & 1 || len & 1) {
		type = STLINK_DEBUG_READMEM_8BIT;
		if (len > Stlink.block_size) {
			DEBUG_WARN(" Too large!\n");
			return;
		}
		if (len == 1)
			read_len ++; /* Fix read length as in openocd*/
	} else if (src & 3 || len & 3) {
		type = STLINK_DEBUG_APIV2_READMEM_16BIT;
	} else {
		type = STLINK_DEBUG_READMEM_32BIT;
	}
	uint8_t cmd[16] = {
		STLINK_DEBUG_COMMAND,
		type,
		src & 0xff, (src >>  8) & 0xff, (src >> 16) & 0xff,
		(src >> 24) & 0xff,
		len & 0xff, len >> 8, ap->apsel};
	int res = read_retry(cmd, 16, dest, read_len);
	if (res != STLINK_ERROR_OK) {
		/* FIXME: What is the right measure when failing?
		 *
		 * E.g. TM4C129 gets here when NRF probe reads 0x10000010
		 * Approach taken:
		 * Fill the memory with some fixed pattern so hopefully
		 * the caller notices the error*/
		DEBUG_WARN("stlink_readmem from  %" PRIx32 " to %" PRIx32 ", len %"
				   PRIx32 "failed\n", src, dest, (uint32_t) len);
		memset(dest, 0xff, len);
	}
	DEBUG_PROBE("stlink_readmem from %" PRIx32 " to %" PRIx32 ", len %" PRIx32
				"\n", src, dest, (uint32_t) len);
}

static void stlink_writemem8(usb_link_t *link, ADIv5_AP_t *ap, uint32_t addr,
							 size_t len, uint8_t *buffer)
{
	(void)link;
	while (len) {
		size_t length;
		/* OpenOCD has some note about writemem8*/
		if (len > Stlink.block_size)
			length = Stlink.block_size;
		else
			length = len;
		uint8_t cmd[16] = {
			STLINK_DEBUG_COMMAND,
			STLINK_DEBUG_WRITEMEM_8BIT,
			addr & 0xff, (addr >>  8) & 0xff, (addr >> 16) & 0xff,
			(addr >> 24) & 0xff,
			length & 0xff, length >> 8, ap->apsel};
		send_recv(link, cmd, 16, NULL, 0);
		send_recv(link, (void*)buffer, length, NULL, 0);
		stlink_usb_get_rw_status(true);
		len -= length;
		addr += length;
	}
}

static void stlink_writemem16(usb_link_t *link, ADIv5_AP_t *ap, uint32_t addr,
							  size_t len, uint16_t *buffer)
{
	(void)link;
	uint8_t cmd[16] = {
		STLINK_DEBUG_COMMAND,
		STLINK_DEBUG_APIV2_WRITEMEM_16BIT,
		addr & 0xff, (addr >>  8) & 0xff, (addr >> 16) & 0xff,
		(addr >> 24) & 0xff,
		len & 0xff, len >> 8, ap->apsel};
	send_recv(link, cmd, 16, NULL, 0);
	send_recv(link, (void*)buffer, len, NULL, 0);
	stlink_usb_get_rw_status(true);
}

static void stlink_writemem32(usb_link_t * link, ADIv5_AP_t *ap, uint32_t addr,
							  size_t len, uint32_t *buffer)
{
	(void)link;
	uint8_t cmd[16] = {
		STLINK_DEBUG_COMMAND,
		STLINK_DEBUG_WRITEMEM_32BIT,
		addr & 0xff, (addr >>  8) & 0xff, (addr >> 16) & 0xff,
		(addr >> 24) & 0xff,
		len & 0xff, len >> 8, ap->apsel};
	write_retry(cmd, 16, (void*)buffer, len);
}

static void stlink_regs_read(ADIv5_AP_t *ap, void *data)
{
	uint8_t cmd[16] = {STLINK_DEBUG_COMMAND, STLINK_DEBUG_APIV2_READALLREGS,
					   ap->apsel};
	uint8_t res[88];
	DEBUG_PROBE("AP %d: Read all core registers\n", ap->apsel);
	send_recv(info.usb_link, cmd, 16, res, 88);
	stlink_usb_error_check(res, true);
	memcpy(data, res + 4, 84);
}

static uint32_t stlink_reg_read(ADIv5_AP_t *ap, int num)
{
	uint8_t cmd[16] = {STLINK_DEBUG_COMMAND, STLINK_DEBUG_APIV2_READREG, num,
					   ap->apsel};
	uint8_t res[8];
	send_recv(info.usb_link, cmd, 16, res, 8);
	stlink_usb_error_check(res, true);
	uint32_t ret = res[0] | res[1] << 8 | res[2] << 16 | res[3] << 24;
	DEBUG_PROBE("AP %d: Read reg %02" PRId32 " val 0x%08" PRIx32 "\n",
				 ap->apsel, num, ret);
	return ret;
}

static void stlink_reg_write(ADIv5_AP_t *ap, int num, uint32_t val)
{
	uint8_t cmd[16] = {
		STLINK_DEBUG_COMMAND, STLINK_DEBUG_APIV2_WRITEREG, num,
		val & 0xff, (val >>  8) & 0xff, (val >> 16) & 0xff,
		(val >> 24) & 0xff, ap->apsel};
	uint8_t res[2];
	send_recv(info.usb_link, cmd, 16, res, 2);
	DEBUG_PROBE("AP %d: Write reg %02" PRId32 " val 0x%08" PRIx32 "\n",
				 ap->apsel, num, val);
	stlink_usb_error_check(res, true);
}

static void stlink_mem_write_sized(	ADIv5_AP_t *ap, uint32_t dest,
									const void *src, size_t len,
									enum align align)
{
	if (len == 0)
		return;
	usb_link_t *link = info.usb_link;
	switch(align) {
	case ALIGN_BYTE:
		stlink_writemem8(link, ap, dest, len, (uint8_t *) src);
		break;
	case ALIGN_HALFWORD:
		stlink_writemem16(link,ap, dest, len, (uint16_t *) src);
		break;
	case ALIGN_WORD:
	case ALIGN_DWORD:
		stlink_writemem32(link, ap, dest, len, (uint32_t *) src);
		break;
	}
}

static void stlink_ap_write(ADIv5_AP_t *ap, uint16_t addr, uint32_t value)
{
       stlink_write_dp_register(ap->apsel, addr, value);
}

static uint32_t stlink_ap_read(ADIv5_AP_t *ap, uint16_t addr)
{
	uint32_t ret;
	stlink_read_dp_register(ap->apsel, addr, &ret);
	return ret;
}

int jtag_scan_stlinkv2(bmp_info_t *info, const uint8_t *irlens)
{
	uint32_t idcodes[JTAG_MAX_DEVS+1];
	(void) *irlens;
	target_list_free();

	jtag_dev_count = 0;
	memset(&jtag_devs, 0, sizeof(jtag_devs));
	if (stlink_enter_debug_jtag(info))
		return 0;
	jtag_dev_count = stlink_read_idcodes(info, idcodes);
	/* Check for known devices and handle accordingly */
	for(int i = 0; i < jtag_dev_count; i++)
		jtag_devs[i].jd_idcode = idcodes[i];
	for(int i = 0; i < jtag_dev_count; i++)
		for(int j = 0; dev_descr[j].idcode; j++)
			if((jtag_devs[i].jd_idcode & dev_descr[j].idmask) ==
			   dev_descr[j].idcode) {
				if(dev_descr[j].handler)
					dev_descr[j].handler(i, dev_descr[j].idcode);
				break;
			}

	return jtag_dev_count;
}

int stlink_jtag_dp_init(ADIv5_DP_t *dp)
{
	dp->dp_read = stlink_dp_read;
	dp->error = stlink_dp_error;
	dp->low_access = stlink_dp_low_access;
	dp->abort = stlink_dp_abort;

	return true;

}

void stlink_adiv5_dp_defaults(ADIv5_DP_t *dp)
{
	dp->ap_regs_read = stlink_regs_read;
	dp->ap_reg_read = stlink_reg_read;
	dp->ap_reg_write = stlink_reg_write;
	dp->ap_setup = stlink_ap_setup;
	dp->ap_cleanup = stlink_ap_cleanup;
	dp->ap_write = stlink_ap_write;
	dp->ap_read = stlink_ap_read;
	dp->mem_read = stlink_readmem;
	dp->mem_write_sized = stlink_mem_write_sized;
}

int stlink_enter_debug_swd(bmp_info_t *info, ADIv5_DP_t *dp)
{
	stlink_leave_state(info);
	Stlink.transport_mode = STLINK_MODE_SWD;
	if (Stlink.ver_stlink == 3)
		stlink3_set_freq_divisor(info, 2);
	else
		stlink_set_freq_divisor(info, 1);
	uint8_t cmd[16] = {STLINK_DEBUG_COMMAND,
					  STLINK_DEBUG_APIV2_ENTER,
					  STLINK_DEBUG_ENTER_SWD_NO_RESET};
	uint8_t data[2];
	DEBUG_INFO("Enter SWD\n");
	stlink_send_recv_retry(cmd, 16, data, 2);
	if (stlink_usb_error_check(data, true))
		return -1;
	dp->idcode = stlink_read_coreid();
	dp->dp_read = stlink_dp_read;
	dp->error = stlink_dp_error;
	dp->low_access = stlink_dp_low_access;
	dp->abort = stlink_dp_abort;

	stlink_dp_error(dp);
	return 0;
}
