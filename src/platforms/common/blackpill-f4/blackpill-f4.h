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

/* This file provides the platform specific declarations for the blackpill-f4 implementation. */

/* References: ST doc
 * RM0383 Rev 3, 2015: https://www.st.com/resource/en/reference_manual/dm00119316-stm32f411xc-e-advanced-arm-based-32-bit-mcus-stmicroelectronics.pdf
 * DS10314 Rev 7, 2017: https://www.st.com/resource/en/datasheet/stm32f411ce.pdf
 */

#ifndef PLATFORMS_COMMON_BLACKPILL_F4_H
#define PLATFORMS_COMMON_BLACKPILL_F4_H

#include "gpio.h"
#include "timing.h"
#include "timing_stm32.h"

#if ENABLE_DEBUG == 1
#define PLATFORM_HAS_DEBUG
extern bool debug_bmp;
#endif

/*
 * If the SHIELD macro is passed to make, other macros are defined.
 * Build the code using `make PROBE_HOST=blackpill-f4x1cx SHIELD=1` to define the SHIELD macro.
 */
#ifdef SHIELD
/* Error handling for the SHIELD macro. If SHIELD has a value > 1, or < 1, an error is thrown. */
#if SHIELD < 1 || SHIELD > 1
#error "Invalid value for SHIELD. Value is smaller than 1, or larger than 1. If SHIELD is defined, the value must be 1"
#endif
/* If SHIELD is defined, the platform is able to power the vRef pin using the PWR_BR pin and the PLATFORM_HAS_POWER_SWITCH is defined. */
#ifndef PLATFORM_HAS_POWER_SWITCH
#define PLATFORM_HAS_POWER_SWITCH
#endif /* PLATFORM_HAS_POWER_SWITCH */
/* If SHIELD is defined and ALTERNATIVE_PINOUT is not defined, the ALTERNATIVE_PINOUT 1 is selected. */
#ifndef ALTERNATIVE_PINOUT
#define ALTERNATIVE_PINOUT 1
#endif /* ALTERNATIVE_PINOUT */
#endif /* SHIELD */

/*
 * Error handling for ALTERNATIVE_PINOUT
 * If ALTERNATIVE_PINOUT has a value >= 4 (undefined), or <= 0, an error is thrown.
 */
#ifdef ALTERNATIVE_PINOUT
#if ALTERNATIVE_PINOUT < 1 || ALTERNATIVE_PINOUT > 3
#error "Invalid value for ALTERNATIVE_PINOUT. Value is smaller than 1, or larger than 3. Value must be between 1 and 3"
#endif
#endif /* ALTERNATIVE_PINOUT */

/*
 * Pinout switcher helper function for alternative pinouts.
 * If ALTERNATIVE_PINOUT is passed to make, an alternative pinout is selected.
 * If ALTERNATIVE_PINOUT == 1, it outputs the argument opt1,
 * if ALTERNATIVE_PINOUT == 2, it outputs the argument opt2,
 * if ALTERNATIVE_PINOUT == 3, it outputs the argument opt3,
 * if ALTERNATIVE_PINOUT is not defined it outputs the argument opt0.
 * If the number of arguments is less than ALTERNATIVE_PINOUT+1, e.g. 2 arguments while ALTERNATIVE_PINOUT==2, an error is thrown.
 * The maximum number of input arguments is 4.
 * The 3rd and 4th arguments to this function are optional.
 */
#ifndef ALTERNATIVE_PINOUT              // if ALTERNATIVE_PINOUT is not defined
#define PINOUT_SWITCH(opt0, ...) (opt0) // select the first argument
#elif ALTERNATIVE_PINOUT == 1
#define PINOUT_SWITCH(opt0, opt1, ...) (opt1) // select the second argument
#elif ALTERNATIVE_PINOUT == 2
#define PINOUT_SWITCH(opt0, opt1, opt2, ...) (opt2) // select the third argument
#elif ALTERNATIVE_PINOUT == 3
#define PINOUT_SWITCH(opt0, opt1, opt2, opt3, ...) (opt3) // select the fourth argument
#endif                                                    /* ALTERNATIVE_PINOUT */

/*
 * Important pin mappings for STM32 implementation:
 *   * JTAG/SWD
 *     * PB6 or PB5: TDI
 *     * PB7 or PB6: TDO/TRACESWO
 *     * PB8 or PB7: TCK/SWCLK
 *     * PB9 or PB8: TMS/SWDIO
 *     * PA6 or PB3: TRST
 *     * PA5 or PB4: nRST
 *   * USB USART
 *     * PA2: USART TX
 *     * PA3: USART RX
 *   * +3V3
 *     * PA1 or PB9: power pin
 *   * Force DFU mode button:
 *     * PA0: user button KEY
 */

