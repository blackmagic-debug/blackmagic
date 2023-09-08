/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2024 1BitSquared <info@1bitsquared.com>
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

#include "general.h"
#include "buffer_utils.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"
#include "cortex_internal.h"
#include "riscv_debug.h"
#include "spi.h"
#include "sfdp.h"

#define RP2350_XIP_FLASH_BASE 0x10000000U
#define RP2350_XIP_CACHE_BASE 0x18000000U
#define RP2350_XIP_FLASH_SIZE 0x04000000U
#define RP2350_SRAM_BASE      0x20000000U
#define RP2350_SRAM_SIZE      0x00082000U

#define RP2350_REG_ACCESS_NORMAL              0x0000U
#define RP2350_REG_ACCESS_WRITE_XOR           0x1000U
#define RP2350_REG_ACCESS_WRITE_ATOMIC_BITSET 0x2000U
#define RP2350_REG_ACCESS_WRITE_ATOMIC_BITCLR 0x3000U

#define RP2350_XIP_CACHE_INVALIDATE_BY_SET_WAY 0x0U
#define RP2350_XIP_CACHE_CLEAN_BY_SET_WAY      0x1U
#define RP2350_XIP_CACHE_INVALIDATE_BY_ADDRESS 0x2U
#define RP2350_XIP_CACHE_CLEAN_BY_ADDRESS      0x3U
#define RP2350_XIP_CACHE_PIN_BY_SET_WAY        0x7U

#define RP2350_BOOTROM_BASE  0x00000000U
#define RP2350_BOOTROM_MAGIC (RP2350_BOOTROM_BASE + 0x0010U)

#define RP2350_BOOTROM_MAGIC_VALUE   ((uint32_t)'M' | ((uint32_t)'u' << 8U) | (2U << 16U))
#define RP2350_BOOTROM_MAGIC_MASK    0x00ffffffU
#define RP2350_BOOTROM_VERSION_SHIFT 24U

#define RP2350_RESETS_BASE       0x40020000U
#define RP2350_RESETS_RESET      (RP2350_RESETS_BASE + 0x000U)
#define RP2350_RESETS_RESET_DONE (RP2350_RESETS_BASE + 0x008U)

#define RP2350_RESETS_RESET_IO_QSPI   (1U << 7U)
#define RP2350_RESETS_RESET_PADS_QSPI (1U << 10U)

#define RP2350_GPIO_QSPI_BASE      0x40030000U
#define RP2350_GPIO_QSPI_SCLK_CTRL (RP2350_GPIO_QSPI_BASE + 0x014U)
#define RP2350_GPIO_QSPI_CS_CTRL   (RP2350_GPIO_QSPI_BASE + 0x01cU)
#define RP2350_GPIO_QSPI_SD0_CTRL  (RP2350_GPIO_QSPI_BASE + 0x024U)
#define RP2350_GPIO_QSPI_SD1_CTRL  (RP2350_GPIO_QSPI_BASE + 0x02cU)
#define RP2350_GPIO_QSPI_SD2_CTRL  (RP2350_GPIO_QSPI_BASE + 0x034U)
#define RP2350_GPIO_QSPI_SD3_CTRL  (RP2350_GPIO_QSPI_BASE + 0x03cU)

#define RP2350_GPIO_QSPI_CTRL_FUNCSEL_MASK (0x1fU << 0U)
#define RP2350_GPIO_QSPI_CTRL_FUNCSEL_NONE (0x1fU << 0U)
#define RP2350_GPIO_QSPI_CTRL_INOVER_MASK  (0x3U << 16U)
#define RP2350_GPIO_QSPI_CTRL_INOVER_NONE  (0x0U << 16U)

