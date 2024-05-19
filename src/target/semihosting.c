/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2012-2020 Black Sphere Technologies Ltd.
 * Copyright (C) 2022-2024 1BitSquared <info@1bitsquared.com>
 * Written by Rachel Mant <git@dragonmux.network>
 * Contains source and ideas from Gareth McMullin <gareth@blacksphere.co.nz>
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

/*
 * This file implements support for the ARM-defined semihosting interface for
 * target to debugger service syscalls
 *
 * References:
 * DUI0471 - ARM Compiler Software DEvelopment Guide Version 5.06 (semihosting v1)
 *   https://developer.arm.com/documentation/dui0471/m/what-is-semihosting-
 * ARM Architecture ABI: Semihosting v2
 *   https://developer.arm.com/documentation/100863/latest/ ->
 *   https://github.com/ARM-software/abi-aa/blob/main/semihosting/semihosting.rst
 *
 * This implementation uses GDB's File I/O upcalls in the firmware and for stdio
 * to implement the semihosted syscall utilities, and uses native syscalls otherwise
 * when built as BMDA.
 *
 * Additionally we simulate two special files - :tt for the stdio facilities, and
 * :semihosting-features so the firmware can determine what Semihosting v2 extensions
 * this implementation supports. More on this latter part is noted below.
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "gdb_main.h"
#include "gdb_packet.h"
#include "hex_utils.h"
#include "semihosting.h"
#include "semihosting_internal.h"
#include "buffer_utils.h"
#include "timeofday.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#if PC_HOSTED == 1
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <fcntl.h>
#ifdef _WIN32
#include <io.h>
#define O_BINARY _O_BINARY
#define O_NOCTTY 0
#else
#define O_BINARY 0
#endif
#endif

/* This stores the current SYS_CLOCK epoch relative to the values from SYS_TIME */
uint32_t semihosting_wallclock_epoch = UINT32_MAX;
/* This stores the current :semihosting-features "file" access offset */
static uint8_t semihosting_features_offset = 0U;

/*
 * "SHFB" is the magic number header for the :semihosting-features "file"
 * Following that comes a byte of feature bits:
 * - bit 0 defines if we support extended exit
 * - bit 1 defines if we support both stdout and stderr via :tt
 * Given we support both, we set this to 0b00000011
 */
#define SEMIHOSTING_FEATURES_LENGTH 5U
static const char semihosting_features[SEMIHOSTING_FEATURES_LENGTH] = {'S', 'H', 'F', 'B', '\x03'};

static const char semihosting_tempname_template[] = "tempAA.tmp";
#define SEMIHOSTING_TEMPNAME_LENGTH ARRAY_LENGTH(semihosting_tempname_template)

#if ENABLE_DEBUG == 1
const char *const semihosting_names[] = {
	"",
	"SYS_OPEN",
	"SYS_CLOSE",
	"SYS_WRITEC",
	"SYS_WRITE0",
	"SYS_WRITE",
	"SYS_READ",
	"SYS_READC",
	"SYS_ISERROR",
	"SYS_ISTTY",
	"SYS_SEEK",
	"0x0b",
	"SYS_FLEN",
	"SYS_TMPNAM",
	"SYS_REMOVE",
	"SYS_RENAME",
	"SYS_CLOCK",
	"SYS_TIME",
	"SYS_SYSTEM",
	"SYS_ERRNO",
	"0x14",
	"SYS_GET_CMDLINE",
	"SYS_HEAPINFO",
	"0x17",
	[SEMIHOSTING_SYS_EXIT] = "SYS_EXIT",
	/* 7 reserved */
	[SEMIHOSTING_SYS_EXIT_EXTENDED] = "SYS_EXIT_EXTENDED",
	/* 15 reserved */
	[SEMIHOSTING_SYS_ELAPSED] = "SYS_ELAPSED",
	[SEMIHOSTING_SYS_TICKFREQ] = "SYS_TICKFREQ",
	"",
};
#endif

#if PC_HOSTED == 1
static semihosting_errno_e semihosting_errno(void);
#endif

