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

/* This file implements the GDB Remote Serial Debugging protocol as
 * described in "Debugging with GDB" build from GDB source.
 *
 * Originally written for GDB 6.8, updated and tested with GDB 7.2.
 */

#include "general.h"
#include "platform.h"
#include "ctype.h"
#include "hex_utils.h"
#include "gdb_if.h"
#include "gdb_packet.h"
#include "gdb_main.h"
#include "gdb_hostio.h"
#include "target.h"
#include "target_internal.h"
#include "command.h"
#include "crc32.h"
#include "morse.h"
#ifdef ENABLE_RTT
#include "rtt.h"
#endif

#if ADVERTISE_NOACKMODE == 1
/*
 * This lets GDB know that the probe supports ‘QStartNoAckMode’
 * and prefers to operate in no-acknowledgment mode
 *
 * When not present, GDB will assume that the probe does not support it
 * but can still be manually enabled by the user with:
 * set remote noack-packet on
 */
#define GDB_QSUPPORTED_NOACKMODE ";QStartNoAckMode+"
#else
#define GDB_QSUPPORTED_NOACKMODE
#endif

#if defined(_WIN32)
#include <malloc.h>
#else
#include <alloca.h>
#endif
#include <stdlib.h>

typedef enum gdb_signal {
	GDB_SIGINT = 2,
	GDB_SIGTRAP = 5,
	GDB_SIGSEGV = 11,
	GDB_SIGLOST = 29,
} gdb_signal_e;

#define GDB_MAX_PACKET_SIZE 1024U

#define ERROR_IF_NO_TARGET()   \
	if (!cur_target) {         \
		gdb_putpacketz("EFF"); \
		break;                 \
	}

typedef struct cmd_executer {
	const char *cmd_prefix;
	void (*func)(const char *packet, size_t len);
} cmd_executer_s;

target_s *cur_target;
target_s *last_target;
bool gdb_target_running = false;
static bool gdb_needs_detach_notify = false;

static void handle_q_packet(char *packet, size_t len);
static void handle_v_packet(char *packet, size_t len);
static void handle_z_packet(char *packet, size_t len);
static void handle_kill_target(void);

static void gdb_target_destroy_callback(target_controller_s *tc, target_s *t)
{
	(void)tc;
	if (cur_target == t) {
		gdb_put_notificationz("%Stop:W00");
		gdb_out("You are now detached from the previous target.\n");
		cur_target = NULL;
		gdb_needs_detach_notify = true;
	}

	if (last_target == t)
		last_target = NULL;
}

static void gdb_target_printf(target_controller_s *tc, const char *fmt, va_list ap)
{
	(void)tc;
	gdb_voutf(fmt, ap);
}

target_controller_s gdb_controller = {
	.destroy_callback = gdb_target_destroy_callback,
	.printf = gdb_target_printf,

	.open = hostio_open,
	.close = hostio_close,
	.read = hostio_read,
	.write = hostio_write,
	.lseek = hostio_lseek,
	.rename = hostio_rename,
	.unlink = hostio_unlink,
	.stat = hostio_stat,
	.fstat = hostio_fstat,
	.gettimeofday = hostio_gettimeofday,
	.isatty = hostio_isatty,
	.system = hostio_system,
};

/* execute gdb remote command stored in 'pbuf'. returns immediately, no busy waiting. */