#define RP2350_PADS_QSPI_BASE 0x40040000U
#define RP2350_PADS_QSPI_SCLK (RP2350_PADS_QSPI_BASE + 0x004U)
#define RP2350_PADS_QSPI_SD0  (RP2350_PADS_QSPI_BASE + 0x008U)
#define RP2350_PADS_QSPI_SD1  (RP2350_PADS_QSPI_BASE + 0x00cU)
#define RP2350_PADS_QSPI_SD2  (RP2350_PADS_QSPI_BASE + 0x010U)
#define RP2350_PADS_QSPI_SD3  (RP2350_PADS_QSPI_BASE + 0x014U)
#define RP2350_PADS_QSPI_CS   (RP2350_PADS_QSPI_BASE + 0x018U)

#define RP2350_PADS_QSPI_GPIO_PULL_DOWN_ENABLE (1U << 2U)
#define RP2350_PADS_QSPI_GPIO_PULL_UP_ENABLE   (1U << 3U)
#define RP2350_PADS_QSPI_GPIO_INPUT_ENABLE     (1U << 6U)
#define RP2350_PADS_QSPI_GPIO_OUTPUT_DISABLE   (1U << 7U)
#define RP2350_PADS_QSPI_GPIO_ISOLATE          (1U << 8U)

#define RP2350_QMI_BASE       0x400d0000U
#define RP2350_QMI_DIRECT_CSR (RP2350_QMI_BASE + 0x000U)
#define RP2350_QMI_DIRECT_TX  (RP2350_QMI_BASE + 0x004U)
#define RP2350_QMI_DIRECT_RX  (RP2350_QMI_BASE + 0x008U)

#define RP2350_QMI_DIRECT_CSR_DIRECT_ENABLE  (1U << 0U)
#define RP2350_QMI_DIRECT_CSR_BUSY           (1U << 1U)
#define RP2350_QMI_DIRECT_CSR_ASSERT_CS0N    (1U << 2U)
#define RP2350_QMI_DIRECT_CSR_ASSERT_CS1N    (1U << 3U)
#define RP2350_QMI_DIRECT_CSR_AUTO_CS0N      (1U << 6U)
#define RP2350_QMI_DIRECT_CSR_AUTO_CS1N      (1U << 7U)
#define RP2350_QMI_DIRECT_CSR_TXFULL         (1U << 10U)
#define RP2350_QMI_DIRECT_CSR_TXEMPTY        (1U << 11U)
#define RP2350_QMI_DIRECT_CSR_RXEMPTY        (1U << 16U)
#define RP2350_QMI_DIRECT_CSR_RXFULL         (1U << 17U)
#define RP2350_QMI_DIRECT_CSR_CLKDIV_DEFAULT (12U << 22U)
#define RP2350_QMI_DIRECT_TX_MODE_SINGLE     (0x0 << 16U)
#define RP2350_QMI_DIRECT_TX_MODE_DUAL       (0x1 << 16U)
#define RP2350_QMI_DIRECT_TX_MODE_QUAD       (0x3 << 16U)
#define RP2350_QMI_DIRECT_TX_DATA_8BIT       (0U << 18U)
#define RP2350_QMI_DIRECT_TX_DATA_16BIT      (1U << 18U)
#define RP2350_QMI_DIRECT_TX_OUTPUT_ENABLE   (1U << 19U)
#define RP2350_QMI_DIRECT_TX_NOPUSH_RX       (1U << 20U)

#define ID_RP2350_ARM   0x0040U
#define ID_RP2350_RISCV 0x0004U

static bool rp2350_attach(target_s *target);
static bool rp2350_flash_prepare(target_s *target);
static bool rp2350_flash_resume(target_s *target);

static bool rp2350_spi_prepare(target_s *target);
static void rp2350_spi_resume(target_s *target);
static void rp2350_spi_read(target_s *target, uint16_t command, target_addr32_t address, void *buffer, size_t length);
static void rp2350_spi_write(
	target_s *target, uint16_t command, target_addr32_t address, const void *buffer, size_t length);
static void rp2350_spi_run_command(target_s *target, uint16_t command, target_addr32_t address);