int32_t semihosting_reply(target_controller_s *const tc, char *const pbuf)
{
	/*
	 * File-I/O Remote Protocol Extension
	 * See https://sourceware.org/gdb/onlinedocs/gdb/Protocol-Basics.html#Protocol-Basics
	 *
	 * This handles the F Reply Packet, sent by GDB after handling the File-I/O Request Packet.
	 *
	 * The F reply packet consists of the following:
	 *
	 * - retcode, the return code of the system call as hexadecimal value.
	 * - errno, the errno set by the call, in protocol-specific representation.
	 * 		Can be omitted if the call was successful.
	 * - Ctrl-C flag, sent only if user requested a break.
	 * 		In this case, errno must be sent as well, even if the call was successful.
	 * 		The Ctrl-C flag itself consists of the character ‘C’:
	 */

	uint32_t retcode = 0;
	uint32_t gdb_errno = 0;
	const char *rest = NULL;

	/* This function will handle '-' preceding the return code and correctly negate the result. */
	if (!read_hex32(pbuf, &rest, &retcode, READ_HEX_NO_FOLLOW)) {
		/*
		 * There is no retcode in the packet, so what do?
		 * FIXME: how do we properly handle this?
		 */
		tc->interrupted = false;
		tc->gdb_errno = TARGET_EUNKNOWN;
		return -1;
	}

	tc->gdb_errno = TARGET_SUCCESS;

	/* If the call was successful the errno may be omitted */
	if (rest[0] == ',' && read_hex32(rest + 1, &rest, &gdb_errno, READ_HEX_NO_FOLLOW)) {
		tc->gdb_errno = gdb_errno;
		/* If break is requested */
		if (rest[0] == ',')
			tc->interrupted = rest[1] == 'C';
	}

	return retcode;
}

static int32_t semihosting_get_gdb_response(target_controller_s *const tc)
{
	char *const packet_buffer = gdb_packet_buffer();
	/* Still have to service normal 'X'/'m'-packets */
	while (true) {
		/* Get back the next packet to process and have the main loop handle it */
		const size_t size = gdb_getpacket(packet_buffer, GDB_PACKET_BUFFER_SIZE);
		/* If this was an escape packet (or gdb_if reports link closed), fail the call */
		if (size == 1U && packet_buffer[0] == '\x04')
			return -1;
		/* 
		 * If this was an F-packet, we are done waiting.
		 * Check before gdb_main_loop as it may clobber the packet buffer.
		 */
		const bool done = packet_buffer[0] == 'F';
		const int32_t result = gdb_main_loop(tc, packet_buffer, GDB_PACKET_BUFFER_SIZE, size, true);
		if (done)
			return result;
	}
}

/* Interface to host system calls */
static int32_t semihosting_remote_read(
	target_s *const target, const int32_t fd, const target_addr_t buf_taddr, const uint32_t count)
{
#if PC_HOSTED == 1
	if ((target->stdout_redirected && fd == STDIN_FILENO) || fd > STDERR_FILENO) {
		uint8_t *const buf = malloc(count);
		if (buf == NULL)
			return -1;
		const ssize_t result = read(fd, buf, count);
		target->tc->gdb_errno = semihosting_errno();
		target_mem32_write(target, buf_taddr, buf, count);
		free(buf);
		if (target_check_error(target))
			return -1;
		return result;
	}
#endif
	gdb_putpacket_f("Fread,%08X,%08" PRIX32 ",%08" PRIX32, (unsigned)fd, buf_taddr, count);
	return semihosting_get_gdb_response(target->tc);
}

/* Interface to host system calls */
static int32_t semihosting_remote_write(
	target_s *const target, const int32_t fd, const target_addr_t buf_taddr, const uint32_t count)
{
#if PC_HOSTED == 1
	if (fd > STDERR_FILENO) {
		uint8_t *const buf = malloc(count);
		if (buf == NULL)
			return -1;
		target_mem32_read(target, buf, buf_taddr, count);
		if (target_check_error(target)) {
			free(buf);
			return -1;
		}
		const int32_t result = write(fd, buf, count);
		target->tc->gdb_errno = semihosting_errno();
		free(buf);
		return result;
	}
#endif

	if (target->stdout_redirected && (fd == STDOUT_FILENO || fd == STDERR_FILENO)) {
		uint8_t buffer[STDOUT_READ_BUF_SIZE];
		for (size_t offset = 0; offset < count; offset += STDOUT_READ_BUF_SIZE) {
			const size_t amount = MIN(count - offset, STDOUT_READ_BUF_SIZE);
			target_mem32_read(target, buffer, buf_taddr, amount);
#if PC_HOSTED == 0
			debug_serial_send_stdout(buffer, amount);
#else
			const ssize_t result = write(fd, buffer, amount);
			if (result == -1) {
				target->tc->gdb_errno = semihosting_errno();
				return offset;
			}
#endif
		}
		return (int32_t)count;
	}

	gdb_putpacket_f("Fwrite,%08X,%08" PRIX32 ",%08" PRIX32, (unsigned)fd, buf_taddr, count);
	return semihosting_get_gdb_response(target->tc);
}

