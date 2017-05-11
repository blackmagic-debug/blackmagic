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

char *serialno_read(char *s)
{
#if defined(STM32L0) || defined(STM32F3) || defined(STM32F4)
	volatile uint16_t *uid = (volatile uint16_t *)DESIG_UNIQUE_ID_BASE;
# if defined(STM32F4)
	int offset = 3;
# elif defined(STM32L0) || defined(STM32F4)
	int offset = 5;
#endif
	sprintf(s, "%04X%04X%04X",
            uid[1] + uid[5], uid[0] + uid[4], uid[offset]);
#else
	volatile uint32_t *unique_id_p = (volatile uint32_t *)0x1FFFF7E8;
	uint32_t unique_id = *unique_id_p +
			*(unique_id_p + 1) +
			*(unique_id_p + 2);
	int i;

	/* Fetch serial number from chip's unique ID */
	for(i = 0; i < 8; i++) {
		s[7-i] = ((unique_id >> (4*i)) & 0xF) + '0';
	}
	for(i = 0; i < 8; i++)
		if(s[i] > '9')
			s[i] += 'A' - '9' - 1;
	s[8] = 0;

#endif
	return s;
}