static void rp2350_add_flash(target_s *const target)
{
	const bool mode_switched = rp2350_spi_prepare(target);
	/* Try to detect the flash that should be attached */
	spi_flash_id_s flash_id;
	rp2350_spi_read(target, SPI_FLASH_CMD_READ_JEDEC_ID, 0, &flash_id, sizeof(flash_id));
	/* If we read out valid Flash information, set up a region for it */
	if (flash_id.manufacturer != 0xffU && flash_id.type != 0xffU && flash_id.capacity != 0xffU) {
		const uint32_t capacity = 1U << flash_id.capacity;
		DEBUG_INFO("SPI Flash: mfr = %02x, type = %02x, capacity = %08" PRIx32 "\n", flash_id.manufacturer,
			flash_id.type, capacity);
		bmp_spi_add_flash(target, RP2350_XIP_FLASH_BASE, MIN(capacity, RP2350_XIP_FLASH_SIZE), rp2350_spi_read,
			rp2350_spi_write, rp2350_spi_run_command);
	}
	if (mode_switched)
		rp2350_spi_resume(target);
}

bool rp2350_probe(target_s *const target)
{
	/* Check that the target has the right part number */
	if (target->part_id != ID_RP2350_ARM && target->part_id != ID_RP2350_RISCV)
		return false;

	/* Check the boot ROM magic for a more positive identification of the part */
	const uint32_t boot_magic = target_mem32_read32(target, RP2350_BOOTROM_MAGIC);
	if ((boot_magic & RP2350_BOOTROM_MAGIC_MASK) != RP2350_BOOTROM_MAGIC_VALUE) {
		DEBUG_ERROR("Wrong Bootmagic %08" PRIx32 " found!\n", boot_magic);
		return false;
	}
	DEBUG_TARGET("Boot ROM version: %x\n", (uint8_t)(boot_magic >> RP2350_BOOTROM_VERSION_SHIFT));

	target->driver = "RP2350";
	target->attach = rp2350_attach;
	target->enter_flash_mode = rp2350_flash_prepare;
	target->exit_flash_mode = rp2350_flash_resume;
	return true;
}

static bool rp2350_attach(target_s *const target)
{
	/* Complete the attach to the core first */
	if (target->priv_free == cortex_priv_free) {
		if (!cortexm_attach(target))
			return false;
#ifdef CONFIG_RISCV
	} else {
		if (!riscv_attach(target))
			return false;
#endif
	}

	/* Then figure out the memory map */
	target_mem_map_free(target);
	target_add_ram32(target, RP2350_SRAM_BASE, RP2350_SRAM_SIZE);
	rp2350_add_flash(target);
	return true;
}

static bool rp2350_flash_prepare(target_s *const target)
{
	/* Configure the QMI over to direct access mode */
	rp2350_spi_prepare(target);
	return true;
}

static bool rp2350_flash_resume(target_s *const target)
{
	/* Reset the target then reconfigure the QMI back to direct access mode */
	target_reset(target);
	rp2350_spi_resume(target);
	return true;
}

static void rp2350_spi_drain_fifos(target_s *const target, uint32_t status)
{
	while (!(status & (RP2350_QMI_DIRECT_CSR_RXEMPTY | RP2350_QMI_DIRECT_CSR_TXEMPTY)) ||
		(status & RP2350_QMI_DIRECT_CSR_BUSY)) {
		/* Read out the RX FIFO if that's not empty */
		if (!(status & RP2350_QMI_DIRECT_CSR_RXEMPTY))
			target_mem32_read16(target, RP2350_QMI_DIRECT_RX);
		status = target_mem32_read32(target, RP2350_QMI_DIRECT_CSR);
	}
}

