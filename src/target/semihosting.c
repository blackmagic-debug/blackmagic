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
	unsigned int errno_ = 0;
	char ctrl_c_flag = '\0';
	const int items = sscanf(pbuf + (retcode_is_negative ? 2U : 1U), "%x,%x,%c", &retcode, &errno_, &ctrl_c_flag);

	if (items < 1) {
		/*
		 * Something went wrong with the sscanf or the packet format, avoid UB
		 * FIXME: how do we properly handle this?
		 */
		tc->interrupted = false;
		tc->errno_ = TARGET_EUNKNOWN;
		return -1;
	}

	/* If the call was successful the errno may be omitted */
	tc->errno_ = items >= 2 ? errno_ : 0;

	/* If break is requested */
	tc->interrupted = items == 3 && ctrl_c_flag == 'C';

	return retcode_is_negative ? -retcode : retcode;
}

static int semihosting_get_gdb_response(target_controller_s *const tc)
{
	char *const packet_buffer = gdb_packet_buffer();
	/* Still have to service normal 'X'/'m'-packets */
	while (true) {
		/* Get back the next packet to process and have the main loop handle it */
		const size_t size = gdb_getpacket(packet_buffer, GDB_PACKET_BUFFER_SIZE);
		/* If this was an escape packet (or gdb_if reports link closed), fail the call */
		if (size == 1U && packet_buffer[0] == '\x04')
			return -1;
		const int result = gdb_main_loop(tc, packet_buffer, GDB_PACKET_BUFFER_SIZE, size, true);
		/* If this was an F-packet, we're done */
		if (packet_buffer[0] == 'F')
			return result;
	}
}

/* Interface to host system calls */
int hostio_open(target_controller_s *tc, target_addr_t path, size_t path_len, target_open_flags_e flags, mode_t mode)
{
	gdb_putpacket_f("Fopen,%08" PRIX32 "/%08" PRIX32 ",%08X,%08" PRIX32, path, (uint32_t)path_len, flags, mode);
	return semihosting_get_gdb_response(tc);
}

int hostio_close(target_controller_s *tc, int fd)
{
	gdb_putpacket_f("Fclose,%08X", fd);
	return semihosting_get_gdb_response(tc);
}

int hostio_read(target_controller_s *tc, int fd, target_addr_t buf, unsigned int count)
{
	gdb_putpacket_f("Fread,%08X,%08" PRIX32 ",%08X", fd, buf, count);
	return semihosting_get_gdb_response(tc);
}

int hostio_write(target_controller_s *tc, int fd, target_addr_t buf, unsigned int count)
{
	gdb_putpacket_f("Fwrite,%08X,%08" PRIX32 ",%08X", fd, buf, count);
	return semihosting_get_gdb_response(tc);
}

long hostio_lseek(target_controller_s *tc, int fd, long offset, target_seek_flag_e flag)
{
	gdb_putpacket_f("Flseek,%08X,%08lX,%08X", fd, offset, flag);
	return semihosting_get_gdb_response(tc);
}

int hostio_rename(target_controller_s *tc, target_addr_t oldpath, size_t old_len, target_addr_t newpath, size_t new_len)
{
	gdb_putpacket_f("Frename,%08" PRIX32 "/%08" PRIX32 ",%08" PRIX32 "/%08" PRIX32, oldpath, (uint32_t)old_len, newpath,
		(uint32_t)new_len);
	return semihosting_get_gdb_response(tc);
}

int hostio_unlink(target_controller_s *tc, target_addr_t path, size_t path_len)
{
	gdb_putpacket_f("Funlink,%08" PRIX32 "/%08" PRIX32, path, (uint32_t)path_len);
	return semihosting_get_gdb_response(tc);
}

int hostio_stat(target_controller_s *tc, target_addr_t path, size_t path_len, target_addr_t buf)
{
	gdb_putpacket_f("Fstat,%08" PRIX32 "/%08" PRIX32 ",%08" PRIX32, path, (uint32_t)path_len, buf);
	return semihosting_get_gdb_response(tc);
}

int hostio_fstat(target_controller_s *tc, int fd, target_addr_t buf)
{
	gdb_putpacket_f("Ffstat,%X,%08" PRIX32, fd, buf);
	return semihosting_get_gdb_response(tc);
}

int hostio_gettimeofday(target_controller_s *tc, target_addr_t tv, target_addr_t tz)
{
	gdb_putpacket_f("Fgettimeofday,%08" PRIX32 ",%08" PRIX32, tv, tz);
	return semihosting_get_gdb_response(tc);
}

