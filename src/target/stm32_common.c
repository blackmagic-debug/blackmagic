/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2023-2024 1BitSquared <info@1bitsquared.com>
 * Written by ALTracer <tolstov_den@mail.ru>
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

#include "stm32_common.h"
#include "buffer_utils.h"

typedef struct stm32_uid_fields {
	uint16_t wafer_xcoord;
	uint16_t wafer_ycoord;
	uint8_t wafer_number;
	uint8_t lot_number[7];
} stm32_uid_s;

/*
 * Print the Unique device ID.
 * Can be reused for other STM32 devices with uid_base as parameter.
 */
bool stm32_uid(target_s *const target, const target_addr_t uid_base)
{
	char uid_hex[25] = {0};
	uint8_t uid_bytes[12] = {0};
	if (target_mem32_read(target, uid_bytes, uid_base, 12))
		return false;

	for (size_t i = 0; i < 12U; i += 4U) {
		const uint32_t value = read_le4(uid_bytes, i);
		//utoa_upper(value, &uid_hex[i * 2U], 16);
		snprintf(uid_hex + i * 2U, 9, "%08" PRIX32, value);
	}
	tc_printf(target, "0x%s\n", uid_hex);

	stm32_uid_s uid;
	uid.wafer_xcoord = read_le2(uid_bytes, 0);
	uid.wafer_ycoord = read_le2(uid_bytes, 2);
	uid.wafer_number = uid_bytes[4];
	memcpy(uid.lot_number, &uid_bytes[5], 7);
	/* Avoid decoding as non-printable characters */
	for (size_t j = 5U; j < 12U; j++) {
		if (uid_bytes[j] < 0x20U || uid_bytes[j] >= 0x7fU)
			return true;
	}

	tc_printf(target, "Wafer coords X=%u, Y=%u, number %u; Lot number %.7s\n", uid.wafer_xcoord, uid.wafer_ycoord,
		uid.wafer_number, uid.lot_number);
	return true;
}
