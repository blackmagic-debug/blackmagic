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
#include "hex_utils.h"
#include "gdb_if.h"
#include "gdb_packet.h"
#include "gdb_main.h"
#include "jtagtap.h"
#include "jtag_scan.h"
#include "adiv5.h"
#include "target.h"
#include "command.h"
#include "crc32.h"

#define BUF_SIZE	1024

#define ERROR_IF_NO_TARGET()	\
	if(!cur_target) { gdb_putpacketz("EFF"); break; }

static char pbuf[BUF_SIZE];

static target *cur_target;
static target *last_target;

static void handle_q_packet(char *packet, int len);
static void handle_v_packet(char *packet, int len);
static void handle_z_packet(char *packet, int len);

static void gdb_target_destroy_callback(target *t)
{
	if (cur_target == t)
		cur_target = NULL;

	if (last_target == t)
		last_target = NULL;
}

void
gdb_main(void)
{
	int size;
	bool single_step = false;
	char last_activity = 0;

	DEBUG("Entring GDB protocol main loop\n");
	/* GDB protocol main loop */
	while(1) {
		SET_IDLE_STATE(1);
		size = gdb_getpacket(pbuf, BUF_SIZE);
		SET_IDLE_STATE(0);
	continue_activity:
		switch(pbuf[0]) {
		/* Implementation of these is mandatory! */
		case 'g': { /* 'g': Read general registers */
			ERROR_IF_NO_TARGET();
			uint8_t arm_regs[target_regs_size(cur_target)];
			target_regs_read(cur_target, arm_regs);
			gdb_putpacket(hexify(pbuf, arm_regs, sizeof(arm_regs)),
			              sizeof(arm_regs) * 2);
			break;
			}
		case 'm': {	/* 'm addr,len': Read len bytes from addr */
			uint32_t addr, len;
			ERROR_IF_NO_TARGET();
			sscanf(pbuf, "m%" SCNx32 ",%" SCNx32, &addr, &len);
			DEBUG("m packet: addr = %" PRIx32 ", len = %" PRIx32 "\n", addr, len);
			uint8_t mem[len];
			target_mem_read(cur_target, mem, addr, len);
			if(target_check_error(cur_target))
				gdb_putpacketz("E01");
			else
				gdb_putpacket(hexify(pbuf, mem, len), len*2);
			break;
			}
		case 'G': {	/* 'G XX': Write general registers */
			ERROR_IF_NO_TARGET();
			uint8_t arm_regs[target_regs_size(cur_target)];
			unhexify(arm_regs, &pbuf[1], sizeof(arm_regs));
			target_regs_write(cur_target, arm_regs);
			gdb_putpacketz("OK");
			break;
			}
		case 'M': { /* 'M addr,len:XX': Write len bytes to addr */
			uint32_t addr, len;
			int hex;
			ERROR_IF_NO_TARGET();
			sscanf(pbuf, "M%" SCNx32 ",%" SCNx32 ":%n", &addr, &len, &hex);
			DEBUG("M packet: addr = %" PRIx32 ", len = %" PRIx32 "\n", addr, len);
			uint8_t mem[len];
			unhexify(mem, pbuf + hex, len);
			target_mem_write(cur_target, addr, mem, len);
			if(target_check_error(cur_target))
				gdb_putpacketz("E01");
			else
				gdb_putpacketz("OK");
			break;
			}
		case 's':	/* 's [addr]': Single step [start at addr] */
			single_step = true;
			// Fall through to resume target
		case 'c':	/* 'c [addr]': Continue [at addr] */
			if(!cur_target) {
				gdb_putpacketz("X1D");
				break;
			}

			target_halt_resume(cur_target, single_step);
			SET_RUN_STATE(1);
			single_step = false;
			// Fall through to wait for target halt
		case '?': {	/* '?': Request reason for target halt */
			/* This packet isn't documented as being mandatory,
			 * but GDB doesn't work without it. */
			uint32_t watch_addr;
			int sig;

			if(!cur_target) {
				/* Report "target exited" if no target */
				gdb_putpacketz("W00");
				break;
			}

			last_activity = pbuf[0];
			/* Wait for target halt */
			while(!(sig = target_halt_wait(cur_target))) {
				unsigned char c = gdb_if_getchar_to(0);
				if((c == '\x03') || (c == '\x04')) {
					target_halt_request(cur_target);
					last_activity = 's';
				}
			}
			SET_RUN_STATE(0);

			/* Negative signal indicates we're in a syscall */
			if (sig < 0)
				break;

			/* Target disappeared */
			if (cur_target == NULL) {
				gdb_putpacket_f("X%02X", sig);
				break;
			}

			/* Report reason for halt */
			if(target_check_hw_wp(cur_target, &watch_addr)) {
				/* Watchpoint hit */
				gdb_putpacket_f("T%02Xwatch:%08X;", sig, watch_addr);
			} else {
				gdb_putpacket_f("T%02X", sig);
			}
			break;
			}
		case 'F': {	/* Semihosting call finished */
			int retcode, errcode, items;
			char c, *p;
			if (pbuf[1] == '-')
				p = &pbuf[2];
			else
				p = &pbuf[1];
			items = sscanf(p, "%x,%x,%c", &retcode, &errcode, &c);
			if (pbuf[1] == '-')
				retcode = -retcode;

			target_hostio_reply(cur_target, retcode, errcode);

			/* if break is requested */
			if (items == 3 && c == 'C') {
				gdb_putpacketz("T02");
				break;
			}

			pbuf[0] = last_activity;
			goto continue_activity;
		}

		/* Optional GDB packet support */
		case '!':	/* Enable Extended GDB Protocol. */
			/* This doesn't do anything, we support the extended
			 * protocol anyway, but GDB will never send us a 'R'
			 * packet unless we answer 'OK' here.
			 */
			gdb_putpacketz("OK");
			break;

		case 0x04:
		case 'D':	/* GDB 'detach' command. */
			if(cur_target)
				target_detach(cur_target);
			last_target = cur_target;
			cur_target = NULL;
			gdb_putpacketz("OK");
			break;

		case 'k':	/* Kill the target */
			if(cur_target) {
				target_reset(cur_target);
				target_detach(cur_target);
				last_target = cur_target;
				cur_target = NULL;
			}
			break;

		case 'r':	/* Reset the target system */
		case 'R':	/* Restart the target program */
			if(cur_target)
				target_reset(cur_target);
			else if(last_target) {
				cur_target = target_attach(last_target,
						gdb_target_destroy_callback);
				target_reset(cur_target);
			}
			break;

		case 'X': { /* 'X addr,len:XX': Write binary data to addr */
			uint32_t addr, len;
			int bin;
			ERROR_IF_NO_TARGET();
			sscanf(pbuf, "X%" SCNx32 ",%" SCNx32 ":%n", &addr, &len, &bin);
			DEBUG("X packet: addr = %" PRIx32 ", len = %" PRIx32 "\n", addr, len);
			target_mem_write(cur_target, addr, pbuf+bin, len);
			if(target_check_error(cur_target))
				gdb_putpacketz("E01");
			else
				gdb_putpacketz("OK");
			break;
			}

		case 'q':	/* General query packet */
			handle_q_packet(pbuf, size);
			break;

		case 'v':	/* General query packet */
			handle_v_packet(pbuf, size);
			break;

		/* These packet implement hardware break-/watchpoints */
		case 'Z':	/* Z type,addr,len: Set breakpoint packet */
		case 'z':	/* z type,addr,len: Clear breakpoint packet */
			ERROR_IF_NO_TARGET();
			handle_z_packet(pbuf, size);
			break;

		default: 	/* Packet not implemented */
			DEBUG("*** Unsupported packet: %s\n", pbuf);
			gdb_putpacketz("");
		}
	}
}