#if PC_HOSTED == 1
/*
 * Convert an errno value from a syscall into its GDB-compat target errno equivalent
 *
 * NB: Must be called immediately after the syscall that might generate a value.
 * No functions or actions may be performed between these two points.
 */
static semihosting_errno_e semihosting_errno(void)
{
	const int32_t error = errno;
	switch (error) {
	case 0:
		return TARGET_SUCCESS;
	case EPERM:
		return TARGET_EPERM;
	case ENOENT:
		return TARGET_ENOENT;
	case EINTR:
		return TARGET_EINTR;
	case EIO:
		return TARGET_EIO;
	case EBADF:
		return TARGET_EBADF;
	case EACCES:
		return TARGET_EACCES;
	case EFAULT:
		return TARGET_EFAULT;
	case EBUSY:
		return TARGET_EBUSY;
	case EEXIST:
		return TARGET_EEXIST;
	case ENODEV:
		return TARGET_ENODEV;
	case ENOTDIR:
		return TARGET_ENOTDIR;
	case EISDIR:
		return TARGET_EISDIR;
	case EINVAL:
		return TARGET_EINVAL;
	case ENFILE:
		return TARGET_ENFILE;
	case EMFILE:
		return TARGET_EMFILE;
	case EFBIG:
		return TARGET_EFBIG;
	case ESPIPE:
		return TARGET_ESPIPE;
	case EROFS:
		return TARGET_EROFS;
	case ENOSYS:
		return TARGET_ENOSYS;
	case ENAMETOOLONG:
		return TARGET_ENAMETOOLONG;
	}
	return TARGET_EUNKNOWN;
}

const char *semihosting_read_string(
	target_s *const target, const target_addr_t string_taddr, const uint32_t string_length)
{
	if (string_taddr == TARGET_NULL || string_length == 0)
		return NULL;
	char *string = malloc(string_length + 1U);
	if (string == NULL)
		return NULL;
	target_mem32_read(target, string, string_taddr, string_length + 1U);
	if (target_check_error(target)) {
		free(string);
		return NULL;
	}
	string[string_length] = '\0';
	return string;
}
#endif

int32_t semihosting_open(target_s *const target, const semihosting_s *const request)
{
	const target_addr_t file_name_taddr = request->params[0];
	const uint32_t file_name_length = request->params[2];

	/*
	 * Translation table of fopen() modes to GDB-compatible open flags
	 * See DUI0471C, Table 8-3
	 */
	static const uint16_t open_mode_flags[] = {
		OPEN_MODE_RDONLY,                                      /* r, rb */
		OPEN_MODE_RDWR,                                        /* r+, r+b */
		OPEN_MODE_WRONLY | OPEN_MODE_CREAT | OPEN_MODE_TRUNC,  /* w, wb */
		OPEN_MODE_RDWR | OPEN_MODE_CREAT | OPEN_MODE_TRUNC,    /* w+, w+b */
		OPEN_MODE_WRONLY | OPEN_MODE_CREAT | OPEN_MODE_APPEND, /* a, ab */
		OPEN_MODE_RDWR | OPEN_MODE_CREAT | OPEN_MODE_APPEND,   /* a+, a+b */
	};
	const uint32_t open_mode = open_mode_flags[request->params[1] >> 1U];

	if (file_name_length <= 4U) {
		char file_name[4U];
		target_mem32_read(target, file_name, file_name_taddr, file_name_length + 1U);

		/* Handle requests for console I/O */
		if (!strncmp(file_name, ":tt", 4U)) {
			int32_t result = -1;
			if (open_mode == OPEN_MODE_RDONLY)
				result = STDIN_FILENO;
			else if (open_mode & OPEN_MODE_TRUNC)
				result = STDOUT_FILENO;
			else
				result = STDERR_FILENO;
			return result + 1;
		}
	} else if (file_name_length <= 22U) {
		char file_name[22U];
		target_mem32_read(target, file_name, file_name_taddr, file_name_length + 1U);

		/* Handle a request for the features "file" */
		if (!strncmp(file_name, ":semihosting-features", 22U)) {
			/* Only let the firmware "open" the file if they ask for it in read-only mode */
			if (open_mode == OPEN_MODE_RDONLY) {
				semihosting_features_offset = 0U;
				return INT32_MAX;
			}
			return -1;
		}
	}

#if PC_HOSTED == 1
	const char *const file_name = semihosting_read_string(target, file_name_taddr, file_name_length);
	if (file_name == NULL)
		return -1;

	/* Translation table of fopen() modes to libc-native open() mode flags */
	static const int32_t native_open_mode_flags[] = {
		O_RDONLY,                      /* r, rb */
		O_RDWR,                        /* r+, r+b */
		O_WRONLY | O_CREAT | O_TRUNC,  /* w, wb */
		O_RDWR | O_CREAT | O_TRUNC,    /* w+, w+b */
		O_WRONLY | O_CREAT | O_APPEND, /* a, ab */
		O_RDWR | O_CREAT | O_APPEND,   /* a+, a+b */
	};
	int32_t native_open_mode = native_open_mode_flags[request->params[1] >> 1U];
	if (request->params[1] & 1U)
		native_open_mode |= O_BINARY;

	const int32_t result = open(file_name, native_open_mode | O_NOCTTY, 0644);
	target->tc->gdb_errno = semihosting_errno();
	free((void *)file_name);
#else
	gdb_putpacket_f("Fopen,%08" PRIX32 "/%08" PRIX32 ",%08" PRIX32 ",%08X", file_name_taddr, file_name_length + 1U,
		open_mode, 0644U);
	const int32_t result = semihosting_get_gdb_response(target->tc);
#endif
	if (result != -1)
		return result + 1;
	return result;
}

