/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2022  Black Sphere Technologies Ltd.
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

/* Semihosting support */

/*
 * If the target wants to read the special filename ":semihosting-features"
 * to know what semihosting features are supported, it's easiest to create
 * that file on the host in the directory where gdb runs,
 * or, if using pc-hosted, where blackmagic_hosted runs.
 *
 * $ echo -e 'SHFB\x03' > ":semihosting-features"
 * $ chmod 0444 ":semihosting-features"
 */

/* ARM Semihosting syscall numbers, from "Semihosting for AArch32 and AArch64 Version 3.0" */

#define SEMIHOSTING_SYS_CLOCK         0x10U
#define SEMIHOSTING_SYS_CLOSE         0x02U
#define SEMIHOSTING_SYS_ELAPSED       0x30U
#define SEMIHOSTING_SYS_ERRNO         0x13U
#define SEMIHOSTING_SYS_EXIT          0x18U
#define SEMIHOSTING_SYS_EXIT_EXTENDED 0x20U
#define SEMIHOSTING_SYS_FLEN          0x0cU
#define SEMIHOSTING_SYS_GET_CMDLINE   0x15U
#define SEMIHOSTING_SYS_HEAPINFO      0x16U
#define SEMIHOSTING_SYS_ISERROR       0x08U
#define SEMIHOSTING_SYS_ISTTY         0x09U
#define SEMIHOSTING_SYS_OPEN          0x01U
#define SEMIHOSTING_SYS_READ          0x06U
#define SEMIHOSTING_SYS_READC         0x07U
#define SEMIHOSTING_SYS_REMOVE        0x0eU
#define SEMIHOSTING_SYS_RENAME        0x0fU
#define SEMIHOSTING_SYS_SEEK          0x0aU
#define SEMIHOSTING_SYS_SYSTEM        0x12U
#define SEMIHOSTING_SYS_TICKFREQ      0x31U
#define SEMIHOSTING_SYS_TIME          0x11U
#define SEMIHOSTING_SYS_TMPNAM        0x0dU
#define SEMIHOSTING_SYS_WRITE         0x05U
#define SEMIHOSTING_SYS_WRITEC        0x03U
#define SEMIHOSTING_SYS_WRITE0        0x04U

#endif /* TARGET_SEMIHOSTING_H */