/* Hardware definitions... */
/* Build the code using `make PROBE_HOST=blackpill-f4x1cx ALTERNATIVE_PINOUT=1` to select the second pinout. */
#define TDI_PORT GPIOB
#define TDI_PIN  PINOUT_SWITCH(GPIO6, GPIO5)

#define TDO_PORT GPIOB
#define TDO_PIN  PINOUT_SWITCH(GPIO7, GPIO6)

#define TCK_PORT   GPIOB
#define TCK_PIN    PINOUT_SWITCH(GPIO8, GPIO7)
#define SWCLK_PORT TCK_PORT
#define SWCLK_PIN  TCK_PIN

#define TMS_PORT   GPIOB
#define TMS_PIN    PINOUT_SWITCH(GPIO9, GPIO8)
#define SWDIO_PORT TMS_PORT
#define SWDIO_PIN  TMS_PIN

#define SWDIO_MODE_REG_MULT_PB9 (1U << (9U << 1U))
#define SWDIO_MODE_REG_MULT_PB8 (1U << (8U << 1U))
/* Update when adding more alternative pinouts */
#define SWDIO_MODE_REG_MULT PINOUT_SWITCH(SWDIO_MODE_REG_MULT_PB9, SWDIO_MODE_REG_MULT_PB8)
#define SWDIO_MODE_REG      GPIO_MODER(TMS_PORT)

#define TMS_SET_MODE()                                                    \
	gpio_mode_setup(TMS_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, TMS_PIN); \
	gpio_set_output_options(TMS_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, TMS_PIN);

/* Perform SWDIO bus turnaround faster than a gpio_mode_setup() call */
#define SWDIO_MODE_FLOAT()                       \
	do {                                         \
		uint32_t mode_reg = SWDIO_MODE_REG;      \
		mode_reg &= ~(3U * SWDIO_MODE_REG_MULT); \
		SWDIO_MODE_REG = mode_reg;               \
	} while (0)

#define SWDIO_MODE_DRIVE()                      \
	do {                                        \
		uint32_t mode_reg = SWDIO_MODE_REG;     \
		mode_reg |= (1U * SWDIO_MODE_REG_MULT); \
		SWDIO_MODE_REG = mode_reg;              \
	} while (0)

#define TRST_PORT PINOUT_SWITCH(GPIOA, GPIOB)
#define TRST_PIN  PINOUT_SWITCH(GPIO6, GPIO3)

#define NRST_PORT PINOUT_SWITCH(GPIOA, GPIOB)
#define NRST_PIN  PINOUT_SWITCH(GPIO5, GPIO4)

#define PWR_BR_PORT PINOUT_SWITCH(GPIOA, GPIOB)
#define PWR_BR_PIN  PINOUT_SWITCH(GPIO1, GPIO9)

#define USER_BUTTON_KEY_PORT GPIOA
#define USER_BUTTON_KEY_PIN  GPIO0

#define LED_PORT       GPIOC
#define LED_IDLE_RUN   GPIO13
#define LED_ERROR      GPIO14
#define LED_BOOTLOADER GPIO15

#define LED_PORT_UART GPIOA
#define LED_UART      PINOUT_SWITCH(GPIO4, GPIO1)

/* SPI2: PB12/13/14/15 to external chips */
#define EXT_SPI         SPI2
#define EXT_SPI_PORT    GPIOB
#define EXT_SPI_SCLK    GPIO13
#define EXT_SPI_MISO    GPIO14
#define EXT_SPI_MOSI    GPIO15
#define EXT_SPI_CS_PORT GPIOB
#define EXT_SPI_CS      GPIO12

/* SPI1: PA4/5/6/7 to onboard w25q64 */
#define OB_SPI         SPI1
#define OB_SPI_PORT    GPIOA
#define OB_SPI_SCLK    GPIO5
#define OB_SPI_MISO    GPIO6
#define OB_SPI_MOSI    GPIO7
#define OB_SPI_CS_PORT GPIOA
#define OB_SPI_CS      GPIO4

