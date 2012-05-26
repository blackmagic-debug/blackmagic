/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
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

/* This file implements a the USB Communications Device Class - Abstract
 * Control Model (CDC-ACM) as defined in CDC PSTN subclass 1.2.  
 * A Device Firmware Upgrade (DFU 1.1) class interface is provided for 
 * field firmware upgrade.
 * 
 * The device's unique id is used as the USB serial number string.
 */

#include <libopencm3/stm32/f1/rcc.h>
#include <libopencm3/stm32/nvic.h>
#include <libopencm3/stm32/exti.h>
#include <libopencm3/stm32/f1/gpio.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/cdc.h>
#include <libopencm3/stm32/f1/scb.h>
#include <libopencm3/usb/dfu.h>
#include <stdlib.h>

#include "platform.h"
#include "traceswo.h"
#include "usbuart.h"

#define DFU_IF_NO 4

static char *get_dev_unique_id(char *serial_no);

static int configured;
static int cdcacm_gdb_dtr = 1;

static const struct usb_device_descriptor dev = {
        .bLength = USB_DT_DEVICE_SIZE,
        .bDescriptorType = USB_DT_DEVICE,
        .bcdUSB = 0x0200,
        .bDeviceClass = 0xEF,		/* Miscellaneous Device */
        .bDeviceSubClass = 2,		/* Common Class */
        .bDeviceProtocol = 1,		/* Interface Association */
        .bMaxPacketSize0 = 64,
        .idVendor = 0x1D50,
        .idProduct = 0x6018,
        .bcdDevice = 0x0100,
        .iManufacturer = 1,
        .iProduct = 2,
        .iSerialNumber = 3,
        .bNumConfigurations = 1,
};

/* This notification endpoint isn't implemented. According to CDC spec its 
 * optional, but its absence causes a NULL pointer dereference in Linux cdc_acm 
 * driver. */
static const struct usb_endpoint_descriptor gdb_comm_endp[] = {{
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = 0x82,
	.bmAttributes = USB_ENDPOINT_ATTR_INTERRUPT,
	.wMaxPacketSize = 16,
	.bInterval = 255,
}};

static const struct usb_endpoint_descriptor gdb_data_endp[] = {{
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = 0x01,
	.bmAttributes = USB_ENDPOINT_ATTR_BULK,
	.wMaxPacketSize = CDCACM_PACKET_SIZE,
	.bInterval = 1,
}, {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = 0x81,
	.bmAttributes = USB_ENDPOINT_ATTR_BULK,
	.wMaxPacketSize = CDCACM_PACKET_SIZE,
	.bInterval = 1,
}};

static const struct {
	struct usb_cdc_header_descriptor header;
	struct usb_cdc_call_management_descriptor call_mgmt;
	struct usb_cdc_acm_descriptor acm;
	struct usb_cdc_union_descriptor cdc_union;
} __attribute__((packed)) gdb_cdcacm_functional_descriptors = {
	.header = {
		.bFunctionLength = sizeof(struct usb_cdc_header_descriptor),
		.bDescriptorType = CS_INTERFACE,
		.bDescriptorSubtype = USB_CDC_TYPE_HEADER,
		.bcdCDC = 0x0110,
	},
	.call_mgmt = {
		.bFunctionLength = 
			sizeof(struct usb_cdc_call_management_descriptor),
		.bDescriptorType = CS_INTERFACE,
		.bDescriptorSubtype = USB_CDC_TYPE_CALL_MANAGEMENT,
		.bmCapabilities = 0,
		.bDataInterface = 1,
	},
	.acm = {
		.bFunctionLength = sizeof(struct usb_cdc_acm_descriptor),
		.bDescriptorType = CS_INTERFACE,
		.bDescriptorSubtype = USB_CDC_TYPE_ACM,
		.bmCapabilities = 2, /* SET_LINE_CODING supported */
	},
	.cdc_union = {
		.bFunctionLength = sizeof(struct usb_cdc_union_descriptor),
		.bDescriptorType = CS_INTERFACE,
		.bDescriptorSubtype = USB_CDC_TYPE_UNION,
		.bControlInterface = 0,
		.bSubordinateInterface0 = 1, 
	 }
};

