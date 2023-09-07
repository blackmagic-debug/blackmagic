/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2025 1BitSquared <info@1bitsquared.com>
 * Written by ALTracer <11005378+ALTracer@users.noreply.github.com>
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

/* This file implements the platform specific functions for the WeActStudio.BluePill-Plus implementation. */

#include "general.h"
#include "platform.h"
#include "usb.h"
#include "aux_serial.h"

#include <libopencm3/cm3/vector.h>
#include <libopencm3/cm3/scb.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/dbgmcu.h>
#include <libopencm3/stm32/spi.h>

volatile uint32_t magic[2] __attribute__((section(".noinit")));

static void platform_detach_usb(void);

#define RCC_CFGR_USBPRE_SHIFT          22
#define RCC_CFGR_USBPRE_MASK           (0x3 << RCC_CFGR_USBPRE_SHIFT)
#define RCC_CFGR_USBPRE_PLL_CLK_DIV1_5 0x0
#define RCC_CFGR_USBPRE_PLL_CLK_NODIV  0x1
#define RCC_CFGR_USBPRE_PLL_CLK_DIV2_5 0x2
#define RCC_CFGR_USBPRE_PLL_CLK_DIV2   0x3

static const struct rcc_clock_scale rcc_hse_config_hse8_120mhz = {
	/* hse8, pll to 120 */
	.pll_mul = RCC_CFGR_PLLMUL_PLL_CLK_MUL15,
	.pll_source = RCC_CFGR_PLLSRC_HSE_CLK,
	.hpre = RCC_CFGR_HPRE_NODIV,
	.ppre1 = RCC_CFGR_PPRE_DIV2,
	.ppre2 = RCC_CFGR_PPRE_NODIV,
	.adcpre = RCC_CFGR_ADCPRE_DIV8,
	.flash_waitstates = 5, /* except WSEN is 0 and WSCNT don't care */
	.prediv1 = RCC_CFGR2_PREDIV_NODIV,
	.usbpre = RCC_CFGR_USBPRE_PLL_CLK_DIV1_5, /* libopencm3_stm32f1 hack */
	.ahb_frequency = 120000000,
	.apb1_frequency = 60000000,
	.apb2_frequency = 120000000,
};

static const struct rcc_clock_scale rcc_hse_config_hse8_96mhz = {
	/* hse8, pll to 96 */
	.pll_mul = RCC_CFGR_PLLMUL_PLL_CLK_MUL12,
	.pll_source = RCC_CFGR_PLLSRC_HSE_CLK,
	.hpre = RCC_CFGR_HPRE_NODIV,
	.ppre1 = RCC_CFGR_PPRE_DIV2,
	.ppre2 = RCC_CFGR_PPRE_NODIV,
	.adcpre = RCC_CFGR_ADCPRE_DIV8,
	.flash_waitstates = 3, /* except WSEN is 0 and WSCNT don't care */
	.prediv1 = RCC_CFGR2_PREDIV_NODIV,
	.usbpre = RCC_CFGR_USBPRE_PLL_CLK_NODIV, /* libopencm3_stm32f1 hack */
	.ahb_frequency = 96000000,
	.apb1_frequency = 48000000,
	.apb2_frequency = 96000000,
};

/* Set USB CK48M prescaler on GD32F30x before enabling RCC_APB1ENR_USBEN */
static void rcc_set_usbpre_gd32f30x(uint32_t usbpre)
{
#if 1
	/* NuttX style */
	uint32_t regval = RCC_CFGR;
	regval &= ~RCC_CFGR_USBPRE_MASK;
	regval |= (usbpre << RCC_CFGR_USBPRE_SHIFT);
	RCC_CFGR = regval;
#else
	/* libopencm3 style */
	RCC_CFGR = (RCC_CFGR & ~RCC_CFGR_USBPRE_MASK) | (usbpre << RCC_CFGR_USBPRE_SHIFT);
#endif
}

/* ROM table CIDR values */
#define CIDR0_OFFSET 0xff0U /* DBGCID0 */
#define CIDR1_OFFSET 0xff4U /* DBGCID1 */
#define CIDR2_OFFSET 0xff8U /* DBGCID2 */
#define CIDR3_OFFSET 0xffcU /* DBGCID3 */

