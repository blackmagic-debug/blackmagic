/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2012-2020 Black Sphere Technologies Ltd.
 * Copyright (C) 2022-2023 1BitSquared <info@1bitsquared.com>
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

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "gdb_main.h"
#include "gdb_packet.h"
#include "cortexm.h"
#include "semihosting.h"
#include "semihosting_internal.h"
#include "buffer_utils.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#if PC_HOSTED == 1
/*
 * pc-hosted semihosting does keyboard, file and screen i/o on the system
 * where blackmagic_hosted runs, using linux system calls.
 * semihosting in the probe does keyboard, file and screen i/o on the system
 * where gdb runs, using gdb file i/o calls.
 */

#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <fcntl.h>
#endif

static uint32_t time0_sec = UINT32_MAX; /* sys_clock time origin */

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

#if PC_HOSTED == 0
int semihosting_reply(target_controller_s *const tc, char *const pbuf, const int len)
{
	(void)len;

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

	const bool retcode_is_negative = pbuf[1U] == '-';

	unsigned int retcode = 0;
	unsigned int gdb_errno = 0;
	char ctrl_c_flag = '\0';
	const int items = sscanf(pbuf + (retcode_is_negative ? 2U : 1U), "%x,%x,%c", &retcode, &gdb_errno, &ctrl_c_flag);

	if (items < 1) {
		/*
		 * Something went wrong with the sscanf or the packet format, avoid UB
		 * FIXME: how do we properly handle this?
		 */
		tc->interrupted = false;
		tc->gdb_errno = TARGET_EUNKNOWN;
		return -1;
	}

	/* If the call was successful the errno may be omitted */
	tc->gdb_errno = items >= 2 ? gdb_errno : 0;

	/* If break is requested */
	tc->interrupted = items == 3 && ctrl_c_flag == 'C';

	return retcode_is_negative ? -retcode : retcode;
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
		const int32_t result = gdb_main_loop(tc, packet_buffer, GDB_PACKET_BUFFER_SIZE, size, true);
		/* If this was an F-packet, we're done */
		if (packet_buffer[0] == 'F')
			return result;
	}
}

/* Interface to host system calls */
static int hostio_read(target_controller_s *tc, int fd, target_addr_t buf, unsigned int count)
{
	gdb_putpacket_f("Fread,%08X,%08" PRIX32 ",%08X", fd, buf, count);
	return semihosting_get_gdb_response(tc);
}

/* Interface to host system calls */
static int tc_write(target_s *target, int fd, target_addr_t buf, uint32_t count)
{
	if (target->stdout_redirected && (fd == STDOUT_FILENO || fd == STDERR_FILENO)) {
		while (count) {
			uint8_t tmp[STDOUT_READ_BUF_SIZE];
			size_t cnt = sizeof(tmp);
			if (cnt > count)
				cnt = count;
			target_mem_read(target, tmp, buf, cnt);
			debug_serial_send_stdout(tmp, cnt);
			count -= cnt;
			buf += cnt;
		}
		return 0;
	}

	gdb_putpacket_f("Fwrite,%08X,%08" PRIX32 ",%08" PRIX32, (unsigned)fd, buf, count);
	return semihosting_get_gdb_response(target->tc);
}

/* probe memory access functions */
static void probe_mem_read(
	target_s *target __attribute__((unused)), void *probe_dest, target_addr_t target_src, size_t len)
{
	uint8_t *dst = (uint8_t *)probe_dest;
	uint8_t *src = (uint8_t *)target_src;

	DEBUG_INFO("probe_mem_read\n");

	memcpy(dst, src, len);
}

static void probe_mem_write(
	target_s *target __attribute__((unused)), target_addr_t target_dest, const void *probe_src, size_t len)
{
	uint8_t *dst = (uint8_t *)target_dest;
	uint8_t *src = (uint8_t *)probe_src;

	DEBUG_INFO("probe_mem_write\n");
	memcpy(dst, src, len);
}
#endif

