/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2026 1BitSquared <info@1bitsquared.com>
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
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLEs
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "general.h"
#include "jtag_scan.h"
#include "jtagtap.h"
#include "buffer_utils.h"
#include "target.h"
#include "target_internal.h"
#include "lattice_isp_clock.h"

/* Instructions for the ispCLOCK that we care about */
#define IR_ADDRESS_SHIFT    0x01U
#define IR_DATA_SHIFT       0x02U
#define IR_BULK_ERASE       0x03U
#define IR_PROGRAM          0x07U
#define IR_PROGRAM_SECURITY 0x09U
#define IR_VERIFY           0x0aU
#define IR_DISCHARGE        0x14U
#define IR_PROGRAM_ENABLE   0x15U
#define IR_USERCODE         0x17U
#define IR_PROGRAM_USERCODE 0x1aU
#define IR_PROGRAM_DISABLE  0x1eU
#define IR_ADDRESS_INIT     0x21U
#define IR_ERASE_DONE       0x24U
#define IR_PROGRAM_INCR     0x27U
#define IR_READ_INCR        0x2aU
#define IR_PROGRAM_DONE     0x2fU

#define ISP_CLOCK_NVM_BASE 0x00000000U
#define ISP_CLOCK_NVM_SIZE 0x00001500U

typedef struct isp_clock {
	uint8_t dev_index;
} isp_clock_s;

static void isp_clock_mem_read(target_s *target, void *dest, target_addr64_t src, size_t len);

static bool isp_clock_enter_flash_mode(target_s *target);
static bool isp_clock_exit_flash_mode(target_s *target);
static bool isp_clock_flash_prepare(target_flash_s *flash);
static bool isp_clock_flash_done(target_flash_s *flash);
static bool isp_clock_flash_erase(target_flash_s *flash, target_addr_t dest, size_t length);
static bool isp_clock_flash_write(target_flash_s *flash, target_addr_t dest, const void *buffer, size_t length);

static void isp_clock_add_flash(target_s *const target)
{
	target_flash_s *flash = calloc(1, sizeof(*flash));
	if (!flash) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return;
	}

	/*
	 * The ispCLOCK device has a small NVM that we treat like it's Flash here.
	 * It's arranged in 42-bit blocks ("columns") with a 10-bit address - this gives
	 * a total of 5376 bytes of "E²CMOS" NVM.
	 */
	flash->start = ISP_CLOCK_NVM_BASE;
	flash->length = ISP_CLOCK_NVM_SIZE;
	flash->blocksize = ISP_CLOCK_NVM_SIZE;
	/* Have to write 4 blocks at a time to get an integer number of bytes */
	flash->writesize = 21U;
	flash->prepare = isp_clock_flash_prepare;
	flash->done = isp_clock_flash_done;
	flash->erase = isp_clock_flash_erase;
	flash->write = isp_clock_flash_write;
	flash->erased = 0xffU;
	target_add_flash(target, flash);
}

void lattice_isp_clock_handler(const uint8_t dev_index)
{
	target_s *target = target_new();
	target->driver = "Lattice";
	target->core = "ispCLOCK";

	isp_clock_s *priv = calloc(1, sizeof(*priv));
	if (!priv) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return;
	}
	priv->dev_index = dev_index;
	target->priv = priv;
	target->priv_free = free;
	target->mem_read = isp_clock_mem_read;

	target->enter_flash_mode = isp_clock_enter_flash_mode;
	target->exit_flash_mode = isp_clock_exit_flash_mode;
	isp_clock_add_flash(target);
}

static void isp_clock_mem_read(target_s *const target, void *const dest, const target_addr64_t src, const size_t length)
{
	const isp_clock_s *const priv = (isp_clock_s *)target->priv;
	const uint8_t dev_index = priv->dev_index;

	/* Set up the address to which we want to program data */
	uint8_t address[2] = {0};
	write_le2(address, 0U, src);
	jtag_dev_write_ir(dev_index, IR_ADDRESS_SHIFT);
	jtag_dev_shift_dr(dev_index, NULL, address, 10U);

	/* Setup for programming, auto-incrementing the EEPROM address */
	jtag_dev_write_ir(dev_index, IR_READ_INCR);

	/* Ensure that the destination buffer is clean */
	memset(dest, 0U, length);

	/* Byte access pointer to the destination data buffer */
	uint8_t *const buffer = (uint8_t *)dest;
	/* Loop through the data to read 42 bits at a time */
	for (size_t bit_offset = 0U; bit_offset < length * 8U; bit_offset += 42U) {
		/* Buffer for the 5.25 bytes of data needed per read instruction */
		uint8_t data[6] = {0};
		/* Read the 42 bits for this address from the device */
		jtag_dev_shift_dr(dev_index, data, NULL, 42U);

		/* Copy out the next 42 bits of data from the buffer */
		for (size_t buffer_offset = 0U; buffer_offset < 42U; ++buffer_offset) {
			/* Convert the position into a bit and byte index into the intermediary buffer */
			const size_t data_bit = buffer_offset & 7U;
			const size_t data_byte = buffer_offset >> 3U;
			/* Extract the value */
			const bool value = ((data[data_byte] >> data_bit) & 1U) != 0U;
			/* Convert the position into a bit and byte index from the destination data buffer */
			const size_t offset = bit_offset + buffer_offset;
			const size_t dest_bit = offset & 7U;
			const size_t dest_byte = offset >> 3U;

			buffer[dest_byte] |= (uint8_t)value << dest_bit;
		}
	}
}

