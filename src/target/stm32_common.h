/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2022 1BitSquared <info@1bitsquared.com>
 * Written by Rachel Mant <git@dragonmux.network>
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

#ifndef TARGET_STM32_COMMON_H
#define TARGET_STM32_COMMON_H

#include "general.h"
#include "target_internal.h"
#include "adiv5.h"

static inline const char *stm32_psize_to_string(const align_e psize)
{
	switch (psize) {
	case ALIGN_64BIT:
		return "x64";
	case ALIGN_32BIT:
		return "x32";
	case ALIGN_16BIT:
		return "x16";
	default:
		return "x8";
	}
}

static inline bool stm32_psize_from_string(target_s *t, const char *const str, align_e *psize)
{
	if (strcasecmp(str, "x8") == 0)
		*psize = ALIGN_8BIT;
	else if (strcasecmp(str, "x16") == 0)
		*psize = ALIGN_16BIT;
	else if (strcasecmp(str, "x32") == 0)
		*psize = ALIGN_32BIT;
	else if (strcasecmp(str, "x64") == 0)
		*psize = ALIGN_64BIT;
	else {
		tc_printf(t, "usage: monitor psize (x8|x16|x32|x32)\n");
		return false;
	}
	return true;
}

#endif /*TARGET_STM32_COMMON_H*/
