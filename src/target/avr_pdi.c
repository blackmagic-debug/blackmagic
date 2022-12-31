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
#include "jtag_scan.h"
#include "avr_pdi.h"

#define PDI_BREAK 0xbbU
#define PDI_DELAY 0xdbU
#define PDI_EMPTY 0xebU

void avr_jtag_pdi_handler(const uint8_t dev_index)
{
	avr_pdi_s *pdi = calloc(1, sizeof(*pdi));
	if (!pdi) { /* calloc failed: heap exhaustion */
		DEBUG_WARN("calloc: failed in %s\n", __func__);
		return;
	}

	pdi->dev_index = dev_index;
	pdi->idcode = jtag_devs[dev_index].jd_idcode;
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
	do {
		data = (uint8_t *)&request;
		jtagtap_shift_dr();
		jtag_proc.jtagtap_tdi_seq(false, ones, dev->dr_prescan);
		data[0] = data_in;
		/* Calculate the parity bit */
		for (uint8_t i = 0; i < 8; ++i)
			data[1] ^= (data_in >> i) & 1U;
		jtag_proc.jtagtap_tdi_tdo_seq((uint8_t *)&response, !dev->dr_postscan, (uint8_t *)&request, 9);
		jtag_proc.jtagtap_tdi_seq(true, ones, dev->dr_postscan);
		jtagtap_return_idle(0);
		data = (uint8_t *)&response;
	} while (data[0] == PDI_DELAY && data[1] == 1);
	/* Calculate the parity bit */
	for (uint8_t i = 0; i < 8; ++i)
		result ^= (data[0] >> i) & 1U;
	*data_out = data[0];
	DEBUG_WARN("Sent 0x%02x to target, response was 0x%02x (0x%x)\n", data_in, data[0], data[1]);
	return result == data[1];
}
