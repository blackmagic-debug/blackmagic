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

char *serial_no_read(char *s, int max)
{
#if defined(STM32F1) || defined(USE_BMP_SERIAL)
/* Only STM32F103 has no DFU Bootloader. Generate a ID comatible
 * with the BMP Bootloader since ages.
 */
	uint32_t *unique_id_p = (uint32_t *)DESIG_UNIQUE_ID_BASE;
	uint32_t unique_id = *unique_id_p +
		*(unique_id_p + 1) +
		*(unique_id_p + 2);
	snprintf(s, max, "%08" PRIX32, unique_id);
#else
	/* Use the same serial number as the ST DFU Bootloader.*/
	uint16_t *uid = (uint16_t *)DESIG_UNIQUE_ID_BASE;
# if defined(STM32F4)
	int offset = 3;
# elif defined(STM32L0) || defined(STM32F3)
	int offset = 5;
# endif
	snprintf(s, max, "%04X%04X%04X",
            uid[1] + uid[5], uid[0] + uid[4], uid[offset]);

#endif
	return s;
}

