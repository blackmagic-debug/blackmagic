/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2022 Black Sphere Technologies Ltd.
 * Copyright (C) 2023 1BitSquared <info@1bitsquared.com>
 * Written by Perigoso <git@dragonmux.network>
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

#ifndef TARGET_SEMIHOSTING_H
#define TARGET_SEMIHOSTING_H

#include "general.h"

extern uint32_t semihosting_wallclock_epoch;

int32_t semihosting_request(target_s *target, uint32_t syscall, uint32_t r1);
int32_t semihosting_reply(target_controller_s *tc, char *packet);

#endif /* TARGET_SEMIHOSTING_H */
