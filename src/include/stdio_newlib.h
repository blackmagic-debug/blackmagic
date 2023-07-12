/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2023 1BitSquared <info@1bitsquared.com>
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

#ifndef STDIO_NEWLIB_H
#define STDIO_NEWLIB_H

/* Use newlib provided integer-only stdio functions */

#ifdef sscanf
#undef sscanf
#endif
#define sscanf siscanf

#ifdef sprintf
#undef sprintf
#endif
#define sprintf siprintf

#ifdef vasprintf
#undef vasprintf
#endif
#define vasprintf vasiprintf

#ifdef snprintf
#undef snprintf
#endif
#define snprintf sniprintf

#endif /* STDIO_NEWLIB_H */