#define PIDR0_OFFSET 0xfe0U /* DBGPID0 */
#define PIDR1_OFFSET 0xfe4U /* DBGPID1 */
#define PIDR2_OFFSET 0xfe8U /* DBGPID2 */
#define PIDR3_OFFSET 0xfecU /* DBGPID3 */
#define PIDR4_OFFSET 0xfd0U /* DBGPID4 */
#define PIDR5_OFFSET 0xfd4U /* DBGPID5 (Reserved) */
#define PIDR6_OFFSET 0xfd8U /* DBGPID6 (Reserved) */
#define PIDR7_OFFSET 0xfdcU /* DBGPID7 (Reserved) */

#define ROMTABLE_BASE 0xe00ff000U

static uint64_t coresight_romtable_pidr(void)
{
	uint8_t pidr[8] = {0};
	uint64_t pid64 = 0;

	/* Pack bytes from sparse Product ID registers */
	pidr[0] = *(const uint32_t *)(ROMTABLE_BASE + PIDR0_OFFSET);
	pidr[1] = *(const uint32_t *)(ROMTABLE_BASE + PIDR1_OFFSET);
	pidr[2] = *(const uint32_t *)(ROMTABLE_BASE + PIDR2_OFFSET);
	pidr[3] = *(const uint32_t *)(ROMTABLE_BASE + PIDR3_OFFSET);
	pidr[4] = *(const uint32_t *)(ROMTABLE_BASE + PIDR4_OFFSET);

	memcpy(&pid64, pidr, 8);
	return pid64;
}

static uint32_t coresight_romtable_cidr_check(void)
{
	uint8_t cidr[4] = {0};
	uint32_t cid32 = 0;
	/* Pack bytes from sparse Component ID registers */
	cidr[0] = *(const uint32_t *)(ROMTABLE_BASE + CIDR0_OFFSET);
	cidr[1] = *(const uint32_t *)(ROMTABLE_BASE + CIDR1_OFFSET);
	cidr[2] = *(const uint32_t *)(ROMTABLE_BASE + CIDR2_OFFSET);
	cidr[3] = *(const uint32_t *)(ROMTABLE_BASE + CIDR3_OFFSET);

	memcpy(&cid32, cidr, 4);
	return cid32;
}

static void platform_detect_variant(void)
{
	/* Detect platform chip */
	const uint32_t device_id = DBGMCU_IDCODE & DBGMCU_IDCODE_DEV_ID_MASK;
	const uint32_t cpu_id = SCB_CPUID & SCB_CPUID_PARTNO;
	const uint64_t romtable_pidr = coresight_romtable_pidr();
	const uint32_t romtable_cidr = coresight_romtable_cidr_check();
	/* STM32F103CB: 0x410 (Medium density) is readable as 0x000 (errata) without debugger. So default to 72 MHz. */
	const struct rcc_clock_scale *clock = &rcc_hse_configs[RCC_CLOCK_HSE8_72MHZ];
	/*
	 * Pick one of 72/96/120 MHz PLL configs.
	 * Disable USBD clock (after bootloaders)
	 * then change USBDPSC[1:0] the CK_USBD prescaler
	 * and finally enable PLL.
	 */
	if ((device_id == 0x410 || device_id == 0x000) && cpu_id == 0xc230 && SCB_CPUID == 0x411fc231U) {
		/* STM32F103CB: 0x410 (Medium density), 0x411fc231 (Cortex-M3 r1p1) */
		if (romtable_cidr == 0xb105100dU && romtable_pidr == 0xa0410) {
			/* STM32F103: Manufacturer 020 Partno 410 (PIDR = 0x00000a0410) */
			clock = &rcc_hse_configs[RCC_CLOCK_HSE8_72MHZ];
		}
	}

	if (device_id == 0x410 && cpu_id == 0xc230 && SCB_CPUID == 0x412fc231U) {
		/* GD32F103CB: 0x410 (Medium Density), 0x412fc231 (Cortex-M3 r2p1) */
		if (romtable_cidr == 0xb105100dU && romtable_pidr == 0x07000d1f64ULL) {
			/* GD32F103: Manufacturer 751 Partno f64 (PIDR = 0x07000d1f64) */
			clock = &rcc_hse_config_hse8_96mhz;
			rcc_periph_clock_disable(RCC_USB);
			/* Set 96/2=48MHz USB divisor before enabling PLL */
			rcc_set_usbpre_gd32f30x(RCC_CFGR_USBPRE_PLL_CLK_DIV2);
		}
	}

	if (device_id == 0x414 && cpu_id == 0xc240 && SCB_CPUID == 0x410fc241) {
		/* GD32F303CC: High density, 0x410fc241 (Cortex-M4F r0p1) */
		if (romtable_cidr == 0xb105100dU && romtable_pidr == 0x07000d1050ULL) {
			/* GD32F303: Manufacturer 751 Partno 050 (PIDR = 0x07000d1050) */
			clock = &rcc_hse_config_hse8_120mhz;
			rcc_periph_clock_disable(RCC_USB);
			/* Set 120/2.5=48MHz USB divisor before enabling PLL */
			rcc_set_usbpre_gd32f30x(RCC_CFGR_USBPRE_PLL_CLK_DIV2_5);
		}
	}

	/* Enable PLL */
	rcc_clock_setup_pll(clock);
}

