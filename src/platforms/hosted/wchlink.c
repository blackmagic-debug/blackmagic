/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2023-2025 1BitSquared <info@1bitsquared.com>
 * Written by Rafael Silva <perigoso@riseup.net>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <libusb.h>

#include "wchlink.h"
#include "wchlink_protocol.h"
#include "buffer_utils.h"

typedef struct wchlink {
	struct wchlink_fw_version {
		uint8_t major;
		uint8_t minor;
	} fw_version; /* Firmware version */

	uint8_t hw_type; /* Hardware type */

	uint8_t riscvchip; /* The attached RISC-V chip code */
} wchlink_s;

static wchlink_s wchlink;

/* WCH-Link USB protocol functions */

static char *wchlink_command_error(const uint8_t command, const uint8_t subcommand, const uint8_t error)
{
	/* Only this error is formally known, so we hack it's identification a bit for now */
	if (command == WCH_CMD_CONTROL && subcommand == WCH_CONTROL_SUBCMD_ATTACH && error == WCH_ERR_ATTACH)
		return "Failed to attach to target";
	return "Unknown";
}

/*
 * Send a command to the WCH-Link.
 *
 * ┌─────────────┬────────┬─────────┬──────────────┬──────────────────────────────┐
 * │    Byte     │   0    │    1    │      2       │            3:End             │
 * ├─────────────┼────────┼─────────┼──────────────┼──────────────────────────────┤
 * │ Description │ Header │ Command │ Payload Size │ Payload (Sub-command + Data) │
 * └─────────────┴────────┴─────────┴──────────────┴──────────────────────────────┘
 *	See wchlink_protocol.h for more information.
 *  
 * Returns true for success, false for failure.
 */
bool wchlink_command_send_recv(const uint8_t command, const uint8_t subcommand, const void *const payload,
	const size_t payload_length, void *const response, const size_t response_length)
{
	/*
	 * Total request size is packet header + command + payload size + payload (for which we always add the subcommand byte) 
	 * Total response size is packet header + command/error + payload size + payload
	 */
	const size_t request_size = 4U + payload_length;
	const size_t response_size = 3U + response_length;

	/* Stack buffer for the transfer, this is much larger than we need */
	uint8_t buffer[256U] = {0};
	if (request_size > sizeof(buffer) || response_size > sizeof(buffer))
		return false;

	/* Prepare the command packet */
	buffer[WCH_CMD_PACKET_HEADER_OFFSET] = WCH_CMD_PACKET_HEADER_OUT; /* Command packet header */
	buffer[WCH_CMD_PACKET_CMD_ERROR_OFFSET] = command;                /* Command */
	buffer[WCH_CMD_PACKET_SIZE_OFFSET] = payload_length + 1U;         /* Payload size */
	buffer[WCH_CMD_PACKET_PAYLOAD_OFFSET] = subcommand;               /* Subcommand as the first byte of the payload */

	/* Copy in the payload if any */
	if (payload_length && payload)
		memcpy(buffer + WCH_CMD_PACKET_PAYLOAD_OFFSET + 1U, payload, payload_length);

	/* Send the command and receive the response */
	if (bmda_usb_transfer(bmda_probe_info.usb_link, buffer, request_size, buffer, response_size, WCH_USB_TIMEOUT) < 0)
		return false;

	/* Check the response */
	if (buffer[WCH_CMD_PACKET_HEADER_OFFSET] != WCH_CMD_PACKET_HEADER_IN) {
		DEBUG_ERROR("wchlink protocol error: malformed response\n");
		return false;
	}
	if (buffer[WCH_CMD_PACKET_CMD_ERROR_OFFSET] != command) {
		DEBUG_ERROR("wchlink protocol error: 0x%02x - %s\n", buffer[WCH_CMD_PACKET_CMD_ERROR_OFFSET],
			wchlink_command_error(command, subcommand, buffer[WCH_CMD_PACKET_CMD_ERROR_OFFSET]));
		return false;
	}
	if (buffer[WCH_CMD_PACKET_SIZE_OFFSET] != response_length) {
		DEBUG_ERROR("wchlink protocol error: response payload size mismatch\n");
		return false;
	}

	/* Copy the response payload if requested */
	if (response_length && response)
		memcpy(response, buffer + WCH_CMD_PACKET_PAYLOAD_OFFSET, response_length);

	return true;
}

