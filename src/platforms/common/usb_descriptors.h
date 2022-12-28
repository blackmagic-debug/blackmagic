/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2022 1BitSquared <info@1bitsquared.com>
 * Written by Rachel Mant <git@dragonmux.network>
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

#ifndef PLATFORMS_COMMON_USB_DESCRIPTORS_H
#define PLATFORMS_COMMON_USB_DESCRIPTORS_H

#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/bos.h>
#include <libopencm3/usb/cdc.h>
#include <libopencm3/usb/dfu.h>
#include <libopencm3/usb/microsoft.h>

#include "usb.h"
#include "serialno.h"
#include "version.h"
#include "usb_types.h"

#define BOARD_IDENT "Black Magic Probe " PLATFORM_IDENT FIRMWARE_VERSION

/* Top-level device descriptor */
static const usb_device_descriptor_s dev_desc = {
	.bLength = USB_DT_DEVICE_SIZE,
	.bDescriptorType = USB_DT_DEVICE,
	.bcdUSB = 0x0201,
	.bDeviceClass = 0xef, /* Miscellaneous Device */
	.bDeviceSubClass = 2, /* Common Class */
	.bDeviceProtocol = 1, /* Interface Association */
#if defined(LM4F) || defined(USB_HS)
	/* The USB specification requires that the control endpoint size for high
	 * speed devices (e.g., stlinkv3) is 64 bytes.
	 * On the LM4F is is required to be 64 bytes for the ICDI driver. */
	.bMaxPacketSize0 = 64,
#else
	.bMaxPacketSize0 = 32,
#endif
	.idVendor = 0x1d50,
	.idProduct = 0x6018,
	.bcdDevice = 0x0109,
	.iManufacturer = 1,
	.iProduct = 2,
	.iSerialNumber = 3,
	.bNumConfigurations = 1,
};

/* GDB interface descriptors */

/* This notification endpoint isn't implemented. According to CDC spec its
 * optional, but its absence causes a NULL pointer dereference in Linux cdc_acm
 * driver. */
static const usb_endpoint_descriptor_s gdb_comm_endp = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = (CDCACM_GDB_ENDPOINT + 1U) | USB_REQ_TYPE_IN,
	.bmAttributes = USB_ENDPOINT_ATTR_INTERRUPT,
	.wMaxPacketSize = 16,
	.bInterval = USB_MAX_INTERVAL,
};

static const usb_endpoint_descriptor_s gdb_data_endp[] = {
	{
		.bLength = USB_DT_ENDPOINT_SIZE,
		.bDescriptorType = USB_DT_ENDPOINT,
		.bEndpointAddress = CDCACM_GDB_ENDPOINT,
		.bmAttributes = USB_ENDPOINT_ATTR_BULK,
		.wMaxPacketSize = CDCACM_PACKET_SIZE,
		.bInterval = 1,
	},
	{
		.bLength = USB_DT_ENDPOINT_SIZE,
		.bDescriptorType = USB_DT_ENDPOINT,
		.bEndpointAddress = CDCACM_GDB_ENDPOINT | USB_REQ_TYPE_IN,
		.bmAttributes = USB_ENDPOINT_ATTR_BULK,
		.wMaxPacketSize = CDCACM_PACKET_SIZE,
		.bInterval = 1,
	},
};

