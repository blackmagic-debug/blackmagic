/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2013 Gareth McMullin <gareth@blacksphere.co.nz>
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

#ifndef PLATFORMS_STM32_USBDFU_H
#define PLATFORMS_STM32_USBDFU_H

#include <stddef.h>
#include <libopencm3/usb/usbd.h>

/* Commands sent with wBlockNum == 0 as per ST implementation. */
#define CMD_SETADDR 0x21U
#define CMD_ERASE   0x41U
extern uintptr_t app_address;

/* dfucore.c - DFU core, common to libopencm3 platforms. */
void dfu_init(const usbd_driver *driver);
void dfu_main(void);

/* Device specific functions */
void dfu_check_and_do_sector_erase(uint32_t sector);
void dfu_flash_program_buffer(uint32_t baseaddr, const void *buf, size_t len);
uint32_t dfu_poll_timeout(uint8_t cmd, uint32_t addr, uint16_t blocknum);
void dfu_protect(bool enable);
void dfu_jump_app_if_valid(void);
void dfu_event(void);

/* Platform specific function */
void dfu_detach(void);

#endif /* PLATFORMS_STM32_USBDFU_H */