#if PC_HOSTED == 1
const char *semihosting_read_string(
	target_s *const target, const target_addr_t string_taddr, const uint32_t string_length)
{
	if (string_taddr == TARGET_NULL || string_length == 0)
		return NULL;
	char *string = malloc(string_length + 1U);
	if (string == NULL)
		return NULL;
	target_mem_read(target, string, string_taddr, string_length + 1U);
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
	 * Translation table of fopen modes to open() flags
	 * See DUI0471C, Table 8-3
	 */
	static const uint32_t open_mode_flags[] = {
#if PC_HOSTED == 1
		O_RDONLY,                      /* r, rb */
		O_RDWR,                        /* r+, r+b */
		O_WRONLY | O_CREAT | O_TRUNC,  /*w*/
		O_RDWR | O_CREAT | O_TRUNC,    /*w+*/
		O_WRONLY | O_CREAT | O_APPEND, /*a*/
		O_RDWR | O_CREAT | O_APPEND,   /*a+*/
#else
		TARGET_O_RDONLY,                                    /* r, rb */
		TARGET_O_RDWR,                                      /* r+, r+b */
		TARGET_O_WRONLY | TARGET_O_CREAT | TARGET_O_TRUNC,  /*w*/
		TARGET_O_RDWR | TARGET_O_CREAT | TARGET_O_TRUNC,    /*w+*/
		TARGET_O_WRONLY | TARGET_O_CREAT | TARGET_O_APPEND, /*a*/
		TARGET_O_RDWR | TARGET_O_CREAT | TARGET_O_APPEND,   /*a+*/
#endif
	};
	const uint32_t open_mode = open_mode_flags[request->params[1] >> 1U];

	char filename[4];
	target_mem_read(target, filename, file_name_taddr, sizeof(filename));

	/* Handle requests for console I/O */
	if (!strncmp(filename, ":tt", 4U)) {
		int32_t result = -1;
		if (open_mode == TARGET_O_RDONLY)
			result = STDIN_FILENO;
		else if (open_mode & TARGET_O_TRUNC)
			result = STDOUT_FILENO;
		else
			result = STDERR_FILENO;
		return result + 1;
	}

#if PC_HOSTED == 1
	const char *const file_name = semihosting_read_string(target, file_name_taddr, file_name_length);
	if (file_name == NULL)
		return -1;
	const int32_t result = open(file_name, open_mode, 0644);
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
#if PC_HOSTED == 1
	(void)target;
	return close(fd);
#else
	gdb_putpacket_f("Fclose,%08X", (unsigned)fd);
	return semihosting_get_gdb_response(target->tc);
#endif
}

int32_t semihosting_read(target_s *const target, const semihosting_s *const request)
{
	const target_addr_t buf_taddr = request->params[1];
	if (buf_taddr == TARGET_NULL)
		return -1;
	const uint32_t buf_len = request->params[2];
	if (buf_len == 0)
		return 0;
	const int32_t fd = request->params[0] - 1;

#if PC_HOSTED == 1
	uint8_t *const buf = malloc(buf_len);
	if (buf == NULL)
		return -1;
	const ssize_t result = read(fd, buf, buf_len);
	target_mem_write(target, buf_taddr, buf, buf_len);
	free(buf);
	if (target_check_error(target))
		return -1;
#else
	const int32_t result = hostio_read(target->tc, fd, buf_taddr, buf_len);
#endif
	if (result >= 0)
		return buf_len - result;
	return result;
}

int32_t semihosting_write(target_s *const target, const semihosting_s *const request)
{
	const int32_t fd = request->params[0] - 1;
	const target_addr_t buf_taddr = request->params[1];
	if (buf_taddr == TARGET_NULL)
		return -1;
	const uint32_t buf_len = request->params[2];
	if (buf_len == 0)
		return 0;

#if PC_HOSTED == 1
	uint8_t *const buf = malloc(buf_len);
	if (buf == NULL)
		return -1;
	target_mem_read(target, buf, buf_taddr, buf_len);
	if (target_check_error(target)) {
		free(buf);
		return -1;
	}
	const int32_t result = write(fd, buf, buf_len);
	free(buf);
#else
	const int32_t result = tc_write(target, fd, buf_taddr, buf_len);
#endif
	if (result >= 0)
		return buf_len - result;
	return result;
}

int32_t semihosting_writec(target_s *const target, const semihosting_s *const request)
{
	const target_addr_t ch_taddr = request->r1;
	if (ch_taddr == TARGET_NULL)
		return -1;
#if PC_HOSTED == 1
	const uint8_t ch = target_mem_read8(target, ch_taddr);
	if (target_check_error(target))
		return -1;
	fputc(ch, stderr);
	return 0;
#else
	return tc_write(target, STDERR_FILENO, ch_taddr, 1);
#endif
}

