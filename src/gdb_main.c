/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011 Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 * Copyright (C) 2022-2024 1BitSquared <info@1bitsquared.com>
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

/* This file implements the GDB Remote Serial Debugging protocol as
 * described in "Debugging with GDB" build from GDB source.
 *
 * Originally written for GDB 6.8, updated and tested with GDB 7.2.
 */

#include "general.h"
#include "platform.h"
#include "ctype.h"
#include "hex_utils.h"
#include "buffer_utils.h"
#include "gdb_if.h"
#include "gdb_packet.h"
#include "gdb_main.h"
#include "target.h"
#include "target_internal.h"
#include "semihosting.h"
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

#include <stdlib.h>

typedef enum gdb_signal {
	GDB_SIGINT = 2,
	GDB_SIGTRAP = 5,
	GDB_SIGSEGV = 11,
	GDB_SIGLOST = 29,
} gdb_signal_e;

#define ERROR_IF_NO_TARGET()         \
	if (!cur_target) {               \
		gdb_put_packet_error(0xffU); \
		break;                       \
	}

typedef struct cmd_executer {
	const char *cmd_prefix;
	void (*func)(const char *packet, size_t len);
} cmd_executer_s;

target_s *cur_target;
target_s *last_target;
bool gdb_target_running = false;
static bool gdb_needs_detach_notify = false;

static void handle_q_packet(const gdb_packet_s *packet);
static void handle_v_packet(const gdb_packet_s *packet);
static void handle_z_packet(const gdb_packet_s *packet);
static void handle_kill_target(void);