/*
 * Do a DMI transfer.
 *
 * ┌────────────────────────────┐
 * │          Payload           │
 * ├─────────┬──────┬───────────┤
 * │    0    │ 1:4  │     5     │
 * ├─────────┼──────┼───────────┤
 * │ Address │ Data │ Operation │
 * └─────────┴──────┴───────────┘
 * ┌────────────────────────────┐
 * │      Response payload      │
 * ├─────────┬──────┬───────────┤
 * │    0    │ 1:4  │     5     │
 * ├─────────┼──────┼───────────┤
 * │ Address │ Data │  Status   │
 * └─────────┴──────┴───────────┘
 *	See wchlink_protocol.h for more information.
 *  
 * Returns true for success, false for failure.
 */
bool wchlink_transfer_dmi(const uint8_t operation, const uint32_t address, const uint32_t data_in,
	uint32_t *const data_out, uint8_t *const status)
{
	/* The DMI register address must be a 7 or 8-bit address */
	if (address & ~0xffU) {
		DEBUG_ERROR("wchlink protocol error: DMI address 0x%08" PRIx32 " is out of range\n", address);
		return false;
	}

	/* Stack buffer for the transfer */
	uint8_t buffer[9U] = {0};

	/* Prepare the command packet */
	buffer[WCH_CMD_PACKET_HEADER_OFFSET] = WCH_CMD_PACKET_HEADER_OUT; /* Command packet header */
	buffer[WCH_CMD_PACKET_CMD_ERROR_OFFSET] = WCH_CMD_DMI;            /* Command */
	buffer[WCH_CMD_PACKET_SIZE_OFFSET] = 6U;                          /* Payload size */

	/* Construct the payload */
	buffer[WCH_CMD_PACKET_PAYLOAD_OFFSET + WCH_DMI_ADDR_OFFSET] = address & 0xffU;   /* Address */
	write_be4(buffer, WCH_CMD_PACKET_PAYLOAD_OFFSET + WCH_DMI_DATA_OFFSET, data_in); /* Data */
	buffer[WCH_CMD_PACKET_PAYLOAD_OFFSET + WCH_DMI_OP_STATUS_OFFSET] = operation;    /* Operation */

	/* Send the command and receive the response */
	if (bmda_usb_transfer(bmda_probe_info.usb_link, buffer, sizeof(buffer), buffer, sizeof(buffer), WCH_USB_TIMEOUT) <
		0)
		return false;

	/* Check the response */
	if (buffer[WCH_CMD_PACKET_HEADER_OFFSET] != WCH_CMD_PACKET_HEADER_IN) {
		DEBUG_ERROR("wchlink protocol error: malformed response\n");
		return false;
	}
	if (buffer[WCH_CMD_PACKET_CMD_ERROR_OFFSET] != WCH_CMD_DMI) {
		DEBUG_ERROR("wchlink protocol error: 0x%02x - %s\n", buffer[WCH_CMD_PACKET_CMD_ERROR_OFFSET],
			wchlink_command_error(WCH_CMD_DMI, 0, buffer[WCH_CMD_PACKET_CMD_ERROR_OFFSET]));
		return false;
	}
	if (buffer[WCH_CMD_PACKET_SIZE_OFFSET] != 6U) {
		DEBUG_ERROR("wchlink protocol error: response payload size mismatch\n");
		return false;
	}

	/* Copy over the result */
	if (data_out)
		*data_out = read_be4(buffer, WCH_CMD_PACKET_PAYLOAD_OFFSET + WCH_DMI_DATA_OFFSET);
	if (status)
		*status = buffer[WCH_CMD_PACKET_PAYLOAD_OFFSET + WCH_DMI_OP_STATUS_OFFSET];

	return true;
}

/*
 * Try to claim the debugging interface of a WCH-Link.
 * On success this copies the command endpoint addresses identified into the
 * usb_link_s sub-structure of bmda_probe_s (bmda_probe_info.usb_link) for later use.
 * Returns true for success, false for failure.
 */
