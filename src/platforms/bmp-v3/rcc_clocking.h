/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2025 1BitSquared <info@1bitsquared.com>
 * Written by Rachel Mant <git@dragonmux.network>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* This file provides the RCC clocking configuration for the BMPv3 platform. */

#ifndef PLATFORMS_BMP_V3_RCC_CLOCKING_H
#define PLATFORMS_BMP_V3_RCC_CLOCKING_H

#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/pwr.h>
#include <libopencm3/stm32/flash.h>

static struct rcc_pll_config rcc_hsi_config = {
	/* Use PLL1 as our clock source, HSE unused */
	.sysclock_source = RCC_PLL1,
	.hse_frequency = 0U,
	/* Set the MSIS up to output 48MHz, which is the 3x the max in for the PLLs */
	.msis_range = RCC_MSI_RANGE_48MHZ,
	.pll1 =
		{
			/* PLL1 is then set up to consume MSIS as input */
			.pll_source = RCC_PLLCFGR_PLLSRC_MSIS,
			/* Divide 48MHz down to 16MHz as input to get the clock in range */
			.divm = 3U,
			/* Multiply up to 320 MHz */
			.divn = 20U,
			/* Make use of output R for the main system clock at 160MHz */
			.divr = 2U,
		},
	.pll2 =
		{
			.pll_source = RCC_PLLCFGR_PLLSRC_NONE,
			.divm = 0U,
		},
	.pll3 =
		{
			.pll_source = RCC_PLLCFGR_PLLSRC_NONE,
			.divm = 0U,
		},
	/* SYSCLK is 160MHz, so no need to divide it down for AHB */
	.hpre = RCC_CFGR2_HPRE_NODIV,
	/* Or for APB1 */
	.ppre1 = RCC_PPRE_NODIV,
	/* Or for APB2 */
	.ppre2 = RCC_PPRE_NODIV,
	/* APB3 is fed by SYSCLK too and may also run at 160MHz */
	.ppre3 = RCC_PPRE_NODIV,
	/* We aren't using DSI, so let that be at defaults */
	.dpre = RCC_CFGR2_DPRE_DEFAULT,
	/* Flash requires 4 wait states to access at 160MHz per RM0456 §7.3.3 Read access latency */
	.flash_waitstates = FLASH_ACR_LATENCY_4WS,
	/* 1.2V -> 160MHz f(max), user the LDO to power everything as we don't have a SMPS in this package */
	.voltage_scale = PWR_VOS_SCALE_1,
	.power_mode = PWR_SYS_LDO,
};

#endif /* PLATFORMS_BMP_V3_RCC_CLOCKING_H */