/* USART2 with PA2 and PA3 is selected as USBUSART. Alternatively USART1 with PB6 and PB7 can be used. */
#define USBUSART               USBUSART2
#define USBUSART_CR1           USBUSART2_CR1
#define USBUSART_DR            USBUSART2_DR
#define USBUSART_IRQ           USBUSART2_IRQ
#define USBUSART_CLK           USBUSART2_CLK
#define USBUSART_PORT          USBUSART2_PORT
#define USBUSART_TX_PIN        USBUSART2_TX_PIN
#define USBUSART_RX_PIN        USBUSART2_RX_PIN
#define USBUSART_ISR(x)        USBUSART2_ISRx(x)
#define USBUSART_DMA_BUS       USBUSART2_DMA_BUS
#define USBUSART_DMA_CLK       USBUSART2_DMA_CLK
#define USBUSART_DMA_TX_CHAN   USBUSART2_DMA_TX_CHAN
#define USBUSART_DMA_TX_IRQ    USBUSART2_DMA_TX_IRQ
#define USBUSART_DMA_TX_ISR(x) USBUSART2_DMA_TX_ISRx(x)
#define USBUSART_DMA_RX_CHAN   USBUSART2_DMA_RX_CHAN
#define USBUSART_DMA_RX_IRQ    USBUSART2_DMA_RX_IRQ
#define USBUSART_DMA_RX_ISR(x) USBUSART2_DMA_RX_ISRx(x)
/* For STM32F4 DMA trigger source must be specified. Channel 4 is selected, in line with the USART selected in the DMA table. */
#define USBUSART_DMA_TRG DMA_SxCR_CHSEL_4

/*
 * To use USART1 as USBUSART, DMA2 is selected from RM0383, page 170, table 28.
 * This table defines USART1_TX as stream 7, channel 4, and USART1_RX as stream 5, channel 4.
 * Because USART1 is on APB2 with max Pclk of 100 MHz,
 * reachable baudrates are up to 12.5M with OVER8 or 6.25M with default OVER16 (per DS10314, page 31, table 6)
 */
#define USBUSART1                USART1
#define USBUSART1_CR1            USART1_CR1
#define USBUSART1_DR             USART1_DR
#define USBUSART1_IRQ            NVIC_USART1_IRQ
#define USBUSART1_CLK            RCC_USART1
#define USBUSART1_PORT           GPIOB
#define USBUSART1_TX_PIN         GPIO6
#define USBUSART1_RX_PIN         GPIO7
#define USBUSART1_ISRx(x)        usart1_isr(x)
#define USBUSART1_DMA_BUS        DMA2
#define USBUSART1_DMA_CLK        RCC_DMA2
#define USBUSART1_DMA_TX_CHAN    DMA_STREAM7
#define USBUSART1_DMA_TX_IRQ     NVIC_DMA2_STREAM7_IRQ
#define USBUSART1_DMA_TX_ISRx(x) dma2_stream7_isr(x)
#define USBUSART1_DMA_RX_CHAN    DMA_STREAM5
#define USBUSART1_DMA_RX_IRQ     NVIC_DMA2_STREAM5_IRQ
#define USBUSART1_DMA_RX_ISRx(x) dma2_stream5_isr(x)

/*
 * To use USART2 as USBUSART, DMA1 is selected from RM0383, page 170, table 27.
 * This table defines USART2_TX as stream 6, channel 4, and USART2_RX as stream 5, channel 4.
 * Because USART2 is on APB1 with max Pclk of 50 MHz,
 * reachable baudrates are up to 6.25M with OVER8 or 3.125M with default OVER16 (per DS10314, page 31, table 6)
 */
#define USBUSART2                USART2
#define USBUSART2_CR1            USART2_CR1
#define USBUSART2_DR             USART2_DR
#define USBUSART2_IRQ            NVIC_USART2_IRQ
#define USBUSART2_CLK            RCC_USART2
#define USBUSART2_PORT           GPIOA
#define USBUSART2_TX_PIN         GPIO2
#define USBUSART2_RX_PIN         GPIO3
#define USBUSART2_ISRx(x)        usart2_isr(x)
#define USBUSART2_DMA_BUS        DMA1
#define USBUSART2_DMA_CLK        RCC_DMA1
#define USBUSART2_DMA_TX_CHAN    DMA_STREAM6
#define USBUSART2_DMA_TX_IRQ     NVIC_DMA1_STREAM6_IRQ
#define USBUSART2_DMA_TX_ISRx(x) dma1_stream6_isr(x)
#define USBUSART2_DMA_RX_CHAN    DMA_STREAM5
#define USBUSART2_DMA_RX_IRQ     NVIC_DMA1_STREAM5_IRQ
#define USBUSART2_DMA_RX_ISRx(x) dma1_stream5_isr(x)

