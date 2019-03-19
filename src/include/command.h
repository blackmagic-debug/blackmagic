/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
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

#ifndef __COMMAND_H
#define __COMMAND_H

#include <stdbool.h>

#include "target.h"

int command_process(target *t, char *cmd);

/*
 * Attempts to parse a string as either being "enable" or "disable".
 * If the parse is successful, returns true and sets the out param to
 * indicate what was parsed. If not successful, emits a warning to the
 * gdb port, returns false and leaves out untouched.
 */
bool parse_enable_or_disable(const char *s, bool *out);

#endif

