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
#include "stub.h"

// spi.h
#define SPI_FLASH_OPCODE_MASK      0x00ffU
#define SPI_FLASH_DUMMY_MASK       0x0700U
#define SPI_FLASH_DUMMY_SHIFT      8U
#define SPI_FLASH_OPCODE_MODE_MASK 0x0800U
#define SPI_FLASH_OPCODE_3B_ADDR   (1U << 11U)

// rp.c
#define RP_SSI_BASE_ADDR 0x18000000U
#define RP_SSI_CTRL1     *((volatile uint32_t *)(RP_SSI_BASE_ADDR + 0x04U))
#define RP_SSI_DR0       *((volatile uint32_t *)(RP_SSI_BASE_ADDR + 0x60U))
#define RP_SSI_SR        *((volatile uint32_t *)(RP_SSI_BASE_ADDR + 0x28U))
#define RP_SSI_SR_TFE    (1U << 2U)

#define RP_GPIO_QSPI_BASE_ADDR     0x40018000U
#define RP_GPIO_QSPI_CS_CTRL       *((volatile uint32_t *)(RP_GPIO_QSPI_BASE_ADDR + 0x0cU))
#define RP_GPIO_QSPI_CS_DRIVE_MASK 0x00000300U

#define RP_GPIO_QSPI_CS_DRIVE_LOW  (2U << 8U)
#define RP_GPIO_QSPI_CS_DRIVE_HIGH (3U << 8U)

#define SPI_CHIP_SELECT(state) RP_GPIO_QSPI_CS_CTRL = (RP_GPIO_QSPI_CS_CTRL & ~RP_GPIO_QSPI_CS_DRIVE_MASK) | state

static __attribute__((always_inline)) uint8_t rp_spi_xfer_data(const uint8_t data)
{
	RP_SSI_DR0 = data;
	while (!(RP_SSI_SR & RP_SSI_SR_TFE))
		asm volatile("nop");

	return RP_SSI_DR0 & 0xffU;
}

static __attribute__((always_inline)) void rp_spi_setup_xfer(
	const uint16_t command, const size_t address, const size_t length)
{
	RP_SSI_CTRL1 = length;
	SPI_CHIP_SELECT(RP_GPIO_QSPI_CS_DRIVE_LOW);

	const uint8_t opcode = command & SPI_FLASH_OPCODE_MASK;
	rp_spi_xfer_data(opcode);

	if ((command & SPI_FLASH_OPCODE_MODE_MASK) == SPI_FLASH_OPCODE_3B_ADDR) {
		rp_spi_xfer_data((address >> 16U) & 0xffU);
		rp_spi_xfer_data((address >> 8U) & 0xffU);
		rp_spi_xfer_data(address & 0xffU);
	}

	const size_t inter_length = (command & SPI_FLASH_DUMMY_MASK) >> SPI_FLASH_DUMMY_SHIFT;
	for (size_t i = 0; i < inter_length; ++i)
		rp_spi_xfer_data(0);
}

void __attribute__((naked))
rp_flash_write_stub(const uint32_t command, const uint32_t dest, const uint32_t *const src, const uint32_t length)
{
	rp_spi_setup_xfer(command, dest, length);

	for (size_t i = 0; i < length; ++i)
		rp_spi_xfer_data(src[i]);

	SPI_CHIP_SELECT(RP_GPIO_QSPI_CS_DRIVE_HIGH);

	stub_exit(0);
}
