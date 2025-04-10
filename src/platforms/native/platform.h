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

/* This file provides the platform specific declarations for the native implementation. */

#ifndef PLATFORMS_NATIVE_PLATFORM_H
#define PLATFORMS_NATIVE_PLATFORM_H

#include "gpio.h"
#include "timing.h"
#include "timing_stm32.h"

#define PLATFORM_HAS_TRACESWO
#define PLATFORM_HAS_POWER_SWITCH

#if ENABLE_DEBUG == 1
#define PLATFORM_HAS_DEBUG
extern bool debug_bmp;
#endif

#define PLATFORM_IDENT   ""
#define UPD_IFACE_STRING "@Internal Flash   /0x08000000/8*001Kg"

extern int hwversion;
/*
 * Hardware version switcher helper - when the hardware
 * version is smaller than ver it outputs opt1, otherwise opt2
 */
#define HW_SWITCH(ver, opt1, opt2) (hwversion < (ver) ? (opt1) : (opt2))

/*
 * Important pin mappings for native implementation:
 *
 * LED0     = PB2   (Yellow LED : Running)
 * LED1     = PB10  (Orange LED : Idle)
 * LED2     = PB11  (Red LED    : Error)
 *
 * TPWR     = PB0  (input)  -- analogue on mini design ADC1, CH8
 * nTRST    = PB1  (output) [blackmagic]
 * PWR_BR   = PB1  (output) [blackmagic_mini] -- supply power to the target, active low
 * TMS_DIR  = PA1  (output) [blackmagic_mini v2.1] -- choose direction of the TMS pin, input low, output high
 * nRST     = PA2  (output) -- Hardware 5 and older
 *          = PA9  (output) -- Hardware 6 and newer
 * TDI      = PA3  (output) -- Hardware 5 and older
 *          = PA7  (output) -- Hardware 6 and newer
 * TMS      = PA4  (input/output for SWDIO)
 * TCK      = PA5  (output SWCLK)
 * TCK_DIR  = PC15 (output) -- Hardware 6 and newer -- choose direction of the TCK pin.
 *                                                     external pull-up (default high, output)
 *                                                     set to LOW to allow multiple BMP to share the TCK/SWDCLK line
 *                                                     for in-circuit communication (I2C, SPI) set to low to prevent conflict
 *                                                     with other devices in the circuit
 *                                                     input low, output high
 * TDO      = PA6  (input)
 * TRACESWO = PB7  (input)  -- To allow trace decoding using USART1
 *                             Hardware 4 has a normally open jumper between TDO and TRACESWO
 *                             Hardware 5 has hardwired connection between TDO and TRACESWO
 *          = PA10 (input)  -- Hardware 6 and newer
 * nRST_SNS = PA7  (input)  -- Hardware 5 and older
 *          = PC13 (input)  -- Hardware 6 and newer
 *
 * USB_PU   = PA8  (output)
 * USB_VBUS = PB13 (input)  -- New on mini design.
 *                             Enable pull up for compatibility.
 *                             Hardware 4 and older. (we needed the pin for SPI on 5)
 * 	        = PA15 (input)  -- Hardware 5 and newer.
 * BTN1     = PB12 (input)  -- Force DFU bootloader when pressed during powerup.
 *
 * UART_TX  = PA9  (output) -- USART1 Hardware 5 and older
 *          = PA2  (output) -- USART2 Hardware 6 and newer
 * UART_RX  = PA10 (input)  -- USART1 Hardware 5 and older
 *          = PA3  (input)  -- USART2 Hardware 6 and newer
 *
 * On Board OTG Flash: -- Optional on Hardware 5 and newer, since Hardware 6 can be on the main board
 * FLASH_CS   = PB5  (output)
 * SCLK       = PB13 (output)
 * COPI       = PB15 (output)
 * CIPO       = PB14 (input)
 *
 * AUX Interface: -- Hardware 5 and newer
 * SCLK       = PB13 (output)
 * COPI       = PB15 (output)
 * CIPO       = PB14 (input)
 * FLASH_CS   = PB5  (output) -- Only Hardware 5
 * SD_CS      = PB6  (output) -- Hardware 6 and newer
 * DISPLAY_CS = PB6  (output) -- OnlyHardware 5
 *            = PB7  (output) -- Hardware 6 and newer
 * DISPLAY_DC = PB8  (output)
 * BTN1       = PB12 (input)  -- Shared with the DFU bootloader button
 * BTN2       = PB9  (input)
 * VBAT       = PA0  (input)  -- Battery voltage sense ADC2, CH0
 *
 * nRST_SNS is the nRST sense line
 */

