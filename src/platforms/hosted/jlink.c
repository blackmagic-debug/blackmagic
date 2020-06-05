/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2020 Uwe Bonnes (bon@elektron.ikp.physik.tu-darmstadt.de)
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
/* Base on code from git://repo.or.cz/libjaylink.git
 * and https://github.com/afaerber/jlink.git*/

#include "general.h"
#include "gdb_if.h"
#include "adiv5.h"
#include "exception.h"

#include <assert.h>
#include <unistd.h>
#include <signal.h>
#include <ctype.h>
#include <sys/time.h>

#include "cl_utils.h"
#include "jlink.h"

#define USB_PID_SEGGER       0x1366

/* Only two devices PIDS tested so long */
#define USB_VID_SEGGER_0101  0x0101
#define USB_VID_SEGGER_0105  0x0105

static void jlink_print_caps(bmp_info_t *info)
{
	uint8_t cmd[1] = {CMD_GET_CAPS};
	uint8_t res[4];
	send_recv(info->usb_link, cmd, 1, res, sizeof(res));
	uint32_t caps = res[0] | (res[1] << 8) | (res[2] << 16) | (res[3] << 24);
	DEBUG_INFO("Caps %" PRIx32 "\n", caps);
	if (caps & JLINK_CAP_GET_HW_VERSION) {
		uint8_t cmd[1] = {CMD_GET_HW_VERSION};
		send_recv(info->usb_link, cmd, 1, NULL, 0);
		send_recv(info->usb_link, NULL, 0, res, sizeof(res));
		DEBUG_INFO("HW: Type %d, Major %d, Minor %d, Rev %d\n",
			   res[3], res[2], res[1], res[0]);
	}
}
static void jlink_print_speed(bmp_info_t *info)
{
	uint8_t cmd[1] = {CMD_GET_SPEED};
	uint8_t res[6];
	send_recv(info->usb_link, cmd, 1, res, sizeof(res));
	uint32_t speed = res[0] | (res[1] << 8) | (res[2] << 16) | (res[3] << 24);
	double freq_mhz = speed / 1000000.0;
	uint16_t divisor = res[4] | (res[5] << 8);
	DEBUG_INFO("Emulator speed %3.1f MHz, Mindiv %d\n", freq_mhz, divisor);
}

static void jlink_print_version(bmp_info_t *info)
{
	uint8_t cmd[1] = {CMD_GET_VERSION};
	uint8_t len_str[2];
	send_recv(info->usb_link, cmd, 1, len_str, sizeof(len_str));
	uint8_t version[0x70];
	send_recv(info->usb_link, NULL, 0, version, sizeof(version));
	DEBUG_INFO("%s\n", version );
}

static void jlink_print_interfaces(bmp_info_t *info)
{
	uint8_t cmd[2] = {CMD_GET_SELECT_IF, JLINK_IF_GET_ACTIVE};
	uint8_t res[4];
	send_recv(info->usb_link, cmd, 2, res, sizeof(res));
	cmd[1] = JLINK_IF_GET_AVAILABLE;
	uint8_t res1[4];
	send_recv(info->usb_link, cmd, 2, res1, sizeof(res1));
	DEBUG_INFO("%s active", (res[0] == SELECT_IF_SWD) ? "SWD":
		   (res[0] == SELECT_IF_JTAG) ? "JTAG" : "NONE");
	uint8_t other_interface = res1[0] - (res[0] + 1);
	if (other_interface)
		DEBUG_INFO(", %s available\n",
			   (other_interface == JLINK_IF_SWD) ? "SWD": "JTAG");
	else
		DEBUG_WARN(", %s not available\n",
			   ((res[0] + 1) == JLINK_IF_SWD) ? "JTAG": "SWD");
}

static void jlink_info(bmp_info_t *info)
{
	jlink_print_version(info);
	jlink_print_speed(info);
	jlink_print_caps(info);
	jlink_print_interfaces(info);
}