static void gdb_target_destroy_callback(target_controller_s *tc, target_s *t)
{
	(void)tc;
	if (cur_target == t) {
		gdb_put_notification_str("%Stop:W00");
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
};

/* execute gdb remote command stored in 'pbuf'. returns immediately, no busy waiting. */
int32_t gdb_main_loop(target_controller_s *const tc, const gdb_packet_s *const packet, const bool in_syscall)
{
	bool single_step = false;
	const char *rest = NULL;

	/* GDB protocol main loop */
	switch (packet->data[0]) {
	/* Implementation of these is mandatory! */
	case 'g': { /* 'g': Read general registers */
		ERROR_IF_NO_TARGET();
		const size_t reg_size = target_regs_size(cur_target);
		if (reg_size) {
			uint8_t *gp_regs = alloca(reg_size);
			target_regs_read(cur_target, gp_regs);
			gdb_put_packet_hex(gp_regs, reg_size);
		} else {
			/**
			 * Register data is unavailable
			 * See: https://sourceware.org/gdb/current/onlinedocs/gdb.html/Packets.html#read-registers-packet
			 * 
			 * ... the stub may also return a string of literal ‘x’ in place of the register data digits,
			 * to indicate that the corresponding register’s value is unavailable.
			 */
			gdb_put_packet_str("xx");
		}
		break;
	}
	case 'm': { /* 'm addr,len': Read len bytes from addr */
		uint32_t addr, len;
		ERROR_IF_NO_TARGET();
		if (read_hex32(packet->data + 1, &rest, &addr, ',') && read_hex32(rest, NULL, &len, READ_HEX_NO_FOLLOW)) {
			if (len > GDB_PACKET_BUFFER_SIZE / 2U) {
				gdb_put_packet_error(2U);
				break;
			}
			DEBUG_GDB("m packet: addr = %" PRIx32 ", len = %" PRIx32 "\n", addr, len);
			uint8_t *mem = alloca(len);
			if (target_mem32_read(cur_target, mem, addr, len))
				gdb_put_packet_error(1U);
			else
				gdb_put_packet_hex(mem, len);
		} else
			gdb_put_packet_error(0xffU);
		break;
	}
	case 'G': { /* 'G XX': Write general registers */
		ERROR_IF_NO_TARGET();
		const size_t reg_size = target_regs_size(cur_target);
		if (reg_size) {
			uint8_t *gp_regs = alloca(reg_size);
			unhexify(gp_regs, packet->data + 1, reg_size);
			target_regs_write(cur_target, gp_regs);
		}
		gdb_put_packet_ok();
		break;
	}
	case 'M': { /* 'M addr,len:XX': Write len bytes to addr */
		uint32_t addr = 0;
		uint32_t len = 0;
		ERROR_IF_NO_TARGET();
		if (read_hex32(packet->data + 1, &rest, &addr, ',') && read_hex32(rest, &rest, &len, ':')) {
			if (len > (packet->size - (size_t)(rest - packet->data)) / 2U) {
				gdb_put_packet_error(2U);
				break;
			}
			DEBUG_GDB("M packet: addr = %" PRIx32 ", len = %" PRIx32 "\n", addr, len);
			uint8_t *mem = alloca(len);
			unhexify(mem, rest, len);
			if (target_mem32_write(cur_target, addr, mem, len))
				gdb_put_packet_error(1U);
			else
				gdb_put_packet_ok();
		} else
			gdb_put_packet_error(0xffU);
		break;
	}
	/*
	 * '[m|M|g|G|c][thread-id]' : Set the thread ID for the given subsequent operation
	 * (we don't actually care which as we only care about the TID for whether to send OK or an error)
	 */
	case 'H': {
		uint32_t thread_id = 0;
		/*
		 * Since we don't care about the operation just skip it but check there is at least 3 characters
		 * in the packet.
		 */
		if (packet->size >= 3 && read_hex32(packet->data + 2, NULL, &thread_id, READ_HEX_NO_FOLLOW) && thread_id <= 1)
			gdb_put_packet_ok();
		else
			gdb_put_packet_error(1U);
		break;
	}
	case 's': /* 's [addr]': Single step [start at addr] */
		single_step = true;
		BMD_FALLTHROUGH
	case 'c': /* 'c [addr]': Continue [at addr] */
	case 'C': /* 'C sig[;addr]': Continue with signal [at addr] */
		if (!cur_target) {
			gdb_put_packet_str("X1D");
			break;
		}

		target_halt_resume(cur_target, single_step);
		SET_RUN_STATE(true);
		BMD_FALLTHROUGH
	case '?': { /* '?': Request reason for target halt */
		/*
		 * This packet isn't documented as being mandatory,
		 * but GDB doesn't work without it.
		 */
		if (!cur_target) {
			gdb_put_packet_str("W00"); /* Report "target exited" if no target */
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
			if (!read_hex32(packet->data + 1, NULL, &reg, READ_HEX_NO_FOLLOW))
				gdb_put_packet_error(0xffU);
			else {
				uint8_t val[8];
				const size_t length = target_reg_read(cur_target, reg, val, sizeof(val));
				if (length != 0)
					gdb_put_packet_hex(val, length);
				else
					gdb_put_packet_error(0xffU);
			}
		} else {
			/**
			 * Register data is unavailable
			 * See: https://sourceware.org/gdb/current/onlinedocs/gdb.html/Packets.html#read-registers-packet
			 * 
			 * ... the stub may also return a string of literal ‘x’ in place of the register data digits,
			 * to indicate that the corresponding register’s value is unavailable.
			 */
			gdb_put_packet_str("xx");
		}
		break;
	}
	case 'P': { /* Write single register */
		ERROR_IF_NO_TARGET();
		if (cur_target->reg_write) {
			/*
			 * P packets are in the form P[reg]=<value> where [reg] is a hexadecimal-encoded register number
			 * and <value> is a hexadecimal encoded value to write to the register. Seeing a register and
			 * its value must be present, use tools like strtoul() and unhexify() to extract the value.
			 * For now we only support 32-bit targets which have registers the same width, so constrain
			 * the value buffer accordingly. If the `=` is missing it's an invalid packet.
			 */
			uint32_t reg;

			/* Extract the register number and check that '=' follows it */
			if (!read_hex32(packet->data + 1, &rest, &reg, '=')) {
				gdb_put_packet_error(0xffU);
				break;
			}
			const size_t value_length = strlen(rest) / 2U;
			/* If the value is bigger than 4 bytes report error */
			if (value_length > 4U) {
				gdb_put_packet_error(0xffU);
				break;
			}
			uint8_t value[4] = {0};
			unhexify(value, rest, value_length);
			/* Finally, write the converted value to the target */
			if (target_reg_write(cur_target, reg, value, sizeof(value)) != 0)
				gdb_put_packet_ok();
			else
				gdb_put_packet_error(0xffU);
		} else {
			gdb_put_packet_ok();
		}
		break;
	}

	case 'F': /* Semihosting call finished */
		if (in_syscall)
			/* Trim off the 'F' before calling semihosting_reply so that it doesn't have to skip it */
			return semihosting_reply(tc, packet->data + 1);
		else {
			DEBUG_GDB("*** F packet when not in syscall! '%s'\n", packet->data);
			gdb_put_packet_empty();
		}
		break;

	case '!': /* Enable Extended GDB Protocol. */
		/*
		 * This doesn't do anything, we support the extended
		 * protocol anyway, but GDB will never send us a 'R'
		 * packet unless we answer 'OK' here.
		 */
		gdb_put_packet_ok();
		break;

	case '\x04':
	case 'D': /* GDB 'detach' command. */
#if CONFIG_BMDA == 1
		if (shutdown_bmda)
			return 0;
#endif
		if (cur_target) {
			SET_RUN_STATE(true);
			target_detach(cur_target);
			last_target = cur_target;
			cur_target = NULL;
		}
		if (packet->data[0] == 'D')
			gdb_put_packet_ok();
		else /* packet->data[0] == '\x04' */
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
		target_addr32_t addr;
		uint32_t len;
		ERROR_IF_NO_TARGET();
		if (read_hex32(packet->data + 1, &rest, &addr, ',') && read_hex32(rest, &rest, &len, ':')) {
			if (len > (packet->size - (size_t)(rest - packet->data))) {
				gdb_put_packet_error(2U);
				break;
			}
			DEBUG_GDB("X packet: addr = %" PRIx32 ", len = %" PRIx32 "\n", addr, len);
			if (target_mem32_write(cur_target, addr, rest, len))
				gdb_put_packet_error(1U);
			else
				gdb_put_packet_ok();
		} else
			gdb_put_packet_error(0xffU);
		break;
	}

	case 'Q': /* General set packet */
	case 'q': /* General query packet */
		handle_q_packet(packet);
		break;

	case 'v': /* Verbose command packet */
		handle_v_packet(packet);
		break;

	/* These packet implement hardware break-/watchpoints */
	case 'Z': /* Z type,addr,len: Set breakpoint packet */
	case 'z': /* z type,addr,len: Clear breakpoint packet */
		ERROR_IF_NO_TARGET();
		handle_z_packet(packet);
		break;

	default: /* Packet not implemented */
		DEBUG_GDB("*** Unsupported packet: %s\n", packet->data);
		gdb_put_packet_empty();
	}
	return 0;
}