int32_t semihosting_write0(target_s *const target, const semihosting_s *const request)
{
	const target_addr_t str_begin_taddr = request->r1;
	if (str_begin_taddr == TARGET_NULL)
		return -1;
#if PC_HOSTED == 1
	for (target_addr_t char_taddr = str_begin_taddr; !target_check_error(target); ++char_taddr) {
		const uint8_t chr = target_mem_read8(target, char_taddr);
		if (chr == 0U)
			break;
		fputc(chr, stderr);
	}
#else
	target_addr_t str_end_taddr;
	for (str_end_taddr = str_begin_taddr; target_mem_read8(target, str_end_taddr) != 0; ++str_end_taddr) {
		if (target_check_error(target))
			break;
	}
	const int32_t len = str_end_taddr - str_begin_taddr;
	if (len >= 0) {
		const int32_t result = tc_write(target, STDERR_FILENO, str_begin_taddr, len);
		if (result != len)
			return -1;
	}
#endif
	return 0;
}

int32_t semihosting_isatty(target_s *const target, const semihosting_s *const request)
{
	const int32_t fd = request->params[0] - 1;
#if PC_HOSTED == 1
	(void)target;
	return isatty(fd);
#else
	gdb_putpacket_f("Fisatty,%08X", (unsigned)fd);
	return semihosting_get_gdb_response(target->tc);
#endif
}

int32_t semihosting_seek(target_s *const target, const semihosting_s *const request)
{
	const int32_t fd = request->params[0] - 1;
	const off_t offset = request->params[1];
#if PC_HOSTED == 1
	(void)target;
	return lseek(fd, offset, SEEK_SET) == offset ? 0 : -1;
#else
	gdb_putpacket_f("Flseek,%08X,%08lX,%08X", (unsigned)fd, (unsigned long)offset, TARGET_SEEK_SET);
	return semihosting_get_gdb_response(target->tc) == offset ? 0 : -1;
#endif
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
	free((void *)file_name);
	return result;
#else
	gdb_putpacket_f("Funlink,%08" PRIX32 "/%08" PRIX32, request->params[0], request->params[1] + 1U);
	return semihosting_get_gdb_response(target->tc);
#endif
}

int32_t semihosting_system(target_s *const target, const semihosting_s *const request)
{
#if PC_HOSTED == 1
	const char *cmd = semihosting_read_string(target, request->params[0], request->params[1]);
	if (cmd == NULL)
		return -1;
	const int32_t result = system(cmd);
	free((void *)cmd);
	return result;
#else
	/* NB: Before use first enable system calls with the following gdb command: 'set remote system-call-allowed 1' */
	gdb_putpacket_f("Fsystem,%08" PRIX32 "/%08" PRIX32, request->params[0], request->params[1] + 1U);
	return semihosting_get_gdb_response(target->tc);
#endif
}

int32_t semihosting_file_length(target_s *const target, const semihosting_s *const request)
{
	const int32_t fd = request->params[0] - 1;
#if PC_HOSTED == 1
	(void)target;
	struct stat file_stat;
	if (fstat(fd, &file_stat) != 0 || file_stat.st_size > INT32_MAX)
		return -1;
	return file_stat.st_size;
#else
	uint32_t fio_stat[16]; /* Same size as fio_stat in gdb/include/gdb/fileio.h */
	void (*saved_mem_read)(target_s *target, void *dest, target_addr_t src, size_t len);
	void (*saved_mem_write)(target_s *target, target_addr_t dest, const void *src, size_t len);
	saved_mem_read = target->mem_read;
	saved_mem_write = target->mem_write;
	target->mem_read = probe_mem_read;
	target->mem_write = probe_mem_write;
	/* Write fstat() result into fio_stat */
	gdb_putpacket_f("Ffstat,%X,%08" PRIX32, (unsigned)fd, (target_addr_t)fio_stat);
	const int32_t stat_result = semihosting_get_gdb_response(target->tc);
	target->mem_read = saved_mem_read;
	target->mem_write = saved_mem_write;
	/* Extract the big endian file size from the buffer */
	const uint32_t result = read_be4((uint8_t *)fio_stat, sizeof(uint32_t) * 8U);
	/* Check if tc_fstat() failed or if the size was more than 2GiB */
	if (stat_result || fio_stat[7] != 0 || (result & 0x80000000U) != 0)
		return -1; /* tc_fstat() failed */
	return result;
#endif
}