void platform_request_boot(void)
{
	magic[0] = BOOTMAGIC0;
	magic[1] = BOOTMAGIC1;
	SCB_VTOR = 0;
	platform_detach_usb();
	scb_reset_system();
}

void platform_init(void)
{
	/* Enable peripherals */
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_GPIOB);
	rcc_periph_clock_enable(RCC_GPIOC);
	rcc_periph_clock_enable(RCC_AFIO);
	rcc_periph_clock_enable(RCC_CRC);
	rcc_periph_clock_enable(RCC_USB);
#if SWO_ENCODING == 1 || SWO_ENCODING == 3
	/* Make sure to power up the timer used for trace */
	rcc_periph_clock_enable(SWO_TIM_CLK);
#endif
#if SWO_ENCODING == 2 || SWO_ENCODING == 3
	/* Enable relevant USART and DMA early in platform init */
	rcc_periph_clock_enable(SWO_UART_CLK);
	rcc_periph_clock_enable(SWO_DMA_CLK);
#endif

	/* Detect which chip we're running on, and set Hclk as legally fast as possible */
	platform_detect_variant();

	gpio_set_mode(TMS_PORT, GPIO_MODE_OUTPUT_10_MHZ, GPIO_CNF_INPUT_FLOAT, TMS_PIN);
	gpio_set_mode(TCK_PORT, GPIO_MODE_OUTPUT_10_MHZ, GPIO_CNF_INPUT_FLOAT, TCK_PIN);
	gpio_set_mode(TDI_PORT, GPIO_MODE_OUTPUT_10_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, TDI_PIN);
	gpio_set_mode(TDO_PORT, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_INPUT_FLOAT, TDO_PIN);
	platform_nrst_set_val(false);

	gpio_set_mode(LED_PORT, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, LED_IDLE_RUN);

	/* Relocate interrupt vector table here */
	SCB_VTOR = (uintptr_t)&vector_table;

	platform_timing_init();
	platform_detach_usb();
	blackmagic_usb_init();
	aux_serial_init();
	/* By default, do not drive the SWD bus too fast. */
	platform_max_frequency_set(2000000);
}

void platform_nrst_set_val(bool assert)
{
	if (assert) {
		gpio_set_mode(NRST_PORT, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_OPENDRAIN, NRST_PIN);
		gpio_clear(NRST_PORT, NRST_PIN);
	} else {
		gpio_set_mode(NRST_PORT, GPIO_MODE_INPUT, GPIO_CNF_INPUT_PULL_UPDOWN, NRST_PIN);
		gpio_set(NRST_PORT, NRST_PIN);
	}
}

