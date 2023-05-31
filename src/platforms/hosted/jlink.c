/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2020 Uwe Bonnes (bon@elektron.ikp.physik.tu-darmstadt.de)
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.	 If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Originally based on code from:
 * https://gitlab.zapb.de/libjaylink/libjaylink and
 * https://github.com/afaerber/jlink
 */

#include "general.h"
#include "gdb_if.h"
#include "adiv5.h"
#include "exception.h"

#include <assert.h>
#include <unistd.h>
#include <signal.h>
#include <ctype.h>
#include <sys/time.h>
#include <libusb.h>

#include "cli.h"
#include "jlink.h"

#define USB_VID_SEGGER 0x1366U

#define USB_PID_SEGGER_0101 0x0101U
#define USB_PID_SEGGER_0105 0x0105U
#define USB_PID_SEGGER_1015 0x1015U
#define USB_PID_SEGGER_1020 0x1020U

static uint32_t emu_caps;
static uint32_t emu_speed_khz;
static uint16_t emu_min_divisor;
static uint16_t emu_current_divisor;

static void jlink_print_caps(bmp_info_s *const info)
{
	uint8_t cmd = CMD_GET_CAPS;
	uint8_t res[4];
	bmda_usb_transfer(info->usb_link, &cmd, 1, res, sizeof(res));
	emu_caps = res[0] | (res[1] << 8U) | (res[2] << 16U) | (res[3] << 24U);
	DEBUG_INFO("Caps %" PRIx32 "\n", emu_caps);
	if (emu_caps & JLINK_CAP_GET_HW_VERSION) {
		uint8_t cmd = CMD_GET_HW_VERSION;
		bmda_usb_transfer(info->usb_link, &cmd, 1, NULL, 0);
		bmda_usb_transfer(info->usb_link, NULL, 0, res, sizeof(res));
		DEBUG_INFO("HW: Type %u, Major %u, Minor %u, Rev %u\n", res[3], res[2], res[1], res[0]);
	}
}

static void jlink_print_speed(bmp_info_s *const info)
{
	uint8_t cmd = CMD_GET_SPEEDS;
	uint8_t res[6];
	bmda_usb_transfer(info->usb_link, &cmd, 1, res, sizeof(res));
	emu_speed_khz = (res[0] | (res[1] << 8U) | (res[2] << 16U) | (res[3] << 24U)) / 1000U;
	emu_min_divisor = res[4] | (res[5] << 8U);
	DEBUG_INFO("Emulator speed %ukHz, minimum divisor %u%s\n", emu_speed_khz, emu_min_divisor,
		(emu_caps & JLINK_CAP_GET_SPEEDS) ? "" : ", fixed");
}

static void jlink_print_version(bmp_info_s *const info)
{
	uint8_t cmd = CMD_GET_VERSION;
	uint8_t len_str[2];
	bmda_usb_transfer(info->usb_link, &cmd, 1, len_str, sizeof(len_str));
	uint8_t version[0x70];
	bmda_usb_transfer(info->usb_link, NULL, 0, version, sizeof(version));
	version[0x6f] = '\0';
	DEBUG_INFO("%s\n", version);
}

static void jlink_print_interfaces(bmp_info_s *const info)
{
	uint8_t cmd[2] = {
		CMD_GET_SELECT_IF,
		JLINK_IF_GET_ACTIVE,
	};
	uint8_t selected_if[4];
	bmda_usb_transfer(info->usb_link, cmd, 2, selected_if, sizeof(selected_if));
	cmd[1] = JLINK_IF_GET_AVAILABLE;
	uint8_t available_ifs[4];
	bmda_usb_transfer(info->usb_link, cmd, 2, available_ifs, sizeof(available_ifs));
	if (selected_if[0] == SELECT_IF_SWD)
		DEBUG_INFO("SWD active");
	else if (selected_if[0] == SELECT_IF_JTAG)
		DEBUG_INFO("JTAG active");
	else
		DEBUG_INFO("No interfaces active");
	const uint8_t other_interface = available_ifs[0] - (selected_if[0] + 1U);
	if (other_interface)
		DEBUG_INFO(", %s available\n", other_interface == JLINK_IF_SWD ? "SWD" : "JTAG");
	else
		DEBUG_INFO(", %s not available\n", selected_if[0] + 1U == JLINK_IF_SWD ? "JTAG" : "SWD");
}

static void jlink_info(bmp_info_s *const info)
{
	jlink_print_version(info);
	jlink_print_caps(info);
	jlink_print_speed(info);
	jlink_print_interfaces(info);
}

/*
 * Try to claim the debugging interface of a J-Link adaptor.
 * On success this copies the endpoint addresses identified into the
 * usb_link_s sub-structure of bmp_info_s (info.usb_link) for later use.
 * Returns true for success, false for failure.
 */