int gdb_main_loop(target_controller_s *tc, char *pbuf, size_t pbuf_size, size_t size, bool in_syscall)
{
	bool single_step = false;

	/* GDB protocol main loop */
	switch (pbuf[0]) {
	/* Implementation of these is mandatory! */
	case 'g': { /* 'g': Read general registers */
		ERROR_IF_NO_TARGET();
		const size_t reg_size = target_regs_size(cur_target);
		if (reg_size) {
			uint8_t gp_regs[reg_size];
			target_regs_read(cur_target, gp_regs);
			gdb_putpacket(hexify(pbuf, gp_regs, reg_size), reg_size * 2U);
		} else {
			gdb_putpacketz("00");
		}
		break;
	}
	case 'm': { /* 'm addr,len': Read len bytes from addr */
		uint32_t addr, len;
		ERROR_IF_NO_TARGET();
		sscanf(pbuf, "m%" SCNx32 ",%" SCNx32, &addr, &len);
		if (len > pbuf_size / 2U) {
			gdb_putpacketz("E02");
			break;
		}
		DEBUG_GDB("m packet: addr = %" PRIx32 ", len = %" PRIx32 "\n", addr, len);
		uint8_t mem[len];
		if (target_mem_read(cur_target, mem, addr, len))
			gdb_putpacketz("E01");
		else
			gdb_putpacket(hexify(pbuf, mem, len), len * 2U);
		break;
	}
	case 'G': { /* 'G XX': Write general registers */
		ERROR_IF_NO_TARGET();
		const size_t reg_size = target_regs_size(cur_target);
		if (reg_size) {
			uint8_t gp_regs[reg_size];
			unhexify(gp_regs, &pbuf[1], reg_size);
			target_regs_write(cur_target, gp_regs);
		}
		gdb_putpacketz("OK");
		break;
	}
	case 'M': { /* 'M addr,len:XX': Write len bytes to addr */
		uint32_t addr = 0;
		uint32_t len = 0;
		int hex;
		ERROR_IF_NO_TARGET();
		sscanf(pbuf, "M%" SCNx32 ",%" SCNx32 ":%n", &addr, &len, &hex);
		if (len > (unsigned)(size - hex) / 2U) {
			gdb_putpacketz("E02");
			break;
		}
		DEBUG_GDB("M packet: addr = %" PRIx32 ", len = %" PRIx32 "\n", addr, len);
		uint8_t mem[len];
		unhexify(mem, pbuf + hex, len);
		if (target_mem_write(cur_target, addr, mem, len))
			gdb_putpacketz("E01");
		else
			gdb_putpacketz("OK");
		break;
	}
	/*
	 * '[m|M|g|G|c][thread-id]' : Set the thread ID for the given subsequent operation
	 * (we don't actually care which as we only care about the TID for whether to send OK or an error)
	 */
	case 'H': {
		char operation = 0;
		uint32_t thread_id = 0;
		sscanf(pbuf, "H%c%" SCNx32, &operation, &thread_id);
		if (thread_id <= 1)
			gdb_putpacketz("OK");
		else
			gdb_putpacketz("E01");
		break;
	}
	case 's': /* 's [addr]': Single step [start at addr] */
		single_step = true;
		/* fall through */
	case 'c': /* 'c [addr]': Continue [at addr] */
	case 'C': /* 'C sig[;addr]': Continue with signal [at addr] */
		if (!cur_target) {
			gdb_putpacketz("X1D");
			break;
		}

		target_halt_resume(cur_target, single_step);
		SET_RUN_STATE(true);
		single_step = false;
		/* fall through */
	case '?': { /* '?': Request reason for target halt */
		/*
		 * This packet isn't documented as being mandatory,
		 * but GDB doesn't work without it.
		 */
		if (!cur_target) {
			gdb_putpacketz("W00"); /* Report "target exited" if no target */
			break;
		}

		/*
		 * The target is running, so there is no response to give.
		 * The calling function will poll the state of the target
		 * by calling gdb_poll_target() as long as `cur_target`
		 * is not NULL and `gdb_target_running` is true.
		 */
		gdb_target_running = true;
		break;
	}

	/* Optional GDB packet support */
	case 'p': { /* Read single register */
		ERROR_IF_NO_TARGET();
		if (cur_target->reg_read) {
			uint32_t reg;
			sscanf(pbuf, "p%" SCNx32, &reg);
			uint8_t val[8];
			size_t s = target_reg_read(cur_target, reg, val, sizeof(val));
			if (s > 0)
				gdb_putpacket(hexify(pbuf, val, s), s * 2U);
			else
				gdb_putpacketz("EFF");
		} else {
			gdb_putpacketz("00");
		}
		break;
	}
	case 'P': { /* Write single register */
		ERROR_IF_NO_TARGET();
		if (cur_target->reg_write) {
			uint32_t reg;
			int n;
			sscanf(pbuf, "P%" SCNx32 "=%n", &reg, &n);
			// TODO: FIXME, VLAs considered harmful.
			uint8_t val[strlen(pbuf + n) / 2U];
			unhexify(val, pbuf + n, sizeof(val));
			if (target_reg_write(cur_target, reg, val, sizeof(val)) > 0)
				gdb_putpacketz("OK");
			else
				gdb_putpacketz("EFF");
		} else {
			gdb_putpacketz("OK");
		}
		break;
	}

	case 'F': /* Semihosting call finished */
		if (in_syscall)
			return hostio_reply(tc, pbuf, size);
		else {
			DEBUG_GDB("*** F packet when not in syscall! '%s'\n", pbuf);
			gdb_putpacketz("");
		}
		break;

	case '!': /* Enable Extended GDB Protocol. */
		/*
		 * This doesn't do anything, we support the extended
		 * protocol anyway, but GDB will never send us a 'R'
		 * packet unless we answer 'OK' here.
		 */
		gdb_putpacketz("OK");
		break;

	case '\x04':
	case 'D': /* GDB 'detach' command. */
#if PC_HOSTED == 1
		if (shutdown_bmda)
			return 0;
#endif
		if (cur_target) {
			SET_RUN_STATE(true);
			target_detach(cur_target);
			last_target = cur_target;
			cur_target = NULL;
		}
		if (pbuf[0] == 'D')
			gdb_putpacketz("OK");
		gdb_set_noackmode(false);
		break;

	case 'k': /* Kill the target */
		handle_kill_target();
		break;

	case 'r': /* Reset the target system */
	case 'R': /* Restart the target program */
		if (cur_target)
			target_reset(cur_target);
		else if (last_target) {
			cur_target = target_attach(last_target, &gdb_controller);
			if (cur_target)
				morse(NULL, false);
			target_reset(cur_target);
		}
		break;

	case 'X': { /* 'X addr,len:XX': Write binary data to addr */
		uint32_t addr, len;
		int bin;
		ERROR_IF_NO_TARGET();
		sscanf(pbuf, "X%" SCNx32 ",%" SCNx32 ":%n", &addr, &len, &bin);
		if (len > (unsigned)(size - bin)) {
			gdb_putpacketz("E02");
			break;
		}
		DEBUG_GDB("X packet: addr = %" PRIx32 ", len = %" PRIx32 "\n", addr, len);
		if (target_mem_write(cur_target, addr, pbuf + bin, len))
			gdb_putpacketz("E01");
		else
			gdb_putpacketz("OK");
		break;
	}

	case 'Q': /* General set packet */
	case 'q': /* General query packet */
		handle_q_packet(pbuf, size);
		break;

	case 'v': /* Verbose command packet */
		handle_v_packet(pbuf, size);
		break;

	/* These packet implement hardware break-/watchpoints */
	case 'Z': /* Z type,addr,len: Set breakpoint packet */
	case 'z': /* z type,addr,len: Clear breakpoint packet */
		ERROR_IF_NO_TARGET();
		handle_z_packet(pbuf, size);
		break;

	default: /* Packet not implemented */
		DEBUG_GDB("*** Unsupported packet: %s\n", pbuf);
		gdb_putpacketz("");
	}
	return 0;
}

