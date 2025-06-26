/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2022 1BitSquared <info@1bitsquared.com>
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
#include "target_probe.h"
#include "target_internal.h"
#include "jtag_scan.h"
#include "jtagtap.h"
#include "avr_pdi.h"
#include "gdb_packet.h"
#include "exception.h"

#define IR_PDI    0x7U
#define IR_BYPASS 0xfU

#define PDI_BREAK 0xbbU
#define PDI_DELAY 0xdbU
#define PDI_EMPTY 0xebU

#define PDI_LDS    0x00U
#define PDI_LD     0x20U
#define PDI_STS    0x40U
#define PDI_ST     0x60U
#define PDI_LDCS   0x80U
#define PDI_REPEAT 0xa0U
#define PDI_STCS   0xc0U
#define PDI_KEY    0xe0U

#define PDI_ADDR_8  0x00U
#define PDI_ADDR_16 0x04U
#define PDI_ADDR_24 0x08U
#define PDI_ADDR_32 0x0cU

#define PDI_MODE_MASK 0xf3U

#define PDI_RESET 0x59U

typedef enum pdi_key {
	PDI_NVM = 0x02U,
	PDI_DEBUG = 0x04U,
} pdi_key_e;

static const uint8_t pdi_key_nvm[] = {0xff, 0x88, 0xd8, 0xcd, 0x45, 0xab, 0x89, 0x12};
static const uint8_t pdi_key_debug[] = {0x21, 0x81, 0x7c, 0x9f, 0xd4, 0x2d, 0x21, 0x3a};

static bool avr_pdi_init(avr_pdi_s *pdi);
static bool avr_attach(target_s *target);
static void avr_detach(target_s *target);

static void avr_reset(target_s *target);
static void avr_halt_request(target_s *target);
static target_halt_reason_e avr_halt_poll(target_s *target, target_addr_t *watch);

static int avr_breakwatch_set(target_s *target, breakwatch_s *breakwatch);
static int avr_breakwatch_clear(target_s *target, breakwatch_s *breakwatch);

void avr_jtag_pdi_handler(const uint8_t dev_index)
{
	avr_pdi_s *pdi = calloc(1, sizeof(*pdi));
	if (!pdi) { /* calloc failed: heap exhaustion */
		DEBUG_WARN("calloc: failed in %s\n", __func__);
		return;
	}

	/* Setup and try to initialise the PDI controller */
	pdi->dev_index = dev_index;
	pdi->idcode = jtag_devs[dev_index].jd_idcode;
	/* If we failed, free the structure */
	if (!avr_pdi_init(pdi))
		free(pdi);
	/* Reset the JTAG machinary back to bypass to scan the next device in the chain */
	jtag_dev_write_ir(dev_index, IR_BYPASS);
}

static bool avr_pdi_init(avr_pdi_s *const pdi)
{
	/* Check for a valid part number in the JTAG ID code */
	if ((pdi->idcode & 0x0ffff000U) == 0) {
		DEBUG_WARN("Invalid PDI idcode %08" PRIx32 "\n", pdi->idcode);
		return false;
	}
	DEBUG_INFO("AVR ID 0x%08" PRIx32 " (v%u)\n", pdi->idcode, (uint8_t)((pdi->idcode >> 28U) & 0xfU));
	/* Transition the part into PDI mode */
	jtag_dev_write_ir(pdi->dev_index, IR_PDI);

	/* Allocate a new target */
	target_s *target = target_new();
	if (!target)
		return false;

	/* Do preliminary setup of the target structure for an AVR target */
	target->cpuid = pdi->idcode;
	target->part_id = (pdi->idcode >> 12U) & 0xffffU;
	target->driver = "Atmel AVR";
	target->core = "AVR";
	target->priv = pdi;
	target->priv_free = free;

	target->attach = avr_attach;
	target->detach = avr_detach;

	target->halt_request = avr_halt_request;
	target->halt_poll = avr_halt_poll;
	target->reset = avr_reset;
	/*
	 * Unlike on an ARM processor, where this is the length of a table, here
	 * we return the size of a sutable registers structure.
	 */
	target->regs_size = sizeof(avr_regs_s);

	target->breakwatch_set = avr_breakwatch_set;
	target->breakwatch_clear = avr_breakwatch_clear;

	/* Try probing for various known AVR parts */
	PROBE(atxmega_probe);

#if PC_HOSTED == 0
	gdb_outf("Please report unknown AVR device with Part ID 0x%x\n", target->part_id);
#else
	DEBUG_WARN("Please report unknown AVR device with Part ID 0x%x\n", target->part_id);
#endif
	return true;
}

