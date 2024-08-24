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

#include <assert.h>
#include "bmp_remote.h"
#include "protocol_v3_adiv5.h"
#include "protocol_v4_defs.h"
#include "protocol_v4_adiv6.h"
#include "hex_utils.h"
#include "exception.h"

uint32_t remote_v4_adiv6_ap_read(adiv5_access_port_s *base_ap, uint16_t addr)
{
	adiv6_access_port_s *const ap = (adiv6_access_port_s *)base_ap;
	char buffer[REMOTE_MAX_MSG_SIZE];
	/* Create the request and send it to the remote */
	ssize_t length =
		snprintf(buffer, REMOTE_MAX_MSG_SIZE, REMOTE_ADIV6_AP_READ_STR, ap->base.dp->dev_index, ap->ap_address, addr);
	platform_buffer_write(buffer, length);
	/* Read back the answer and check for errors */
	length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	if (!remote_v3_adiv5_check_error(__func__, ap->base.dp, buffer, length))
		return 0U;
	/* If the response indicates all's OK, decode the data read and return it */
	uint32_t value = 0U;
	unhexify(&value, buffer + 1, 4);
	DEBUG_PROBE("%s: addr %04x -> %08" PRIx32 "\n", __func__, addr, value);
	return value;
}

void remote_v4_adiv6_ap_write(adiv5_access_port_s *base_ap, uint16_t addr, uint32_t value)
{
	adiv6_access_port_s *const ap = (adiv6_access_port_s *)base_ap;
	char buffer[REMOTE_MAX_MSG_SIZE];
	/* Create the request and send it to the remote */
	ssize_t length = snprintf(
		buffer, REMOTE_MAX_MSG_SIZE, REMOTE_ADIV6_AP_WRITE_STR, ap->base.dp->dev_index, ap->ap_address, addr, value);
	platform_buffer_write(buffer, length);
	/* Read back the answer and check for errors */
	length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	if (!remote_v3_adiv5_check_error(__func__, ap->base.dp, buffer, length))
		return;
	DEBUG_PROBE("%s: addr %04x <- %08" PRIx32 "\n", __func__, addr, value);
}