static const struct usb_interface_descriptor gdb_comm_iface[] = {{
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,
	.bInterfaceNumber = 0,
	.bAlternateSetting = 0,
	.bNumEndpoints = 1,
	.bInterfaceClass = USB_CLASS_CDC,
	.bInterfaceSubClass = USB_CDC_SUBCLASS_ACM,
	.bInterfaceProtocol = USB_CDC_PROTOCOL_AT,
	.iInterface = 4,

	.endpoint = gdb_comm_endp,

	.extra = &gdb_cdcacm_functional_descriptors,
	.extralen = sizeof(gdb_cdcacm_functional_descriptors)
}};

static const struct usb_interface_descriptor gdb_data_iface[] = {{
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,
	.bInterfaceNumber = 1,
	.bAlternateSetting = 0,
	.bNumEndpoints = 2,
	.bInterfaceClass = USB_CLASS_DATA,
	.bInterfaceSubClass = 0,
	.bInterfaceProtocol = 0,
	.iInterface = 0,

	.endpoint = gdb_data_endp,
}};

static const struct usb_iface_assoc_descriptor gdb_assoc = {
	.bLength = USB_DT_INTERFACE_ASSOCIATION_SIZE,
	.bDescriptorType = USB_DT_INTERFACE_ASSOCIATION,
	.bFirstInterface = 0,
	.bInterfaceCount = 2,
	.bFunctionClass = USB_CLASS_CDC,
	.bFunctionSubClass = USB_CDC_SUBCLASS_ACM,
	.bFunctionProtocol = USB_CDC_PROTOCOL_AT,
	.iFunction = 0,
};

/* Serial ACM interface */
static const struct usb_endpoint_descriptor uart_comm_endp[] = {{
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = 0x84,
	.bmAttributes = USB_ENDPOINT_ATTR_INTERRUPT,
	.wMaxPacketSize = 16,
	.bInterval = 255,
}};

static const struct usb_endpoint_descriptor uart_data_endp[] = {{
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = 0x03,
	.bmAttributes = USB_ENDPOINT_ATTR_BULK,
	.wMaxPacketSize = CDCACM_PACKET_SIZE,
	.bInterval = 1,
}, {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = 0x83,
	.bmAttributes = USB_ENDPOINT_ATTR_BULK,
	.wMaxPacketSize = CDCACM_PACKET_SIZE,
	.bInterval = 1,
}};

static const struct {
	struct usb_cdc_header_descriptor header;
	struct usb_cdc_call_management_descriptor call_mgmt;
	struct usb_cdc_acm_descriptor acm;
	struct usb_cdc_union_descriptor cdc_union;
} __attribute__((packed)) uart_cdcacm_functional_descriptors = {
	.header = {
		.bFunctionLength = sizeof(struct usb_cdc_header_descriptor),
		.bDescriptorType = CS_INTERFACE,
		.bDescriptorSubtype = USB_CDC_TYPE_HEADER,
		.bcdCDC = 0x0110,
	},
	.call_mgmt = {
		.bFunctionLength = 
			sizeof(struct usb_cdc_call_management_descriptor),
		.bDescriptorType = CS_INTERFACE,
		.bDescriptorSubtype = USB_CDC_TYPE_CALL_MANAGEMENT,
		.bmCapabilities = 0,
		.bDataInterface = 3,
	},
	.acm = {
		.bFunctionLength = sizeof(struct usb_cdc_acm_descriptor),
		.bDescriptorType = CS_INTERFACE,
		.bDescriptorSubtype = USB_CDC_TYPE_ACM,
		.bmCapabilities = 2, /* SET_LINE_CODING supported*/
	},
	.cdc_union = {
		.bFunctionLength = sizeof(struct usb_cdc_union_descriptor),
		.bDescriptorType = CS_INTERFACE,
		.bDescriptorSubtype = USB_CDC_TYPE_UNION,
		.bControlInterface = 2,
		.bSubordinateInterface0 = 3, 
	 }
};