#if PC_HOSTED == 0
semihosting_time_s semihosting_get_time(target_s *const target)
{
	/* Provide space for a packed uint32_t and uint64_t */
	uint8_t time_value[12U];

	void (*saved_mem_read)(target_s *target, void *dest, target_addr_t src, size_t len);
	void (*saved_mem_write)(target_s *target, target_addr_t dest, const void *src, size_t len);
	saved_mem_read = target->mem_read;
	saved_mem_write = target->mem_write;
	target->mem_read = probe_mem_read;
	target->mem_write = probe_mem_write;
	/* Write gettimeofday() result in time_value */
	gdb_putpacket_f("Fgettimeofday,%08" PRIX32 ",%08" PRIX32, (target_addr_t)time_value, (target_addr_t)NULL);
	const int32_t result = semihosting_get_gdb_response(target->tc);
	target->mem_read = saved_mem_read;
	target->mem_write = saved_mem_write;
	/* Check if tc_gettimeofday() failed */
	if (result)
		return (semihosting_time_s){UINT64_MAX, UINT32_MAX};
	/* Convert the resulting time value from big endian */
	return (semihosting_time_s){read_be8(time_value, 4), read_be4(time_value, 0)};
}
#endif

int32_t semihosting_clock(target_s *const target)
{
#if PC_HOSTED == 1
	(void)target;
	/* NB: Can't use clock() because that would give cpu time of BMDA process */
	struct timeval current_time;
	/* Get the current time from the host */
	if (gettimeofday(&current_time, NULL) != 0)
		return -1;
	/* Extract the time value components */
	uint32_t seconds = current_time.tv_sec;
	const uint32_t microseconds = (uint32_t)current_time.tv_usec;
#else
	/* Get the current time from the host */
	const semihosting_time_s current_time = semihosting_get_time(target);
	if (current_time.seconds == UINT32_MAX && current_time.microseconds == UINT64_MAX)
		return (int32_t)current_time.seconds;
	uint32_t seconds = current_time.seconds;
	uint32_t microseconds = (uint32_t)current_time.microseconds;
#endif
	/* Clamp the seconds value appropriately */
	if (time0_sec > seconds)
		time0_sec = seconds;
	seconds -= time0_sec;
	/*
	 * Convert the resulting time to centiseconds (hundredths of a second)
	 * NB: At the potential cost of some precision, the microseconds value has been
	 *   cast down to a uint32_t to avoid doing a 64-bit division in the firmware.
	 */
	const uint64_t centiseconds = (seconds * 100U) + (microseconds / 10000U);
	/* Truncate the result back to a positive 32-bit integer */
	return centiseconds & 0x7fffffffU;
}

