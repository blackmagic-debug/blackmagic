/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 * Copyright (C) 2022  1bitsquared - Rachel Mant <git@dragonmux.network>
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
#include "jtag_devs.h"

const jtag_dev_descr_s dev_descr[] = {
	{
		.idcode = 0x0ba00477U,
		.idmask = 0x0fff0fffU,
#ifdef ENABLE_DEBUG
		.descr = "ADIv5 JTAG-DP port.",
#endif
		.handler = adiv5_jtag_dp_handler,
	},
#ifdef ENABLE_DEBUG
	{
		.idcode = 0x00000477U,
		.idmask = 0x00000fffU,
		.descr = "Unknown ARM.",
	},
	{
		.idcode = 0x06410041U,
		.idmask = 0x0fffffffU,
		.descr = "STM32, Medium density.",
	},
	{
		.idcode = 0x06412041U,
		.idmask = 0x0fffffffU,
		.descr = "STM32, Low density.",
	},
	{
		.idcode = 0x06414041U,
		.idmask = 0x0fffffffU,
		.descr = "STM32, High density.",
	},
	{
		.idcode = 0x06416041U,
		.idmask = 0x0fffffffU,
		.descr = "STM32L.",
	},
	{
		.idcode = 0x06418041U,
		.idmask = 0x0fffffffU,
		.descr = "STM32, Connectivity Line.",
	},
	{
		.idcode = 0x06420041U,
		.idmask = 0x0fffffffU,
		.descr = "STM32, Value Line.",
	},
	{
		.idcode = 0x06428041U,
		.idmask = 0x0fffffffU,
		.descr = "STM32, Value Line, High density.",
	},
	{
		.idcode = 0x06411041U,
		.idmask = 0xffffffffU,
		.descr = "STM32F2xx.",
	},
	{
		.idcode = 0x06422041U,
		.idmask = 0xffffffffU,
		.descr = "STM32F3xx.",
	},
	{
		.idcode = 0x06413041U,
		.idmask = 0xffffffffU,
		.descr = "STM32F4xx.",
	},
	{
		.idcode = 0x00000041U,
		.idmask = 0x00000fffU,
		.descr = "STM32 BSD.",
	},
	{
		.idcode = 0x0bb11477U,
		.idmask = 0xffffffffU,
		.descr = "NPX: LPC11C24.",
	},
	{
		.idcode = 0x4ba00477U,
		.idmask = 0xffffffffU,
		.descr = "NXP: LPC17xx family.",
	},
#endif
	{
		.idcode = 0x00000093U,
		.idmask = 0x00000fffU,
#ifdef ENABLE_DEBUG
		.descr = "Xilinx.",
#endif
		.ir_quirks =
			{
				.ir_length = 6U,
				.ir_value = 1U,
			},
	},
#ifdef ENABLE_DEBUG
	{
		.idcode = 0x0000063dU,
		.idmask = 0x00000fffU,
		.descr = "Xambala: RVDBG013.",
	},
	{
		.idcode = 0x000007a3U,
		.idmask = 0x00000fffU,
		.descr = "Gigadevice BSD.",
	},
	/* Just for fun, unsupported */
	{
		.idcode = 0x8940303fU,
		.idmask = 0xffffffffU,
		.descr = "ATMega16.",
	},
	{
		.idcode = 0x0792603fU,
		.idmask = 0xffffffffU,
		.descr = "AT91SAM9261.",
	},
	{
		.idcode = 0x20270013U,
		.idmask = 0xffffffffU,
		.descr = "i80386ex.",
	},
	{
		.idcode = 0x07b7617fU,
		.idmask = 0xffffffffU,
		.descr = "BCM2835.",
	},
	{
		.idcode = 0x4ba00477U,
		.idmask = 0xffffffffU,
		.descr = "BCM2836.",
	},
#endif
	{
		.idcode = 0U,
		.idmask = 0U,
#ifdef ENABLE_DEBUG
		.descr = "Unknown",
#endif
	},
};