static bool wchlink_claim_interface(void)
{
	libusb_config_descriptor_s *config = NULL;
	const int result = libusb_get_active_config_descriptor(bmda_probe_info.libusb_dev, &config);
	if (result != LIBUSB_SUCCESS) {
		DEBUG_ERROR("Failed to get configuration descriptor: %s\n", libusb_error_name(result));
		return false;
	}
	const libusb_interface_descriptor_s *descriptor = NULL;
	for (size_t idx = 0; idx < config->bNumInterfaces; ++idx) {
		const libusb_interface_s *const interface = &config->interface[idx];
		const libusb_interface_descriptor_s *const interface_desc = &interface->altsetting[0];
		if (interface_desc->bInterfaceClass == LIBUSB_CLASS_VENDOR_SPEC &&
			interface_desc->bInterfaceSubClass == WCH_USB_INTERFACE_SUBCLASS && interface_desc->bNumEndpoints > 1U) {
			const int claim_result = libusb_claim_interface(bmda_probe_info.usb_link->device_handle, (int)idx);
			if (claim_result) {
				DEBUG_ERROR("Can not claim handle: %s\n", libusb_error_name(claim_result));
				return false;
			}
			bmda_probe_info.usb_link->interface = idx;
			descriptor = interface_desc;
			break;
		}
	}
	if (!descriptor) {
		DEBUG_ERROR("No suitable interface found\n");
		libusb_free_config_descriptor(config);
		return false;
	}
	for (size_t i = 0; i < descriptor->bNumEndpoints; i++) {
		const libusb_endpoint_descriptor_s *endpoint = &descriptor->endpoint[i];
		if ((endpoint->bEndpointAddress & LIBUSB_ENDPOINT_ADDRESS_MASK) == WCH_USB_MODE_RV_CMD_EPT_ADDR) {
			if (endpoint->bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK)
				bmda_probe_info.usb_link->ep_rx = endpoint->bEndpointAddress;
			else
				bmda_probe_info.usb_link->ep_tx = endpoint->bEndpointAddress;
		}
	}
	libusb_free_config_descriptor(config);
	return true;
}

/* WCH-Link command functions */

static char *wchlink_hw_type_to_string(const uint8_t hardware_id)
{
	switch (hardware_id) {
	case WCH_HARDWARE_TYPE_WCHLINK:
		return "WCH-Link (CH549)";
	case WCH_HARDWARE_TYPE_WCHLINKE2:
	case WCH_HARDWARE_TYPE_WCHLINKE:
		return "WCH-LinkE (CH32V305)";
	case WCH_HARDWARE_TYPE_WCHLINKS:
		return "WCH-LinkS (CH32V203)";
	case WCH_HARDWARE_TYPE_WCHLINKB:
		return "WCH-LinkB";
	case WCH_HARDWARE_TYPE_WCHLINKW:
		return "WCH-LinkW (CH32V208)";
	default:
		return "Unknown";
	}
}

static char *wchlink_riscvchip_to_string(const uint8_t hardware_id)
{
	switch (hardware_id) {
	case WCH_RISCVCHIP_CH32V103:
		return "CH32V103 RISC-V3A series";
	case WCH_RISCVCHIP_CH57X:
		return "CH571/CH573 RISC-V3A BLE 4.2 series";
	case WCH_RISCVCHIP_CH56X:
		return "CH565/CH569 RISC-V3A series";
	case WCH_RISCVCHIP_CH32V20X:
		return "CH32V20X RISC-V4B/V4C series";
	case WCH_RISCVCHIP_CH32V30X:
		return "CH32V30X RISC-V4C/V4F series";
	case WCH_RISCVCHIP_CH58X:
		return "CH581/CH582/CH583 RISC-V4A BLE 5.3 series";
	case WCH_RISCVCHIP_CH32V003:
		return "CH32V003 RISC-V2A series";
	case WCH_RISCVCHIP_CH59X:
		return "CH59x RISC-V4C BLE 5.4 series";
	case WCH_RISCVCHIP_CH32X035:
		return "CH32X035 RISC-V4C series";
	default:
		return "Unknown";
	}
}