int32_t semihosting_close(target_s *const target, const semihosting_s *const request)
{
	const int32_t fd = request->params[0] - 1;
	/*
	 * If the file descriptor requested is one of the special ones from ":tt" operations,
	 * or from "":semihosting-features", do nothing
	 */
	if (fd == STDIN_FILENO || fd == STDOUT_FILENO || fd == STDERR_FILENO || request->params[0] == INT32_MAX)
		return 0;
		/* Otherwise close the descriptor returned by semihosting_open() */
#if PC_HOSTED == 1
	const int32_t result = close(fd);
	target->tc->gdb_errno = semihosting_errno();
	return result;
#else
	gdb_putpacket_f("Fclose,%08X", (unsigned)fd);
	return semihosting_get_gdb_response(target->tc);
#endif
}

int32_t semihosting_read(target_s *const target, const semihosting_s *const request)
{
	const target_addr_t buf_taddr = request->params[1];
	const uint32_t buf_len = request->params[2];
#if PC_HOSTED == 1
	if (buf_len == 0)
		return 0;
#endif

	/* Check if this is a request to read from the :semihosting-features "file" */
	if (request->params[0] == INT32_MAX) {
		/* Clamp the requested amount to the amount we actually have left */
		const uint32_t amount = MIN(buf_len, SEMIHOSTING_FEATURES_LENGTH - semihosting_features_offset);
		/* Copy the chunk requested to the target, updating our internal offset */
		target_mem32_write(target, buf_taddr, semihosting_features + semihosting_features_offset, amount);
		semihosting_features_offset += amount;
		/* Return how much was left from what we transferred */
		return buf_len - amount;
	}

	const int32_t fd = request->params[0] - 1;
	const int32_t result = semihosting_remote_read(target, fd, buf_taddr, buf_len);
	if (result >= 0)
		return buf_len - result;
	return result;
}

int32_t semihosting_write(target_s *const target, const semihosting_s *const request)
{
	/* Write requests to the :semihosting-features "file" always fail */
	if (request->params[0] == INT32_MAX)
		return -1;

	const int32_t fd = request->params[0] - 1;
	const target_addr_t buf_taddr = request->params[1];
	const uint32_t buf_len = request->params[2];
#if PC_HOSTED == 1
	if (buf_len == 0)
		return 0;
#endif

	const int32_t result = semihosting_remote_write(target, fd, buf_taddr, buf_len);
	if (result >= 0)
		return buf_len - result;
	return result;
}

int32_t semihosting_writec(target_s *const target, const semihosting_s *const request)
{
	const target_addr_t ch_taddr = request->r1;
	(void)semihosting_remote_write(target, STDOUT_FILENO, ch_taddr, 1);
	return 0;
}

int32_t semihosting_write0(target_s *const target, const semihosting_s *const request)
{
	const target_addr_t str_begin_taddr = request->r1;
	target_addr_t str_end_taddr;
	for (str_end_taddr = str_begin_taddr; target_mem32_read8(target, str_end_taddr) != 0; ++str_end_taddr) {
		if (target_check_error(target))
			break;
	}
	const int32_t len = str_end_taddr - str_begin_taddr;
	if (len >= 0) {
		const int32_t result = semihosting_remote_write(target, STDOUT_FILENO, str_begin_taddr, len);
		if (result != len)
			return -1;
	}
	return 0;
}