/* Hardware definitions... */
#define JTAG_PORT    GPIOA
#define TDI_PORT     JTAG_PORT
#define TMS_DIR_PORT JTAG_PORT
#define TMS_PORT     JTAG_PORT
#define TCK_PORT     JTAG_PORT
#define TCK_DIR_PORT GPIOC
#define TDO_PORT     JTAG_PORT
#define TDI_PIN      HW_SWITCH(6, GPIO3, GPIO7)
#define TMS_DIR_PIN  GPIO1
#define TMS_PIN      GPIO4
#define TCK_PIN      GPIO5
#define TCK_DIR_PIN  GPIO15
#define TDO_PIN      GPIO6

#define SWDIO_DIR_PORT JTAG_PORT
#define SWDIO_PORT     JTAG_PORT
#define SWCLK_PORT     JTAG_PORT
#define SWDIO_DIR_PIN  TMS_DIR_PIN
#define SWDIO_PIN      TMS_PIN
#define SWCLK_PIN      TCK_PIN

#define TRST_PORT       GPIOB
#define TRST_PIN        GPIO1
#define NRST_PORT       GPIOA
#define NRST_PIN        HW_SWITCH(6, GPIO2, GPIO9)
#define NRST_SENSE_PORT HW_SWITCH(6, GPIOA, GPIOC)
#define NRST_SENSE_PIN  HW_SWITCH(6, GPIO7, GPIO13)

/*
 * SWO comes in on PB7 (TIM4 CH2) before HW6, and PA10 (TIM1 CH3) after -
 * however, because of Shenanigans™ with timers and other pins, this has to
 * reuse TDO (PA6, TIM3 CH1) to not wind up clobbering timers and timer pins
 */
#define SWO_PORT GPIOA
#define SWO_PIN  GPIO6

/*
 * These are the control output pin definitions for TPWR.
 * TPWR is sensed via PB0 by sampling ADC1's channel 8.
 */
#define PWR_BR_PORT GPIOB
#define PWR_BR_PIN  GPIO1
#define TPWR_PORT   GPIOB
#define TPWR_PIN    GPIO0

/* USB pin definitions */
#define USB_PU_PORT GPIOA
#define USB_PORT    GPIOA
#define USB_PU_PIN  GPIO8
#define USB_DP_PIN  GPIO12
#define USB_DM_PIN  GPIO11

/* For HW Rev 4 and older */
#define USB_VBUS_PORT GPIOB
#define USB_VBUS_PIN  GPIO13
/* IRQ stays the same for all hw revisions. */
#define USB_VBUS_IRQ NVIC_EXTI15_10_IRQ

/* For HW Rev 5 and newer */
#define USB_VBUS5_PORT GPIOA
#define USB_VBUS5_PIN  GPIO15

#define LED_PORT      GPIOB
#define LED_PORT_UART GPIOB
#define LED_0         GPIO2
#define LED_1         GPIO10
#define LED_2         GPIO11
#define LED_UART      LED_0
#define LED_IDLE_RUN  LED_1
#define LED_ERROR     LED_2

/* OTG Flash HW Rev 5 and newer */
#define OTG_PORT GPIOB
#define OTG_CS   GPIO5
#define OTG_SCLK GPIO13
#define OTG_COPI GPIO15
#define OTG_CIPO GPIO14

/* AUX Port HW Rev 5 and newer */
#define AUX_PORT      GPIOB
#define AUX_SCLK_PORT AUX_PORT
#define AUX_COPI_PORT AUX_PORT
#define AUX_CIPO_PORT AUX_PORT
#define AUX_FCS_PORT  AUX_PORT
#define AUX_SDCS_PORT AUX_PORT
#define AUX_DCS_PORT  AUX_PORT
#define AUX_DDC_PORT  AUX_PORT
#define AUX_BTN1_PORT AUX_PORT
#define AUX_BTN2_PORT AUX_PORT
#define AUX_SCLK      GPIO13
#define AUX_COPI      GPIO15
#define AUX_CIPO      GPIO14
#define AUX_FCS       GPIO5
#define AUX_SDCS      GPIO6
#define AUX_DCS       GPIO6
#define AUX_DCS6      GPIO7
#define AUX_DDC       GPIO8
#define AUX_BTN1      GPIO12
#define AUX_BTN2      GPIO9
/* Note that VBat is on PA0, not PB. */
#define AUX_VBAT_PORT GPIOA
#define AUX_VBAT      GPIO0

/* SPI bus definitions */
#define AUX_SPI         SPI2
#define EXT_SPI         SPI1
#define EXT_SPI_CS_PORT GPIOA
#define EXT_SPI_CS      GPIO4

#define SWD_CR       GPIO_CRL(SWDIO_PORT)
#define SWD_CR_SHIFT (4U << 2U)