static void
handle_q_string_reply(const char *str, const char *param)
{
	unsigned long addr, len;

	if (sscanf(param, "%08lx,%08lx", &addr, &len) != 2) {
		gdb_putpacketz("E01");
		return;
	}
	if (addr < strlen (str)) {
		char reply[len+2];
		reply[0] = 'm';
		strncpy (reply + 1, &str[addr], len);
		if(len > strlen(&str[addr]))
			len = strlen(&str[addr]);
		gdb_putpacket(reply, len + 1);
	} else if (addr == strlen (str)) {
		gdb_putpacketz("l");
	} else
		gdb_putpacketz("E01");
}

static void
handle_q_packet(char *packet, int len)
{
	uint32_t addr, alen;

	if(!strncmp(packet, "qRcmd,", 6)) {
		char *data;
		int datalen;

		/* calculate size and allocate buffer for command */
		datalen = (len - 6) / 2;
		data = alloca(datalen+1);
		/* dehexify command */
		unhexify(data, packet+6, datalen);
		data[datalen] = 0;	/* add terminating null */

		int c = command_process(cur_target, data);
		if(c < 0)
			gdb_putpacketz("");
		else if(c == 0)
			gdb_putpacketz("OK");
		else
			gdb_putpacketz("E");

	} else if (!strncmp (packet, "qSupported", 10)) {
		/* Query supported protocol features */
		gdb_putpacket_f("PacketSize=%X;qXfer:memory-map:read+;qXfer:features:read+", BUF_SIZE);

	} else if (strncmp (packet, "qXfer:memory-map:read::", 23) == 0) {
		/* Read target XML memory map */
		if((!cur_target) && last_target) {
			/* Attach to last target if detached. */
			cur_target = target_attach(last_target,
						gdb_target_destroy_callback);
		}
		if (!cur_target) {
			gdb_putpacketz("E01");
			return;
		}
		handle_q_string_reply(target_mem_map(cur_target), packet + 23);

	} else if (strncmp (packet, "qXfer:features:read:target.xml:", 31) == 0) {
		/* Read target description */
		if((!cur_target) && last_target) {
			/* Attach to last target if detached. */
			cur_target = target_attach(last_target,
						gdb_target_destroy_callback);
		}
		if (!cur_target) {
			gdb_putpacketz("E01");
			return;
		}
		handle_q_string_reply(target_tdesc(cur_target), packet + 31);
	} else if (sscanf(packet, "qCRC:%" PRIx32 ",%" PRIx32, &addr, &alen) == 2) {
		if(!cur_target) {
			gdb_putpacketz("E01");
			return;
		}
		gdb_putpacket_f("C%lx", generic_crc32(cur_target, addr, alen));

	} else {
		DEBUG("*** Unsupported packet: %s\n", packet);
		gdb_putpacket("", 0);
	}
}