int32_t semihosting_isatty(target_s *const target, const semihosting_s *const request)
{
	const int32_t fd = request->params[0] - 1;
#if PC_HOSTED == 1
	if (!target->stdout_redirected || fd > STDERR_FILENO) {
		const int32_t result = isatty(fd);
		target->tc->gdb_errno = semihosting_errno();
		return result;
	}
#endif
	gdb_putpacket_f("Fisatty,%08X", (unsigned)fd);
	return semihosting_get_gdb_response(target->tc);
}

int32_t semihosting_seek(target_s *const target, const semihosting_s *const request)
{
	const int32_t fd = request->params[0] - 1;
	const off_t offset = request->params[1];
	/* Check if this is a request to seek in the :semihosting-features "file" */
	if (request->params[0] == INT32_MAX) {
		if (offset >= 0 && offset < (off_t)SEMIHOSTING_FEATURES_LENGTH)
			semihosting_features_offset = (uint8_t)offset;
		else
			semihosting_features_offset = SEMIHOSTING_FEATURES_LENGTH;
		return 0;
	}
#if PC_HOSTED == 1
	if (!target->stdout_redirected || fd > STDERR_FILENO) {
		const int32_t result = lseek(fd, offset, SEEK_SET) == offset ? 0 : -1;
		target->tc->gdb_errno = semihosting_errno();
		return result;
	}
#endif
	gdb_putpacket_f("Flseek,%08X,%08lX,%08X", (unsigned)fd, (unsigned long)offset, SEEK_MODE_SET);
	return semihosting_get_gdb_response(target->tc) == offset ? 0 : -1;
}

int32_t semihosting_rename(target_s *const target, const semihosting_s *const request)
{
#if PC_HOSTED == 1
	const char *const old_file_name = semihosting_read_string(target, request->params[0], request->params[1]);
	if (old_file_name == NULL)
		return -1;
	const char *const new_file_name = semihosting_read_string(target, request->params[2], request->params[3]);
	if (new_file_name == NULL) {
		free((void *)old_file_name);
		return -1;
	}
	const int32_t result = rename(old_file_name, new_file_name);
	target->tc->gdb_errno = semihosting_errno();
	free((void *)old_file_name);
	free((void *)new_file_name);
	return result;
#else
	gdb_putpacket_f("Frename,%08" PRIX32 "/%08" PRIX32 ",%08" PRIX32 "/%08" PRIX32, request->params[0],
		request->params[1] + 1U, request->params[2], request->params[3] + 1U);
	return semihosting_get_gdb_response(target->tc);
#endif
}

int32_t semihosting_remove(target_s *const target, const semihosting_s *const request)
{
#if PC_HOSTED == 1
	const char *const file_name = semihosting_read_string(target, request->params[0], request->params[1]);
	if (file_name == NULL)
		return -1;
	const int32_t result = remove(file_name);
	target->tc->gdb_errno = semihosting_errno();
	free((void *)file_name);
	return result;
#else
	gdb_putpacket_f("Funlink,%08" PRIX32 "/%08" PRIX32, request->params[0], request->params[1] + 1U);
	return semihosting_get_gdb_response(target->tc);
#endif
}

int32_t semihosting_system(target_s *const target, const semihosting_s *const request)
{
	/* NB: Before use first enable system calls with the following gdb command: 'set remote system-call-allowed 1' */
	gdb_putpacket_f("Fsystem,%08" PRIX32 "/%08" PRIX32, request->params[0], request->params[1] + 1U);
	return semihosting_get_gdb_response(target->tc);
}

