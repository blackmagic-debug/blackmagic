/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2015  Black Sphere Technologies Ltd.
 * Copyright (C) 2017-2021 Uwe Bonnes <bon@elektron.ikp.physik.tu-darmstadt.de>
 * Copyright (C) 2022-2024 1BitSquared <info@1bitsquared.com>
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 * Modified by Uwe Bonnes <bon@elektron.ikp.physik.tu-darmstadt.de>
 * Modified by Rachel Mant <git@dragonmux.network>
 * Modified by ALTracer <tolstov_den@mail.ru>
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
#include "general.h"
#include "platform.h"
#include <libopencm3/stm32/desig.h>

char serial_no[DFU_SERIAL_LENGTH];

static char *utoa_upper(uint32_t value, char *const str, uint8_t base) __attribute__((unused));

void read_serial_number(void)
{
#if DFU_SERIAL_LENGTH == 9
	const volatile uint32_t *const unique_id_p = (uint32_t *)DESIG_UNIQUE_ID_BASE;
	const uint32_t unique_id = unique_id_p[0] + unique_id_p[1] + unique_id_p[2];
	/* Fetch serial number from chip's unique ID */
	for (size_t i = 0; i < 8U; ++i) {
		serial_no[7U - i] = ((unique_id >> (i * 4U)) & 0x0fU) + '0';
		/* If the character is something above 9, then add the offset to make it ASCII A-F */
		if (serial_no[7U - i] > '9')
			serial_no[7U - i] += 7; /* 'A' - '9' = 8, less 1 gives 7. */
	}
#elif DFU_SERIAL_LENGTH == 13
	/* Use the same serial number as the ST DFU Bootloader.*/
	const volatile uint16_t *const uid = (uint16_t *)DESIG_UNIQUE_ID_BASE;
#if defined(STM32F4) || defined(STM32F7)
	int offset = 3;
#elif defined(STM32L0) || defined(STM32F0) || defined(STM32F3)
	int offset = 5;
#endif
	utoa_upper(uid[1] + uid[5], serial_no, 16);
	utoa_upper(uid[0] + uid[4], serial_no + 4, 16);
	utoa_upper(uid[offset], serial_no + 8, 16);
#elif DFU_SERIAL_LENGTH == 25
	const volatile uint32_t *const unique_id_p = (uint32_t *)DESIG_UNIQUE_ID_BASE;
	uint32_t unique_id = 0;
	for (size_t i = 0; i < 24U; ++i) {
		const size_t chunk = i >> 3U;
		const size_t nibble = i & 7U;
		const size_t idx = (chunk << 3U) + (7U - nibble);
		if (nibble == 0)
			unique_id = unique_id_p[chunk];
		serial_no[idx] = ((unique_id >> (nibble * 4U)) & 0xfU) + '0';

		/* If the character is something above 9, then add the offset to make it ASCII A-F */
		if (serial_no[idx] > '9')
			serial_no[idx] += 7; /* 'A' - '9' = 8, less 1 gives 7. */
	}
#else
#warning "Unhandled DFU_SERIAL_LENGTH"
#endif
	serial_no[DFU_SERIAL_LENGTH - 1] = '\0';
}

/*
 * Converts an unsigned integer value into an equivalent base-N ASCII printable representation.
 * It takes as arguments (in order): the value to convert,
 * a pointer to a buffer large enough to store the result,
 * and the base to perform the conversion to, in between 2 and 36 inclusive.
 * The result of this function is NULL on error (unsupported base),
 * or a pointer into the input buffer advanced by the amount written,
 * not including the nul terminator guaranteed by this function.
 */
static char *utoa_upper(uint32_t value, char *const str, const uint8_t base)
{
	static const char digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	static char buf[32] = {0};
	size_t total = 0;

	/* Check base is supported. */
	if (base < 2 || base > 36) {
		str[0] = '\0';
		return NULL;
	}

	/* Push up to 32 symbols (base-2 aka binary, worst case) */
	for (size_t offset = 0; offset < 32; ++offset) {
		const div_t result = div(value, base);
		const uint32_t remainder = result.rem;
		buf[offset] = digits[remainder];
		if (result.quot == 0) {
			total = offset;
			break;
		}
		value = result.quot;
	}

	/* Reverse local buffer into caller buffer */
	for (size_t i = 0; i <= total; ++i)
		str[i] = buf[total - i];

	/* Terminate the result just in case */
	str[++total] = '\0';

	return str + total;
}
