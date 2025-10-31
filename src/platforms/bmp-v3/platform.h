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

/* This file provides the platform specific declarations for the BMPv3 implementation. */

#ifndef PLATFORMS_BMP_V3_PLATFORM_H
#define PLATFORMS_BMP_V3_PLATFORM_H

#include "gpio.h"

#define PLATFORM_IDENT "v3 "

/*
 * Important pin mappings for BMPv3:
 *
 * State Indication LEDs:
 * LED0      = PB5  (Yellow LED: Running)
 * LED1      = PB4  (Orange LED: Idle)
 * LED2      = PA10 (Red LED   : Error)
 * LED3      = PA8  (Green LED : Power/Connection state)
 *
 * Host Interface & Misc:
 * USB_VBUS  = PA9
 * USB_D+    = PA12
 * USB_D-    = PA11
 * BTN1      = PA15
 *
 * Target Debug Interface:
 * TPWR_SNS  = PB2
 * TPWR_EN   = PA5
 * nRST      = PH1
 * nRST_SNS  = PH0
 * TCK       = PB13
 * TMS       = PB12
 * TDI       = PB15
 * TDO       = PB14
 * SWCLK     = PB13
 * SWDIO     = PB12
 * SWO       = PA1
 * TCKTDI_EN = PC15
 * TMS_DIR   = PC14
 * SWCLK_DIR = PC15
 * SWDIO_DIR = PC14
 *
 * Target Comms Interface:
 * TXD1      = PA2
 * RXD1      = PA3
 * TXD2      = PB6
 * RXD2      = PB7
 * UART2_DIR = PC13
 *
 * On-Board Flash:
 * FLASH_nCS = PA4
 * FLASH_CLK = PB10
 * FLASH_IO0 = PB1
 * FLASH_IO1 = PB0
 * FLASH_IO2 = PA7
 * FLASH_IO3 = PA6
 *
 * AUX Interface:
 * AUX_SCL   = PB8
 * AUX_SDA   = PB9
 */

/* Hardware definitions... */

#endif /* PLATFORMS_BMP_V3_PLATFORM_H */