#define BOOTMAGIC0 UINT32_C(0xb007da7a)
#define BOOTMAGIC1 UINT32_C(0xbaadfeed)

#define UART_PIN_SETUP()                                                                            \
	do {                                                                                            \
		gpio_mode_setup(USBUSART_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, USBUSART_TX_PIN);              \
		gpio_set_output_options(USBUSART_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_100MHZ, USBUSART_TX_PIN); \
		gpio_set_af(USBUSART_PORT, GPIO_AF7, USBUSART_TX_PIN);                                      \
		gpio_mode_setup(USBUSART_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, USBUSART_RX_PIN);            \
		gpio_set_output_options(USBUSART_PORT, GPIO_OTYPE_OD, GPIO_OSPEED_100MHZ, USBUSART_RX_PIN); \
		gpio_set_af(USBUSART_PORT, GPIO_AF7, USBUSART_RX_PIN);                                      \
	} while (0)

#define USB_DRIVER stm32f107_usb_driver
#define USB_IRQ    NVIC_OTG_FS_IRQ
#define USB_ISR(x) otg_fs_isr(x)
/*
 * Interrupt priorities. Low numbers are high priority.
 * TIM3 is used for traceswo capture and must be highest priority.
 */
#define IRQ_PRI_USB          (1U << 4U)
#define IRQ_PRI_USBUSART     (2U << 4U)
#define IRQ_PRI_USBUSART_DMA (2U << 4U)
#define IRQ_PRI_TRACE        (0U << 4U)
#define IRQ_PRI_SWO_DMA      (0U << 4U)

#define PLATFORM_HAS_TRACESWO
#define NUM_TRACE_PACKETS 256U /* 16K buffer */
//#define TRACESWO_PROTOCOL 2U   /* 1 = RZ/Manchester, 2 = NRZ/async/uart */

#if TRACESWO_PROTOCOL == 1

/* Use TIM4 Input 2 (from PB7/TDO) or Input 1 (from PB6/TDO), AF2, trigger on Rising Edge */
#define TRACE_TIM          TIM4
#define TRACE_TIM_CLK_EN() rcc_periph_clock_enable(RCC_TIM4)
#define TRACE_IRQ          NVIC_TIM4_IRQ
#define TRACE_ISR(x)       tim4_isr(x)
#define TRACE_IC_IN        PINOUT_SWITCH(TIM_IC_IN_TI2, TIM_IC_IN_TI1)
#define TRACE_TRIG_IN      TIM_SMCR_TS_TI1FP1
#define TRACE_TIM_PIN_AF   GPIO_AF2

#elif TRACESWO_PROTOCOL == 2

/* On F411 use USART1_RX mapped on PB7 for async capture */
#define SWO_UART        USBUSART1
#define SWO_UART_CLK    USBUSART1_CLK
#define SWO_UART_DR     USBUSART1_DR
#define SWO_UART_PORT   GPIOB
#define SWO_UART_RX_PIN GPIO7
#define SWO_UART_PIN_AF GPIO_AF7

/* Bind to the same DMA Rx channel */
#define SWO_DMA_BUS     USBUSART1_DMA_BUS
#define SWO_DMA_CLK     USBUSART1_DMA_CLK
#define SWO_DMA_CHAN    USBUSART1_DMA_RX_CHAN
#define SWO_DMA_IRQ     USBUSART1_DMA_RX_IRQ
#define SWO_DMA_ISR(x)  USBUSART1_DMA_RX_ISRx(x)
#define SWO_DMA_TRG     DMA_SxCR_CHSEL_4

#endif /* TRACESWO_PROTOCOL */

#define SET_RUN_STATE(state)      \
	{                             \
		running_status = (state); \
	}
/*
 * The state of LED_IDLE_RUN is inverted, as the led used for
 * LED_IDLE_RUN (PC13) needs to be pulled low to turn the led on.
 */
#define SET_IDLE_STATE(state)                         \
	{                                                 \
		gpio_set_val(LED_PORT, LED_IDLE_RUN, !state); \
	}
#define SET_ERROR_STATE(state)                    \
	{                                             \
		gpio_set_val(LED_PORT, LED_ERROR, state); \
	}

#endif /* PLATFORMS_COMMON_BLACKPILL_F4_H */