static void rp2350_flash_exit_xip(target_s *const target)
{
	/* Bring the QMI into a known state */
	target_mem32_write32(
		target, RP2350_QMI_DIRECT_CSR, RP2350_QMI_DIRECT_CSR_CLKDIV_DEFAULT | RP2350_QMI_DIRECT_CSR_AUTO_CS0N);
	/* XXX: Might need to care about the transaction timing stuff, unsure */
	/* Enter direct mode and drain FIFOs */
	target_mem32_write32(
		target, RP2350_QMI_DIRECT_CSR | RP2350_REG_ACCESS_WRITE_ATOMIC_BITSET, RP2350_QMI_DIRECT_CSR_DIRECT_ENABLE);
	rp2350_spi_drain_fifos(target, target_mem32_read32(target, RP2350_QMI_DIRECT_CSR));
	target_mem32_write32(
		target, RP2350_QMI_DIRECT_CSR | RP2350_REG_ACCESS_WRITE_ATOMIC_BITCLR, RP2350_QMI_DIRECT_CSR_DIRECT_ENABLE);

	/* Run `Exit QPI Mode` on the SPI Flash chips and put them in straight SPI mode */
	uint32_t padctrl_tmp = (target_mem32_read32(target, RP2350_PADS_QSPI_SD0) & ~RP2350_PADS_QSPI_GPIO_PULL_UP_ENABLE) |
		RP2350_PADS_QSPI_GPIO_OUTPUT_DISABLE | RP2350_PADS_QSPI_GPIO_PULL_DOWN_ENABLE;

	for (size_t cycle = 0U; cycle < 2U; ++cycle) {
		/* Set all the pads to be pulled to drive 32 bus cycles */
		target_mem32_write32(target, RP2350_PADS_QSPI_SD0, padctrl_tmp);
		target_mem32_write32(target, RP2350_PADS_QSPI_SD1, padctrl_tmp);
		target_mem32_write32(target, RP2350_PADS_QSPI_SD2, padctrl_tmp);
		target_mem32_write32(target, RP2350_PADS_QSPI_SD3, padctrl_tmp);
		/* Wait a brief delay for the pulls to take effect */
		platform_delay(10U);

		/* Now run those 32 cycles */
		target_mem32_write32(target, RP2350_QMI_DIRECT_CSR | RP2350_REG_ACCESS_WRITE_ATOMIC_BITSET,
			RP2350_QMI_DIRECT_CSR_DIRECT_ENABLE | RP2350_QMI_DIRECT_CSR_ASSERT_CS0N);
		target_mem32_write32(target, RP2350_QMI_DIRECT_TX,
			RP2350_QMI_DIRECT_TX_MODE_SINGLE | RP2350_QMI_DIRECT_TX_DATA_16BIT | RP2350_QMI_DIRECT_TX_NOPUSH_RX |
				0xffffU);
		target_mem32_write32(target, RP2350_QMI_DIRECT_TX,
			RP2350_QMI_DIRECT_TX_MODE_SINGLE | RP2350_QMI_DIRECT_TX_DATA_16BIT | RP2350_QMI_DIRECT_TX_NOPUSH_RX |
				0xffffU);
		/* Wait for the cycles to complete */
		while (target_mem32_read32(target, RP2350_QMI_DIRECT_CSR) & RP2350_QMI_DIRECT_CSR_BUSY)
			continue;
		target_mem32_write32(target, RP2350_QMI_DIRECT_CSR | RP2350_REG_ACCESS_WRITE_ATOMIC_BITCLR,
			RP2350_QMI_DIRECT_CSR_DIRECT_ENABLE | RP2350_QMI_DIRECT_CSR_ASSERT_CS0N);

		padctrl_tmp ^= RP2350_PADS_QSPI_GPIO_PULL_UP_ENABLE | RP2350_PADS_QSPI_GPIO_PULL_DOWN_ENABLE;
	}

	/* Now set up the I/O pads with SD2 and 3 pulled up in case of ~WP or ~HOLD usage */
	padctrl_tmp ^= RP2350_PADS_QSPI_GPIO_OUTPUT_DISABLE;
	target_mem32_write32(target, RP2350_PADS_QSPI_SD0, padctrl_tmp);
	target_mem32_write32(target, RP2350_PADS_QSPI_SD1, padctrl_tmp);
	padctrl_tmp ^= RP2350_PADS_QSPI_GPIO_PULL_UP_ENABLE | RP2350_PADS_QSPI_GPIO_PULL_DOWN_ENABLE;
	target_mem32_write32(target, RP2350_PADS_QSPI_SD2, padctrl_tmp);
	target_mem32_write32(target, RP2350_PADS_QSPI_SD3, padctrl_tmp);

	/* Next, run a 0xf5 QPI exit sequence */
	target_mem32_write32(target, RP2350_QMI_DIRECT_CSR | RP2350_REG_ACCESS_WRITE_ATOMIC_BITSET,
		RP2350_QMI_DIRECT_CSR_DIRECT_ENABLE | RP2350_QMI_DIRECT_CSR_ASSERT_CS0N);
	target_mem32_write32(target, RP2350_QMI_DIRECT_TX,
		RP2350_QMI_DIRECT_TX_MODE_QUAD | RP2350_QMI_DIRECT_TX_DATA_8BIT | RP2350_QMI_DIRECT_TX_OUTPUT_ENABLE |
			RP2350_QMI_DIRECT_TX_NOPUSH_RX | 0xf5U);
	/* Wait for the cycles to complete */
	while (target_mem32_read32(target, RP2350_QMI_DIRECT_CSR) & RP2350_QMI_DIRECT_CSR_BUSY)
		continue;
	target_mem32_write32(target, RP2350_QMI_DIRECT_CSR | RP2350_REG_ACCESS_WRITE_ATOMIC_BITCLR,
		RP2350_QMI_DIRECT_CSR_DIRECT_ENABLE | RP2350_QMI_DIRECT_CSR_ASSERT_CS0N);
	/* Followed by an all 1's sequence for 16 cycles */
	target_mem32_write32(target, RP2350_QMI_DIRECT_CSR | RP2350_REG_ACCESS_WRITE_ATOMIC_BITSET,
		RP2350_QMI_DIRECT_CSR_DIRECT_ENABLE | RP2350_QMI_DIRECT_CSR_ASSERT_CS0N);
	target_mem32_write32(target, RP2350_QMI_DIRECT_TX,
		RP2350_QMI_DIRECT_TX_MODE_SINGLE | RP2350_QMI_DIRECT_TX_DATA_16BIT | RP2350_QMI_DIRECT_TX_NOPUSH_RX | 0xffffU);
	/* Wait for the cycles to complete */
	while (target_mem32_read32(target, RP2350_QMI_DIRECT_CSR) & RP2350_QMI_DIRECT_CSR_BUSY)
		continue;
	target_mem32_write32(target, RP2350_QMI_DIRECT_CSR | RP2350_REG_ACCESS_WRITE_ATOMIC_BITCLR,
		RP2350_QMI_DIRECT_CSR_DIRECT_ENABLE | RP2350_QMI_DIRECT_CSR_ASSERT_CS0N);
	/* And a 0xff QPI exit sequence */
	target_mem32_write32(target, RP2350_QMI_DIRECT_CSR | RP2350_REG_ACCESS_WRITE_ATOMIC_BITSET,
		RP2350_QMI_DIRECT_CSR_DIRECT_ENABLE | RP2350_QMI_DIRECT_CSR_ASSERT_CS0N);
	target_mem32_write32(target, RP2350_QMI_DIRECT_TX,
		RP2350_QMI_DIRECT_TX_MODE_QUAD | RP2350_QMI_DIRECT_TX_DATA_8BIT | RP2350_QMI_DIRECT_TX_OUTPUT_ENABLE |
			RP2350_QMI_DIRECT_TX_NOPUSH_RX | 0xffU);
	/* Wait for the cycles to complete */
	while (target_mem32_read32(target, RP2350_QMI_DIRECT_CSR) & RP2350_QMI_DIRECT_CSR_BUSY)
		continue;
	target_mem32_write32(target, RP2350_QMI_DIRECT_CSR | RP2350_REG_ACCESS_WRITE_ATOMIC_BITCLR,
		RP2350_QMI_DIRECT_CSR_DIRECT_ENABLE | RP2350_QMI_DIRECT_CSR_ASSERT_CS0N);
}