avr_pdi_s *avr_pdi_struct(target_s *const target)
{
	return (avr_pdi_s *)target->priv;
}

/*
 * This is a PDI-specific DR manipulation function that handles PDI_DELAY responses
 * transparently to the caller. It also does parity validation, returning true for
 * valid parity.
 */
static bool avr_jtag_shift_dr(uint8_t dev_index, uint8_t *data_out, const uint8_t data_in)
{
	jtag_dev_s *dev = &jtag_devs[dev_index];
	uint8_t result = 0;
	uint16_t request = 0;
	uint16_t response = 0;
	uint8_t *data;
	if (!data_out)
		return false;
	/* Try to perform a PDI access, with retry when the reply is a delay packet */
	do {
		data = (uint8_t *)&request;
		/* Begin the data register transaction, handling any devices in bypass before us in the chain */
		jtagtap_shift_dr();
		jtag_proc.jtagtap_tdi_seq(false, ones, dev->dr_prescan);
		/* Build the PDI packet to send */
		data[0] = data_in;
		/* Calculate the parity bit of the request */
		for (uint8_t i = 0; i < 8; ++i)
			data[1] ^= (data_in >> i) & 1U;
		/* Then send it and handle any devices in bypass after us in the chain */
		jtag_proc.jtagtap_tdi_tdo_seq((uint8_t *)&response, !dev->dr_postscan, (uint8_t *)&request, 9);
		jtag_proc.jtagtap_tdi_seq(true, ones, dev->dr_postscan);
		/* Then return the JTAG bus to idle */
		jtagtap_return_idle(0);
		data = (uint8_t *)&response;
	} while (data[0] == PDI_DELAY && data[1] == 1);
	/* Calculate the parity bit from the reply */
	for (uint8_t i = 0; i < 8; ++i)
		result ^= (data[0] >> i) & 1U;
	*data_out = data[0];
	DEBUG_TARGET("Sent 0x%02x to target, response was 0x%02x (0x%x)\n", data_in, data[0], data[1]);
	/* Return if the calculated parity matches the received parity */
	return result == data[1];
}

bool avr_pdi_reg_write(const avr_pdi_s *const pdi, const uint8_t reg, const uint8_t value)
{
	uint8_t result = 0;
	const uint8_t command = PDI_STCS | reg;
	if (reg >= 16 || avr_jtag_shift_dr(pdi->dev_index, &result, command) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(pdi->dev_index, &result, value))
		return false;
	return result == PDI_EMPTY;
}

uint8_t avr_pdi_reg_read(const avr_pdi_s *const pdi, const uint8_t reg)
{
	uint8_t result = 0;
	const uint8_t command = PDI_LDCS | reg;
	if (reg >= 16 || avr_jtag_shift_dr(pdi->dev_index, &result, command) || result != PDI_EMPTY ||
		!avr_jtag_shift_dr(pdi->dev_index, &result, 0))
		return 0xffU; // TODO - figure out a better way to indicate failure.
	return result;
}

bool avr_pdi_write(const avr_pdi_s *const pdi, const uint8_t bytes, const uint32_t reg, const uint32_t value)
{
	uint8_t result = 0;
	uint8_t command = PDI_STS | PDI_ADDR_32 | bytes;
	uint8_t data_bytes[4] = {
		value & 0xffU,
		(value >> 8U) & 0xffU,
		(value >> 16U) & 0xffU,
		(value >> 24U) & 0xffU,
	};

	if (avr_jtag_shift_dr(pdi->dev_index, &result, command) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(pdi->dev_index, &result, reg & 0xffU) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(pdi->dev_index, &result, (reg >> 8U) & 0xffU) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(pdi->dev_index, &result, (reg >> 16U) & 0xffU) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(pdi->dev_index, &result, (reg >> 24U) & 0xffU) || result != PDI_EMPTY)
		return false;
	// This is intentionally <= to avoid `bytes + 1` silliness
	for (uint8_t i = 0; i <= bytes; ++i) {
		if (avr_jtag_shift_dr(pdi->dev_index, &result, data_bytes[i]) || result != PDI_EMPTY)
			return false;
	}
	return true;
}

