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

/* Provides main entry point. Initialise subsystems and enter GDB protocol loop. */

#include "general.h"
#include "platform.h"
#include "gdb_if.h"
#include "gdb_main.h"
#include "target.h"
#include "exception.h"
#include "gdb_packet.h"
#include "morse.h"
#include "command.h"
#ifdef ENABLE_RTT
#include "rtt.h"
#endif

static void bmp_poll_loop(void)
{
	SET_IDLE_STATE(false);
	while (gdb_target_running && cur_target) {
		gdb_poll_target();

		// Check again, as `gdb_poll_target()` may
		// alter these variables.
		if (!gdb_target_running || !cur_target)
			break;
		char c = gdb_if_getchar_to(0);
		if (c == '\x03' || c == '\x04')
			target_halt_request(cur_target);
#ifdef ENABLE_RTT
		else if (rtt_enabled)
			poll_rtt(cur_target);
#endif
		platform_pace_poll();
	}

	SET_IDLE_STATE(true);
	const gdb_packet_s *const packet = gdb_packet_receive();
	// If port closed and target detached, stay idle
	if (packet->data[0] != '\x04' || cur_target)
		SET_IDLE_STATE(false);
	gdb_main(packet);
}

#if CONFIG_BMDA == 1
int main(int argc, char **argv)
{
	platform_init(argc, argv);
#else
int main(void)
{
	platform_init();
#endif

	while (true) {
		TRY (EXCEPTION_ALL) {
			bmp_poll_loop();
		}
		CATCH () {
		default:
			gdb_put_packet_error(0xffU);
			target_list_free();
			gdb_outf("Uncaught exception: %s\n", exception_frame.msg);
			morse("TARGET LOST.", true);
		}
#if CONFIG_BMDA == 1
		if (shutdown_bmda)
			break;
#endif
	}

	target_list_free();
	return 0;
}
