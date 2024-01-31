/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2023-2024 1BitSquared <info@1bitsquared.com>
 * Written by Maciej 'vesim' Kuliński <vesim809@pm.me>
 * Modified by Rachel Mant <git@dragonmux.network>
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
#include <stdint.h>
#include <stddef.h>

// spi.h
#define SPI_FLASH_OPCODE_MASK      0x00ffU
#define SPI_FLASH_DUMMY_MASK       0x0700U
#define SPI_FLASH_DUMMY_SHIFT      8U
#define SPI_FLASH_OPCODE_MODE_MASK 0x0800U
#define SPI_FLASH_OPCODE_3B_ADDR   (1U << 11U)

/* SSI peripheral registers */
typedef struct ssi {
	volatile uint32_t ctrl0;
	volatile uint32_t ctrl1;
	/* These next registers aren't actually reserved, we just don't care about them */
	uint32_t reserved1[8U];
	const volatile uint32_t status;
	/* Not all of these are reserved, but we don't care about them */
	uint32_t reserved2[13U];
	volatile uint32_t data;
	/* We don't bother defining the rest of the registers as they're not important to us */
} ssi_s;

/* QSPI GPIO bank peripheral registers */
typedef struct gpio_qspi {
	const volatile uint32_t sclk_status;
	volatile uint32_t sclk_ctrl;
	const volatile uint32_t cs_status;
	volatile uint32_t cs_ctrl;
	/* We don't bother defining the rest of the registers as they're not important to us */
} gpio_qspi_s;

/* SSI peripheral base address and register bit definitions */
#define RP_SSI_BASE_ADDR            0x18000000U
#define RP_SSI_STATUS_FX_FIFO_EMPTY (1U << 2U)

/* QSPI GPIO peripheral base address and register bit definitions */
#define RP_GPIO_QSPI_BASE_ADDR     0x40018000U
#define RP_GPIO_QSPI_CS_DRIVE_MASK 0x00000300U
#define RP_GPIO_QSPI_CS_DRIVE_LOW  (2U << 8U)
#define RP_GPIO_QSPI_CS_DRIVE_HIGH (3U << 8U)

/* Define the controllers so we can access them */
static ssi_s *const ssi = (ssi_s *)RP_SSI_BASE_ADDR;
static gpio_qspi_s *const gpio_qspi = (gpio_qspi_s *)RP_GPIO_QSPI_BASE_ADDR;

#define SPI_CHIP_SELECT(state) gpio_qspi->cs_ctrl = (gpio_qspi->cs_ctrl & ~RP_GPIO_QSPI_CS_DRIVE_MASK) | state

static uint8_t rp_spi_xfer_data(const uint8_t data);

void __attribute__((naked, used, section(".entry")))
rp_flash_write_stub(const uint16_t command, const uint32_t dest, const uint8_t *const src, const uint32_t length)
{
	/* Create a stack for our own sanity */
	__asm__("ldr r4, =#0x20042000\n"
			"mov sp, r4\n"
			"bl rp_flash_write\n"
			"bkpt #1\n");
}

static uint8_t rp_spi_xfer_data(const uint8_t data)
{
	/* Initiate the 8-bit transfer */
	ssi->data = data;
	/* Wait for it to complete */
	while (!(ssi->status & RP_SSI_STATUS_FX_FIFO_EMPTY))
		continue;
	/* Then read the result so the FIFO doesn't wind up filled */
	return ssi->data & 0xffU;
}

static __attribute__((used, section(".entry"))) void rp_flash_write(
	const uint16_t command, const uint32_t address, const uint8_t *const src, const uint32_t length)
{
	/* Configure the controller, and select the Flash */
	ssi->ctrl1 = length;
	SPI_CHIP_SELECT(RP_GPIO_QSPI_CS_DRIVE_LOW);

	rp_spi_xfer_data((uint8_t)(command & SPI_FLASH_OPCODE_MASK));

	if ((command & SPI_FLASH_OPCODE_MODE_MASK) == SPI_FLASH_OPCODE_3B_ADDR) {
		/* For each byte sent here, we have to manually clean up from the controller with a read */
		rp_spi_xfer_data((address >> 16U) & 0xffU);
		rp_spi_xfer_data((address >> 8U) & 0xffU);
		rp_spi_xfer_data(address & 0xffU);
	}

	const size_t inter_length = (command & SPI_FLASH_DUMMY_MASK) >> SPI_FLASH_DUMMY_SHIFT;
	for (size_t i = 0; i < inter_length; ++i)
		/* For each byte sent here, we have to manually clean up from the controller with a read */
		rp_spi_xfer_data(0);

	/* Now write out the data requested */
	for (size_t i = 0; i < length; ++i)
		/* Do a write to read*/
		rp_spi_xfer_data(src[i]);

	/* Deselect the Flash to complete the transaction */
	SPI_CHIP_SELECT(RP_GPIO_QSPI_CS_DRIVE_HIGH);
}
