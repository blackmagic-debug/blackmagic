/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 * Copyright (C) 2022  1bitsquared - Rachel Mant <git@dragonmux.network>
 * Modified by LAK132
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
#include "riscv_debug.h"
#include "icepick.h"
#include "jtag_devs.h"

const jtag_dev_descr_s dev_descr[] = {
	{
		.idcode = 0x0ba00477U,
		.idmask = 0x0fff0fffU,
#if ENABLE_DEBUG == 1
		.descr = "ADIv5 JTAG-DP port.",
#endif
		.handler = adiv5_jtag_dp_handler,
	},
#if ENABLE_DEBUG == 1
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
#endif
#ifdef CONFIG_XILINX
	{
		.idcode = 0x1396d093U,
		.idmask = 0xffffffffU,
#if ENABLE_DEBUG == 1
		.descr = "Xilinx XCVU440.",
#endif
		.ir_quirks =
			{
				.ir_length = 18U,
				.ir_value = 0x11451U,
			},
	},
	{
		.idcode = 0x0484a093U,
		.idmask = 0x0fffffffU,
#if ENABLE_DEBUG == 1
		.descr = "Xilinx, 6-bit IR.",
#endif
		.ir_quirks =
			{
				.ir_length = 6U,
				.ir_value = 1U,
			},
	},
	{
		.idcode = 0x04af2093U,
		.idmask = 0x0fffffffU,
#if ENABLE_DEBUG == 1
		.descr = "Xilinx 12-bit IR.",
#endif
		.ir_quirks =
			{
				.ir_length = 12U,
				.ir_value = 1U,
			},
	},
	{
		.idcode = 0x036d9093U,
		.idmask = 0x0fffffffU,
#if ENABLE_DEBUG == 1
		.descr = "Xilinx 22-bit IR.",
#endif
		.ir_quirks =
			{
				.ir_length = 22U,
				.ir_value = 1U,
			},
	},
	{
		.idcode = 0x036db093U,
		.idmask = 0x0fffffffU,
#if ENABLE_DEBUG == 1
		.descr = "Xilinx 38-bit IR.",
#endif
		.ir_quirks =
			{
				.ir_length = 38U,
				.ir_value = 1U,
			},
	},
	{
		.idcode = 0x0396d093U,
		.idmask = 0x0fffdfffU,
#if ENABLE_DEBUG == 1
		.descr = "Xilinx 18-bit IR.",
#endif
		.ir_quirks =
			{
				.ir_length = 18U,
				.ir_value = 1U,
			},
	},
	{
		.idcode = 0x03931093U,
		.idmask = 0x0fffdfffU,
#if ENABLE_DEBUG == 1
		.descr = "Xilinx 18-bit IR.",
#endif
		.ir_quirks =
			{
				.ir_length = 18U,
				.ir_value = 1U,
			},
	},
	{
		.idcode = 0x04b79093U,
		.idmask = 0x0fffbfffU,
#if ENABLE_DEBUG == 1
		.descr = "Xilinx 18-bit IR.",
#endif
		.ir_quirks =
			{
				.ir_length = 18U,
				.ir_value = 1U,
			},
	},
	{
		.idcode = 0x04ac0033U,
		.idmask = 0x0fff9fffU,
#if ENABLE_DEBUG == 1
		.descr = "Xilinx 6-bit IR.",
#endif
		.ir_quirks =
			{
				.ir_length = 6U,
				.ir_value = 1U,
			},
	},
	{
		.idcode = 0x0380d093U,
		.idmask = 0x0feddfffU,
#if ENABLE_DEBUG == 1
		.descr = "Xilinx 12-bit IR.",
#endif
		.ir_quirks =
			{
				.ir_length = 12U,
				.ir_value = 1U,
			},
	},
	{
		.idcode = 0x04600093U,
		.idmask = 0x0fe53fffU,
#if ENABLE_DEBUG == 1
		.descr = "Xilinx 12-bit IR.",
#endif
		.ir_quirks =
			{
				.ir_length = 12U,
				.ir_value = 1U,
			},
	},
	{
		.idcode = 0x04b21093U,
		.idmask = 0x0ffa1fffU,
#if ENABLE_DEBUG == 1
		.descr = "Xilinx 12-bit IR.",
#endif
		.ir_quirks =
			{
				.ir_length = 12U,
				.ir_value = 1U,
			},
	},
	{
		.idcode = 0x04b01093U,
		.idmask = 0x0ffa1fffU,
#if ENABLE_DEBUG == 1
		.descr = "Xilinx 18-bit IR.",
#endif
		.ir_quirks =
			{
				.ir_length = 18U,
				.ir_value = 1U,
			},
	},
	{
		.idcode = 0x04b01093U,
		.idmask = 0x0ff81fffU,
#if ENABLE_DEBUG == 1
		.descr = "Xilinx 18-bit IR.",
#endif
		.ir_quirks =
			{
				.ir_length = 18U,
				.ir_value = 1U,
			},
	},
	{
		.idcode = 0x04b01093U,
		.idmask = 0x0ff09fffU,
#if ENABLE_DEBUG == 1
		.descr = "Xilinx 24-bit IR.",
#endif
		.ir_quirks =
			{
				.ir_length = 24U,
				.ir_value = 1U,
			},
	},
	{
		.idcode = 0x04d00093U,
		.idmask = 0x0ffc0fffU,
#if ENABLE_DEBUG == 1
		.descr = "Xilinx 21-bit OR 14-bit IR.",
#endif
		.ir_quirks =
			{
				.ir_length = 21U, // Not ideal but *shrug*
				.ir_value = 1U,
			},
	},
	{
		.idcode = 0x03681093U,
		.idmask = 0x0ff81fffU,
#if ENABLE_DEBUG == 1
		.descr = "Xilinx 24-bit IR.",
#endif
		.ir_quirks =
			{
				.ir_length = 6U,
				.ir_value = 1U,
			},
	},
	{
		.idcode = 0x04c00093U,
		.idmask = 0x0fe88fffU,
#if ENABLE_DEBUG == 1
		.descr = "Xilinx 28-bit IR.",
#endif
		.ir_quirks =
			{
				.ir_length = 28U,
				.ir_value = 1U,
			},
	},
	{
		.idcode = 0x03800093U,
		.idmask = 0x0fe80fffU,
#if ENABLE_DEBUG == 1
		.descr = "Xilinx 6-bit IR.",
#endif
		.ir_quirks =
			{
				.ir_length = 6U,
				.ir_value = 1U,
			},
	},
	{
		.idcode = 0x03636093U,
		.idmask = 0x0fffffffU,
#if ENABLE_DEBUG == 1
		.descr = "Xilinx 6-bit IR.",
#endif
		.ir_quirks =
			{
				.ir_length = 6U,
				.ir_value = 0x35U,
			},
	},
	{
		.idcode = 0x03600093U,
		.idmask = 0x0fe00fffU,
#if ENABLE_DEBUG == 1
		.descr = "Xilinx 6-bit IR.",
#endif
		.ir_quirks =
			{
				.ir_length = 6U,
				.ir_value = 0x11U,
			},
	},
	{
		.idcode = 0x04c00093U,
		.idmask = 0x0fe00fffU,
#if ENABLE_DEBUG == 1
		.descr = "Xilinx 6-bit IR.",
#endif
		.ir_quirks =
			{
				.ir_length = 6U,
				.ir_value = 1U,
			},
	},
	{
		.idcode = 0x04a00093U,
		.idmask = 0x0fe00fffU,
#if ENABLE_DEBUG == 1
		.descr = "Xilinx 6-bit IR.",
#endif
		.ir_quirks =
			{
				.ir_length = 6U,
				.ir_value = 1U,
			},
	},
	{
		.idcode = 0x04600093U,
		.idmask = 0x0fe00fffU,
#if ENABLE_DEBUG == 1
		.descr = "Xilinx 12-bit IR.",
#endif
		.ir_quirks =
			{
				.ir_length = 12U,
				.ir_value = 1U,
			},
	},
	{
		.idcode = 0x00000093U,
		.idmask = 0x00000fffU,
#if ENABLE_DEBUG == 1
		.descr = "Xilinx.",
#endif
		.ir_quirks =
			{
				.ir_length = 6U,
				.ir_value = 1U,
			},
	},
#endif
#ifdef CONFIG_RISCV
	{
		.idcode = 0x0000563dU,
		.idmask = 0x0fffffffU,
#if ENABLE_DEBUG == 1
		.descr = "RISC-V debug v0.13.",
#endif
		.handler = riscv_jtag_dtm_handler,
	},
#endif
#if defined(CONFIG_CORTEXAR) && defined(ENABLE_TI_SITARA)
	{
		.idcode = 0x0b90002fU,
		.idmask = 0x0ff00fffU,
#if ENABLE_DEBUG == 1
		.descr = "TI ICEPick.",
#endif
		.handler = icepick_router_handler,
	},
#endif
#if ENABLE_DEBUG == 1
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
#endif
	{
		.idcode = 0U,
		.idmask = 0U,
#if ENABLE_DEBUG == 1
		.descr = "Unknown",
#endif
	},
};