static bool avr_pdi_read(const avr_pdi_s *const pdi, const uint8_t bytes, const uint32_t reg, uint32_t *const value)
{
	uint8_t result = 0;
	uint8_t command = PDI_LDS | PDI_ADDR_32 | bytes;
	uint8_t data_bytes[4];
	uint32_t data = 0xffffffffU;
	if (avr_jtag_shift_dr(pdi->dev_index, &result, command) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(pdi->dev_index, &result, reg & 0xffU) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(pdi->dev_index, &result, (reg >> 8U) & 0xffU) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(pdi->dev_index, &result, (reg >> 16U) & 0xffU) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(pdi->dev_index, &result, (reg >> 24U) & 0xffU) || result != PDI_EMPTY)
		return false;
	for (uint8_t i = 0; i <= bytes; ++i) {
		if (!avr_jtag_shift_dr(pdi->dev_index, &data_bytes[i], 0))
			return false;
	}
	data = data_bytes[0];
	if (bytes > PDI_DATA_8)
		data |= (uint32_t)data_bytes[1] << 8U;
	if (bytes > PDI_DATA_16)
		data |= (uint32_t)data_bytes[2] << 16U;
	if (bytes > PDI_DATA_24)
		data |= (uint32_t)data_bytes[3] << 24U;
	*value = data;
	return true;
}

bool avr_pdi_read8(const avr_pdi_s *const pdi, const uint32_t reg, uint8_t *const value)
{
	uint32_t data;
	const bool result = avr_pdi_read(pdi, PDI_DATA_8, reg, &data);
	if (result)
		*value = data;
	return result;
}

bool avr_pdi_read16(const avr_pdi_s *const pdi, const uint32_t reg, uint16_t *const value)
{
	uint32_t data;
	const bool result = avr_pdi_read(pdi, PDI_DATA_16, reg, &data);
	if (result)
		*value = data;
	return result;
}

bool avr_pdi_read24(const avr_pdi_s *const pdi, const uint32_t reg, uint32_t *const value)
{
	return avr_pdi_read(pdi, PDI_DATA_24, reg, value);
}

bool avr_pdi_read32(const avr_pdi_s *const pdi, const uint32_t reg, uint32_t *const value)
{
	return avr_pdi_read(pdi, PDI_DATA_32, reg, value);
}

// Runs `st ptr <addr>`
static bool avr_pdi_write_ptr(const avr_pdi_s *const pdi, const uint32_t addr)
{
	const uint8_t command = PDI_ST | PDI_MODE_DIR_PTR | PDI_DATA_32;
	uint8_t result = 0;
	return !avr_jtag_shift_dr(pdi->dev_index, &result, command) && result == PDI_EMPTY &&
		!avr_jtag_shift_dr(pdi->dev_index, &result, addr & 0xffU) && result == PDI_EMPTY &&
		!avr_jtag_shift_dr(pdi->dev_index, &result, (addr >> 8U) & 0xffU) && result == PDI_EMPTY &&
		!avr_jtag_shift_dr(pdi->dev_index, &result, (addr >> 16U) & 0xffU) && result == PDI_EMPTY &&
		!avr_jtag_shift_dr(pdi->dev_index, &result, (addr >> 24U) & 0xffU) && result == PDI_EMPTY;
}

// Runs `repeat <count - 1>`
static bool avr_pdi_repeat(const avr_pdi_s *const pdi, const uint32_t count)
{
	const uint32_t repeat = count - 1U;
	const uint8_t command = PDI_REPEAT | PDI_DATA_32;
	uint8_t result = 0;
	return !avr_jtag_shift_dr(pdi->dev_index, &result, command) && result == PDI_EMPTY &&
		!avr_jtag_shift_dr(pdi->dev_index, &result, repeat & 0xffU) && result == PDI_EMPTY &&
		!avr_jtag_shift_dr(pdi->dev_index, &result, (repeat >> 8U) & 0xffU) && result == PDI_EMPTY &&
		!avr_jtag_shift_dr(pdi->dev_index, &result, (repeat >> 16U) & 0xffU) && result == PDI_EMPTY &&
		!avr_jtag_shift_dr(pdi->dev_index, &result, (repeat >> 24U) & 0xffU) && result == PDI_EMPTY;
}