static const struct {
	usb_cdc_header_descriptor_s header;
	usb_cdc_call_management_descriptor_s call_mgmt;
	usb_cdc_acm_descriptor_s acm;
	usb_cdc_union_descriptor_s cdc_union;
} __attribute__((packed)) gdb_cdcacm_functional_descriptors = {
	.header =
		{
			.bFunctionLength = sizeof(usb_cdc_header_descriptor_s),
			.bDescriptorType = CS_INTERFACE,
			.bDescriptorSubtype = USB_CDC_TYPE_HEADER,
			.bcdCDC = 0x0110,
		},
	.call_mgmt =
		{
			.bFunctionLength = sizeof(usb_cdc_call_management_descriptor_s),
			.bDescriptorType = CS_INTERFACE,
			.bDescriptorSubtype = USB_CDC_TYPE_CALL_MANAGEMENT,
			.bmCapabilities = 0,
			.bDataInterface = GDB_IF_NO + 1U,
		},
	.acm =
		{
			.bFunctionLength = sizeof(usb_cdc_acm_descriptor_s),
			.bDescriptorType = CS_INTERFACE,
			.bDescriptorSubtype = USB_CDC_TYPE_ACM,
			.bmCapabilities = 2, /* SET_LINE_CODING supported */
		},
	.cdc_union =
		{
			.bFunctionLength = sizeof(usb_cdc_union_descriptor_s),
			.bDescriptorType = CS_INTERFACE,
			.bDescriptorSubtype = USB_CDC_TYPE_UNION,
			.bControlInterface = GDB_IF_NO,
			.bSubordinateInterface0 = GDB_IF_NO + 1U,
		},
};

static const usb_interface_descriptor_s gdb_comm_iface = {
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,
	.bInterfaceNumber = 0,
	.bAlternateSetting = 0,
	.bNumEndpoints = 1,
	.bInterfaceClass = USB_CLASS_CDC,
	.bInterfaceSubClass = USB_CDC_SUBCLASS_ACM,
	.bInterfaceProtocol = USB_CDC_PROTOCOL_NONE,
	.iInterface = 4,

	.endpoint = &gdb_comm_endp,

	.extra = &gdb_cdcacm_functional_descriptors,
	.extralen = sizeof(gdb_cdcacm_functional_descriptors),
};

static const usb_interface_descriptor_s gdb_data_iface = {
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,
	.bInterfaceNumber = GDB_IF_NO + 1U,
	.bAlternateSetting = 0,
	.bNumEndpoints = 2,
	.bInterfaceClass = USB_CLASS_DATA,
	.bInterfaceSubClass = 0,
	.bInterfaceProtocol = 0,
	.iInterface = 0,

	.endpoint = gdb_data_endp,
};

static const usb_iface_assoc_descriptor_s gdb_assoc = {
	.bLength = USB_DT_INTERFACE_ASSOCIATION_SIZE,
	.bDescriptorType = USB_DT_INTERFACE_ASSOCIATION,
	.bFirstInterface = GDB_IF_NO,
	.bInterfaceCount = 2,
	.bFunctionClass = USB_CLASS_CDC,
	.bFunctionSubClass = USB_CDC_SUBCLASS_ACM,
	.bFunctionProtocol = USB_CDC_PROTOCOL_NONE,
	.iFunction = 4,
};

/* Physical/debug UART interface */

static const usb_endpoint_descriptor_s uart_comm_endp = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = (CDCACM_UART_ENDPOINT + 1U) | USB_REQ_TYPE_IN,
	.bmAttributes = USB_ENDPOINT_ATTR_INTERRUPT,
	.wMaxPacketSize = 16,
	.bInterval = USB_MAX_INTERVAL,
};

static const usb_endpoint_descriptor_s uart_data_endp[] = {
	{
		.bLength = USB_DT_ENDPOINT_SIZE,
		.bDescriptorType = USB_DT_ENDPOINT,
		.bEndpointAddress = CDCACM_UART_ENDPOINT,
		.bmAttributes = USB_ENDPOINT_ATTR_BULK,
#if defined(USB_HS)
		.wMaxPacketSize = CDCACM_PACKET_SIZE,
#else
		.wMaxPacketSize = CDCACM_PACKET_SIZE / 2U,
#endif
		.bInterval = 1,
	},
	{
		.bLength = USB_DT_ENDPOINT_SIZE,
		.bDescriptorType = USB_DT_ENDPOINT,
		.bEndpointAddress = CDCACM_UART_ENDPOINT | USB_REQ_TYPE_IN,
		.bmAttributes = USB_ENDPOINT_ATTR_BULK,
		.wMaxPacketSize = CDCACM_PACKET_SIZE,
		.bInterval = 1,
	},
};