static bool exec_command(const char *const packet, const size_t length, const cmd_executer_s *exec)
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

	const int result = command_process(cur_target, data);
	if (result < 0)
		gdb_put_packet_empty();
	else if (result == 0)
		gdb_put_packet_ok();
	else {
		static const char response[] = "Failed\n";
		gdb_put_packet_hex(response, ARRAY_LENGTH(response));
	}
}

static void handle_q_string_reply(const char *reply, const char *param)
{
	const size_t reply_length = strlen(reply);
	uint32_t addr = 0;
	uint32_t len = 0;
	const char *rest = NULL;

	if (!read_hex32(param, &rest, &addr, ',') || !read_hex32(rest, NULL, &len, READ_HEX_NO_FOLLOW)) {
		gdb_put_packet_error(1U);
		return;
	}
	if (addr > reply_length) {
		gdb_put_packet_error(1U);
		return;
	}
	if (addr == reply_length) {
		gdb_put_packet_str("l");
		return;
	}
	size_t output_len = reply_length - addr;
	if (output_len > len)
		output_len = len;
	gdb_put_packet("m", 1U, reply + addr, output_len, false);
}

static void exec_q_supported(const char *packet, const size_t length)
{
	(void)packet;
	(void)length;

	/*
	 * This might be the first packet of a GDB connection, If NoAckMode is enabled it might
	 * be because the previous session was terminated abruptly, and we need to acknowledge
	 * the first packet of the new session.
	 * 
	 * If NoAckMode was intentionally enabled before (e.g. LLDB enables NoAckMode first),
	 * the acknowledgment should be safely ignored.
	 */
	if (gdb_noackmode())
		gdb_packet_ack(true);

	/*
	 * The Remote Protocol documentation is not clear on what format the PacketSize feature should be in,
	 * according to the GDB source code (as of version 15.2) it should be a hexadecimal encoded number
	 * to be parsed by strtoul() with a base of 16.
	 */
	gdb_putpacket_str_f("PacketSize=%x;qXfer:memory-map:read+;qXfer:features:read+;"
						"vContSupported+" GDB_QSUPPORTED_NOACKMODE,
		GDB_PACKET_BUFFER_SIZE);

	/*
	 * If an acknowledgement was received in response while in NoAckMode, then NoAckMode is probably
	 * not meant to be enabled and is likely a result of the previous session being terminated
	 * abruptly. Disable NoAckMode to prevent any further issues.
	 */
	if (gdb_noackmode() && gdb_packet_get_ack(100U)) {
		DEBUG_GDB("Received acknowledgment in NoAckMode, likely result of a session being terminated abruptly\n");
		gdb_set_noackmode(false);
	}
}

