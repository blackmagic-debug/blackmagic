/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011 Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 * Copyright (C) 2020 Uwe Bonnes (bon@elektron.ikp.physik.tu-darmstadt.de)
 * Copyright (C) 2022-2024 1BitSquared <info@1bitsquared.com>
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

#ifndef TARGET_ADIV5_INTERFACE_H
#define TARGET_ADIV5_INTERFACE_H

#include "adiv5_internal.h"
#include "exception.h"

#ifndef DEBUG_PROTO_IS_NOOP
void decode_access(uint16_t addr, uint8_t rnw, uint8_t apsel, uint32_t value);
#endif

static inline bool adiv5_write_no_check(adiv5_debug_port_s *const dp, const uint16_t addr, const uint32_t value)
{
#ifndef DEBUG_PROTO_IS_NOOP
	decode_access(addr, ADIV5_LOW_WRITE, 0U, value);
	DEBUG_PROTO("0x%08" PRIx32 "\n", value);
#endif
	return dp->write_no_check(addr, value);
}

static inline uint32_t adiv5_read_no_check(adiv5_debug_port_s *const dp, const uint16_t addr)
{
	uint32_t result = dp->read_no_check(addr);
#ifndef DEBUG_PROTO_IS_NOOP
	decode_access(addr, ADIV5_LOW_READ, 0U, 0U);
	DEBUG_PROTO("0x%08" PRIx32 "\n", result);
#endif
	return result;
}

static inline uint32_t adiv5_dp_read(adiv5_debug_port_s *const dp, const uint16_t addr)
{
	uint32_t ret = dp->dp_read(dp, addr);
#ifndef DEBUG_PROTO_IS_NOOP
	decode_access(addr, ADIV5_LOW_READ, 0U, 0U);
	DEBUG_PROTO("0x%08" PRIx32 "\n", ret);
#endif
	return ret;
}

static inline void adiv5_dp_write(adiv5_debug_port_s *const dp, const uint16_t addr, const uint32_t value)
{
#ifndef DEBUG_PROTO_IS_NOOP
	decode_access(addr, ADIV5_LOW_WRITE, 0U, value);
	DEBUG_PROTO("0x%08" PRIx32 "\n", value);
#endif
	dp->low_access(dp, ADIV5_LOW_WRITE, addr, value);
}

static inline uint32_t adiv5_dp_low_access(
	adiv5_debug_port_s *const dp, const uint8_t rnw, const uint16_t addr, const uint32_t value)
{
	uint32_t ret = dp->low_access(dp, rnw, addr, value);
#ifndef DEBUG_PROTO_IS_NOOP
	decode_access(addr, rnw, 0U, value);
	DEBUG_PROTO("0x%08" PRIx32 "\n", rnw ? ret : value);
#endif
	return ret;
}

static inline uint32_t adiv5_dp_error(adiv5_debug_port_s *const dp)
{
	uint32_t ret = dp->error(dp, false);
	DEBUG_PROTO("DP Error 0x%08" PRIx32 "\n", ret);
	return ret;
}

static inline void adiv5_dp_abort(adiv5_debug_port_s *const dp, const uint32_t abort)
{
	DEBUG_PROTO("Abort: %08" PRIx32 "\n", abort);
	dp->abort(dp, abort);
}

static inline uint32_t adiv5_ap_read(adiv5_access_port_s *const ap, const uint16_t addr)
{
	uint32_t ret = ap->dp->ap_read(ap, addr);
#ifndef DEBUG_PROTO_IS_NOOP
	decode_access(addr, ADIV5_LOW_READ, ap->apsel, 0U);
	DEBUG_PROTO("0x%08" PRIx32 "\n", ret);
#endif
	return ret;
}

static inline void adiv5_ap_write(adiv5_access_port_s *const ap, const uint16_t addr, const uint32_t value)
{
#ifndef DEBUG_PROTO_IS_NOOP
	decode_access(addr, ADIV5_LOW_WRITE, ap->apsel, value);
	DEBUG_PROTO("0x%08" PRIx32 "\n", value);
#endif
	ap->dp->ap_write(ap, addr, value);
}

static inline void adiv5_mem_read(
	adiv5_access_port_s *const ap, void *const dest, const target_addr64_t src, const size_t len)
{
	ap->dp->mem_read(ap, dest, src, len);
	DEBUG_PROTO("%s @ %" PRIx64 " len %zu:", __func__, src, len);
#ifndef DEBUG_PROTO_IS_NOOP
	const uint8_t *const data = (const uint8_t *)dest;
	for (size_t offset = 0; offset < len; ++offset) {
		if (offset == 16U)
			break;
		DEBUG_PROTO(" %02x", data[offset]);
	}
	if (len > 16U)
		DEBUG_PROTO(" ...");
#endif
	DEBUG_PROTO("\n");
}

static inline void adiv5_mem_write_aligned(adiv5_access_port_s *const ap, const target_addr64_t dest,
	const void *const src, const size_t len, const align_e align)
{
	DEBUG_PROTO("%s @ %" PRIx64 " len %zu, align %u:", "adiv5_mem_write", dest, len, 1U << align);
#ifndef DEBUG_PROTO_IS_NOOP
	const uint8_t *const data = (const uint8_t *)src;
	for (size_t offset = 0; offset < len; ++offset) {
		if (offset == 16U)
			break;
		DEBUG_PROTO(" %02x", data[offset]);
	}
	if (len > 16U)
		DEBUG_PROTO(" ...");
#endif
	DEBUG_PROTO("\n");
	ap->dp->mem_write(ap, dest, src, len, align);
}

static inline uint32_t adiv5_dp_recoverable_access(adiv5_debug_port_s *dp, uint8_t rnw, uint16_t addr, uint32_t value)
{
	const uint32_t result = dp->low_access(dp, rnw, addr, value);
	/* If the access results in the no-response response, retry after clearing the error state */
	if (dp->fault == SWDP_ACK_NO_RESPONSE) {
		uint32_t response;
		/* Wait the response period, then clear the error */
		swd_proc.seq_in_parity(&response, 32);
		DEBUG_WARN("Recovering and re-trying access\n");
		dp->error(dp, true);
		response = dp->low_access(dp, rnw, addr, value);
		/* If the access results in no-response again, throw to propergate that up */
		if (dp->fault == SWDP_ACK_NO_RESPONSE)
			raise_exception(EXCEPTION_ERROR, "SWD invalid ACK");
		return response;
	}
	return result;
}

#endif /* TARGET_ADIV5_INTERFACE_H */