bool isp_clock_enter_flash_mode(target_s *const target)
{
	const isp_clock_s *const priv = (isp_clock_s *)target->priv;
	jtag_dev_write_ir(priv->dev_index, IR_PROGRAM_ENABLE);
	return true;
}

bool isp_clock_exit_flash_mode(target_s *const target)
{
	const isp_clock_s *const priv = (isp_clock_s *)target->priv;
	jtag_dev_write_ir(priv->dev_index, IR_PROGRAM_DISABLE);
	return true;
}

static bool isp_clock_flash_prepare(target_flash_s *const flash)
{
	const target_s *const target = flash->t;
	const isp_clock_s *const priv = (isp_clock_s *)target->priv;

	if (flash->operation == FLASH_OPERATION_WRITE)
		jtag_dev_write_ir(priv->dev_index, IR_ADDRESS_INIT);

	return true;
}

static bool isp_clock_flash_done(target_flash_s *const flash)
{
	const target_s *const target = flash->t;
	const isp_clock_s *const priv = (isp_clock_s *)target->priv;

	if (flash->operation == FLASH_OPERATION_WRITE) {
		const uint32_t freq = platform_max_frequency_get();
		const uint32_t cycles = (freq * 50U) / 1000U;

		jtag_dev_write_ir(priv->dev_index, IR_PROGRAM_DONE);

		for (uint32_t cycle = 0U; cycle < cycles; cycle += 128U)
			jtag_proc.jtagtap_cycle(false, false, MIN(128U, cycles - cycle));
	}

	return true;
}

static bool isp_clock_flash_erase(target_flash_s *flash, target_addr_t dest, size_t length)
{
	(void)dest;
	(void)length;
	const target_s *const target = flash->t;
	const isp_clock_s *const priv = (isp_clock_s *)target->priv;
	const uint8_t dev_index = priv->dev_index;

	const uint32_t freq = platform_max_frequency_get();

	/* Issue to the device that we want it to bulk erase */
	jtag_dev_write_ir(dev_index, IR_BULK_ERASE);

	uint32_t cycles = (freq * 200U) / 1000U;
	for (uint32_t cycle = 0U; cycle < cycles; cycle += 128U)
		jtag_proc.jtagtap_cycle(false, false, MIN(128U, cycles - cycle));

	/* Then ask it to discharge the supply used for this now it's done */
	jtag_dev_write_ir(dev_index, IR_DISCHARGE);

	cycles = (freq * 10U) / 1000U;
	for (uint32_t cycle = 0U; cycle < cycles; cycle += 128U)
		jtag_proc.jtagtap_cycle(false, false, MIN(128U, cycles - cycle));

	return true;
}

static bool isp_clock_flash_write(
	target_flash_s *const flash, const target_addr_t dest, const void *const buffer, const size_t length)
{
	(void)dest;
	const target_s *const target = flash->t;
	const isp_clock_s *const priv = (isp_clock_s *)target->priv;
	const uint8_t dev_index = priv->dev_index;

	/* Setup for programming, auto-incrementing the EEPROM address */
	jtag_dev_write_ir(dev_index, IR_PROGRAM_INCR);

	/* Calculate the programming delay in cycles */
	const uint32_t freq = platform_max_frequency_get();
	const uint32_t cycles = (freq * 40U) / 1000U;

	/* Byte access pointer to the source data buffer */
	const uint8_t *const src = (const uint8_t *)buffer;
	/* Loop through the data to write 42 bits at a time */
	for (size_t bit_offset = 0U; bit_offset < length * 8U; bit_offset += 42U) {
		/* Buffer for the 5.25 bytes of data needed per program instruction */
		uint8_t data[6] = {0};

		/* Copy out the next 42 bits of data into the buffer */
		for (size_t buffer_offset = 0U; buffer_offset < 42U; ++buffer_offset) {
			/* Convert the position into a bit and byte index from the source data buffer */
			const size_t offset = bit_offset + buffer_offset;
			const size_t src_bit = 7U - (offset & 7U);
			const size_t src_byte = offset >> 3U;
			/* Extract the value */
			const bool value = ((src[src_byte] >> src_bit) & 1U) != 0U;
			/* Convert the position into a bit and byte index into the intermediary buffer */
			const size_t data_bit = buffer_offset & 7U;
			const size_t data_byte = buffer_offset >> 3U;
			data[data_byte] |= (uint8_t)value << data_bit;
		}

		/* Now we have the 42 bits to program set up in the buffer, send them to the device for programming */
		jtag_dev_shift_dr(dev_index, NULL, data, 42U);

		/* Wait for the slow-ass flash in the ispCLOCK to program */
		for (uint32_t cycle = 0U; cycle < cycles; cycle += 128U)
			jtag_proc.jtagtap_cycle(false, false, MIN(128U, cycles - cycle));
	}

	return true;
}
