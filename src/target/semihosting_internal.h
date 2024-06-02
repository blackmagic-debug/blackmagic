/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2023 1BitSquared <info@1bitsquared.com>
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

#ifndef TARGET_SEMIHOSTING_INTERNAL_H
#define TARGET_SEMIHOSTING_INTERNAL_H

#include "general.h"

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

#define TARGET_NULL ((target_addr_t)0)

#define STDOUT_READ_BUF_SIZE 64U

typedef struct semihosting {
	uint32_t r1;
	uint32_t params[4U];
} semihosting_s;

typedef struct semihosting_time {
	uint64_t microseconds;
	uint32_t seconds;
} semihosting_time_s;

typedef enum semihosting_open_flags {
	OPEN_MODE_RDONLY = 0x0,
	OPEN_MODE_WRONLY = 0x1,
	OPEN_MODE_RDWR = 0x2,
	OPEN_MODE_APPEND = 0x8,
	OPEN_MODE_CREAT = 0x200,
	OPEN_MODE_TRUNC = 0x400,
} semihosting_open_flags_e;

typedef enum semihosting_seek_flag {
	SEEK_MODE_SET = 0,
	SEEK_MODE_CUR = 1,
	SEEK_MODE_END = 2,
} semihosting_seek_flag_e;

typedef enum semihosting_exit_reason {
	/* Hardware exceptions */
	EXIT_REASON_BRANCH_THROUGH_ZERO = 0x20000U,
	EXIT_REASON_UNDEFINED_INSN = 0x20001U,
	EXIT_REASON_SOFTWARE_INTERRUPT = 0x20002U,
	EXIT_REASON_PREFETCH_ABORT = 0x20003U,
	EXIT_REASON_DATA_ABORT = 0x20004U,
	EXIT_REASON_ADDRESS_EXCEPTION = 0x20005U,
	EXIT_REASON_IRQ = 0x20006U,
	EXIT_REASON_FIQ = 0x20007U,

	/* Software reasons */
	EXIT_REASON_BREAKPOINT = 0x20020U,
	EXIT_REASON_WATCHPOINT = 0x20021U,
	EXIT_REASON_STEP_COMPLETE = 0x20022U,
	EXIT_REASON_RUNTIME_ERROR_UNKNOWN = 0x20023U,
	EXIT_REASON_INTERNAL_ERROR = 0x20024U,
	EXIT_REASON_USER_INTERRUPTION = 0x20025U,
	EXIT_REASON_APPLICATION_EXIT = 0x20026U,
	EXIT_REASON_STACK_OVERFLOW = 0x20027U,
	EXIT_REASON_DIVIDE_BY_ZERO = 0x20028U,
	EXIT_REASON_OS_SPECIFIC = 0x20029U,
} semihosting_exit_reason_e;

#endif /* TARGET_SEMIHOSTING_INTERNAL_H */