static bool exec_command(char *packet, const size_t length, const cmd_executer_s *exec)
{
	while (exec->cmd_prefix) {
		const size_t prefix_length = strlen(exec->cmd_prefix);
		if (!strncmp(packet, exec->cmd_prefix, prefix_length)) {
			exec->func(packet + prefix_length, length - prefix_length);
			return true;
		}
		++exec;
	}
	return false;
}

static void exec_q_rcmd(const char *packet, const size_t length)
{
	/* calculate size and allocate buffer for command */
	const size_t datalen = length / 2U;
	// This needs replacing with something more sensible.
	// It should be pinging -Wvla among other things, and it failing is straight-up UB
	char *data = alloca(datalen + 1U);
	/* dehexify command */
	unhexify(data, packet, datalen);
	data[datalen] = 0; /* add terminating null */

	const int c = command_process(cur_target, data);
	if (c < 0)
		gdb_putpacketz("");
	else if (c == 0)
		gdb_putpacketz("OK");
	else {
		const char *const response = "Failed\n";
		const size_t response_length = strlen(response);
		char pbuf[response_length * 2 + 1];
		gdb_putpacket(hexify(pbuf, response, response_length), 2 * response_length);
	}
}

static void handle_q_string_reply(const char *reply, const char *param)
{
	const size_t reply_length = strlen(reply);
	uint32_t addr = 0;
	uint32_t len = 0;

	if (sscanf(param, "%08" PRIx32 ",%08" PRIx32, &addr, &len) != 2) {
		gdb_putpacketz("E01");
		return;
	}
	if (addr > reply_length) {
		gdb_putpacketz("E01");
		return;
	}
	if (addr == reply_length) {
		gdb_putpacketz("l");
		return;
	}
	size_t output_len = reply_length - addr;
	if (output_len > len)
		output_len = len;
	gdb_putpacket2("m", 1U, reply + addr, output_len);
}

