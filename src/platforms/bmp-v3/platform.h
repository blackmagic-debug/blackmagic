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
#include "timing.h"
#include "timing_stm32.h"

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
#define TCK_PORT     GPIOB
#define TCK_PIN      GPIO13
#define TMS_PORT     GPIOB
#define TMS_PIN      GPIO12
#define TDI_PORT     GPIOB
#define TDI_PIN      GPIO15
#define TDO_PORT     GPIOB
#define TDO_PIN      GPIO14
#define TCK_DIR_PORT GPIOC
#define TCK_DIR_PIN  GPIO15
#define TMS_DIR_PORT GPIOC
#define TMS_DIR_PIN  GPIO14

#define SWCLK_PORT     GPIOB
#define SWDIO_PORT     GPIOB
#define SWCLK_PIN      GPIO13
#define SWDIO_PIN      GPIO12
#define SWCLK_DIR_PORT GPIOC
#define SWCLK_DIR_PIN  GPIO15
#define SWDIO_DIR_PORT GPIOC
#define SWDIO_DIR_PIN  GPIO14

#define EXT_SPI           SPI2
#define EXT_SPI_SCLK_PORT GPIOB
#define EXT_SPI_SCLK_PIN  GPIO13
#define EXT_SPI_CS_PORT   GPIOB
#define EXT_SPI_CS_PIN    GPIO12
#define EXT_SPI_POCI_PORT GPIOB
#define EXT_SPI_POCI_PIN  GPIO14
#define EXT_SPI_PICO_PORT GPIOB
#define EXT_SPI_PICO_PIN  GPIO15

#define NRST_PORT       GPIOH
#define NRST_PIN        GPIO1
#define NRST_SENSE_PORT GPIOH
#define NRST_SENSE_PIN  GPIO0

#define SWO_PORT GPIOA
#define SWO_PIN  GPIO1

#define TPWR_EN_PORT    GPIOA
#define TPWR_EN_PIN     GPIO5
#define TPWR_SENSE_PORT GPIOB
#define TPWR_SENSE_PIN  GPIO2

#define USB_PORT   GPIOA
#define USB_DP_PIN GPIO12
#define USB_DM_PIN GPIO11

#define USB_VBUS_PORT GPIOA
#define USB_VBUS_PIN  GPIO9

#define LED0_PORT         GPIOB
#define LED0_PIN          GPIO5
#define LED1_PORT         GPIOB
#define LED1_PIN          GPIO4
#define LED2_PORT         GPIOA
#define LED2_PIN          GPIO10
#define LED3_PORT         GPIOA
#define LED3_PIN          GPIO8
#define LED_UART_PORT     LED0_PORT
#define LED_UART_PIN      LED0_PIN
#define LED_IDLE_RUN_PORT LED1_PORT
#define LED_IDLE_RUN_PIN  LED1_PIN
#define LED_ERROR_PORT    LED2_PORT
#define LED_ERROR_PIN     LED2_PIN

#endif /* PLATFORMS_BMP_V3_PLATFORM_H */