static const struct {
	usb_cdc_header_descriptor_s header;
	usb_cdc_call_management_descriptor_s call_mgmt;
	usb_cdc_acm_descriptor_s acm;
	usb_cdc_union_descriptor_s cdc_union;
} __attribute__((packed)) uart_cdcacm_functional_descriptors = {
	.header =
		{
			.bFunctionLength = sizeof(usb_cdc_header_descriptor_s),
			.bDescriptorType = CS_INTERFACE,
			.bDescriptorSubtype = USB_CDC_TYPE_HEADER,
			.bcdCDC = 0x0110,
		},
	.call_mgmt =
		{
			.bFunctionLength = sizeof(usb_cdc_call_management_descriptor_s),
			.bDescriptorType = CS_INTERFACE,
			.bDescriptorSubtype = USB_CDC_TYPE_CALL_MANAGEMENT,
			.bmCapabilities = 0,
			.bDataInterface = UART_IF_NO + 1U,
		},
	.acm =
		{
			.bFunctionLength = sizeof(usb_cdc_acm_descriptor_s),
			.bDescriptorType = CS_INTERFACE,
			.bDescriptorSubtype = USB_CDC_TYPE_ACM,
			.bmCapabilities = 2, /* SET_LINE_CODING supported*/
		},
	.cdc_union =
		{
			.bFunctionLength = sizeof(usb_cdc_union_descriptor_s),
			.bDescriptorType = CS_INTERFACE,
			.bDescriptorSubtype = USB_CDC_TYPE_UNION,
			.bControlInterface = UART_IF_NO,
			.bSubordinateInterface0 = UART_IF_NO + 1U,
		},
};

static const usb_interface_descriptor_s uart_comm_iface = {
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,
	.bInterfaceNumber = UART_IF_NO,
	.bAlternateSetting = 0,
	.bNumEndpoints = 1,
	.bInterfaceClass = USB_CLASS_CDC,
	.bInterfaceSubClass = USB_CDC_SUBCLASS_ACM,
	.bInterfaceProtocol = USB_CDC_PROTOCOL_NONE,
	.iInterface = 5,

	.endpoint = &uart_comm_endp,

	.extra = &uart_cdcacm_functional_descriptors,
	.extralen = sizeof(uart_cdcacm_functional_descriptors),
};

static const usb_interface_descriptor_s uart_data_iface = {
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,
	.bInterfaceNumber = UART_IF_NO + 1U,
	.bAlternateSetting = 0,
	.bNumEndpoints = 2,
	.bInterfaceClass = USB_CLASS_DATA,
	.bInterfaceSubClass = 0,
	.bInterfaceProtocol = 0,
	.iInterface = 0,

	.endpoint = uart_data_endp,
};

static const usb_iface_assoc_descriptor_s uart_assoc = {
	.bLength = USB_DT_INTERFACE_ASSOCIATION_SIZE,
	.bDescriptorType = USB_DT_INTERFACE_ASSOCIATION,
	.bFirstInterface = UART_IF_NO,
	.bInterfaceCount = 2,
	.bFunctionClass = USB_CLASS_CDC,
	.bFunctionSubClass = USB_CDC_SUBCLASS_ACM,
	.bFunctionProtocol = USB_CDC_PROTOCOL_NONE,
	.iFunction = 5,
};

/* DFU interface */

const usb_dfu_descriptor_s dfu_function = {
	.bLength = sizeof(usb_dfu_descriptor_s),
	.bDescriptorType = DFU_FUNCTIONAL,
	.bmAttributes = USB_DFU_CAN_DOWNLOAD | USB_DFU_WILL_DETACH,
	.wDetachTimeout = 255,
	.wTransferSize = 1024,
	.bcdDFUVersion = 0x011a,
};

const usb_interface_descriptor_s dfu_iface = {
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,
	.bInterfaceNumber = DFU_IF_NO,
	.bAlternateSetting = 0,
	.bNumEndpoints = 0,
	.bInterfaceClass = 0xfe,
	.bInterfaceSubClass = 1,
	.bInterfaceProtocol = 1,
	.iInterface = 6,

	.extra = &dfu_function,
	.extralen = sizeof(dfu_function),
};