static void exec_q_supported(const char *packet, const size_t length)
{
	(void)packet;
	(void)length;

	/* 
	 * This is the first packet sent by GDB, so we can reset the NoAckMode flag here in case
	 * the previous session was terminated abruptly with NoAckMode enabled
	 */
	gdb_set_noackmode(false);

	gdb_putpacket_f(
		"PacketSize=%X;qXfer:memory-map:read+;qXfer:features:read+" GDB_QSUPPORTED_NOACKMODE, GDB_MAX_PACKET_SIZE);
}

static void exec_q_memory_map(const char *packet, const size_t length)
{
	(void)length;
	target_s *target = cur_target;

	/* Read target XML memory map */
	if (!target)
		target = last_target;
	if (!target) {
		gdb_putpacketz("E01");
		return;
	}
	char buf[1024];
	target_mem_map(target, buf, sizeof(buf)); /* Fixme: Check size!*/
	handle_q_string_reply(buf, packet);
}

static void exec_q_feature_read(const char *packet, const size_t length)
{
	(void)length;
	target_s *target = cur_target;

	/* Read target description */
	if (!target)
		target = last_target;
	if (!target) {
		gdb_putpacketz("E01");
		return;
	}
	const char *const description = target_regs_description(target);
	handle_q_string_reply(description ? description : "", packet);
	free((void *)description);
}

static void exec_q_crc(const char *packet, const size_t length)
{
	(void)length;
	uint32_t addr;
	uint32_t addr_length;
	if (sscanf(packet, "%" PRIx32 ",%" PRIx32, &addr, &addr_length) == 2) {
		if (!cur_target) {
			gdb_putpacketz("E01");
			return;
		}
		uint32_t crc;
		if (!generic_crc32(cur_target, &crc, addr, addr_length))
			gdb_putpacketz("E03");
		else
			gdb_putpacket_f("C%lx", crc);
	}
}

/*
 * qC queries are for the current thread. We don't support threads but GDB 11 and 12 require this,
 * so we always answer that the current thread is thread 1.
 */
static void exec_q_c(const char *packet, const size_t length)
{
	(void)packet;
	(void)length;
	gdb_putpacketz("QC1");
}

/*
 * qfThreadInfo queries are required in GDB 11 and 12 as these GDBs require the server to support
 * threading even when there's only the possibility for one thread to exist. In this instance,
 * we have to tell GDB that there is a single active thread so it doesn't think the "thread" died.
 * qsThreadInfo will always follow qfThreadInfo when we reply as we have to specify 'l' at the
 * end to terminate the list.. GDB doesn't like this not happening.
 */
static void exec_q_thread_info(const char *packet, const size_t length)
{
	(void)length;
	if (packet[-11] == 'f' && cur_target)
		gdb_putpacketz("m1");
	else
		gdb_putpacketz("l");
}

/* 
 * GDB will send the packet 'QStartNoAckMode' to enable NoAckMode
 * 
 * To tell GDB to not use NoAckMode do the following before connnecting to the probe:
 * set remote noack-packet off
 */
static void exec_q_noackmode(const char *packet, const size_t length)
{
	(void)packet;
	(void)length;
	gdb_set_noackmode(true);
	gdb_putpacketz("OK");
}

