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

#ifndef PLATFORMS_COMMON_USB_TYPES_H
#define PLATFORMS_COMMON_USB_TYPES_H

#include <libopencm3/usb/dfu.h>

typedef struct usb_device_descriptor usb_device_descriptor_s;
typedef struct usb_config_descriptor usb_config_descriptor_s;
typedef struct usb_interface_descriptor usb_interface_descriptor_s;
typedef struct usb_endpoint_descriptor usb_endpoint_descriptor_s;
typedef struct usb_iface_assoc_descriptor usb_iface_assoc_descriptor_s;
typedef struct usb_interface usb_interface_s;

typedef enum usbd_request_return_codes usbd_request_return_codes_e;
typedef struct usb_setup_data usb_setup_data_s;

typedef struct usb_cdc_header_descriptor usb_cdc_header_descriptor_s;
typedef struct usb_cdc_call_management_descriptor usb_cdc_call_management_descriptor_s;
typedef struct usb_cdc_acm_descriptor usb_cdc_acm_descriptor_s;
typedef struct usb_cdc_union_descriptor usb_cdc_union_descriptor_s;

typedef struct usb_cdc_line_coding usb_cdc_line_coding_s;
typedef struct usb_cdc_notification usb_cdc_notification_s;

typedef struct usb_dfu_descriptor usb_dfu_descriptor_s;

typedef enum dfu_state dfu_state_e;

#endif /* PLATFORMS_COMMON_USB_TYPES_H */