#define TMS_SET_MODE()                                                                       \
	do {                                                                                     \
		gpio_set(TMS_DIR_PORT, TMS_DIR_PIN);                                                 \
		gpio_set_mode(TMS_PORT, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, TMS_PIN); \
	} while (0)

#define SWDIO_MODE_FLOAT()                        \
	do {                                          \
		uint32_t cr = SWD_CR;                     \
		cr &= ~(0xfU << SWD_CR_SHIFT);            \
		cr |= (0x4U << SWD_CR_SHIFT);             \
		GPIO_BRR(SWDIO_DIR_PORT) = SWDIO_DIR_PIN; \
		SWD_CR = cr;                              \
	} while (0)

#define SWDIO_MODE_DRIVE()                         \
	do {                                           \
		uint32_t cr = SWD_CR;                      \
		cr &= ~(0xfU << SWD_CR_SHIFT);             \
		cr |= (0x1U << SWD_CR_SHIFT);              \
		GPIO_BSRR(SWDIO_DIR_PORT) = SWDIO_DIR_PIN; \
		SWD_CR = cr;                               \
	} while (0)

#define UART_PIN_SETUP()                                                                                        \
	do {                                                                                                        \
		gpio_set_mode(USBUSART_PORT, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, USBUSART_TX_PIN); \
		gpio_set_mode(USBUSART_PORT, GPIO_MODE_INPUT, GPIO_CNF_INPUT_PULL_UPDOWN, USBUSART_RX_PIN);             \
		gpio_set(USBUSART_PORT, USBUSART_RX_PIN);                                                               \
	} while (0)

#define USB_DRIVER st_usbfs_v1_usb_driver
#define USB_IRQ    NVIC_USB_LP_CAN_RX0_IRQ
#define USB_ISR(x) usb_lp_can_rx0_isr(x)
/*
 * Interrupt priorities. Low numbers are high priority.
 * TIM3 is used for traceswo capture and must be highest priority.
 */
#define IRQ_PRI_USB          (1U << 4U)
#define IRQ_PRI_USBUSART     (2U << 4U)
#define IRQ_PRI_USBUSART_DMA (2U << 4U)
#define IRQ_PRI_USB_VBUS     (14U << 4U)
#define IRQ_PRI_SWO_TIM      (0U << 4U)
#define IRQ_PRI_SWO_DMA      (0U << 4U)

#define USBUSART        HW_SWITCH(6, USBUSART1, USBUSART2)
#define USBUSART_IRQ    HW_SWITCH(6, NVIC_USART1_IRQ, NVIC_USART2_IRQ)
#define USBUSART_CLK    HW_SWITCH(6, RCC_USART1, RCC_USART2)
#define USBUSART_PORT   GPIOA
#define USBUSART_TX_PIN HW_SWITCH(6, GPIO9, GPIO2)
#define USBUSART_RX_PIN HW_SWITCH(6, GPIO10, GPIO3)

#define USBUSART_DMA_BUS     DMA1
#define USBUSART_DMA_CLK     RCC_DMA1
#define USBUSART_DMA_TX_CHAN HW_SWITCH(6, USBUSART1_DMA_TX_CHAN, USBUSART2_DMA_TX_CHAN)
#define USBUSART_DMA_RX_CHAN HW_SWITCH(6, USBUSART1_DMA_RX_CHAN, USBUSART2_DMA_RX_CHAN)
#define USBUSART_DMA_TX_IRQ  HW_SWITCH(6, USBUSART1_DMA_TX_IRQ, USBUSART2_DMA_TX_IRQ)
#define USBUSART_DMA_RX_IRQ  HW_SWITCH(6, USBUSART1_DMA_RX_IRQ, USBUSART2_DMA_RX_IRQ)

#define USBUSART1               USART1
#define USBUSART1_IRQ           NVIC_USART1_IRQ
#define USBUSART1_ISR(x)        usart1_isr(x)
#define USBUSART1_DMA_TX_CHAN   DMA_CHANNEL4
#define USBUSART1_DMA_TX_IRQ    NVIC_DMA1_CHANNEL4_IRQ
#define USBUSART1_DMA_TX_ISR(x) dma1_channel4_isr(x)
#define USBUSART1_DMA_RX_CHAN   DMA_CHANNEL5
#define USBUSART1_DMA_RX_IRQ    NVIC_DMA1_CHANNEL5_IRQ
#define USBUSART1_DMA_RX_ISR(x) usart1_rx_dma_isr(x)

