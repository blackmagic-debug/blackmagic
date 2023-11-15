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

#ifndef PLATFORMS_COMMON_USB_H
#define PLATFORMS_COMMON_USB_H

#include <stdint.h>
#include <libopencm3/usb/usbd.h>

extern usbd_device *usbdev;
extern uint16_t usb_config;

#if defined(USB_HS)
#define CDCACM_PACKET_SIZE  512U
#define TRACE_ENDPOINT_SIZE 512U
#else
#define CDCACM_PACKET_SIZE  64U
#define TRACE_ENDPOINT_SIZE 64U
#endif

#if !defined(USB_MAX_INTERVAL)
#define USB_MAX_INTERVAL 255U
#endif

#define CDCACM_GDB_ENDPOINT  1U
#define CDCACM_UART_ENDPOINT 2U
#define TRACE_ENDPOINT       3U
/*
 * AN4879, table 6: most STM32F4 families (excluding F412, F413, F423)
 * have OTG_FS DWC2 configured with "4 bidirectional endpoints" (including EP0),
 * meaning EP1IN, EP1OUT, EP2IN, EP2OUT, EP3IN, EP3OUT are fair game,
 * but EP4 and anything above will never produce responses from the device,
 * which the host may observe as STALL. This is an issue for in-tree platforms
 * such as 96b_carbon, blackpill-f4, f4discovery, hydrabus.
 */
#define CDCACM_GDB_NOTIF_ENDPOINT  4U
#define CDCACM_UART_NOTIF_ENDPOINT 5U

#define GDB_IF_NO  0U
#define UART_IF_NO 2U
#define DFU_IF_NO  4U
#ifdef PLATFORM_HAS_TRACESWO
#define TRACE_IF_NO      5U
#define TOTAL_INTERFACES 6U
#else
#define TOTAL_INTERFACES 5U
#endif

void blackmagic_usb_init(void);

/* Returns current usb configuration, or 0 if not configured. */
uint16_t usb_get_config(void);

/* Returns true if usb config has been updated. */
bool usb_config_is_updated(void);

/* Clears usb config updated flag. */
void usb_config_clear_updated(void);

#endif /* PLATFORMS_COMMON_USB_H */