static const struct usb_interface_descriptor uart_comm_iface[] = {{
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,
	.bInterfaceNumber = 2,
	.bAlternateSetting = 0,
	.bNumEndpoints = 1,
	.bInterfaceClass = USB_CLASS_CDC,
	.bInterfaceSubClass = USB_CDC_SUBCLASS_ACM,
	.bInterfaceProtocol = USB_CDC_PROTOCOL_AT,
	.iInterface = 5,

	.endpoint = uart_comm_endp,

	.extra = &uart_cdcacm_functional_descriptors,
	.extralen = sizeof(uart_cdcacm_functional_descriptors)
}};

static const struct usb_interface_descriptor uart_data_iface[] = {{
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,
	.bInterfaceNumber = 3,
	.bAlternateSetting = 0,
	.bNumEndpoints = 2,
	.bInterfaceClass = USB_CLASS_DATA,
	.bInterfaceSubClass = 0,
	.bInterfaceProtocol = 0,
	.iInterface = 0,

	.endpoint = uart_data_endp,
}};

static const struct usb_iface_assoc_descriptor uart_assoc = {
	.bLength = USB_DT_INTERFACE_ASSOCIATION_SIZE,
	.bDescriptorType = USB_DT_INTERFACE_ASSOCIATION,
	.bFirstInterface = 2,
	.bInterfaceCount = 2,
	.bFunctionClass = USB_CLASS_CDC,
	.bFunctionSubClass = USB_CDC_SUBCLASS_ACM,
	.bFunctionProtocol = USB_CDC_PROTOCOL_AT,
	.iFunction = 0,
};

const struct usb_dfu_descriptor dfu_function = {
	.bLength = sizeof(struct usb_dfu_descriptor),
	.bDescriptorType = DFU_FUNCTIONAL,
	.bmAttributes = USB_DFU_CAN_DOWNLOAD | USB_DFU_WILL_DETACH,
	.wDetachTimeout = 255,
	.wTransferSize = 1024,
	.bcdDFUVersion = 0x011A,
};

const struct usb_interface_descriptor dfu_iface = {
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,
	.bInterfaceNumber = DFU_IF_NO,
	.bAlternateSetting = 0,
	.bNumEndpoints = 0,
	.bInterfaceClass = 0xFE,
	.bInterfaceSubClass = 1,
	.bInterfaceProtocol = 1,
	.iInterface = 6,

	.extra = &dfu_function,
	.extralen = sizeof(dfu_function),
};

static const struct usb_iface_assoc_descriptor dfu_assoc = {
	.bLength = USB_DT_INTERFACE_ASSOCIATION_SIZE,
	.bDescriptorType = USB_DT_INTERFACE_ASSOCIATION,
	.bFirstInterface = 4,
	.bInterfaceCount = 1,
	.bFunctionClass = 0xFE,
	.bFunctionSubClass = 1,
	.bFunctionProtocol = 1,
	.iFunction = 6,
};

static const struct usb_endpoint_descriptor trace_endp[] = {{
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = 0x85,
	.bmAttributes = USB_ENDPOINT_ATTR_BULK,
	.wMaxPacketSize = 64,
	.bInterval = 0,
}};

const struct usb_interface_descriptor trace_iface = {
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,
	.bInterfaceNumber = 5,
	.bAlternateSetting = 0,
	.bNumEndpoints = 1,
	.bInterfaceClass = 0xFF,
	.bInterfaceSubClass = 0xFF,
	.bInterfaceProtocol = 0xFF,
	.iInterface = 7,

	.endpoint = trace_endp,
};

static const struct usb_iface_assoc_descriptor trace_assoc = {
	.bLength = USB_DT_INTERFACE_ASSOCIATION_SIZE,
	.bDescriptorType = USB_DT_INTERFACE_ASSOCIATION,
	.bFirstInterface = 5,
	.bInterfaceCount = 1,
	.bFunctionClass = 0xFF,
	.bFunctionSubClass = 0xFF,
	.bFunctionProtocol = 0xFF,
	.iFunction = 7,
};