bool avr_pdi_write_ind(const avr_pdi_s *const pdi, const uint32_t addr, const uint8_t ptr_mode, const void *const src,
	const uint32_t count)
{
	const uint8_t command = PDI_ST | ptr_mode;
	uint8_t result = 0;
	const uint8_t *const data = (const uint8_t *)src;
	if ((ptr_mode & PDI_MODE_MASK) || !count || !avr_pdi_write_ptr(pdi, addr) || !avr_pdi_repeat(pdi, count))
		return false;
	// Run `st <ptr_mode>`
	if (avr_jtag_shift_dr(pdi->dev_index, &result, command) || result != PDI_EMPTY)
		return false;
	for (uint32_t i = 0; i < count; ++i) {
		if (avr_jtag_shift_dr(pdi->dev_index, &result, data[i]) || result != PDI_EMPTY)
			return false;
	}
	return true;
}

bool avr_pdi_read_ind(
	const avr_pdi_s *const pdi, const uint32_t addr, const uint8_t ptr_mode, void *const dst, const uint32_t count)
{
	const uint8_t command = PDI_LD | ptr_mode;
	uint8_t result = 0;
	uint8_t *const data = (uint8_t *)dst;
	if ((ptr_mode & PDI_MODE_MASK) || !count || !avr_pdi_write_ptr(pdi, addr) || !avr_pdi_repeat(pdi, count))
		return false;
	// Run `ld <ptr_mode>`
	if (avr_jtag_shift_dr(pdi->dev_index, &result, command) || result != PDI_EMPTY)
		return false;
	for (uint32_t i = 0; i < count; ++i) {
		if (!avr_jtag_shift_dr(pdi->dev_index, data + i, 0))
			return false;
	}
	return true;
}

/*
 * Enable a PDI feature based on the `what` argument.
 * This sends a PDI `KEY` instruction with the appropriate enablement key and
 * returns if the PDI controller did actually enable the requested feature.
 */
static bool avr_enable(const avr_pdi_s *const pdi, const pdi_key_e what)
{
	const uint8_t *const key = what == PDI_DEBUG ? pdi_key_debug : pdi_key_nvm;
	uint8_t result = 0;
	if (avr_jtag_shift_dr(pdi->dev_index, &result, PDI_KEY) || result != PDI_EMPTY)
		return false;
	for (uint8_t i = 0; i < 8; ++i) {
		if (avr_jtag_shift_dr(pdi->dev_index, &result, key[i]) || result != PDI_EMPTY)
			return false;
	}
	return (avr_pdi_reg_read(pdi, PDI_REG_STATUS) & what) == what;
}

/* Disables the requested PDI feature based on the `what` argument */
static bool avr_disable(const avr_pdi_s *const pdi, const pdi_key_e what)
{
	return avr_pdi_reg_write(pdi, PDI_REG_STATUS, ~what);
}

static bool avr_ensure_nvm_idle(const avr_pdi_s *const pdi)
{
	if (pdi->ensure_nvm_idle)
		return pdi->ensure_nvm_idle(pdi);
	return true;
}

static bool avr_attach(target_s *const target)
{
	const avr_pdi_s *const pdi = avr_pdi_struct(target);
	/* Put the target back in PDI comms mode */
	jtag_dev_write_ir(pdi->dev_index, IR_PDI);

	TRY (EXCEPTION_ALL) {
		/* Attempt to reset the target */
		target_reset(target);
		/* Then enable the debug unit in the PDI controller */
		if (!avr_enable(pdi, PDI_DEBUG))
			return false;
		/* Ask the PDI controller to halt the processor */
		target_halt_request(target);
		/*
		 * And now finish by enabling the NVM unit in the PDI controller and
		 * check that the processor is in administrative halt
		 */
		if (!avr_enable(pdi, PDI_NVM) || !avr_ensure_nvm_idle(pdi) || avr_pdi_reg_read(pdi, PDI_REG_R3) != 0x04U)
			return false;
	}
	CATCH () {
	default:
		return !exception_frame.type;
	}
	return true;
}

static void avr_detach(target_s *const target)
{
	const avr_pdi_s *const pdi = avr_pdi_struct(target);

	/* Disable all enabled PDI controller units to detach from the target */
	avr_disable(pdi, PDI_NVM);
	avr_disable(pdi, PDI_DEBUG);
	jtag_dev_write_ir(pdi->dev_index, IR_BYPASS);
}

