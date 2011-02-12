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

#include <libopencm3/stm32/nvic.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/cdc.h>
#include <libopencm3/stm32/scb.h>
#include <libopencm3/usb/dfu.h>
#include <libopencm3/stm32/usart.h>
#include <stdlib.h>

#include "platform.h"

static char *get_dev_unique_id(char *serial_no);

static int configured;

static const struct usb_device_descriptor dev = {
        .bLength = USB_DT_DEVICE_SIZE,
        .bDescriptorType = USB_DT_DEVICE,
        .bcdUSB = 0x0200,
        .bDeviceClass = USB_CLASS_CDC,
        .bDeviceSubClass = 0,
        .bDeviceProtocol = 0,
        .bMaxPacketSize0 = 64,
        .idVendor = 0x0483,
        .idProduct = 0x5740,
        .bcdDevice = 0x0200,
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
	.wMaxPacketSize = 64,
	.bInterval = 1,
}, {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = 0x81,
	.bmAttributes = USB_ENDPOINT_ATTR_BULK,
	.wMaxPacketSize = 64,
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
		.bmCapabilities = 0,
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
	.iInterface = 0,

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

#ifdef INCLUDE_UART_INTERFACE
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
	.wMaxPacketSize = 64,
	.bInterval = 1,
}, {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = 0x83,
	.bmAttributes = USB_ENDPOINT_ATTR_BULK,
	.wMaxPacketSize = 64,
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
		.bmCapabilities = 0,
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
	.iInterface = 0,

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
#endif

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
#ifdef INCLUDE_UART_INTERFACE
	.bInterfaceNumber = 4,
#else
	.bInterfaceNumber = 2,
#endif
	.bAlternateSetting = 0,
	.bNumEndpoints = 0,
	.bInterfaceClass = 0xFE,
	.bInterfaceSubClass = 1,
	.bInterfaceProtocol = 1,
	.iInterface = 0,

	.extra = &dfu_function,
	.extralen = sizeof(dfu_function),
};

static const struct usb_interface ifaces[] = {{
	.num_altsetting = 1,
	.altsetting = gdb_comm_iface,
}, {
	.num_altsetting = 1,
	.altsetting = gdb_data_iface,
}, {
#ifdef INCLUDE_UART_INTERFACE
	.num_altsetting = 1,
	.altsetting = uart_comm_iface,
}, {
	.num_altsetting = 1,
	.altsetting = uart_data_iface,
}, {
#endif
	.num_altsetting = 1,
	.altsetting = &dfu_iface,
}};

static const struct usb_config_descriptor config = {
	.bLength = USB_DT_CONFIGURATION_SIZE,
	.bDescriptorType = USB_DT_CONFIGURATION,
	.wTotalLength = 0,
#ifdef INCLUDE_UART_INTERFACE
	.bNumInterfaces = 5,
#else
	.bNumInterfaces = 3,
#endif
	.bConfigurationValue = 1,
	.iConfiguration = 0,
	.bmAttributes = 0x80,
	.bMaxPower = 0x32,

	.interface = ifaces,
};

static char serial_no[25];

static const char *usb_strings[] = {
	"x",
	"Black Sphere Technologies",
	"Black Magic Probe",
	serial_no,
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

	switch(req->bRequest) {
	case USB_CDC_REQ_SET_CONTROL_LINE_STATE: 
		/* This Linux cdc_acm driver requires this to be implemented 
		 * even though it's optional in the CDC spec, and we don't 
		 * advertise it in the ACM functional descriptor. */
		return 1;
#ifdef INLCUDE_UART_INTERFACE
	case USB_CDC_REQ_SET_LINE_CODING: {
		if(*len < sizeof(struct usb_cdc_line_coding)) 
			return 0;

		if(req->wIndex != 2) 
			return 0;

		struct usb_cdc_line_coding *coding = *buf;
		usart_set_baudrate(USART1, coding->dwDTERate);
		usart_set_databits(USART1, coding->bDataBits);
		switch(coding->bCharFormat) {
		case 0:
			usart_set_stopbits(USART1, USART_STOPBITS_1);
			break;
		case 1:
			usart_set_stopbits(USART1, USART_STOPBITS_1_5);
			break;
		case 2:
			usart_set_stopbits(USART1, USART_STOPBITS_2);
			break;
		}
		switch(coding->bParityType) {
		case 0:
			usart_set_parity(USART1, USART_PARITY_NONE);
			break;
		case 1:
			usart_set_parity(USART1, USART_PARITY_ODD);
			break;
		case 2:
			usart_set_parity(USART1, USART_PARITY_EVEN);
			break;
		}

		return 1;
		}
#endif
	case DFU_DETACH:
		if(req->wIndex == 4) {
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

#ifdef INCLUDE_UART_INTERFACE
static void cdcacm_data_rx_cb(u8 ep)
{
	(void)ep;

	char buf[64];
	int len = usbd_ep_read_packet(0x03, buf, 64);
	for(int i = 0; i < len; i++)
		usart_send_blocking(USART1, buf[i]);
}
#endif

static void cdcacm_set_config(u16 wValue)
{
	configured = wValue;

	/* GDB interface */
	usbd_ep_setup(0x01, USB_ENDPOINT_ATTR_BULK, 64, NULL);
	usbd_ep_setup(0x81, USB_ENDPOINT_ATTR_BULK, 64, NULL);
	usbd_ep_setup(0x82, USB_ENDPOINT_ATTR_INTERRUPT, 16, NULL);

#ifdef INCLUDE_UART_INTERFACE
	/* Serial interface */
	usbd_ep_setup(0x03, USB_ENDPOINT_ATTR_BULK, 64, cdcacm_data_rx_cb);
	usbd_ep_setup(0x83, USB_ENDPOINT_ATTR_BULK, 64, NULL);
	usbd_ep_setup(0x84, USB_ENDPOINT_ATTR_INTERRUPT, 16, NULL);
#endif

	usbd_register_control_callback(
			USB_REQ_TYPE_CLASS | USB_REQ_TYPE_INTERFACE, 
			USB_REQ_TYPE_TYPE | USB_REQ_TYPE_RECIPIENT,
			cdcacm_control_request);
}

/* We need a special large control buffer for this device: */
uint8_t usbd_control_buffer[256];

void cdcacm_init(void)
{
	get_dev_unique_id(serial_no);

	usbd_init(&stm32f103_usb_driver, &dev, &config, usb_strings);
	usbd_set_control_buffer_size(sizeof(usbd_control_buffer));
	usbd_register_set_config_callback(cdcacm_set_config);

	nvic_enable_irq(NVIC_USB_LP_CAN_RX0_IRQ);

	gpio_set(USB_PU_PORT, USB_PU_PIN);
	gpio_set_mode(USB_PU_PORT, GPIO_MODE_OUTPUT_10_MHZ, 
			GPIO_CNF_OUTPUT_PUSHPULL, USB_PU_PIN);
}

void usb_lp_can_rx0_isr(void)
{
	usbd_poll();
}

static char *get_dev_unique_id(char *s) 
{
        volatile uint8_t *unique_id = (volatile uint8_t *)0x1FFFF7E8;
        int i;

        /* Fetch serial number from chip's unique ID */
        for(i = 0; i < 24; i+=2) {
                s[i] = ((*unique_id >> 4) & 0xF) + '0';
                s[i+1] = (*unique_id++ & 0xF) + '0';
        }
        for(i = 0; i < 24; i++) 
                if(s[i] > '9') 
                        s[i] += 'A' - '9' - 1;

	return s;
}