int32_t semihosting_time(target_s *const target)
{
#if PC_HOSTED == 1
	(void)target;
	/* Get the current time in seconds from the host */
	return time(NULL);
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

int32_t semihosting_readc(target_s *const target)
{
#if PC_HOSTED == 1
	(void)target;
	return getchar();
#else
	void (*saved_mem_read)(target_s *target, void *dest, target_addr_t src, size_t len);
	void (*saved_mem_write)(target_s *target, target_addr_t dest, const void *src, size_t len);
	saved_mem_read = target->mem_read;
	saved_mem_write = target->mem_write;
	target->mem_read = probe_mem_read;
	target->mem_write = probe_mem_write;
	uint8_t ch = '?';
	/* Read a character into ch */
	const int32_t result = hostio_read(target->tc, STDIN_FILENO, (target_addr_t)&ch, 1);
	target->mem_read = saved_mem_read;
	target->mem_write = saved_mem_write;
	if (result == 1)
		return ch;
	return -1;
#endif
}

int32_t semihosting_get_command_line(target_s *const target, const semihosting_s *const request)
{
	/* Extract the location of the result buffer and its length */
	const target_addr_t buffer_taddr = request->params[0];
	const size_t buffer_length = request->params[1];
	/* Figure out how long the command line string is */
	const size_t command_line_length = strlen(target->cmdline) + 1U;
	/* Check that we won't exceed the target buffer with the write */
	if (command_line_length > buffer_length) {
		target->tc->gdb_errno = TARGET_EINVAL;
		return -1;
	}
	/* Try to write the data to the target along with the actual length value */
	if (target_mem_write(target, buffer_taddr, target->cmdline, command_line_length))
		return -1;
	target_mem_write32(target, request->r1 + 4U, command_line_length);
	return target_check_error(target) ? -1 : 0;
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
	if (buffer_length < sizeof(file_name)) {
		target->tc->gdb_errno = TARGET_EINVAL;
		return -1;
	}
	/* If we have enough space, attempt the write back */
	return target_mem_write(target, buffer_taddr, file_name, SEMIHOSTING_TEMPNAME_LENGTH) ? -1 : 0;
}

int32_t semihosting_request(target_s *const target, const uint32_t syscall, const uint32_t r1)
{
	/* Reset the interruption state so we can tell if it was this request that was interrupted */
	target->tc->interrupted = false;

	/* Set up the request block appropriately */
	semihosting_s request = {r1, {}};
	if (syscall != SEMIHOSTING_SYS_EXIT)
		target_mem_read(target, request.params, r1, sizeof(request.params));
	int32_t ret = 0;

#if ENABLE_DEBUG == 1
	const char *syscall_descr = NULL;
	if (syscall < ARRAY_LENGTH(semihosting_names))
		syscall_descr = semihosting_names[syscall];
	if (syscall_descr == NULL)
		syscall_descr = "";

	DEBUG_INFO("syscall %12s (%" PRIx32 " %" PRIx32 " %" PRIx32 " %" PRIx32 ")\n", syscall_descr, request.params[0],
		request.params[1], request.params[2], request.params[3]);
#endif

	switch (syscall) {
	case SEMIHOSTING_SYS_OPEN: /* open */
		return semihosting_open(target, &request);

	case SEMIHOSTING_SYS_CLOSE: /* close */
		return semihosting_close(target, &request);

	case SEMIHOSTING_SYS_READ: /* read */
		return semihosting_read(target, &request);

	case SEMIHOSTING_SYS_WRITE: /* write */
		return semihosting_write(target, &request);

	case SEMIHOSTING_SYS_WRITEC: /* writec */
		return semihosting_writec(target, &request);

	case SEMIHOSTING_SYS_WRITE0: /* write0 */
		return semihosting_write0(target, &request);

	case SEMIHOSTING_SYS_ISTTY: /* isatty */
		return semihosting_isatty(target, &request);

	case SEMIHOSTING_SYS_SEEK: /* lseek */
		return semihosting_seek(target, &request);

	case SEMIHOSTING_SYS_RENAME: /* rename */
		return semihosting_rename(target, &request);

	case SEMIHOSTING_SYS_REMOVE: /* unlink */
		return semihosting_remove(target, &request);

	case SEMIHOSTING_SYS_SYSTEM: /* system */
		return semihosting_system(target, &request);

	case SEMIHOSTING_SYS_FLEN: /* file length */
		return semihosting_file_length(target, &request);

	case SEMIHOSTING_SYS_CLOCK: /* clock */
		return semihosting_clock(target);

	case SEMIHOSTING_SYS_TIME: /* time */
		return semihosting_time(target);

	case SEMIHOSTING_SYS_READC: /* readc */
		return semihosting_readc(target);

	case SEMIHOSTING_SYS_ERRNO: /* errno */
#if PC_HOSTED == 1
		/* Return whatever the current errno value is */
		return errno;
#else
		/* Return the last errno we got from GDB */
		return target->tc->gdb_errno;
#endif

	case SEMIHOSTING_SYS_EXIT: /* _exit() */
		tc_printf(target, "_exit(0x%x)\n", request.r1);
		target_halt_resume(target, 1);
		break;

	case SEMIHOSTING_SYS_EXIT_EXTENDED:                                               /* _exit() */
		tc_printf(target, "_exit(0x%x%08x)\n", request.params[1], request.params[0]); /* exit() with 64bit exit value */
		target_halt_resume(target, 1);
		break;

	case SEMIHOSTING_SYS_GET_CMDLINE: /* get_cmdline */
		return semihosting_get_command_line(target, &request);

	case SEMIHOSTING_SYS_ISERROR: { /* iserror */
		int error = request.params[0];
		ret = error == TARGET_EPERM || error == TARGET_ENOENT || error == TARGET_EINTR || error == TARGET_EIO ||
			error == TARGET_EBADF || error == TARGET_EACCES || error == TARGET_EFAULT || error == TARGET_EBUSY ||
			error == TARGET_EEXIST || error == TARGET_ENODEV || error == TARGET_ENOTDIR || error == TARGET_EISDIR ||
			error == TARGET_EINVAL || error == TARGET_ENFILE || error == TARGET_EMFILE || error == TARGET_EFBIG ||
			error == TARGET_ENOSPC || error == TARGET_ESPIPE || error == TARGET_EROFS || error == TARGET_ENOSYS ||
			error == TARGET_ENAMETOOLONG || error == TARGET_EUNKNOWN;
		break;
	}

	case SEMIHOSTING_SYS_HEAPINFO: /* heapinfo */
		target_mem_write(
			target, request.r1, &target->heapinfo, sizeof(target->heapinfo)); /* See newlib/libc/sys/arm/crt0.S */
		break;

	case SEMIHOSTING_SYS_TMPNAM: /* tmpnam */
		return semihosting_temp_name(target, &request);

	// not implemented yet:
	case SEMIHOSTING_SYS_ELAPSED:  /* elapsed */
	case SEMIHOSTING_SYS_TICKFREQ: /* tickfreq */
		return -1;
	}

	return ret;
}