int32_t semihosting_file_length(target_s *const target, const semihosting_s *const request)
{
	/* Check if this is a request for the length of the :semihosting-features "file" */
	if (request->params[0] == INT32_MAX)
		return SEMIHOSTING_FEATURES_LENGTH;

	const int32_t fd = request->params[0] - 1;
#if PC_HOSTED == 1
	if (!target->stdout_redirected || fd > STDERR_FILENO) {
		struct stat file_stat;
		const bool result = fstat(fd, &file_stat) == 0;
		target->tc->gdb_errno = semihosting_errno();
		if (!result || file_stat.st_size > INT32_MAX)
			return -1;
		return file_stat.st_size;
	}
#endif
	/*
	 * Provide space for receiving a fio_stat structure from GDB
	 * defined as per GDB's gdbsupport/fileio.h
	 * Note that the structure's fields are in big endian.
	 * The field we're interested in (fst_size) starts at uint32_t 7
	 * (the upper half of the file size), and includes uint32_t 8.
	 */
	uint32_t file_stat[16];
	/* Tell the target layer to use this buffer for the IO */
	target->target_options |= TOPT_IN_SEMIHOSTING_SYSCALL;
	target->tc->semihosting_buffer_ptr = file_stat;
	target->tc->semihosting_buffer_len = sizeof(file_stat);
	/* Call GDB and ask for the file descriptor's stat info */
	gdb_putpacket_f("Ffstat,%X,%08" PRIX32, (unsigned)fd, target->ram->start);
	const int32_t stat_result = semihosting_get_gdb_response(target->tc);
	target->target_options &= ~TOPT_IN_SEMIHOSTING_SYSCALL;
	/* Extract the lower half of the file size from the buffer */
	const uint32_t result = read_be4((uint8_t *)file_stat, sizeof(uint32_t) * 8U);
	/* Check if the GDB remote fstat() failed or if the size was more than 2GiB */
	if (stat_result || file_stat[7] != 0 || (result & 0x80000000U) != 0)
		return -1;
	return result;
}

#if PC_HOSTED == 0
semihosting_time_s semihosting_get_time(target_s *const target)
{
	/* Provide space for reciving a fio_timeval structure from GDB */
	uint8_t time_value[12U];
	/* Tell the target layer to use this buffer for the IO */
	target->target_options |= TOPT_IN_SEMIHOSTING_SYSCALL;
	target->tc->semihosting_buffer_ptr = time_value;
	target->tc->semihosting_buffer_len = sizeof(time_value);
	/* Call GDB and ask for the current time using gettimeofday() */
	gdb_putpacket_f("Fgettimeofday,%08" PRIX32 ",%08" PRIX32, target->ram->start, (target_addr_t)NULL);
	const int32_t result = semihosting_get_gdb_response(target->tc);
	target->target_options &= ~TOPT_IN_SEMIHOSTING_SYSCALL;
	/* Check if the GDB remote gettimeofday() failed */
	if (result)
		return (semihosting_time_s){UINT64_MAX, UINT32_MAX};
	/* Convert the resulting time value from big endian */
	return (semihosting_time_s){read_be8(time_value, 4), read_be4(time_value, 0)};
}
#endif

int32_t semihosting_clock(target_s *const target)
{
#if PC_HOSTED == 1
	/* NB: Can't use clock() because that would give cpu time of BMDA process */
	struct timeval current_time;
	/* Get the current time from the host */
	const bool result = gettimeofday(&current_time, NULL) == 0;
	target->tc->gdb_errno = semihosting_errno();
	if (!result)
		return -1;
	/* Extract the time value components */
	const uint32_t seconds = current_time.tv_sec;
	const uint32_t microseconds = (uint32_t)current_time.tv_usec;
#else
	/* Get the current time from the host */
	const semihosting_time_s current_time = semihosting_get_time(target);
	if (current_time.seconds == UINT32_MAX && current_time.microseconds == UINT64_MAX)
		return (int32_t)current_time.seconds;
	const uint32_t seconds = current_time.seconds;
	const uint32_t microseconds = (uint32_t)current_time.microseconds;
#endif
	/*
	 * Convert the resulting time to centiseconds (hundredths of a second)
	 * NB: At the potential cost of some precision, the microseconds value has been
	 *   cast down to a uint32_t to avoid doing a 64-bit division in the firmware.
	 */
	uint32_t centiseconds = (seconds * 100U) + (microseconds / 10000U);
	/* If this is the first request for the wallclock since the target started, consider it the start */
	if (semihosting_wallclock_epoch > centiseconds)
		semihosting_wallclock_epoch = centiseconds;
	centiseconds -= semihosting_wallclock_epoch;
	/* Truncate the result back to a positive 32-bit integer */
	return centiseconds & 0x7fffffffU;
}

int32_t semihosting_time(target_s *const target)
{
#if PC_HOSTED == 1
	/* Get the current time in seconds from the host */
	int32_t result = (int32_t)time(NULL);
	target->tc->gdb_errno = semihosting_errno();
	return result;
#else
	/* Get the current time from the host */
	const semihosting_time_s current_time = semihosting_get_time(target);
	/*
	 * If the operation failed, the seconds member is already UINT32_MAX which is `-1`,
	 * so just return it without validation having cast it to an int32_t
	 */
	return (int32_t)current_time.seconds;
#endif
}