#define USBUSART2               USART2
#define USBUSART2_IRQ           NVIC_USART2_IRQ
#define USBUSART2_ISR(x)        usart2_isr(x)
#define USBUSART2_DMA_TX_CHAN   DMA_CHANNEL7
#define USBUSART2_DMA_TX_IRQ    NVIC_DMA1_CHANNEL7_IRQ
#define USBUSART2_DMA_TX_ISR(x) dma1_channel7_isr(x)
#define USBUSART2_DMA_RX_CHAN   DMA_CHANNEL6
#define USBUSART2_DMA_RX_IRQ    NVIC_DMA1_CHANNEL6_IRQ
#define USBUSART2_DMA_RX_ISR(x) dma1_channel6_isr(x)

/* Use TIM3 Input 1 (from PA6/TDO) for Manchester data recovery */
#define SWO_TIM TIM3
#define SWO_TIM_CLK_EN()
#define SWO_TIM_IRQ         NVIC_TIM3_IRQ
#define SWO_TIM_ISR(x)      tim3_isr(x)
#define SWO_IC_IN           TIM_IC_IN_TI1
#define SWO_IC_RISING       TIM_IC1
#define SWO_CC_RISING       TIM3_CCR1
#define SWO_ITR_RISING      TIM_DIER_CC1IE
#define SWO_STATUS_RISING   TIM_SR_CC1IF
#define SWO_IC_FALLING      TIM_IC2
#define SWO_CC_FALLING      TIM3_CCR2
#define SWO_STATUS_FALLING  TIM_SR_CC2IF
#define SWO_STATUS_OVERFLOW (TIM_SR_CC1OF | TIM_SR_CC2OF)
#define SWO_TRIG_IN         TIM_SMCR_TS_TI1FP1

/* Use PA10 (USART1) on HW6+ for UART/NRZ/Async data recovery */
#define SWO_UART        HW_SWITCH(6, 0U, USART1)
#define SWO_UART_CLK    RCC_USART1
#define SWO_UART_DR     USART1_DR
#define SWO_UART_PORT   GPIOA
#define SWO_UART_RX_PIN GPIO10

#define SWO_DMA_BUS    DMA1
#define SWO_DMA_CLK    RCC_DMA1
#define SWO_DMA_CHAN   DMA_CHANNEL5
#define SWO_DMA_IRQ    NVIC_DMA1_CHANNEL5_IRQ
#define SWO_DMA_ISR(x) swo_dma_isr(x)

#define SET_RUN_STATE(state)   running_status = (state)
#define SET_IDLE_STATE(state)  gpio_set_val(LED_PORT, LED_IDLE_RUN, state)
#define SET_ERROR_STATE(state) gpio_set_val(LED_PORT, LED_ERROR, state)

/*
 * These are bounce declarations for the ISR handlers competing for dma1_channel5_isr().
 * The actual handler is defined in platform.c, the USART1 RX handler in aux_serial.c,
 * and the SWO DMA handler in swo_uart.c.
 */
void usart1_rx_dma_isr(void);
void swo_dma_isr(void);

/* Frequency constants (in Hz) for the bitbanging routines */
#define BITBANG_CALIBRATED_FREQS
/*
 * The 3 major JTAG bitbanging routines that get called result in these stats for
 * clock frequency being generated with the _no_delay routines:
 * jtag_proc.jtagtap_next(): 705.882kHz
 * jtag_proc.jtagtap_tms_seq(): 4.4MHz
 * jtag_proc.jtagtap_tdi_tdo_seq(): 750kHz
 * The result is an average 1.95MHz achieved.
 */
#define BITBANG_NO_DELAY_FREQ 1951961U
/*
 * On the _swd_delay routines with the delay loops inoperative, we then get:
 * jtag_proc.jtagtap_next(): 626.181kHz
 * jtag_proc.jtagtap_tms_seq(): 2.8MHz
 * jtag_proc.jtagtap_tdi_tdo_seq(): 727.27kHz
 * The result is an average 1.38MHz achieved.
 */
#define BITBANG_0_DELAY_FREQ 1384484U
/*
 * On the _swd_delay routines with the delay set to 1, we then get:
 * jtag_proc.jtagtap_next(): 521.739kHz
 * jtag_proc.jtagtap_tms_seq(): 1.378MHz
 * jtag_proc.jtagtap_tdi_tdo_seq(): 583.624kHz
 * The result is an average 827.788kHz achieved
 */

/*
 * After taking samples with the delay set to 2, 3, and 4 as well, then running
 * a linear regression on the results using the divider calculation tool, we arrive
 * at an offset of 52 for the ratio and a division factor of 30 to produce divider numbers
 */
#define BITBANG_DIVIDER_OFFSET 52U
#define BITBANG_DIVIDER_FACTOR 30U

#endif /* PLATFORMS_NATIVE_PLATFORM_H */
