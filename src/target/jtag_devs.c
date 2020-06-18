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

#include "general.h"
#include "jtag_scan.h"
#include "adiv5.h"
#include "riscv/rvdbg.h"
#include "jtag_devs.h"

jtag_dev_descr_t dev_descr[] = {
	{.idcode = 0x0BA00477, .idmask = 0x0FFF0FFF,
		.descr = "ARM Limited: ADIv5 JTAG-DP port.",
		.handler = adiv5_jtag_dp_handler},
	{.idcode = 0x1000563D, .idmask = 0xFFFFFFFF,
	 .descr = "GigaDevice: GD32VF103CBT6.",
	 .handler = rvdbg013_jtag_dp_handler},
	{.idcode = 0x06410041, .idmask = 0x0FFFFFFF,
		.descr = "ST Microelectronics: STM32, Medium density."},
	{.idcode = 0x06412041, .idmask = 0x0FFFFFFF,
		.descr = "ST Microelectronics: STM32, Low density."},
	{.idcode = 0x06414041, .idmask = 0x0FFFFFFF,
		.descr = "ST Microelectronics: STM32, High density."},
	{.idcode = 0x06416041, .idmask = 0x0FFFFFFF,
		.descr = "ST Microelectronics: STM32L."},
	{.idcode = 0x06418041, .idmask = 0x0FFFFFFF,
		.descr = "ST Microelectronics: STM32, Connectivity Line."},
	{.idcode = 0x06420041, .idmask = 0x0FFFFFFF,
		.descr = "ST Microelectronics: STM32, Value Line."},
	{.idcode = 0x06428041, .idmask = 0x0FFFFFFF,
		.descr = "ST Microelectronics: STM32, Value Line, High density."},
	{.idcode = 0x06411041, .idmask = 0xFFFFFFFF,
		.descr = "ST Microelectronics: STM32F2xx."},
	{.idcode = 0x06422041, .idmask = 0xFFFFFFFF,
		.descr = "ST Microelectronics: STM32F3xx."},
	{.idcode = 0x06413041 , .idmask = 0xFFFFFFFF,
		.descr = "ST Microelectronics: STM32F4xx."},
	{.idcode = 0x0BB11477 , .idmask = 0xFFFFFFFF,
		.descr = "NPX: LPC11C24."},
	{.idcode = 0x4BA00477 , .idmask = 0xFFFFFFFF,
		.descr = "NXP: LPC17xx family."},
/* Just for fun, unsupported */
	{.idcode = 0x8940303F, .idmask = 0xFFFFFFFF, .descr = "ATMEL: ATMega16."},
	{.idcode = 0x0792603F, .idmask = 0xFFFFFFFF, .descr = "ATMEL: AT91SAM9261."},
	{.idcode = 0x20270013, .idmask = 0xFFFFFFFF, .descr = "Intel: i80386ex."},
	{.idcode = 0x07B7617F, .idmask = 0xFFFFFFFF, .descr = "Broadcom: BCM2835."},
	{.idcode = 0x4BA00477, .idmask = 0xFFFFFFFF, .descr = "Broadcom: BCM2836."},
	{.idcode = 0x790007A3 , .idmask = 0xFFFFFFFF, .descr = "GigaDevice: GD32VF103CBT6 (BSD)."},
	{.idcode = 0, .idmask = 0, .descr = "Unknown"},
};