int32_t semihosting_elapsed(target_s *const target, const semihosting_s *const request)
{
	/* Extract where the write should occur to */
	const target_addr_t block_taddr = request->r1;
	/*
	 * Acquire platform ticks (even if uint32_t ATM).
	 * BMP: SysTicks. This is faster (on-probe) than talking to GDB.
	 * BMDA: gettimeofday() as milliseconds.
	 */
	const uint64_t elapsed = platform_time_ms();
	/* Write the elapsed ticks to the target as a pair of uint32_t in LE order per ABI */
	return target_mem32_write(target, block_taddr, &elapsed, sizeof(elapsed)) ? -1 : 0;
}

int32_t semihosting_readc(target_s *const target)
{
	/* Define space for a character */
	uint8_t ch = '?';

	/* Tell the target layer to write to this character as a buffer for the IO */
	target->target_options |= TOPT_IN_SEMIHOSTING_SYSCALL;
	target->tc->semihosting_buffer_ptr = &ch;
	target->tc->semihosting_buffer_len = 1U;
	/* Call GDB and ask for a character using read(STDIN_FILENO) */
	const int32_t result = semihosting_remote_read(target, STDIN_FILENO, target->ram->start, 1U);
	target->target_options &= ~TOPT_IN_SEMIHOSTING_SYSCALL;
	/* Check if the GDB remote read() */
	if (result != 1)
		return -1;
	/* Extract the character read from the buffer */
	return ch;
}

int32_t semihosting_exit(target_s *const target, const semihosting_exit_reason_e reason, const uint32_t status_code)
{
	if (reason == EXIT_REASON_APPLICATION_EXIT)
		tc_printf(target, "exit(%" PRIu32 ")\n", status_code);
	else
		tc_printf(target, "Exception trapped: %x (%" PRIu32 ")\n", reason, status_code);
	target_halt_resume(target, true);
	return 0;
}

int32_t semihosting_get_command_line(target_s *const target, const semihosting_s *const request)
{
	/* Extract the location of the result buffer and its length */
	const target_addr_t buffer_taddr = request->params[0];
	const size_t buffer_length = request->params[1];
	/* Figure out how long the command line string is */
	const size_t command_line_length = strlen(target->cmdline) + 1U;
	/* Check that we won't exceed the target buffer with the write */
	if (command_line_length > buffer_length ||
		/* Try to write the data to the target along with the actual length value */
		target_mem32_write(target, buffer_taddr, target->cmdline, command_line_length))
		return -1;
	return target_mem32_write32(target, request->r1 + 4U, command_line_length) ? -1 : 0;
}

int32_t semihosting_is_error(const semihosting_errno_e code)
{
	/* Convert a FileIO-domain errno into whether it indicates an error has occured or not */
	const bool is_error = code == TARGET_EPERM || code == TARGET_ENOENT || code == TARGET_EINTR || code == TARGET_EIO ||
		code == TARGET_EBADF || code == TARGET_EACCES || code == TARGET_EFAULT || code == TARGET_EBUSY ||
		code == TARGET_EEXIST || code == TARGET_ENODEV || code == TARGET_ENOTDIR || code == TARGET_EISDIR ||
		code == TARGET_EINVAL || code == TARGET_ENFILE || code == TARGET_EMFILE || code == TARGET_EFBIG ||
		code == TARGET_ENOSPC || code == TARGET_ESPIPE || code == TARGET_EROFS || code == TARGET_ENOSYS ||
		code == TARGET_ENAMETOOLONG || code == TARGET_EUNKNOWN;
	/* The Semihosting ABI specifies any non-zero response s a truthy one, so just return the bool as-is */
	return is_error;
}

int32_t semihosting_heap_info(target_s *const target, const semihosting_s *const request)
{
	/* Extract where the write should occur to */
	const target_addr_t block_taddr = request->r1;
	/*
	 * Write the heapinfo block to the target
	 * See https://github.com/ARM-software/abi-aa/blob/main/semihosting/semihosting.rst#69sys_heapinfo-0x16
	 * for more information on the layout of is block and the significance of how this is structured
	 */
	return target_mem32_write(target, block_taddr, target->heapinfo, sizeof(target->heapinfo)) ? -1 : 0;
}