/* On success endpoints are set and return 0, !0 else */
static int initialize_handle(bmp_info_t *info, libusb_device *dev)
{
	struct libusb_config_descriptor *config;
	int ret =  libusb_get_active_config_descriptor(dev, &config);
	if (ret != LIBUSB_SUCCESS) {
		DEBUG_WARN( "Failed to get configuration descriptor: %s.",
				libusb_error_name(ret));
		return -1;
	}
	const struct libusb_interface *interface;
	bool found_interface = false;
	const struct libusb_interface_descriptor *desc;
	for (int i = 0; i < config->bNumInterfaces; i++) {
		interface = &config->interface[i];
		desc = &interface->altsetting[0];
		if (desc->bInterfaceClass != LIBUSB_CLASS_VENDOR_SPEC)
			continue;
		if (desc->bInterfaceSubClass != LIBUSB_CLASS_VENDOR_SPEC)
			continue;
		if (desc->bNumEndpoints < 2)
			continue;
		found_interface = true;
		if (libusb_claim_interface (
				info->usb_link->ul_libusb_device_handle, i)) {
			DEBUG_WARN( " Can not claim handle\n");
			found_interface = false;
		}
		break;
	}
	if (!found_interface) {
		DEBUG_WARN( "No suitable interface found.");
		libusb_free_config_descriptor(config);
		return -1;
	}
	for (int i = 0; i < desc->bNumEndpoints; i++) {
		const struct libusb_endpoint_descriptor *epdesc = &desc->endpoint[i];
		if (epdesc->bEndpointAddress & LIBUSB_ENDPOINT_IN) {
			info->usb_link->ep_rx = epdesc->bEndpointAddress;
		} else {
			info->usb_link->ep_tx = epdesc->bEndpointAddress;
		}
	}
	libusb_free_config_descriptor(config);
	return 0;
}
/* Return 0 if single J-Link device connected or
 * serial given matches one of several J-Link devices.
 */
int jlink_init(bmp_info_t *info)
{
	usb_link_t *jl = calloc(1, sizeof(usb_link_t));
	if (!jl)
		return -1;
	info->usb_link = jl;
	jl->ul_libusb_ctx = info->libusb_ctx;
	int ret = -1;
	libusb_device **devs;
    if (libusb_get_device_list(info->libusb_ctx, &devs) < 0) {
        DEBUG_WARN( "libusb_get_device_list() failed");
		return ret;
	}
	int i = 0;
	for (;  devs[i]; i++) {
		libusb_device *dev =  devs[i];
		struct libusb_device_descriptor desc;
		if (libusb_get_device_descriptor(dev, &desc) < 0) {
            DEBUG_WARN( "libusb_get_device_descriptor() failed");
			goto error;;
		}
		if (desc.idVendor !=  USB_PID_SEGGER)
			continue;
		if ((desc.idProduct != USB_VID_SEGGER_0101) &&
			(desc.idProduct != USB_VID_SEGGER_0105))
			continue;
		int res = libusb_open(dev, &jl->ul_libusb_device_handle);
		if (res != LIBUSB_SUCCESS)
			continue;
		char buf[32];
		res = libusb_get_string_descriptor_ascii(jl->ul_libusb_device_handle,
			desc.iSerialNumber, (uint8_t*) buf, sizeof(buf));
		if ((res <= 0) || (!strstr(buf, info->serial))) {
			libusb_close(jl->ul_libusb_device_handle);
			continue;
		}
		break;
	}
	if (!devs[i])
		goto error;
	if (initialize_handle(info, devs[i]))
		goto error;
	jl->req_trans = libusb_alloc_transfer(0);
	jl->rep_trans = libusb_alloc_transfer(0);
	if (!jl->req_trans || !jl->rep_trans ||
		!jl->ep_tx || !jl->ep_rx) {
		DEBUG_WARN("Device setup failed\n");
		goto error;
	}
	libusb_free_device_list(devs, 1);
	jlink_info(info);
	return 0;
  error:
	libusb_free_device_list(devs, 1);
	return -1;

}

const char *jlink_target_voltage(bmp_info_t *info)
{
        uint8_t cmd[1] = {CMD_GET_HW_STATUS};
        uint8_t res[8];
        send_recv(info->usb_link, cmd, 1, res, sizeof(res));
        uint16_t mVolt = res[0] | (res[1] << 8);
        static char ret[7];
        sprintf(ret, "%2d.%03d", mVolt / 1000, mVolt % 1000);
        return ret;
}

static bool srst_status = false;
void jlink_srst_set_val(bmp_info_t *info, bool assert)
{
        uint8_t cmd[1];
        cmd[0]= (assert)? CMD_HW_RESET0: CMD_HW_RESET1;
        send_recv(info->usb_link, cmd, 1, NULL, 0);
        platform_delay(2);
        srst_status = assert;
}

bool jlink_srst_get_val(bmp_info_t *info) {
        uint8_t cmd[1] = {CMD_GET_HW_STATUS};
        uint8_t res[8];
        send_recv(info->usb_link, cmd, 1, res, sizeof(res));
        return !(res[6]);
}
