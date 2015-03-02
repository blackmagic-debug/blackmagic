/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2012  Black Sphere Technologies Ltd.
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
#ifndef __USBUART_H
#define __USBUART_H

#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/cdc.h>

void usbuart_init(void);

void usbuart_set_line_coding(struct usb_cdc_line_coding *coding);
void usbuart_usb_out_cb(usbd_device *dev, uint8_t ep);
void usbuart_usb_in_cb(usbd_device *dev, uint8_t ep);

#endif