int32_t semihosting_temp_name(target_s *const target, const semihosting_s *const request)
{
	/* Pull out the value to format into the result string (clamping it into the range 0-255) */
	const uint8_t target_id = request->params[1];
	/* Format the new ID into the file name string */
	char file_name[SEMIHOSTING_TEMPNAME_LENGTH];
	memcpy(file_name, semihosting_tempname_template, SEMIHOSTING_TEMPNAME_LENGTH);
	file_name[4] += target_id >> 4U;
	file_name[5] += target_id & 0xffU;
	/* Now extract and check that we have enough space to write the result back to */
	const target_addr_t buffer_taddr = request->params[0];
	const size_t buffer_length = request->params[2];
	if (buffer_length < sizeof(file_name))
		return -1;
	/* If we have enough space, attempt the write back */
	return target_mem32_write(target, buffer_taddr, file_name, SEMIHOSTING_TEMPNAME_LENGTH) ? -1 : 0;
}

int32_t semihosting_handle_request(target_s *const target, const semihosting_s *const request, const uint32_t syscall)
{
	switch (syscall) {
	case SEMIHOSTING_SYS_OPEN:
		return semihosting_open(target, request);

	case SEMIHOSTING_SYS_CLOSE:
		return semihosting_close(target, request);

	case SEMIHOSTING_SYS_READ:
		return semihosting_read(target, request);

	case SEMIHOSTING_SYS_WRITE:
		return semihosting_write(target, request);

	case SEMIHOSTING_SYS_WRITEC:
		return semihosting_writec(target, request);

	case SEMIHOSTING_SYS_WRITE0:
		return semihosting_write0(target, request);

	case SEMIHOSTING_SYS_ISTTY:
		return semihosting_isatty(target, request);

	case SEMIHOSTING_SYS_SEEK:
		return semihosting_seek(target, request);

	case SEMIHOSTING_SYS_RENAME:
		return semihosting_rename(target, request);

	case SEMIHOSTING_SYS_REMOVE:
		return semihosting_remove(target, request);

	case SEMIHOSTING_SYS_SYSTEM:
		return semihosting_system(target, request);

	case SEMIHOSTING_SYS_FLEN:
		return semihosting_file_length(target, request);

	case SEMIHOSTING_SYS_CLOCK:
		return semihosting_clock(target);

	case SEMIHOSTING_SYS_TIME:
		return semihosting_time(target);

	case SEMIHOSTING_SYS_READC:
		return semihosting_readc(target);

	case SEMIHOSTING_SYS_ERRNO:
		/* Return the last errno we got from GDB */
		return target->tc->gdb_errno;

	case SEMIHOSTING_SYS_EXIT:
		return semihosting_exit(target, request->r1, 0);

	case SEMIHOSTING_SYS_EXIT_EXTENDED:
		return semihosting_exit(target, request->params[0], request->params[1]);

	case SEMIHOSTING_SYS_GET_CMDLINE:
		return semihosting_get_command_line(target, request);

	case SEMIHOSTING_SYS_ISERROR:
		return semihosting_is_error(request->params[0]);

	case SEMIHOSTING_SYS_HEAPINFO:
		return semihosting_heap_info(target, request);

	case SEMIHOSTING_SYS_TMPNAM:
		return semihosting_temp_name(target, request);

	case SEMIHOSTING_SYS_ELAPSED:
		return semihosting_elapsed(target, request);

	case SEMIHOSTING_SYS_TICKFREQ:
		/* 1000 Hz SysTick, or BMDA "precision". Servicing breakpoints over SWD is not fast. */
		return SYSTICKHZ;

	default:
		return -1;
	}
}

int32_t semihosting_request(target_s *const target, const uint32_t syscall, const uint32_t r1)
{
	/* Reset the interruption state so we can tell if it was this request that was interrupted */
	target->tc->interrupted = false;

	/* Set up the request block appropriately */
	semihosting_s request = {r1, {0U}};
	if (syscall != SEMIHOSTING_SYS_EXIT)
		target_mem32_read(target, request.params, r1, sizeof(request.params));

#if ENABLE_DEBUG == 1
	const char *syscall_descr = NULL;
	if (syscall < ARRAY_LENGTH(semihosting_names))
		syscall_descr = semihosting_names[syscall];
	if (syscall_descr == NULL)
		syscall_descr = "";

	DEBUG_INFO("syscall %12s (%" PRIx32 " %" PRIx32 " %" PRIx32 " %" PRIx32 ")\n", syscall_descr, request.params[0],
		request.params[1], request.params[2], request.params[3]);
#endif

#if PC_HOSTED == 1
	if (syscall != SEMIHOSTING_SYS_ERRNO)
		target->tc->gdb_errno = TARGET_SUCCESS;
#endif
	return semihosting_handle_request(target, &request, syscall);
}