static void
handle_v_packet(char *packet, int plen)
{
	unsigned long addr, len;
	int bin;
	static uint8_t flash_mode = 0;

	if (sscanf(packet, "vAttach;%08lx", &addr) == 1) {
		/* Attach to remote target processor */
		target *t;
		uint32_t i;
		for(t = target_list, i = 1; t; t = t->next, i++)
			if(i == addr) {
				cur_target = target_attach(t,
						gdb_target_destroy_callback);
				break;
			}
		if(cur_target)
			gdb_putpacketz("T05");
		else
			gdb_putpacketz("E01");

	} else if (!strcmp(packet, "vRun;")) {
		/* Run target program. For us (embedded) this means reset. */
		if(cur_target) {
			target_reset(cur_target);
			gdb_putpacketz("T05");
		} else if(last_target) {
			cur_target = target_attach(last_target,
						gdb_target_destroy_callback);

                        /* If we were able to attach to the target again */
                        if (cur_target) {
                        	target_reset(cur_target);
                        	gdb_putpacketz("T05");
                        } else	gdb_putpacketz("E01");

		} else	gdb_putpacketz("E01");

	} else if (sscanf(packet, "vFlashErase:%08lx,%08lx", &addr, &len) == 2) {
		/* Erase Flash Memory */
		DEBUG("Flash Erase %08lX %08lX\n", addr, len);
		if(!cur_target) { gdb_putpacketz("EFF"); return; }

		if(!flash_mode) {
			/* Reset target if first flash command! */
			/* This saves us if we're interrupted in IRQ context */
			target_reset(cur_target);
			flash_mode = 1;
		}
		if(target_flash_erase(cur_target, addr, len) == 0)
			gdb_putpacketz("OK");
		else
			gdb_putpacketz("EFF");

	} else if (sscanf(packet, "vFlashWrite:%08lx:%n", &addr, &bin) == 1) {
		/* Write Flash Memory */
		len = plen - bin;
		DEBUG("Flash Write %08lX %08lX\n", addr, len);
		if(cur_target && target_flash_write(cur_target, addr, (void*)packet + bin, len) == 0)
			gdb_putpacketz("OK");
		else
			gdb_putpacketz("EFF");

	} else if (!strcmp(packet, "vFlashDone")) {
		/* Commit flash operations. */
		gdb_putpacketz(target_flash_done(cur_target) ? "EFF" : "OK");
		flash_mode = 0;

	} else {
		DEBUG("*** Unsupported packet: %s\n", packet);
		gdb_putpacket("", 0);
	}
}

static void
handle_z_packet(char *packet, int plen)
{
	(void)plen;

	uint8_t set = (packet[0] == 'Z') ? 1 : 0;
	int type, len;
	uint32_t addr;
	int ret;

	/* I have no idea why this doesn't work. Seems to work
	 * with real sscanf() though... */
	//sscanf(packet, "%*[zZ]%hhd,%08lX,%hhd", &type, &addr, &len);
	type = packet[1] - '0';
	sscanf(packet + 2, ",%" PRIx32 ",%d", &addr, &len);
	switch(type) {
	case 1: /* Hardware breakpoint */
		if(set)
			ret = target_set_hw_bp(cur_target, addr);
		else
			ret = target_clear_hw_bp(cur_target, addr);
		break;

	case 2:
	case 3:
	case 4:
		if(set)
			ret = target_set_hw_wp(cur_target, type, addr, len);
		else
			ret = target_clear_hw_wp(cur_target, type, addr, len);
		break;

	default:
		gdb_putpacketz("");
		return;
	}

	if(!ret)
		gdb_putpacketz("OK");
	else
		gdb_putpacketz("E01");
}