static bool rp2350_spi_prepare(target_s *const target)
{
	/* Check if the QMI peripheral is muxed out to the pads, and if not, fix that */
	if ((target_mem32_read32(target, RP2350_GPIO_QSPI_SCLK_CTRL) & RP2350_GPIO_QSPI_CTRL_FUNCSEL_MASK) ==
		RP2350_GPIO_QSPI_CTRL_FUNCSEL_NONE) {
		DEBUG_INFO("RP2350 Flash controller in POR state, reconfiguring\n");
		/* Reset the GPIO and pads controllers */
		target_mem32_write32(target, RP2350_RESETS_RESET | RP2350_REG_ACCESS_WRITE_ATOMIC_BITSET,
			RP2350_RESETS_RESET_IO_QSPI | RP2350_RESETS_RESET_PADS_QSPI);
		target_mem32_write32(target, RP2350_RESETS_RESET | RP2350_REG_ACCESS_WRITE_ATOMIC_BITCLR,
			RP2350_RESETS_RESET_IO_QSPI | RP2350_RESETS_RESET_PADS_QSPI);
		while ((target_mem32_read32(target, RP2350_RESETS_RESET_DONE) &
				   (RP2350_RESETS_RESET_IO_QSPI | RP2350_RESETS_RESET_PADS_QSPI)) !=
			(RP2350_RESETS_RESET_IO_QSPI | RP2350_RESETS_RESET_PADS_QSPI))
			continue;

		/* Configure the GPIOs to be QMI controlled */
		target_mem32_write32(target, RP2350_GPIO_QSPI_SCLK_CTRL, 0U);
		target_mem32_write32(target, RP2350_GPIO_QSPI_CS_CTRL, 0U);
		target_mem32_write32(target, RP2350_GPIO_QSPI_SD0_CTRL, 0U);
		target_mem32_write32(target, RP2350_GPIO_QSPI_SD1_CTRL, 0U);
		target_mem32_write32(target, RP2350_GPIO_QSPI_SD2_CTRL, 0U);
		target_mem32_write32(target, RP2350_GPIO_QSPI_SD3_CTRL, 0U);

		/* Configure the pads to allow the QMI to see the outside world */
		target_mem32_write32(
			target, RP2350_PADS_QSPI_SCLK | RP2350_REG_ACCESS_WRITE_ATOMIC_BITCLR, RP2350_PADS_QSPI_GPIO_ISOLATE);
		target_mem32_write32(
			target, RP2350_PADS_QSPI_CS | RP2350_REG_ACCESS_WRITE_ATOMIC_BITCLR, RP2350_PADS_QSPI_GPIO_ISOLATE);
		target_mem32_write32(
			target, RP2350_PADS_QSPI_SD0 | RP2350_REG_ACCESS_WRITE_ATOMIC_BITCLR, RP2350_PADS_QSPI_GPIO_ISOLATE);
		target_mem32_write32(
			target, RP2350_PADS_QSPI_SD1 | RP2350_REG_ACCESS_WRITE_ATOMIC_BITCLR, RP2350_PADS_QSPI_GPIO_ISOLATE);
		target_mem32_write32(
			target, RP2350_PADS_QSPI_SD2 | RP2350_REG_ACCESS_WRITE_ATOMIC_BITCLR, RP2350_PADS_QSPI_GPIO_ISOLATE);
		target_mem32_write32(
			target, RP2350_PADS_QSPI_SD3 | RP2350_REG_ACCESS_WRITE_ATOMIC_BITCLR, RP2350_PADS_QSPI_GPIO_ISOLATE);

		rp2350_flash_exit_xip(target);
	}
	/* Check if the Flash is currently inhibited and clear that condition */
	if ((target_mem32_read32(target, RP2350_GPIO_QSPI_SD1_CTRL) & RP2350_GPIO_QSPI_CTRL_INOVER_MASK) !=
		RP2350_GPIO_QSPI_CTRL_INOVER_NONE) {
		target_mem32_write32(target, RP2350_GPIO_QSPI_SD1_CTRL, 0U);
	}

	/* Now check the current peripheral mode */
	const uint32_t state = target_mem32_read32(target, RP2350_QMI_DIRECT_CSR);
	/* If the peripheral is not yet in direct mode, turn it on and do the entry sequence for that */
	if (!(state & RP2350_QMI_DIRECT_CSR_DIRECT_ENABLE)) {
		target_mem32_write32(
			target, RP2350_QMI_DIRECT_CSR | RP2350_REG_ACCESS_WRITE_ATOMIC_BITSET, RP2350_QMI_DIRECT_CSR_DIRECT_ENABLE);
		/* Wait for the ongoing transaction to stop */
		while (target_mem32_read32(target, RP2350_QMI_DIRECT_CSR) & RP2350_QMI_DIRECT_CSR_BUSY)
			continue;
	} else {
		/* Otherwise, we were already in direct mode, so empty down the FIFOs and clear the chip selects */
		rp2350_spi_drain_fifos(target, state);
		target_mem32_write32(target, RP2350_QMI_DIRECT_CSR | RP2350_REG_ACCESS_WRITE_ATOMIC_BITCLR,
			RP2350_QMI_DIRECT_CSR_ASSERT_CS0N | RP2350_QMI_DIRECT_CSR_ASSERT_CS1N);
	}
	/* Return whether we actually had to enable direct mode */
	return !(state & RP2350_QMI_DIRECT_CSR_DIRECT_ENABLE);
}