static void avr_reset(target_s *const target)
{
	avr_pdi_s *const pdi = avr_pdi_struct(target);
	/*
	 * We only actually want to do this if the target is not presently attached as
	 * this resets the NVM and debug enables
	 */
	if (target->attached)
		return;
	/* Write the reset register to initiate reset */
	if (!avr_pdi_reg_write(pdi, PDI_REG_RESET, PDI_RESET))
		raise_exception(EXCEPTION_ERROR, "Error resetting device, device in incorrect state");
	/* If any PDI controller units are presently enabled, disable them */
	if (avr_pdi_reg_read(pdi, PDI_REG_STATUS) != 0x00) {
		avr_disable(pdi, PDI_NVM);
		avr_disable(pdi, PDI_DEBUG);
	}
}

static void avr_halt_request(target_s *const target)
{
	avr_pdi_s *const pdi = avr_pdi_struct(target);
	/*
	 * To halt the processor we go through a few really specific steps:
	 * Write r4 to 1 to indicate we want to put the processor into debug-based pause
	 * Read r3 and check it's 0x10 which indicates the processor is held in reset and no debugging is active
	 * Release reset
	 * Read r3 twice more, the first time should respond 0x14 to indicate the processor is still reset
	 * but that debug pause is requested, and the second should respond 0x04 to indicate the processor is now
	 * in debug pause state (halted)
	 */
	if (!avr_pdi_reg_write(pdi, PDI_REG_R4, 1) || avr_pdi_reg_read(pdi, PDI_REG_R3) != 0x10U ||
		!avr_pdi_reg_write(pdi, PDI_REG_RESET, 0) || avr_pdi_reg_read(pdi, PDI_REG_R3) != 0x14U ||
		avr_pdi_reg_read(pdi, PDI_REG_R3) != 0x04U)
		raise_exception(EXCEPTION_ERROR, "Error halting device, device in incorrect state");
	pdi->halt_reason = TARGET_HALT_REQUEST;
}

static target_halt_reason_e avr_halt_poll(target_s *const target, target_addr_t *const watch)
{
	avr_pdi_s *const pdi = avr_pdi_struct(target);
	(void)watch;

	/* If we're running but the processor stops because it's hit a breakpoint, update */
	if (pdi->halt_reason == TARGET_HALT_RUNNING && avr_pdi_reg_read(pdi, PDI_REG_R3) == 0x04U)
		pdi->halt_reason = TARGET_HALT_BREAKPOINT;
	return pdi->halt_reason;
}

/*
 * The following can be used as a key for understanding the various return results from the breakwatch functions:
 * 0 -> success
 * 1 -> not supported
 * -1 -> an error occured
 */

static int avr_breakwatch_set(target_s *const target, breakwatch_s *const breakwatch)
{
	avr_pdi_s *const pdi = avr_pdi_struct(target);
	switch (breakwatch->type) {
	case TARGET_BREAK_HARD: {
		/* First try and find a unused breakpoint slot */
		size_t breakpoint = 0;
		for (; breakpoint < pdi->breakpoints_available; ++breakpoint) {
			/* Check if the slot is presently in use, breaking if it is not */
			if (!(pdi->breakpoints[breakpoint] & AVR_BREAKPOINT_ENABLED))
				break;
		}
		/* If none was available, return an error */
		if (breakpoint == pdi->breakpoints_available)
			return -1;

		/* Store the address to set the breakpoint, and store the index of the slot used in the breakwatch structure */
		pdi->breakpoints[breakpoint] = AVR_BREAKPOINT_ENABLED | (breakwatch->addr & AVR_BREAKPOINT_MASK);
		breakwatch->reserved[0] = breakpoint;
		/* Tell the debugger that it was successfully able to "set" the breakpoint */
		return 0;
	}
	default:
		/* If the breakwatch type is not one of the above, tell the debugger we don't support it */
		return 1;
	}
}

static int avr_breakwatch_clear(target_s *const target, breakwatch_s *const breakwatch)
{
	avr_pdi_s *const pdi = avr_pdi_struct(target);
	/* Clear the breakpoint slot this used */
	pdi->breakpoints[breakwatch->reserved[0]] = 0U;
	/* Tell the debugger that it was successfully able to "clear" the breakpoint */
	return 0;
}
