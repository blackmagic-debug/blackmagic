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
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"

#define RP2350_XIP_FLASH_BASE 0x10000000U
#define RP2350_XIP_FLASH_SIZE 0x04000000U
#define RP2350_SRAM_BASE      0x20000000U
#define RP2350_SRAM_SIZE      0x00082000U

#define RP2350_BOOTROM_BASE  0x00000000U
#define RP2350_BOOTROM_MAGIC (RP2350_BOOTROM_BASE + 0x0010U)

#define RP2350_BOOTROM_MAGIC_VALUE   ((uint32_t)'M' | ((uint32_t)'u' << 8U) | (2U << 16U))
#define RP2350_BOOTROM_MAGIC_MASK    0x00ffffffU
#define RP2350_BOOTROM_VERSION_SHIFT 24U

#define RP2350_QMI_BASE       0x400d0000U
#define RP2350_QMI_DIRECT_CSR (RP2350_QMI_BASE + 0x000U)
#define RP2350_QMI_DIRECT_TX  (RP2350_QMI_BASE + 0x004U)
#define RP2350_QMI_DIRECT_RX  (RP2350_QMI_BASE + 0x008U)

#define RP2350_QMI_DIRECT_CSR_DIRECT_ENABLE (1U << 0U)
#define RP2350_QMI_DIRECT_CSR_BUSY          (1U << 1U)
#define RP2350_QMI_DIRECT_CSR_ASSERT_CS0N   (1U << 2U)
#define RP2350_QMI_DIRECT_CSR_ASSERT_CS1N   (1U << 3U)
#define RP2350_QMI_DIRECT_CSR_TXFULL        (1U << 10U)
#define RP2350_QMI_DIRECT_CSR_TXEMPTY       (1U << 11U)
#define RP2350_QMI_DIRECT_CSR_RXEMPTY       (1U << 16U)
#define RP2350_QMI_DIRECT_CSR_RXFULL        (1U << 17U)

#define ID_RP2350 0x0040U

static bool rp2350_attach(target_s *target);
static bool rp2350_spi_prepare(target_s *target);
static void rp2350_spi_resume(target_s *target);

static void rp2350_add_flash(target_s *const target)
{
	const bool mode_switched = rp2350_spi_prepare(target);
	if (mode_switched)
		rp2350_spi_resume(target);
}

bool rp2350_probe(target_s *const target)
{
	/* Check that the target has the right part number */
	if (target->part_id != ID_RP2350)
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
	return true;
}

static bool rp2350_attach(target_s *const target)
{
	/* Complete the attach to the core first */
	if (!cortexm_attach(target))
		return false;

	/* Then figure out the memory map */
	target_mem_map_free(target);
	target_add_ram32(target, RP2350_SRAM_BASE, RP2350_SRAM_SIZE);
	rp2350_add_flash(target);
	return true;
}

static bool rp2350_spi_prepare(target_s *const target)
{
	/* Start by checking the current peripheral mode */
	const uint32_t state = target_mem32_read32(target, RP2350_QMI_DIRECT_CSR);
	/* If the peripheral is not yet in direct mode, turn it on and do the entry sequence for that */
	if (!(state & RP2350_QMI_DIRECT_CSR_DIRECT_ENABLE)) {
		target_mem32_write32(target, RP2350_QMI_DIRECT_CSR, state | RP2350_QMI_DIRECT_CSR_DIRECT_ENABLE);
		/* Wait for the ongoing transaction to stop */
		while (target_mem32_read32(target, RP2350_QMI_DIRECT_CSR) & RP2350_QMI_DIRECT_CSR_BUSY)
			continue;
	} else {
		/* Otherwise, we were already in direct mode, so empty down the FIFOs and clear the chip selects */
		uint32_t status = state;
		while (!(status & (RP2350_QMI_DIRECT_CSR_RXEMPTY | RP2350_QMI_DIRECT_CSR_TXEMPTY)) ||
			(status & RP2350_QMI_DIRECT_CSR_BUSY)) {
			/* Read out the RX FIFO if that's not empty */
			if (!(status & RP2350_QMI_DIRECT_CSR_RXEMPTY))
				target_mem32_read16(target, RP2350_QMI_DIRECT_RX);
			status = target_mem32_read32(target, RP2350_QMI_DIRECT_CSR);
		}
		target_mem32_write32(target, RP2350_QMI_DIRECT_CSR,
			status & ~(RP2350_QMI_DIRECT_CSR_ASSERT_CS0N | RP2350_QMI_DIRECT_CSR_ASSERT_CS1N));
	}
	/* Return whether we actually had to enable direct mode */
	return !(state & RP2350_QMI_DIRECT_CSR_DIRECT_ENABLE);
}

static void rp2350_spi_resume(target_s *const target)
{
	/* Turn direct access mode back off, which will re-memory-map the SPI Flash */
	target_mem32_write32(target, RP2350_QMI_DIRECT_CSR,
		target_mem32_read32(target, RP2350_QMI_DIRECT_CSR) & ~RP2350_QMI_DIRECT_CSR_DIRECT_ENABLE);
}