static bool wchlink_get_version(void)
{
	uint8_t response[4U];
	if (!wchlink_command_send_recv(
			WCH_CMD_CONTROL, WCH_CONTROL_SUBCMD_GET_PROBE_INFO, NULL, 0, response, sizeof(response)))
		return false;

	wchlink.fw_version.major = response[WCH_VERSION_MAJOR_OFFSET];
	wchlink.fw_version.minor = response[WCH_VERSION_MINOR_OFFSET];
	DEBUG_INFO("Firmware version: v%" PRIu32 ".%" PRIu32 "\n", wchlink.fw_version.major, wchlink.fw_version.minor);

	const uint8_t hardware_type = response[WCH_HARDWARE_TYPE_OFFSET];
	DEBUG_INFO("Hardware type: %s\n", wchlink_hw_type_to_string(hardware_type));

	/* Build version string onto info struct for version command */
	snprintf(bmda_probe_info.version, sizeof(bmda_probe_info.version), "%s v%" PRIu32 ".%" PRIu32,
		wchlink_hw_type_to_string(hardware_type), wchlink.fw_version.major, wchlink.fw_version.minor);

	return true;
}

/*
 * This function is called when the WCH-Link attaches to certain types of RISC-V chip
 * It is unknown what this function does, but the official WCH-Link software calls it
 * 
 * Removing this function still allows the WCH-Link to work and the scan is successful
 * but it is unknown if it might required for some chips or states
 */
static bool wchlink_after_attach_unknown()
{
	DEBUG_INFO("Sending unknown WCH-Link command after attach\n");

	/* Response seems to be WCH_CONTROL_SUBCMD_UNKNOWN, but without knowing what the command does we won't check it blindly */
	uint8_t response = 0;
	return wchlink_command_send_recv(WCH_CMD_CONTROL, WCH_CONTROL_SUBCMD_UNKNOWN, NULL, 0, &response, sizeof(response));
}

/* WCH-Link attach routine, attempts to detect and attach to a connected RISC-V chip */
bool wchlink_attach()
{
	uint8_t response[5U];
	if (!wchlink_command_send_recv(WCH_CMD_CONTROL, WCH_CONTROL_SUBCMD_ATTACH, NULL, 0, response, sizeof(response)))
		return false;

	wchlink.riscvchip = response[WCH_RISCVCHIP_OFFSET];
	const uint32_t idcode = read_be4(response, WCH_IDCODDE_OFFSET);

	DEBUG_INFO("WCH-Link attached to RISC-V chip: %s\n", wchlink_riscvchip_to_string(wchlink.riscvchip));
	DEBUG_INFO("ID code: 0x%08" PRIx32 "\n", idcode);

	/* Some RISC-V chips require* an additional command to be sent after attach */
	switch (wchlink.riscvchip) {
	case WCH_RISCVCHIP_CH32V103:
	case WCH_RISCVCHIP_CH32V20X:
	case WCH_RISCVCHIP_CH32V30X:
	case WCH_RISCVCHIP_CH32V003:
		if (!wchlink_after_attach_unknown())
			return false;
		break;
	default:
		break;
	}

	return true;
}

bool wchlink_init(void)
{
	usb_link_s *link = calloc(1U, sizeof(usb_link_s));
	if (!link)
		return false;
	bmda_probe_info.usb_link = link;
	link->context = bmda_probe_info.libusb_ctx;
	const int result = libusb_open(bmda_probe_info.libusb_dev, &link->device_handle);
	if (result != LIBUSB_SUCCESS) {
		DEBUG_ERROR("libusb_open() failed (%d): %s\n", result, libusb_error_name(result));
		return false;
	}
	if (!wchlink_claim_interface()) {
		libusb_close(bmda_probe_info.usb_link->device_handle);
		return false;
	}
	if (!link->ep_tx || !link->ep_rx) {
		DEBUG_ERROR("Device setup failed\n");
		libusb_release_interface(bmda_probe_info.usb_link->device_handle, bmda_probe_info.usb_link->interface);
		libusb_close(bmda_probe_info.usb_link->device_handle);
		return false;
	}
	if (!wchlink_get_version()) {
		DEBUG_ERROR("Failed to read WCH-Link information\n");
		libusb_release_interface(bmda_probe_info.usb_link->device_handle, bmda_probe_info.usb_link->interface);
		libusb_close(bmda_probe_info.usb_link->device_handle);
		return false;
	}
	return true;
}