static void rp2350_spi_resume(target_s *const target)
{
	/* Turn direct access mode back off, which will re-memory-map the SPI Flash */
	target_mem32_write32(
		target, RP2350_QMI_DIRECT_CSR | RP2350_REG_ACCESS_WRITE_ATOMIC_BITCLR, RP2350_QMI_DIRECT_CSR_DIRECT_ENABLE);
}

static void rp2350_spi_setup_xfer(target_s *const target, const uint16_t command, const target_addr32_t address)
{
	/* Start by pulling the chip select for the Flash low */
	target_mem32_write32(
		target, RP2350_QMI_DIRECT_CSR | RP2350_REG_ACCESS_WRITE_ATOMIC_BITSET, RP2350_QMI_DIRECT_CSR_ASSERT_CS0N);

	/* Set up the instruction */
	const uint8_t opcode = command & SPI_FLASH_OPCODE_MASK;
	target_mem32_write32(target, RP2350_QMI_DIRECT_TX,
		RP2350_QMI_DIRECT_TX_MODE_SINGLE | RP2350_QMI_DIRECT_TX_DATA_8BIT | RP2350_QMI_DIRECT_TX_NOPUSH_RX | opcode);

	/* If the command has an address phase, handle that */
	if ((command & SPI_FLASH_OPCODE_MODE_MASK) == SPI_FLASH_OPCODE_3B_ADDR) {
		target_mem32_write32(target, RP2350_QMI_DIRECT_TX,
			RP2350_QMI_DIRECT_TX_MODE_SINGLE | RP2350_QMI_DIRECT_TX_DATA_8BIT | RP2350_QMI_DIRECT_TX_NOPUSH_RX |
				((address >> 16U) & 0xffU));
		target_mem32_write32(target, RP2350_QMI_DIRECT_TX,
			RP2350_QMI_DIRECT_TX_MODE_SINGLE | RP2350_QMI_DIRECT_TX_DATA_8BIT | RP2350_QMI_DIRECT_TX_NOPUSH_RX |
				((address >> 8U) & 0xffU));
		target_mem32_write32(target, RP2350_QMI_DIRECT_TX,
			RP2350_QMI_DIRECT_TX_MODE_SINGLE | RP2350_QMI_DIRECT_TX_DATA_8BIT | RP2350_QMI_DIRECT_TX_NOPUSH_RX |
				(address & 0xffU));
	}

	/* Now deal with the dummy bytes phase, if any */
	const size_t dummy_bytes = (command & SPI_FLASH_DUMMY_MASK) >> SPI_FLASH_DUMMY_SHIFT;
	for (size_t i = 0; i < dummy_bytes; ++i)
		target_mem32_write32(target, RP2350_QMI_DIRECT_TX,
			RP2350_QMI_DIRECT_TX_MODE_SINGLE | RP2350_QMI_DIRECT_TX_DATA_8BIT | RP2350_QMI_DIRECT_TX_NOPUSH_RX);
}