bool platform_nrst_get_val(void)
{
	return gpio_get(NRST_PORT, NRST_PIN) == 0;
}

void platform_target_clk_output_enable(bool enable)
{
	if (enable) {
		gpio_set_mode(TCK_PORT, GPIO_MODE_OUTPUT_10_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, TCK_PIN);
		SWDIO_MODE_DRIVE();
	} else {
		SWDIO_MODE_FLOAT();
		gpio_set_mode(TCK_PORT, GPIO_MODE_OUTPUT_10_MHZ, GPIO_CNF_INPUT_FLOAT, TCK_PIN);
	}
}

const char *platform_target_voltage(void)
{
	return "Unknown";
}

bool platform_spi_init(const spi_bus_e bus)
{
	uint32_t controller = 0;
	if (bus == SPI_BUS_INTERNAL) {
		/* Set up onboard flash SPI GPIOs: PA5/6/7 as SPI1 in AF5, PA4 as nCS output push-pull */
		gpio_set_mode(OB_SPI_PORT, GPIO_MODE_OUTPUT_10_MHZ, GPIO_CNF_OUTPUT_ALTFN_PUSHPULL,
			OB_SPI_SCLK | OB_SPI_MISO | OB_SPI_MOSI);
		gpio_set_mode(OB_SPI_PORT, GPIO_MODE_OUTPUT_10_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, OB_SPI_CS);
		/* Deselect the targeted peripheral chip */
		gpio_set(OB_SPI_PORT, OB_SPI_CS);

		rcc_periph_clock_enable(RCC_SPI1);
		rcc_periph_reset_pulse(RST_SPI1);
		controller = OB_SPI;
	} else
		return false;

	/* Set up hardware SPI: master, PCLK/8, Mode 0, 8-bit MSB first */
	spi_init_master(controller, SPI_CR1_BAUDRATE_FPCLK_DIV_8, SPI_CR1_CPOL_CLK_TO_0_WHEN_IDLE,
		SPI_CR1_CPHA_CLK_TRANSITION_1, SPI_CR1_DFF_8BIT, SPI_CR1_MSBFIRST);
	spi_enable(controller);
	return true;
}

bool platform_spi_deinit(const spi_bus_e bus)
{
	if (bus == SPI_BUS_INTERNAL) {
		spi_disable(OB_SPI);
		/* Gate SPI1 APB clock */
		rcc_periph_clock_disable(RCC_SPI1);
		/* Unmap GPIOs */
		gpio_set_mode(
			OB_SPI_PORT, GPIO_MODE_INPUT, GPIO_CNF_INPUT_FLOAT, OB_SPI_SCLK | OB_SPI_MISO | OB_SPI_MOSI | OB_SPI_CS);
		return true;
	} else
		return false;
}

bool platform_spi_chip_select(const uint8_t device_select)
{
	const uint8_t device = device_select & 0x7fU;
	const bool select = !(device_select & 0x80U);
	uint32_t port;
	uint16_t pin;
	switch (device) {
	case SPI_DEVICE_INT_FLASH:
		port = OB_SPI_CS_PORT;
		pin = OB_SPI_CS;
		break;
	default:
		return false;
	}
	gpio_set_val(port, pin, select);
	return true;
}

uint8_t platform_spi_xfer(const spi_bus_e bus, const uint8_t value)
{
	switch (bus) {
	case SPI_BUS_INTERNAL:
		return spi_xfer(OB_SPI, value);
		break;
	default:
		return 0U;
	}
}

int platform_hwversion(void)
{
	return 0;
}

void platform_detach_usb(void)
{
	/*
	 * Disconnect USB after reset:
	 * Pull USB_DP low. Device will reconnect automatically
	 * when USB is set up later, as Pull-Up is hard wired
	 */
	rcc_periph_clock_enable(RCC_USB);
	rcc_periph_reset_pulse(RST_USB);

	rcc_periph_clock_enable(RCC_GPIOA);
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_OPENDRAIN, GPIO12);
	gpio_clear(GPIOA, GPIO12);
	for (volatile uint32_t counter = 10000; counter > 0; --counter)
		continue;
}