static const usb_iface_assoc_descriptor_s dfu_assoc = {
	.bLength = USB_DT_INTERFACE_ASSOCIATION_SIZE,
	.bDescriptorType = USB_DT_INTERFACE_ASSOCIATION,
	.bFirstInterface = DFU_IF_NO,
	.bInterfaceCount = 1,
	.bFunctionClass = 0xfe,
	.bFunctionSubClass = 1,
	.bFunctionProtocol = 1,
	.iFunction = 6,
};

/* Trace/SWO interface */

#ifdef PLATFORM_HAS_TRACESWO
static const usb_endpoint_descriptor_s trace_endp = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = TRACE_ENDPOINT | USB_REQ_TYPE_IN,
	.bmAttributes = USB_ENDPOINT_ATTR_BULK,
	.wMaxPacketSize = TRACE_ENDPOINT_SIZE,
	.bInterval = 0,
};

const usb_interface_descriptor_s trace_iface = {
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,
	.bInterfaceNumber = TRACE_IF_NO,
	.bAlternateSetting = 0,
	.bNumEndpoints = 1,
	.bInterfaceClass = 0xff,
	.bInterfaceSubClass = 0xff,
	.bInterfaceProtocol = 0xff,
	.iInterface = 7,

	.endpoint = &trace_endp,
};

static const usb_iface_assoc_descriptor_s trace_assoc = {
	.bLength = USB_DT_INTERFACE_ASSOCIATION_SIZE,
	.bDescriptorType = USB_DT_INTERFACE_ASSOCIATION,
	.bFirstInterface = TRACE_IF_NO,
	.bInterfaceCount = 1,
	.bFunctionClass = 0xff,
	.bFunctionSubClass = 0xff,
	.bFunctionProtocol = 0xff,
	.iFunction = 7,
};
#endif

/* Interface and configuration descriptors */

static const usb_interface_s ifaces[] = {
	{
		.num_altsetting = 1,
		.iface_assoc = &gdb_assoc,
		.altsetting = &gdb_comm_iface,
	},
	{
		.num_altsetting = 1,
		.altsetting = &gdb_data_iface,
	},
	{
		.num_altsetting = 1,
		.iface_assoc = &uart_assoc,
		.altsetting = &uart_comm_iface,
	},
	{
		.num_altsetting = 1,
		.altsetting = &uart_data_iface,
	},
	{
		.num_altsetting = 1,
		.iface_assoc = &dfu_assoc,
		.altsetting = &dfu_iface,
	},
#if defined(PLATFORM_HAS_TRACESWO)
	{
		.num_altsetting = 1,
		.iface_assoc = &trace_assoc,
		.altsetting = &trace_iface,
	},
#endif
};

static const usb_config_descriptor_s config = {
	.bLength = USB_DT_CONFIGURATION_SIZE,
	.bDescriptorType = USB_DT_CONFIGURATION,
	.wTotalLength = 0,
	.bNumInterfaces = TOTAL_INTERFACES,
	.bConfigurationValue = 1,
	.iConfiguration = 0,
	.bmAttributes = 0x80,
	.bMaxPower = 250,

	.interface = ifaces,
};

static const char *const usb_strings[] = {
	"Black Magic Debug",
	BOARD_IDENT,
	serial_no,
	"Black Magic GDB Server",
	"Black Magic UART Port",
	"Black Magic DFU",
#ifdef PLATFORM_HAS_TRACESWO
	"Black Magic Trace Capture",
#endif
};

/*
 * The GUIDs listed here are modified from 76be5ca1-e3a1-4b32-be5f-d9369d3d201a.
 * This value was generated by https://wasteaguid.info/
 *
 * In this scheme, we can replace any part of the second value chunk (e3a1).
 * For BMP's needs, we require 2 GUIDs for the interfaces, but as one does not
 * use any endpoints, we only encode the interface number into the GUID.
 * This results in the scheme 76be5ca1-e3NN-4b32-be5f-d9369d3d201a, where NN
 * is the interface number associated with the ID.
 */
