/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2015  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
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
	sprintf(serial_no, "%04X%04X%04X", uid[1] + uid[5], uid[0] + uid[4], uid[offset]);
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