static void rp2350_spi_read(target_s *const target, const uint16_t command, const target_addr32_t address,
	void *const buffer, const size_t length)
{
	/* Set up the transaction */
	rp2350_spi_setup_xfer(target, command, address);
	/* Now read back the data that elicited */
	uint8_t *const data = (uint8_t *)buffer;
	for (size_t i = 0; i < length; ++i) {
		/* Do a write to read */
		target_mem32_write32(
			target, RP2350_QMI_DIRECT_TX, RP2350_QMI_DIRECT_TX_MODE_SINGLE | RP2350_QMI_DIRECT_TX_DATA_8BIT);
		data[i] = target_mem32_read8(target, RP2350_QMI_DIRECT_RX);
	}
	/* Deselect the Flash to complete the transaction */
	target_mem32_write32(
		target, RP2350_QMI_DIRECT_CSR | RP2350_REG_ACCESS_WRITE_ATOMIC_BITCLR, RP2350_QMI_DIRECT_CSR_ASSERT_CS0N);
}

static void rp2350_spi_write(target_s *const target, const uint16_t command, const target_addr32_t address,
	const void *const buffer, const size_t length)
{
	/* Set up the transaction */
	rp2350_spi_setup_xfer(target, command, address);
	/* Write out the data associated with this transaction */
	const uint8_t *const data = (const uint8_t *)buffer;
	for (size_t i = 0; i < length; i += 2U) {
		target_mem32_write32(target, RP2350_QMI_DIRECT_TX,
			RP2350_QMI_DIRECT_TX_MODE_SINGLE | RP2350_QMI_DIRECT_TX_DATA_16BIT | RP2350_QMI_DIRECT_TX_NOPUSH_RX |
				read_le2(data, i));
		/* Every 8 bytes when page programming, invalidate the associated cache line */
		if (command == SPI_FLASH_CMD_PAGE_PROGRAM && (i & 7U) == 7U)
			target_mem32_write32(
				target, RP2350_XIP_CACHE_BASE + RP2350_XIP_CACHE_INVALIDATE_BY_ADDRESS + address + (i & ~7U), 0U);
	}
	/* Wait for the transaction cycles to complete */
	while (target_mem32_read32(target, RP2350_QMI_DIRECT_CSR) & RP2350_QMI_DIRECT_CSR_BUSY)
		continue;
	/* Deselect the Flash to complete the transaction */
	target_mem32_write32(
		target, RP2350_QMI_DIRECT_CSR | RP2350_REG_ACCESS_WRITE_ATOMIC_BITCLR, RP2350_QMI_DIRECT_CSR_ASSERT_CS0N);
}

static void rp2350_spi_run_command(target_s *const target, const uint16_t command, const target_addr32_t address)
{
	/* Set up the transaction */
	rp2350_spi_setup_xfer(target, command, address);
	/* Wait for the transaction cycles to complete */
	while (target_mem32_read32(target, RP2350_QMI_DIRECT_CSR) & RP2350_QMI_DIRECT_CSR_BUSY)
		continue;
	/* Deselect the Flash to execute the transaction */
	target_mem32_write32(
		target, RP2350_QMI_DIRECT_CSR | RP2350_REG_ACCESS_WRITE_ATOMIC_BITCLR, RP2350_QMI_DIRECT_CSR_ASSERT_CS0N);
}