static const struct usb_interface ifaces[] = {{
	.num_altsetting = 1,
	.iface_assoc = &gdb_assoc,
	.altsetting = gdb_comm_iface,
}, {
	.num_altsetting = 1,
	.altsetting = gdb_data_iface,
}, {
	.num_altsetting = 1,
	.iface_assoc = &uart_assoc,
	.altsetting = uart_comm_iface,
}, {
	.num_altsetting = 1,
	.altsetting = uart_data_iface,
}, {
	.num_altsetting = 1,
	.iface_assoc = &dfu_assoc,
	.altsetting = &dfu_iface,
}, {
	.num_altsetting = 1,
	.iface_assoc = &trace_assoc,
	.altsetting = &trace_iface,
}};

static const struct usb_config_descriptor config = {
	.bLength = USB_DT_CONFIGURATION_SIZE,
	.bDescriptorType = USB_DT_CONFIGURATION,
	.wTotalLength = 0,
	.bNumInterfaces = 6,
	.bConfigurationValue = 1,
	.iConfiguration = 0,
	.bmAttributes = 0x80,
	.bMaxPower = 0x32,

	.interface = ifaces,
};

char serial_no[9];

static const char *usb_strings[] = {
	"x",
	"Black Sphere Technologies",
	"Black Magic Probe",
	serial_no,
	"Black Magic GDB Server",
	"Black Magic UART Port",
	"Black Magic Firmware Upgrade",
	"Black Magic Trace Capture",
};

static void dfu_detach_complete(struct usb_setup_data *req)
{
	(void)req;

	/* Disconnect USB cable */
	gpio_set_mode(USB_PU_PORT, GPIO_MODE_INPUT, 0, USB_PU_PIN);

	/* Assert boot-request pin */
	gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_2_MHZ, 
			GPIO_CNF_OUTPUT_PUSHPULL, GPIO12);
	gpio_clear(GPIOB, GPIO12);

	/* Reset core to enter bootloader */
	scb_reset_core();
}

static int cdcacm_control_request(struct usb_setup_data *req, uint8_t **buf,
		uint16_t *len, void (**complete)(struct usb_setup_data *req))
{
	(void)complete;
	(void)buf;
	(void)len;

	switch(req->bRequest) {
	case USB_CDC_REQ_SET_CONTROL_LINE_STATE: 
		/* Ignore if not for GDB interface */
		if(req->wIndex != 0)
			return 1;

		cdcacm_gdb_dtr = req->wValue & 1;

		return 1;
	case USB_CDC_REQ_SET_LINE_CODING: 
		if(*len < sizeof(struct usb_cdc_line_coding)) 
			return 0;

		switch(req->wIndex) {
		case 2:
			usbuart_set_line_coding((struct usb_cdc_line_coding*)*buf);
		case 0:
			return 1; /* Ignore on GDB Port */
		default:
			return 0;
		}
	case DFU_GETSTATUS: 
		if(req->wIndex == DFU_IF_NO) {
			(*buf)[0] = DFU_STATUS_OK;
			(*buf)[1] = 0;
			(*buf)[2] = 0;
			(*buf)[3] = 0;
			(*buf)[4] = STATE_APP_IDLE;
			(*buf)[5] = 0;	/* iString not used here */
			*len = 6;

			return 1;
		}
	case DFU_DETACH:
		if(req->wIndex == DFU_IF_NO) {
			*complete = dfu_detach_complete;
			return 1;
		}
		return 0;
	}
	return 0;
}

int cdcacm_get_config(void)
{
	return configured;
}

int cdcacm_get_dtr(void)
{
	return cdcacm_gdb_dtr;
}