static const cmd_executer_s q_commands[] = {
	{"qRcmd,", exec_q_rcmd},
	{"qSupported", exec_q_supported},
	{"qXfer:memory-map:read::", exec_q_memory_map},
	{"qXfer:features:read:target.xml:", exec_q_feature_read},
	{"qCRC:", exec_q_crc},
	{"qC", exec_q_c},
	{"qfThreadInfo", exec_q_thread_info},
	{"qsThreadInfo", exec_q_thread_info},
	{"QStartNoAckMode", exec_q_noackmode},
	{NULL, NULL},
};

static void handle_kill_target(void)
{
	if (cur_target) {
		target_reset(cur_target);
		target_detach(cur_target);
		last_target = cur_target;
		cur_target = NULL;
	}
}

static void handle_q_packet(char *packet, const size_t length)
{
	if (exec_command(packet, length, q_commands))
		return;
	DEBUG_GDB("*** Unsupported packet: %s\n", packet);
	gdb_putpacket("", 0);
}

static void handle_v_packet(char *packet, const size_t plen)
{
	uint32_t addr = 0;
	uint32_t len = 0;
	int bin;

	if (sscanf(packet, "vAttach;%08" PRIx32, &addr) == 1) {
		/* Attach to remote target processor */
		cur_target = target_attach_n(addr, &gdb_controller);
		if (cur_target) {
			morse(NULL, false);
			/*
			 * We don't actually support threads, but GDB 11 and 12 can't work without
			 * us saying we attached to thread 1.. see the following for the low-down of this:
			 * https://sourceware.org/bugzilla/show_bug.cgi?id=28405
			 * https://sourceware.org/bugzilla/show_bug.cgi?id=28874
			 * https://sourceware.org/pipermail/gdb-patches/2021-December/184171.html
			 * https://sourceware.org/pipermail/gdb-patches/2022-April/188058.html
			 * https://sourceware.org/pipermail/gdb-patches/2022-July/190869.html
			 */
			gdb_putpacketz("T05thread:1;");
		} else
			gdb_putpacketz("E01");

	} else if (!strncmp(packet, "vKill;", 6U)) {
		/* Kill the target - we don't actually care about the PID that follows "vKill;" */
		handle_kill_target();
		gdb_putpacketz("OK");

	} else if (!strncmp(packet, "vRun", 4U)) {
		/* Parse command line for get_cmdline semihosting call */
		char cmdline[83];
		char *pcmdline = cmdline;
		char *tok = packet + 4U;
		if (*tok == ';')
			++tok;
		cmdline[0] = '\0';
		while (*tok != '\0') {
			if (strlen(cmdline) + 3U >= sizeof(cmdline))
				break;
			if (*tok == ';') {
				*pcmdline++ = ' ';
				pcmdline[0] = '\0';
				tok++;
				continue;
			}
			/* isxdigit expects int, to handle EOF */
			if (isxdigit((int8_t)tok[0U]) && isxdigit((int8_t)tok[1U])) {
				unhexify(pcmdline, tok, 2U);
				if ((*pcmdline == ' ') || (*pcmdline == '\\')) {
					pcmdline[1U] = *pcmdline;
					*pcmdline++ = '\\';
				}
				pcmdline++;
				tok += 2U;
				pcmdline[0] = '\0';
				continue;
			}
			break;
		}
#ifdef ENABLE_RTT
		/* force searching rtt control block */
		rtt_found = false;
#endif
		/* Run target program. For us (embedded) this means reset. */
		if (cur_target) {
			target_set_cmdline(cur_target, cmdline);
			target_reset(cur_target);
			gdb_putpacketz("T05");
		} else if (last_target) {
			cur_target = target_attach(last_target, &gdb_controller);

			/* If we were able to attach to the target again */
			if (cur_target) {
				target_set_cmdline(cur_target, cmdline);
				target_reset(cur_target);
				morse(NULL, false);
				gdb_putpacketz("T05");
			} else
				gdb_putpacketz("E01");

		} else
			gdb_putpacketz("E01");

	} else if (sscanf(packet, "vFlashErase:%08" PRIx32 ",%08" PRIx32, &addr, &len) == 2) {
		/* Erase Flash Memory */
		DEBUG_GDB("Flash Erase %08" PRIX32 " %08" PRIX32 "\n", addr, len);
		if (!cur_target) {
			gdb_putpacketz("EFF");
			return;
		}

		if (target_flash_erase(cur_target, addr, len))
			gdb_putpacketz("OK");
		else {
			target_flash_complete(cur_target);
			gdb_putpacketz("EFF");
		}

	} else if (sscanf(packet, "vFlashWrite:%08" PRIx32 ":%n", &addr, &bin) == 1) {
		/* Write Flash Memory */
		const uint32_t count = plen - bin;
		DEBUG_GDB("Flash Write %08" PRIX32 " %08" PRIX32 "\n", addr, count);
		if (cur_target && target_flash_write(cur_target, addr, (void *)packet + bin, count))
			gdb_putpacketz("OK");
		else {
			target_flash_complete(cur_target);
			gdb_putpacketz("EFF");
		}

	} else if (!strcmp(packet, "vFlashDone")) {
		/* Commit flash operations. */
		if (target_flash_complete(cur_target))
			gdb_putpacketz("OK");
		else
			gdb_putpacketz("EFF");

	} else if (!strcmp(packet, "vStopped")) {
		if (gdb_needs_detach_notify) {
			gdb_putpacketz("W00");
			gdb_needs_detach_notify = false;
		} else
			gdb_putpacketz("OK");

	} else {
		/*
		 * The vMustReplyEmpty is used as a feature test to check how gdbserver handles
		 * unknown packets, don't print an error message for it.
		 */
		if (strcmp(packet, "vMustReplyEmpty") != 0)
			DEBUG_GDB("*** Unsupported packet: %s\n", packet);
		gdb_putpacket("", 0);
	}
}