#define DESCRIPTOR_SETS                1U
#define PROPERTY_DEVICE_INTERFACE_GUID u"DeviceInterfaceGUID"
#define VALUE_DFU_INTERFACE_GUID       u"{76be5ca1-e304-4b32-be5f-d9369d3d201a}"
#ifdef PLATFORM_HAS_TRACESWO
#define VALUE_TRACE_INTERFACE_GUID u"{76be5ca1-e305-4b32-be5f-d9369d3d201a}"
#endif

static const struct {
	microsoft_os_feature_compatible_id_descriptor driver_binding;
	microsoft_os_feature_registry_property_descriptor interface_guid;
} microsoft_os_dfu_if_features = {
	.driver_binding =
		{
			.header =
				{
					.wLength = MICROSOFT_OS_FEATURE_COMPATIBLE_ID_DESCRIPTOR_SIZE,
					.wDescriptorType = MICROSOFT_OS_FEATURE_COMPATIBLE_ID,
				},
			.compatible_id = MICROSOFT_OS_COMPATIBLE_ID_WINUSB,
			.sub_compatible_id = MICROSOFT_OS_COMPATIBLE_ID_NONE,
		},
	.interface_guid =
		{
			.header =
				{
					.wDescriptorType = MICROSOFT_OS_FEATURE_REG_PROPERTY,
				},
			.wPropertyDataType = REG_SZ,
			.wPropertyNameLength = ARRAY_LENGTH(PROPERTY_DEVICE_INTERFACE_GUID) * 2U,
			.PropertyName = PROPERTY_DEVICE_INTERFACE_GUID,
			.wPropertyDataLength = ARRAY_LENGTH(VALUE_DFU_INTERFACE_GUID) * 2U,
			.PropertyData = VALUE_DFU_INTERFACE_GUID,
		},
};

#ifdef PLATFORM_HAS_TRACESWO
static const struct {
	microsoft_os_feature_compatible_id_descriptor driver_binding;
	microsoft_os_feature_registry_property_descriptor interface_guid;
} microsoft_os_trace_if_features = {
	.driver_binding =
		{
			.header =
				{
					.wLength = MICROSOFT_OS_FEATURE_COMPATIBLE_ID_DESCRIPTOR_SIZE,
					.wDescriptorType = MICROSOFT_OS_FEATURE_COMPATIBLE_ID,
				},
			.compatible_id = MICROSOFT_OS_COMPATIBLE_ID_WINUSB,
			.sub_compatible_id = MICROSOFT_OS_COMPATIBLE_ID_NONE,
		},
	.interface_guid =
		{
			.header =
				{
					.wDescriptorType = MICROSOFT_OS_FEATURE_REG_PROPERTY,
				},
			.wPropertyDataType = REG_SZ,
			.wPropertyNameLength = ARRAY_LENGTH(PROPERTY_DEVICE_INTERFACE_GUID) * 2U,
			.PropertyName = PROPERTY_DEVICE_INTERFACE_GUID,
			.wPropertyDataLength = ARRAY_LENGTH(VALUE_TRACE_INTERFACE_GUID) * 2U,
			.PropertyData = VALUE_TRACE_INTERFACE_GUID,
		},
};
#endif

static const microsoft_os_descriptor_function_subset_header microsoft_os_descriptor_function_subsets[] = {
	{
		.wLength = MICROSOFT_OS_DESCRIPTOR_FUNCTION_SUBSET_HEADER_SIZE,
		.wDescriptorType = MICROSOFT_OS_SUBSET_HEADER_FUNCTION,
		.bFirstInterface = DFU_IF_NO,
		.bReserved = 0,
		.wTotalLength = 0,

		.feature_descriptors = &microsoft_os_dfu_if_features,
		.num_feature_descriptors = 2,
	},
#ifdef PLATFORM_HAS_TRACESWO
	{
		.wLength = MICROSOFT_OS_DESCRIPTOR_FUNCTION_SUBSET_HEADER_SIZE,
		.wDescriptorType = MICROSOFT_OS_SUBSET_HEADER_FUNCTION,
		.bFirstInterface = TRACE_IF_NO,
		.bReserved = 0,
		.wTotalLength = 0,

		.feature_descriptors = &microsoft_os_trace_if_features,
		.num_feature_descriptors = 2,
	},
#endif
};