static void cdcacm_set_config(u16 wValue)
{
	configured = wValue;

	/* GDB interface */
	usbd_ep_setup(0x01, USB_ENDPOINT_ATTR_BULK, CDCACM_PACKET_SIZE, NULL);
	usbd_ep_setup(0x81, USB_ENDPOINT_ATTR_BULK, CDCACM_PACKET_SIZE, NULL);
	usbd_ep_setup(0x82, USB_ENDPOINT_ATTR_INTERRUPT, 16, NULL);

	/* Serial interface */
	usbd_ep_setup(0x03, USB_ENDPOINT_ATTR_BULK, CDCACM_PACKET_SIZE, usbuart_usb_out_cb);
	usbd_ep_setup(0x83, USB_ENDPOINT_ATTR_BULK, CDCACM_PACKET_SIZE, usbuart_usb_in_cb);
	usbd_ep_setup(0x84, USB_ENDPOINT_ATTR_INTERRUPT, 16, NULL);

	/* Trace interface */
	usbd_ep_setup(0x85, USB_ENDPOINT_ATTR_BULK, 64, trace_buf_drain);

	usbd_register_control_callback(
			USB_REQ_TYPE_CLASS | USB_REQ_TYPE_INTERFACE, 
			USB_REQ_TYPE_TYPE | USB_REQ_TYPE_RECIPIENT,
			cdcacm_control_request);

	/* Notify the host that DCD is asserted.  
	 * Allows the use of /dev/tty* devices on *BSD/MacOS
	 */
	char buf[10];
	struct usb_cdc_notification *notif = (void*)buf;
	/* We echo signals back to host as notification */
	notif->bmRequestType = 0xA1;
	notif->bNotification = USB_CDC_NOTIFY_SERIAL_STATE;
	notif->wValue = 0;
	notif->wIndex = 0;
	notif->wLength = 2;
	buf[8] = 3; /* DCD | DSR */
	buf[9] = 0;
	usbd_ep_write_packet(0x82, buf, 10);
	notif->wIndex = 2;
	usbd_ep_write_packet(0x84, buf, 10);
}

/* We need a special large control buffer for this device: */
uint8_t usbd_control_buffer[256];

void cdcacm_init(void)
{
	void exti15_10_isr(void);

	get_dev_unique_id(serial_no);

	usbd_init(&stm32f103_usb_driver, &dev, &config, usb_strings);
	usbd_set_control_buffer_size(sizeof(usbd_control_buffer));
	usbd_register_set_config_callback(cdcacm_set_config);

	nvic_set_priority(NVIC_USB_LP_CAN_RX0_IRQ, IRQ_PRI_USB);
	nvic_enable_irq(NVIC_USB_LP_CAN_RX0_IRQ);
	nvic_set_priority(USB_VBUS_IRQ, IRQ_PRI_USB_VBUS);
	nvic_enable_irq(USB_VBUS_IRQ);

	gpio_set(USB_VBUS_PORT, USB_VBUS_PIN);
	gpio_set(USB_PU_PORT, USB_PU_PIN);

	gpio_set_mode(USB_VBUS_PORT, GPIO_MODE_INPUT, 
			GPIO_CNF_INPUT_PULL_UPDOWN, USB_VBUS_PIN);

	/* Configure EXTI for USB VBUS monitor */
	exti_select_source(USB_VBUS_PIN, USB_VBUS_PORT);
	exti_set_trigger(USB_VBUS_PIN, EXTI_TRIGGER_BOTH);
	exti_enable_request(USB_VBUS_PIN);

	exti15_10_isr();
}

void usb_lp_can_rx0_isr(void)
{
	usbd_poll();
}

void exti15_10_isr(void)
{
	if (gpio_get(USB_VBUS_PORT, USB_VBUS_PIN)) {
		/* Drive pull-up high if VBUS connected */
		gpio_set_mode(USB_PU_PORT, GPIO_MODE_OUTPUT_10_MHZ, 
				GPIO_CNF_OUTPUT_PUSHPULL, USB_PU_PIN);
	} else {
		/* Allow pull-up to float if VBUS disconnected */
		gpio_set_mode(USB_PU_PORT, GPIO_MODE_INPUT, 
				GPIO_CNF_INPUT_FLOAT, USB_PU_PIN);
	}

	exti_reset_request(USB_VBUS_PIN);
}

static char *get_dev_unique_id(char *s) 
{
        volatile uint32_t *unique_id_p = (volatile uint32_t *)0x1FFFF7E8;
	uint32_t unique_id = *unique_id_p +
			*(unique_id_p + 1) +
			*(unique_id_p + 2);
        int i;

        /* Fetch serial number from chip's unique ID */
        for(i = 0; i < 8; i++) {
                s[7-i] = ((unique_id >> (4*i)) & 0xF) + '0';
        }
        for(i = 0; i < 8; i++)
                if(s[i] > '9') 
                        s[i] += 'A' - '9' - 1;
	s[8] = 0;

	return s;
}