int hostio_isatty(target_controller_s *tc, int fd)
{
	gdb_putpacket_f("Fisatty,%08X", fd);
	return semihosting_get_gdb_response(tc);
}

int hostio_system(target_controller_s *tc, target_addr_t cmd, size_t cmd_len)
{
	gdb_putpacket_f("Fsystem,%08" PRIX32 "/%08" PRIX32, cmd, (uint32_t)cmd_len);
	return semihosting_get_gdb_response(tc);
}

/* Interface to host system calls */
int tc_write(target_s *t, int fd, target_addr_t buf, unsigned int count)
{
	if (t->stdout_redirected && (fd == STDOUT_FILENO || fd == STDERR_FILENO)) {
		while (count) {
			uint8_t tmp[STDOUT_READ_BUF_SIZE];
			unsigned int cnt = sizeof(tmp);
			if (cnt > count)
				cnt = count;
			target_mem_read(t, tmp, buf, cnt);
			debug_serial_send_stdout(tmp, cnt);
			count -= cnt;
			buf += cnt;
		}
		return 0;
	}
	return hostio_write(t->tc, fd, buf, count);
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

int32_t semihosting_open(target_s *const target, const semihosting_s *const request)
{
	const target_addr_t file_name_taddr = request->params[0];
	const uint32_t file_name_length = request->params[2];
	if (file_name_taddr == TARGET_NULL || file_name_length == 0)
		return -1;

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
	char *file_name = malloc(file_name_length + 1U);
	if (file_name == NULL)
		return -1;
	target_mem_read(target, file_name, file_name_taddr, file_name_length + 1U);
	if (target_check_error(target)) {
		free(file_name);
		return -1;
	}
	file_name[file_name_length] = '\0';
	const int32_t result = open(file_name, open_mode, 0644);
	free(file_name);
#else
	const int32_t result = hostio_open(target->tc, file_name_taddr, file_name_length + 1U, open_mode, 0644U);
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
	return hostio_close(target->tc, fd);
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

int cortexm_hostio_request(target_s *const target)
{
	semihosting_s request;
	target->tc->interrupted = false;
	target_reg_read(target, 0, &request.syscall, sizeof(request.syscall));
	target_reg_read(target, 1, &request.r1, sizeof(request.r1));

	if (request.syscall != SEMIHOSTING_SYS_EXIT)
		target_mem_read(target, request.params, request.r1, sizeof(request.params));
	int32_t ret = 0;

#if ENABLE_DEBUG == 1
	const char *syscall_descr = NULL;
	if (request.syscall < ARRAY_LENGTH(semihosting_names))
		syscall_descr = semihosting_names[request.syscall];
	if (syscall_descr == NULL)
		syscall_descr = "";

	DEBUG_INFO("syscall %12s (%" PRIx32 " %" PRIx32 " %" PRIx32 " %" PRIx32 ")\n", syscall_descr, request.params[0],
		request.params[1], request.params[2], request.params[3]);
#endif

	switch (request.syscall) {
	case SEMIHOSTING_SYS_OPEN: /* open */
		ret = semihosting_open(target, &request);
		break;

	case SEMIHOSTING_SYS_CLOSE: /* close */
		ret = semihosting_close(target, &request);
		break;

	case SEMIHOSTING_SYS_READ: /* read */
		ret = semihosting_read(target, &request);
		break;

#if PC_HOSTED == 1
		/* code that runs in pc-hosted process. use linux system calls. */

	case SEMIHOSTING_SYS_WRITE: { /* write */
		ret = -1;
		target_addr_t buf_taddr = request.params[1];
		uint32_t buf_len = request.params[2];
		if (buf_taddr == TARGET_NULL)
			break;
		if (buf_len == 0) {
			ret = 0;
			break;
		}
		uint8_t *buf = malloc(buf_len);
		if (buf == NULL)
			break;
		target_mem_read(target, buf, buf_taddr, buf_len);
		if (target_check_error(target)) {
			free(buf);
			break;
		}
		ret = write(request.params[0] - 1, buf, buf_len);
		free(buf);
		if (ret >= 0)
			ret = buf_len - ret;
		break;
	}

	case SEMIHOSTING_SYS_WRITEC: { /* writec */
		ret = -1;
		uint8_t ch;
		target_addr_t ch_taddr = request.r1;
		if (ch_taddr == TARGET_NULL)
			break;
		ch = target_mem_read8(target, ch_taddr);
		if (target_check_error(target))
			break;
		fputc(ch, stderr);
		ret = 0;
		break;
	}

	case SEMIHOSTING_SYS_WRITE0: { /* write0 */
		ret = -1;
		target_addr_t str_addr = request.r1;
		if (str_addr == TARGET_NULL)
			break;
		while (true) {
			const uint8_t str_c = target_mem_read8(target, str_addr++);
			if (target_check_error(target) || str_c == 0x00)
				break;
			fputc(str_c, stderr);
		}
		ret = 0;
		break;
	}

	case SEMIHOSTING_SYS_ISTTY: /* isatty */
		ret = isatty(request.params[0] - 1);
		break;

	case SEMIHOSTING_SYS_SEEK: { /* lseek */
		off_t pos = request.params[1];
		if (lseek(request.params[0] - 1, pos, SEEK_SET) == (off_t)pos)
			ret = 0;
		else
			ret = -1;
		break;
	}

	case SEMIHOSTING_SYS_RENAME: { /* rename */
		ret = -1;
		target_addr_t fnam1_taddr = request.params[0];
		uint32_t fnam1_len = request.params[1];
		if (fnam1_taddr == TARGET_NULL)
			break;
		if (fnam1_len == 0)
			break;
		target_addr_t fnam2_taddr = request.params[2];
		uint32_t fnam2_len = request.params[3];
		if (fnam2_taddr == TARGET_NULL)
			break;
		if (fnam2_len == 0)
			break;
		char *fnam1 = malloc(fnam1_len + 1U);
		if (fnam1 == NULL)
			break;
		target_mem_read(target, fnam1, fnam1_taddr, fnam1_len + 1U);
		if (target_check_error(target)) {
			free(fnam1);
			break;
		}
		fnam1[fnam1_len] = '\0';
		char *fnam2 = malloc(fnam2_len + 1U);
		if (fnam2 == NULL) {
			free(fnam1);
			break;
		}
		target_mem_read(target, fnam2, fnam2_taddr, fnam2_len + 1U);
		if (target_check_error(target)) {
			free(fnam1);
			free(fnam2);
			break;
		}
		fnam2[fnam2_len] = '\0';
		ret = rename(fnam1, fnam2);
		free(fnam1);
		free(fnam2);
		break;
	}

	case SEMIHOSTING_SYS_REMOVE: { /* unlink */
		ret = -1;
		target_addr_t fnam_taddr = request.params[0];
		if (fnam_taddr == TARGET_NULL)
			break;
		uint32_t fnam_len = request.params[1];
		if (fnam_len == 0)
			break;
		char *fnam = malloc(fnam_len + 1U);
		if (fnam == NULL)
			break;
		target_mem_read(target, fnam, fnam_taddr, fnam_len + 1U);
		if (target_check_error(target)) {
			free(fnam);
			break;
		}
		fnam[fnam_len] = '\0';
		ret = remove(fnam);
		free(fnam);
		break;
	}

	case SEMIHOSTING_SYS_SYSTEM: { /* system */
		ret = -1;
		target_addr_t cmd_taddr = request.params[0];
		if (cmd_taddr == TARGET_NULL)
			break;
		uint32_t cmd_len = request.params[1];
		if (cmd_len == 0)
			break;
		char *cmd = malloc(cmd_len + 1U);
		if (cmd == NULL)
			break;
		target_mem_read(target, cmd, cmd_taddr, cmd_len + 1U);
		if (target_check_error(target)) {
			free(cmd);
			break;
		}
		cmd[cmd_len] = '\0';
		ret = system(cmd);
		free(cmd);
		break;
	}

	case SEMIHOSTING_SYS_FLEN: { /* file length */
		ret = -1;
		struct stat stat_buf;
		if (fstat(request.params[0] - 1, &stat_buf) != 0)
			break;
		if (stat_buf.st_size > INT32_MAX)
			break;
		ret = stat_buf.st_size;
		break;
	}

	case SEMIHOSTING_SYS_CLOCK: { /* clock */
		/* can't use clock() because that would give cpu time of pc-hosted process */
		ret = -1;
		struct timeval timeval_buf;
		if (gettimeofday(&timeval_buf, NULL) != 0)
			break;
		uint32_t sec = timeval_buf.tv_sec;
		uint64_t usec = timeval_buf.tv_usec;
		if (time0_sec > sec)
			time0_sec = sec;
		sec -= time0_sec;
		uint64_t csec64 = (sec * UINT64_C(1000000) + usec) / UINT64_C(10000);
		uint32_t csec = csec64 & 0x7fffffffU;
		ret = csec;
		break;
	}

	case SEMIHOSTING_SYS_TIME: /* time */
		ret = time(NULL);
		break;

	case SEMIHOSTING_SYS_READC: /* readc */
		ret = getchar();
		break;

	case SEMIHOSTING_SYS_ERRNO: /* errno */
		ret = errno;
		break;
#else
		/* code that runs in probe. use gdb fileio calls. */

	case SEMIHOSTING_SYS_WRITE: /* write */
		ret = tc_write(target, request.params[0] - 1, request.params[1], request.params[2]);
		if (ret >= 0)
			ret = request.params[2] - ret;
		break;
	case SEMIHOSTING_SYS_WRITEC: /* writec */
		ret = tc_write(target, STDERR_FILENO, request.r1, 1);
		break;
	case SEMIHOSTING_SYS_WRITE0: { /* write0 */
		ret = -1;
		target_addr_t str_begin = request.r1;
		target_addr_t str_end = str_begin;
		while (target_mem_read8(target, str_end) != 0) {
			if (target_check_error(target))
				break;
			str_end++;
		}
		int len = str_end - str_begin;
		if (len != 0) {
			int rc = tc_write(target, STDERR_FILENO, str_begin, len);
			if (rc != len)
				break;
		}
		ret = 0;
		break;
	}
	case SEMIHOSTING_SYS_ISTTY: /* isatty */
		ret = hostio_isatty(target->tc, request.params[0] - 1);
		break;
	case SEMIHOSTING_SYS_SEEK: /* lseek */
		if (hostio_lseek(target->tc, request.params[0] - 1, request.params[1], TARGET_SEEK_SET) ==
			(long)request.params[1])
			ret = 0;
		else
			ret = -1;
		break;
	case SEMIHOSTING_SYS_RENAME: /* rename */
		ret = hostio_rename(
			target->tc, request.params[0], request.params[1] + 1U, request.params[2], request.params[3] + 1U);
		break;
	case SEMIHOSTING_SYS_REMOVE: /* unlink */
		ret = hostio_unlink(target->tc, request.params[0], request.params[1] + 1U);
		break;
	case SEMIHOSTING_SYS_SYSTEM: /* system */
		/* before use first enable system calls with the following gdb command: 'set remote system-call-allowed 1' */
		ret = hostio_system(target->tc, request.params[0], request.params[1] + 1U);
		break;

	case SEMIHOSTING_SYS_FLEN: { /* file length */
		ret = -1;
		uint32_t fio_stat[16]; /* same size as fio_stat in gdb/include/gdb/fileio.h */
		//DEBUG("SYS_FLEN fio_stat addr %p\n", fio_stat);
		void (*saved_mem_read)(target_s *target, void *dest, target_addr_t src, size_t len);
		void (*saved_mem_write)(target_s *target, target_addr_t dest, const void *src, size_t len);
		saved_mem_read = target->mem_read;
		saved_mem_write = target->mem_write;
		target->mem_read = probe_mem_read;
		target->mem_write = probe_mem_write;
		int rc = hostio_fstat(
			target->tc, request.params[0] - 1, (target_addr_t)fio_stat); /* write fstat() result in fio_stat[] */
		target->mem_read = saved_mem_read;
		target->mem_write = saved_mem_write;
		if (rc)
			break;                           /* tc_fstat() failed */
		uint32_t fst_size_msw = fio_stat[7]; /* most significant 32 bits of fst_size in fio_stat */
		uint32_t fst_size_lsw = fio_stat[8]; /* least significant 32 bits of fst_size in fio_stat */
		if (fst_size_msw != 0)
			break;                             /* file size too large for int32_t return type */
		ret = __builtin_bswap32(fst_size_lsw); /* convert from bigendian to target order */
		if (ret < 0)
			ret = -1; /* file size too large for int32_t return type */
		break;
	}

	case SEMIHOSTING_SYS_CLOCK:  /* clock */
	case SEMIHOSTING_SYS_TIME: { /* time */
		/* use same code for SYS_CLOCK and SYS_TIME, more compact */
		ret = -1;

		struct __attribute__((packed, aligned(4))) {
			uint32_t ftv_sec;
			uint64_t ftv_usec;
		} fio_timeval;

		//DEBUG("SYS_TIME fio_timeval addr %p\n", &fio_timeval);
		void (*saved_mem_read)(target_s *target, void *dest, target_addr_t src, size_t len);
		void (*saved_mem_write)(target_s *target, target_addr_t dest, const void *src, size_t len);
		saved_mem_read = target->mem_read;
		saved_mem_write = target->mem_write;
		target->mem_read = probe_mem_read;
		target->mem_write = probe_mem_write;
		/* write gettimeofday() result in fio_timeval[] */
		int rc = hostio_gettimeofday(target->tc, (target_addr_t)&fio_timeval, (target_addr_t)NULL);
		target->mem_read = saved_mem_read;
		target->mem_write = saved_mem_write;
		if (rc) /* tc_gettimeofday() failed */
			break;
		/* convert from bigendian to target order */
		/* XXX: Replace this madness with endian-aware IO */
		uint32_t sec = __builtin_bswap32(fio_timeval.ftv_sec);
		uint64_t usec = __builtin_bswap64(fio_timeval.ftv_usec);
		if (request.syscall == SEMIHOSTING_SYS_TIME) /* SYS_TIME: time in seconds */
			ret = sec;
		else { /* SYS_CLOCK: time in hundredths of seconds */
			if (time0_sec > sec)
				time0_sec = sec; /* set sys_clock time origin */
			sec -= time0_sec;
			/* Cast down microseconds to avoid u64 division */
			uint32_t csec32 = ((uint32_t)usec / 10000U) + (sec * 100U);
			int32_t csec = csec32 & 0x7fffffffU;
			ret = csec;
		}
		break;
	}

	case SEMIHOSTING_SYS_READC: { /* readc */
		uint8_t ch = '?';
		//DEBUG("SYS_READC ch addr %p\n", &ch);
		void (*saved_mem_read)(target_s *target, void *dest, target_addr_t src, size_t len);
		void (*saved_mem_write)(target_s *target, target_addr_t dest, const void *src, size_t len);
		saved_mem_read = target->mem_read;
		saved_mem_write = target->mem_write;
		target->mem_read = probe_mem_read;
		target->mem_write = probe_mem_write;
		int rc = hostio_read(target->tc, STDIN_FILENO, (target_addr_t)&ch, 1); /* read a character in ch */
		target->mem_read = saved_mem_read;
		target->mem_write = saved_mem_write;
		if (rc == 1)
			ret = ch;
		else
			ret = -1;
		break;
	}

	case SEMIHOSTING_SYS_ERRNO: /* Return last errno from GDB */
		ret = target->tc->errno_;
		break;
#endif

	case SEMIHOSTING_SYS_EXIT: /* _exit() */
		tc_printf(target, "_exit(0x%x)\n", request.r1);
		target_halt_resume(target, 1);
		break;

	case SEMIHOSTING_SYS_EXIT_EXTENDED:                                               /* _exit() */
		tc_printf(target, "_exit(0x%x%08x)\n", request.params[1], request.params[0]); /* exit() with 64bit exit value */
		target_halt_resume(target, 1);
		break;

	case SEMIHOSTING_SYS_GET_CMDLINE: { /* get_cmdline */
		uint32_t retval[2];
		ret = -1;
		target_addr_t buf_ptr = request.params[0];
		target_addr_t buf_len = request.params[1];
		if (strlen(target->cmdline) + 1U > buf_len)
			break;
		if (target_mem_write(target, buf_ptr, target->cmdline, strlen(target->cmdline) + 1U))
			break;
		retval[0] = buf_ptr;
		retval[1] = strlen(target->cmdline) + 1U;
		if (target_mem_write(target, request.r1, retval, sizeof(retval)))
			break;
		ret = 0;
		break;
	}

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

	case SEMIHOSTING_SYS_TMPNAM: { /* tmpnam */
		/* Given a target identifier between 0 and 255, returns a temporary name */
		target_addr_t buf_ptr = request.params[0];
		int target_id = request.params[1];
		int buf_size = request.params[2];
		char fnam[] = "tempXX.tmp";
		ret = -1;
		if (buf_ptr == 0)
			break;
		if (buf_size <= 0)
			break;
		if ((target_id < 0) || (target_id > 255))
			break;                         /* target id out of range */
		fnam[5] = 'A' + (target_id & 0xf); /* create filename */
		fnam[4] = 'A' + (target_id >> 4 & 0xf);
		if (strlen(fnam) + 1U > (uint32_t)buf_size)
			break; /* target buffer too small */
		if (target_mem_write(target, buf_ptr, fnam, strlen(fnam) + 1U))
			break; /* copy filename to target */
		ret = 0;
		break;
	}

	// not implemented yet:
	case SEMIHOSTING_SYS_ELAPSED:  /* elapsed */
	case SEMIHOSTING_SYS_TICKFREQ: /* tickfreq */
		ret = -1;
		break;
	}

	target_reg_write(target, 0, &ret, sizeof(ret));
	return target->tc->interrupted;
}
