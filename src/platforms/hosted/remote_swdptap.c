/*
 * This file is part of the Black Magic Debug project.
 *
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 * Modified by Dave Marples <dave@marples.net>
 * Modification (C) 2020 Uwe Bonnes (bon@elektron.ikp.physik.tu-darmstadt.de)
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

/* MPSSE bit-banging SW-DP interface over FTDI with loop unrolled.
 * Speed is sensible.
 */

#include <stdio.h>
#include <assert.h>

#include "general.h"
#include "remote.h"
#include "bmp_remote.h"

static bool swdptap_seq_in_parity(uint32_t *res, int ticks);
static uint32_t swdptap_seq_in(int ticks);
static void swdptap_seq_out(uint32_t MS, int ticks);
static void swdptap_seq_out_parity(uint32_t MS, int ticks);

int remote_swdptap_init(swd_proc_t *swd_proc)
{
	DEBUG_WIRE("remote_swdptap_init\n");
	uint8_t construct[REMOTE_MAX_MSG_SIZE];
	int s;
	s = sprintf((char *)construct,"%s", REMOTE_SWDP_INIT_STR);
	platform_buffer_write(construct, s);

	s = platform_buffer_read(construct, REMOTE_MAX_MSG_SIZE);
	if ((!s) || (construct[0] == REMOTE_RESP_ERR)) {
		DEBUG_WARN("swdptap_init failed, error %s\n",
				s ? (char *)&(construct[1]) : "unknown");
		exit(-1);
    }

	swd_proc->swdptap_seq_in  = swdptap_seq_in;
	swd_proc->swdptap_seq_in_parity  = swdptap_seq_in_parity;
	swd_proc->swdptap_seq_out = swdptap_seq_out;
	swd_proc->swdptap_seq_out_parity  = swdptap_seq_out_parity;

  return 0;
}

static bool swdptap_seq_in_parity(uint32_t *res, int ticks)
{
	uint8_t construct[REMOTE_MAX_MSG_SIZE];
	int s;

	s = sprintf((char *)construct, REMOTE_SWDP_IN_PAR_STR, ticks);
	platform_buffer_write(construct, s);

	s = platform_buffer_read(construct, REMOTE_MAX_MSG_SIZE);
	if ((s<2) || (construct[0] == REMOTE_RESP_ERR)) {
		DEBUG_WARN("swdptap_seq_in_parity failed, error %s\n",
				s ? (char *)&(construct[1]) : "short response");
		exit(-1);
	}

	*res=remotehston(-1, (char *)&construct[1]);
	DEBUG_PROBE("swdptap_seq_in_parity  %2d ticks: %08" PRIx32 " %s\n",
				 ticks, *res, (construct[0] != REMOTE_RESP_OK) ? "ERR" : "OK");
	return (construct[0] != REMOTE_RESP_OK);
}

static uint32_t swdptap_seq_in(int ticks)
{
	uint8_t construct[REMOTE_MAX_MSG_SIZE];
	int s;

	s = sprintf((char *)construct, REMOTE_SWDP_IN_STR, ticks);
	platform_buffer_write(construct,s);

	s = platform_buffer_read(construct, REMOTE_MAX_MSG_SIZE);
	if ((s<2) || (construct[0] == REMOTE_RESP_ERR)) {
      DEBUG_WARN("swdptap_seq_in failed, error %s\n",
			  s ? (char *)&(construct[1]) : "short response");
      exit(-1);
    }
	uint32_t res = remotehston(-1,(char *)&construct[1]);
	DEBUG_PROBE("swdptap_seq_in         %2d ticks: %08" PRIx32 "\n",
				 ticks, res);
	return res;
}

static void swdptap_seq_out(uint32_t MS, int ticks)
{
	uint8_t construct[REMOTE_MAX_MSG_SIZE];
	int s;

	DEBUG_PROBE("swdptap_seq_out        %2d ticks: %08" PRIx32 "\n",
				 ticks, MS);
	s = sprintf((char *)construct,REMOTE_SWDP_OUT_STR, ticks, MS);
	platform_buffer_write(construct, s);

	s=platform_buffer_read(construct, REMOTE_MAX_MSG_SIZE);
	if ((s < 1) || (construct[0] == REMOTE_RESP_ERR)) {
      DEBUG_WARN("swdptap_seq_out failed, error %s\n",
			  s ? (char *)&(construct[1]) : "short response");
      exit(-1);
    }
}

static void swdptap_seq_out_parity(uint32_t MS, int ticks)
{
	uint8_t construct[REMOTE_MAX_MSG_SIZE];
	int s;

	DEBUG_PROBE("swdptap_seq_out_parity %2d ticks: %08" PRIx32 "\n",
				 ticks, MS);
	s = sprintf((char *)construct, REMOTE_SWDP_OUT_PAR_STR, ticks, MS);
	platform_buffer_write(construct, s);

	s = platform_buffer_read(construct, REMOTE_MAX_MSG_SIZE);
	if ((s < 1) || (construct[1] == REMOTE_RESP_ERR)){
      DEBUG_WARN("swdptap_seq_out_parity failed, error %s\n",
			  s ? (char *)&(construct[2]) : "short response");
      exit(-1);
    }
}