static bool jlink_claim_interface(void)
{
	libusb_config_descriptor_s *config;
	const int result = libusb_get_active_config_descriptor(info.libusb_dev, &config);
	if (result != LIBUSB_SUCCESS) {
		DEBUG_ERROR("Failed to get configuration descriptor: %s\n", libusb_error_name(result));
		return false;
	}
	const libusb_interface_descriptor_s *descriptor = NULL;
	for (size_t idx = 0; idx < config->bNumInterfaces; ++idx) {
		const libusb_interface_s *const interface = &config->interface[idx];
		// XXX: This fails to handle multiple alt-modes being present correctly.
		const libusb_interface_descriptor_s *const interface_desc = &interface->altsetting[0];
		if (interface_desc->bInterfaceClass == LIBUSB_CLASS_VENDOR_SPEC &&
			interface_desc->bInterfaceSubClass == LIBUSB_CLASS_VENDOR_SPEC && interface_desc->bNumEndpoints > 1U) {
			const int result = libusb_claim_interface(info.usb_link->device_handle, (int)idx);
			if (result) {
				DEBUG_ERROR("Can not claim handle: %s\n", libusb_error_name(result));
				break;
			}
			info.usb_link->interface = idx;
			descriptor = interface_desc;
		}
	}
	if (!descriptor) {
		DEBUG_ERROR("No suitable interface found\n");
		libusb_free_config_descriptor(config);
		return false;
	}
	for (size_t i = 0; i < descriptor->bNumEndpoints; i++) {
		const libusb_endpoint_descriptor_s *endpoint = &descriptor->endpoint[i];
		if (endpoint->bEndpointAddress & LIBUSB_ENDPOINT_IN)
			info.usb_link->ep_rx = endpoint->bEndpointAddress;
		else
			info.usb_link->ep_tx = endpoint->bEndpointAddress;
	}
	libusb_free_config_descriptor(config);
	return true;
}

/*
 * Return true if single J-Link device connected or
 * serial given matches one of several J-Link devices.
 */
bool jlink_init(bmp_info_s *const info)
{
	usb_link_s *link = calloc(1, sizeof(usb_link_s));
	if (!link)
		return false;
	info->usb_link = link;
	link->context = info->libusb_ctx;
	int result = libusb_open(info->libusb_dev, &link->device_handle);
	if (result != LIBUSB_SUCCESS) {
		DEBUG_ERROR("libusb_open() failed (%d): %s\n", result, libusb_error_name(result));
		return false;
	}
	if (!jlink_claim_interface())
		return false;
	if (!link->ep_tx || !link->ep_rx) {
		DEBUG_ERROR("Device setup failed\n");
		libusb_release_interface(info->usb_link->device_handle, info->usb_link->interface);
		libusb_close(info->usb_link->device_handle);
		return false;
	}
	jlink_info(info);
	return true;
}

const char *jlink_target_voltage(bmp_info_s *const info)
{
	static char ret[7];
	uint8_t cmd = CMD_GET_HW_STATUS;
	uint8_t res[8];
	bmda_usb_transfer(info->usb_link, &cmd, 1, res, sizeof(res));
	const uint16_t millivolts = res[0] | (res[1] << 8U);
	snprintf(ret, sizeof(ret), "%2u.%03u", millivolts / 1000U, millivolts % 1000U);
	return ret;
}

void jlink_nrst_set_val(bmp_info_s *const info, const bool assert)
{
	uint8_t cmd = assert ? CMD_HW_RESET0 : CMD_HW_RESET1;
	bmda_usb_transfer(info->usb_link, &cmd, 1, NULL, 0);
	platform_delay(2);
}

bool jlink_nrst_get_val(bmp_info_s *const info)
{
	uint8_t cmd[1] = {CMD_GET_HW_STATUS};
	uint8_t res[8];
	bmda_usb_transfer(info->usb_link, cmd, 1, res, sizeof(res));
	return res[6] == 0;
}

void jlink_max_frequency_set(bmp_info_s *const info, const uint32_t freq)
{
	if (!(emu_caps & JLINK_CAP_GET_SPEEDS))
		return;
	if (!info->is_jtag)
		return;
	const uint16_t freq_khz = freq / 1000U;
	const uint16_t divisor = (emu_speed_khz + freq_khz - 1U) / freq_khz;
	if (divisor > emu_min_divisor)
		emu_current_divisor = divisor;
	else
		emu_current_divisor = emu_min_divisor;
	const uint16_t speed_khz = emu_speed_khz / emu_current_divisor;
	uint8_t cmd[3] = {
		CMD_SET_SPEED,
		speed_khz & 0xffU,
		speed_khz >> 8U,
	};
	DEBUG_WARN("Set Speed %d\n", speed_khz);
	bmda_usb_transfer(info->usb_link, cmd, 3, NULL, 0);
}

uint32_t jlink_max_frequency_get(bmp_info_s *const info)
{
	if ((emu_caps & JLINK_CAP_GET_SPEEDS) && info->is_jtag)
		return (emu_speed_khz * 1000U) / emu_current_divisor;
	return FREQ_FIXED;
}