static void handle_z_packet(char *packet, const size_t plen)
{
	(void)plen;

	uint32_t type;
	uint32_t len;
	uint32_t addr;
	sscanf(packet, "%*[zZ]%" PRIu32 ",%08" PRIx32 ",%" PRIu32, &type, &addr, &len);

	int ret = 0;
	if (packet[0] == 'Z')
		ret = target_breakwatch_set(cur_target, type, addr, len);
	else
		ret = target_breakwatch_clear(cur_target, type, addr, len);

	if (ret < 0)
		gdb_putpacketz("E01");
	else if (ret > 0)
		gdb_putpacketz("");
	else
		gdb_putpacketz("OK");
}

void gdb_main(char *pbuf, size_t pbuf_size, size_t size)
{
	gdb_main_loop(&gdb_controller, pbuf, pbuf_size, size, false);
}

/* halt target */
void gdb_halt_target(void)
{
	if (cur_target)
		target_halt_request(cur_target);
	else
		/* Report "target exited" if no target */
		gdb_putpacketz("W00");
}

/* poll running target */
void gdb_poll_target(void)
{
	if (!cur_target) {
		/* Report "target exited" if no target */
		gdb_putpacketz("W00");
		return;
	}

	/* poll target */
	target_addr_t watch;
	target_halt_reason_e reason = target_halt_poll(cur_target, &watch);
	if (!reason)
		return;

	/* switch polling off */
	gdb_target_running = false;
	SET_RUN_STATE(0);

	/* Translate reason to GDB signal */
	switch (reason) {
	case TARGET_HALT_ERROR:
		gdb_putpacket_f("X%02X", GDB_SIGLOST);
		morse("TARGET LOST.", true);
		break;
	case TARGET_HALT_REQUEST:
		gdb_putpacket_f("T%02X", GDB_SIGINT);
		break;
	case TARGET_HALT_WATCHPOINT:
		gdb_putpacket_f("T%02Xwatch:%08X;", GDB_SIGTRAP, watch);
		break;
	case TARGET_HALT_FAULT:
		gdb_putpacket_f("T%02X", GDB_SIGSEGV);
		break;
	default:
		gdb_putpacket_f("T%02X", GDB_SIGTRAP);
	}
}