static void exec_q_memory_map(const char *packet, const size_t length)
{
	(void)length;
	target_s *target = cur_target;

	/* Read target XML memory map */
	if (!target)
		target = last_target;
	if (!target) {
		gdb_put_packet_error(1U);
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
		gdb_put_packet_error(1U);
		return;
	}
	const char *const description = target_regs_description(target);
	handle_q_string_reply(description ? description : "", packet);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
	free((void *)description);
#pragma GCC diagnostic pop
}

static void exec_q_crc(const char *packet, const size_t length)
{
	(void)length;
	uint32_t addr;
	uint32_t addr_length;
	const char *rest = NULL;
	if (read_hex32(packet, &rest, &addr, ',') && read_hex32(rest, NULL, &addr_length, READ_HEX_NO_FOLLOW)) {
		if (!cur_target) {
			gdb_put_packet_error(1U);
			return;
		}
		uint32_t crc;
		if (!bmd_crc32(cur_target, &crc, addr, addr_length))
			gdb_put_packet_error(3U);
		else
			gdb_putpacket_str_f("C%" PRIx32, crc);
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
	gdb_put_packet_str("QC1");
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
		gdb_put_packet_str("m1");
	else
		gdb_put_packet_str("l");
}

/*
 * GDB will send the packet 'QStartNoAckMode' to enable NoAckMode
 *
 * To tell GDB to not use NoAckMode do the following before connecting to the probe:
 * set remote noack-packet off
 */
static void exec_q_noackmode(const char *packet, const size_t length)
{
	(void)packet;
	(void)length;
	/*
	 * This might be the first packet of a LLDB connection, If NoAckMode is enabled it might
	 * be because the previous session was terminated abruptly, and we need to acknowledge
	 * the first packet of the new session.
	 */
	if (gdb_noackmode())
		gdb_packet_ack(true);
	gdb_set_noackmode(true);
	gdb_put_packet_ok();
}

/*
 * qAttached queries determine if GDB attached to an existing process, or a new one.
 * What that means in practical terms, is whether the session ending should `k` or `D`
 * (kill or detach) the target. A reply of '1' indicates existing and therefore `D`.
 * A reply of '0' indicates a new process and therefore `k`.
 *
 * We use this distinction by replying with whether the target tollerates `target_reset()`
 * or not, with targets that have nRST inhibited due to bad reset behaviour, using the '1'
 * reply.
 *
 * The request can be optionally followed by a PID which should only happen when we advertise
 * multiprocess extensions support. Therefore, if it is followed by a PID, we signal an error
 * with an `Enn` reply packet as we do not support these extensions.
 */
static void exec_q_attached(const char *packet, const size_t length)
{
	/* If the packet has a trailing PID, or we're not attached to anything, error response */
	if ((length && packet[0] == ':') || !cur_target)
		gdb_put_packet_error(1U);
	/* Check if the target tollerates being reset */
	else if (cur_target->target_options & TOPT_INHIBIT_NRST)
		gdb_put_packet_str("1"); /* It does not. */
	else
		gdb_put_packet_str("0"); /* It does tolelrate reset */
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
	{"qAttached", exec_q_attached},
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

static void handle_q_packet(const gdb_packet_s *const packet)
{
	if (exec_command(packet->data, packet->size, q_commands))
		return;
	DEBUG_GDB("*** Unsupported packet: %s\n", packet->data);
	gdb_put_packet_empty();
}

static void exec_v_attach(const char *const packet, const size_t length)
{
	(void)length;

	uint32_t addr;
	if (read_hex32(packet, NULL, &addr, READ_HEX_NO_FOLLOW)) {
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
			gdb_putpacket_str_f("T%02Xthread:1;", GDB_SIGTRAP);
		} else
			gdb_put_packet_error(1U);

	} else {
		DEBUG_GDB("*** Unsupported packet: %s\n", packet);
		gdb_put_packet_empty();
	}
}

static void exec_v_kill(const char *packet, const size_t length)
{
	(void)packet;
	(void)length;
	/* Kill the target - we don't actually care about the PID that follows "vKill;" */
	handle_kill_target();
	gdb_put_packet_ok();
}

static void exec_v_run(const char *packet, const size_t length)
{
	(void)length;
	/* Parse command line for SYS_GET_CMDLINE semihosting call */
	char cmdline[MAX_CMDLINE];
	size_t offset = 0;
	const char *tok = packet;
	if (tok[0] == ';')
		++tok;
	while (*tok != '\0') {
		/* Check if there's space for another character */
		if (offset + 1U >= MAX_CMDLINE)
			break;
		/* Translate ';' delimeters into spaces */
		if (tok[0] == ';') {
			cmdline[offset++] = ' ';
			++tok;
			continue;
		}
		/* If the next thing's a hex digit pair, decode that */
		if (is_hex(tok[0U]) && is_hex(tok[1U])) {
			unhexify(cmdline + offset, tok, 2U);
			/* If the character decoded is ' ' or '\' then prefix it with a leading '\' */
			if (cmdline[offset] == ' ' || cmdline[offset] == '\\') {
				/* First check if there's space */
				if (offset + 2U >= MAX_CMDLINE)
					break;
				cmdline[offset + 1] = cmdline[offset];
				cmdline[offset++] = '\\';
			}
			++offset;
			tok += 2U;
			continue;
		}
		break;
	}
	cmdline[offset] = '\0';
	/* Reset the semihosting SYS_CLOCK start point */
	semihosting_wallclock_epoch = UINT32_MAX;
#ifdef ENABLE_RTT
	/* Force searching for the RTT control block */
	rtt_found = false;
#endif
	/* Run target program. For us (embedded) this means reset. */
	if (cur_target) {
		target_set_cmdline(cur_target, cmdline, offset);
		target_reset(cur_target);
		gdb_put_packet_str("T05");
	} else if (last_target) {
		cur_target = target_attach(last_target, &gdb_controller);

		/* If we were able to attach to the target again */
		if (cur_target) {
			target_set_cmdline(cur_target, cmdline, offset);
			target_reset(cur_target);
			morse(NULL, false);
			gdb_put_packet_str("T05");
		} else
			gdb_put_packet_error(1U);

	} else
		gdb_put_packet_error(1U);
}

static void exec_v_cont(const char *packet, const size_t length)
{
	(void)length;
	/* Check if this is a "vCont?" packet */
	if (packet[0] == '?') {
		/*
		 * It is, so reply with what we support doing when receiving the command version of this packet.
		 *
		 * We support 'c' (continue), 'C' (continue + signal), and 's' (step) actions.
		 * If we didn't support both 'c' and 'C', then GDB would disable vCont usage even though
		 * 'C' doesn't make any sense in our context.
		 * See https://github.com/bminor/binutils-gdb/blob/de2efa143e3652d69c278dd1eb10a856593917c0/gdb/remote.c#L6526
		 * for more details.
		 *
		 * TODO: Support the 't' (stop) action needed for non-stop debug so GDB can request a halt.
		 */
		gdb_put_packet_str("vCont;c;C;s;t");
		return;
	}

	/* Otherwise it's a standard `vCont` packet, check if we're presently attached to a target */
	if (!cur_target) {
		gdb_put_packet_error(1U);
		return;
	}

	bool single_step = false;
	switch (packet[1]) {
	case 's': /* 's': Single step */
		single_step = true;
		BMD_FALLTHROUGH
	case 'c': /* 'c': Continue */
	case 'C': /* 'C sig': Continue with signal */
		if (!cur_target) {
			gdb_put_packet_str("X1D");
			break;
		}

		target_halt_resume(cur_target, single_step);
		SET_RUN_STATE(true);
		gdb_target_running = true;
		break;
	default:
		break;
	}
}

static void exec_v_flash_erase(const char *packet, const size_t length)
{
	(void)length;
	uint32_t addr;
	uint32_t len;
	const char *rest = NULL;

	if (read_hex32(packet, &rest, &addr, ',') && read_hex32(rest, NULL, &len, READ_HEX_NO_FOLLOW)) {
		/* Erase Flash Memory */
		DEBUG_GDB("Flash Erase %08" PRIX32 " %08" PRIX32 "\n", addr, len);
		if (!cur_target) {
			gdb_put_packet_error(0xffU);
			return;
		}

		if (target_flash_erase(cur_target, addr, len))
			gdb_put_packet_ok();
		else {
			target_flash_complete(cur_target);
			gdb_put_packet_error(0xffU);
		}
	} else
		gdb_put_packet_error(0xffU);
}

static void exec_v_flash_write(const char *packet, const size_t length)
{
	uint32_t addr;
	const char *rest = NULL;
	if (read_hex32(packet, &rest, &addr, ':')) {
		/* Write Flash Memory */
		const uint32_t count = length - (size_t)(rest - packet);
		DEBUG_GDB("Flash Write %08" PRIX32 " %08" PRIX32 "\n", addr, count);
		if (cur_target && target_flash_write(cur_target, addr, (const uint8_t *)rest, count))
			gdb_put_packet_ok();
		else {
			target_flash_complete(cur_target);
			gdb_put_packet_error(0xffU);
		}
	} else
		gdb_put_packet_error(0xffU);
}

static void exec_v_flash_done(const char *packet, const size_t length)
{
	(void)packet;
	(void)length;
	/* Commit flash operations. */
	if (target_flash_complete(cur_target))
		gdb_put_packet_ok();
	else
		gdb_put_packet_error(0xffU);
}

static void exec_v_stopped(const char *packet, const size_t length)
{
	(void)packet;
	(void)length;
	if (gdb_needs_detach_notify) {
		gdb_put_packet_str("W00");
		gdb_needs_detach_notify = false;
	} else
		gdb_put_packet_ok();
}

static const cmd_executer_s v_commands[] = {
	{"vAttach;", exec_v_attach},
	{"vKill;", exec_v_kill},
	{"vRun", exec_v_run},
	{"vCont", exec_v_cont},
	{"vFlashErase:", exec_v_flash_erase},
	{"vFlashWrite:", exec_v_flash_write},
	{"vFlashDone", exec_v_flash_done},
	{"vStopped", exec_v_stopped},
	{NULL, NULL},
};

static void handle_v_packet(const gdb_packet_s *const packet)
{
	if (exec_command(packet->data, packet->size, v_commands))
		return;

#ifndef DEBUG_GDB_IS_NOOP
	/*
	 * The vMustReplyEmpty is used as a feature test to check how gdbserver handles
	 * unknown packets, don't print an error message for it.
	 */
	static const char must_reply_empty[] = "vMustReplyEmpty";
	if (strncmp(packet->data, must_reply_empty, ARRAY_LENGTH(must_reply_empty) - 1U) != 0)
		DEBUG_GDB("*** Unsupported packet: %s\n", packet->data);
#endif
	gdb_put_packet_empty();
}

static void handle_z_packet(const gdb_packet_s *const packet)
{
	uint32_t type;
	uint32_t len;
	uint32_t addr;
	const char *rest = NULL;

	if (read_dec32(packet->data + 1U, &rest, &type, ',') && read_hex32(rest, &rest, &addr, ',') &&
		read_dec32(rest, NULL, &len, READ_HEX_NO_FOLLOW)) {
		int ret = 0;
		if (packet->data[0] == 'Z')
			ret = target_breakwatch_set(cur_target, type, addr, len);
		else
			ret = target_breakwatch_clear(cur_target, type, addr, len);

		/* If the target handler was unable to set/clear the break/watch-point, return an error */
		if (ret < 0)
			gdb_put_packet_error(1U);
		/* If the handler does not support the kind requested, return empty string */
		else if (ret > 0)
			gdb_put_packet_empty();
		/* Otherwise let GDB know that everything went well */
		else
			gdb_put_packet_ok();
	} else
		gdb_put_packet_error(1U);
}

void gdb_main(const gdb_packet_s *const packet)
{
	gdb_main_loop(&gdb_controller, packet, false);
}

/* Request halt on the active target */
void gdb_halt_target(void)
{
	if (cur_target)
		target_halt_request(cur_target);
	else
		/* Report "target exited" if no target */
		gdb_put_packet_str("W00");
}

/* Poll the running target to see if it halted yet */
void gdb_poll_target(void)
{
	if (!cur_target) {
		/* Report "target exited" if no target */
		gdb_put_packet_str("W00");
		return;
	}

	/* poll target */
	target_addr64_t watch;
	target_halt_reason_e reason = target_halt_poll(cur_target, &watch);
	if (!reason)
		return;

	/* switch polling off */
	gdb_target_running = false;
	SET_RUN_STATE(0);

	/* Translate reason to GDB signal */
	switch (reason) {
	case TARGET_HALT_ERROR:
		gdb_putpacket_str_f("X%02X", GDB_SIGLOST);
		morse("TARGET LOST.", true);
		break;
	case TARGET_HALT_REQUEST:
		gdb_putpacket_str_f("T%02Xthread:1;", GDB_SIGINT);
		break;
	case TARGET_HALT_WATCHPOINT:
		gdb_putpacket_str_f(
			"T%02Xwatch:%0" PRIX32 "%08" PRIX32 ";", GDB_SIGTRAP, (uint32_t)(watch >> 32U), (uint32_t)watch);
		break;
	case TARGET_HALT_FAULT:
		gdb_putpacket_str_f("T%02Xthread:1;", GDB_SIGSEGV);
		break;
	default:
		gdb_putpacket_str_f("T%02Xthread:1;", GDB_SIGTRAP);
	}
}