static const microsoft_os_descriptor_config_subset_header microsoft_os_descriptor_config_subset = {
	.wLength = MICROSOFT_OS_DESCRIPTOR_CONFIG_SUBSET_HEADER_SIZE,
	.wDescriptorType = MICROSOFT_OS_SUBSET_HEADER_CONFIGURATION,
	.bConfigurationValue = 0,
	.bReserved = 0,
	.wTotalLength = 0,

	.function_subset_headers = microsoft_os_descriptor_function_subsets,
	.num_function_subset_headers = ARRAY_LENGTH(microsoft_os_descriptor_function_subsets),
};

static const microsoft_os_descriptor_set_header microsoft_os_descriptor_sets[DESCRIPTOR_SETS] = {
	{
		.wLength = MICROSOFT_OS_DESCRIPTOR_SET_HEADER_SIZE,
		.wDescriptorType = MICROSOFT_OS_SET_HEADER,
		.dwWindowsVersion = MICROSOFT_WINDOWS_VERSION_WINBLUE,
		.wTotalLength = 0,

		.vendor_code = 1,
		.num_config_subset_headers = 1,
		.config_subset_headers = &microsoft_os_descriptor_config_subset,
	},
};

static const microsoft_os_descriptor_set_information microsoft_os_descriptor_set_info = {
	.dwWindowsVersion = MICROSOFT_WINDOWS_VERSION_WINBLUE,
	.wMSOSDescriptorSetTotalLength = MICROSOFT_OS_DESCRIPTOR_SET_HEADER_SIZE +
		MICROSOFT_OS_DESCRIPTOR_CONFIG_SUBSET_HEADER_SIZE +
#ifdef PLATFORM_HAS_TRACESWO
		MICROSOFT_OS_DESCRIPTOR_FUNCTION_SUBSET_HEADER_SIZE + MICROSOFT_OS_FEATURE_COMPATIBLE_ID_DESCRIPTOR_SIZE +
		MICROSOFT_OS_FEATURE_REGISTRY_PROPERTY_DESCRIPTOR_SIZE_BASE +
		(ARRAY_LENGTH(PROPERTY_DEVICE_INTERFACE_GUID) * 2U) + (ARRAY_LENGTH(VALUE_TRACE_INTERFACE_GUID) * 2U) +
#endif
		MICROSOFT_OS_DESCRIPTOR_FUNCTION_SUBSET_HEADER_SIZE + MICROSOFT_OS_FEATURE_COMPATIBLE_ID_DESCRIPTOR_SIZE +
		MICROSOFT_OS_FEATURE_REGISTRY_PROPERTY_DESCRIPTOR_SIZE_BASE +
		(ARRAY_LENGTH(PROPERTY_DEVICE_INTERFACE_GUID) * 2U) + (ARRAY_LENGTH(VALUE_DFU_INTERFACE_GUID) * 2U),
	.bMS_VendorCode = 1,
	.bAltEnumCode = 0,
};

static const struct {
	usb_platform_device_capability_descriptor platform_descriptor;
} __attribute__((packed)) device_capability_descriptors = {
	.platform_descriptor =
		{
			.device_capability_descriptor =
				{
					.bLength = USB_DCT_PLATFORM_SIZE + MICROSOFT_OS_DESCRIPTOR_SET_INFORMATION_SIZE,
					.bDescriptorType = USB_DT_DEVICE_CAPABILITY,
					.bDevCapabilityType = USB_DCT_PLATFORM,
				},
			.bReserved = 0,
			.PlatformCapabilityUUID = MICROSOFT_OS_DESCRIPTOR_PLATFORM_CAPABILITY_ID,

			.CapabilityData = &microsoft_os_descriptor_set_info,
		},
};

static const usb_bos_descriptor bos = {
	.bLength = USB_DT_BOS_SIZE,
	.bDescriptorType = USB_DT_BOS,
	.wTotalLength = 0,
	.bNumDeviceCaps = 1,

	.device_capability_descriptors = &device_capability_descriptors,
};

#endif /* PLATFORMS_COMMON_USB_DESCRIPTORS_H */
