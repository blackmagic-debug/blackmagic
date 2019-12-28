/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2018 Uwe Bonnes (bon@elektron.ikp.physik.tu-darmstadt.de)
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 * Modified by Dave Marples <dave@marples.net>
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
#include "swdptap.h"
#include "remote.h"

int swdptap_init(void)

{
  uint8_t construct[PLATFORM_MAX_MSG_SIZE];
  int s;

  s=sprintf((char *)construct,"%s",REMOTE_SWDP_INIT_STR);
  platform_buffer_write(construct,s);

  s=platform_buffer_read(construct, PLATFORM_MAX_MSG_SIZE);
  if ((!s) || (construct[0]==REMOTE_RESP_ERR))
    {
      fprintf(stderr,"swdptap_init failed, error %s\n",s?(char *)&(construct[1]):"unknown");
      exit(-1);
    }

  return 0;
}


bool swdptap_seq_in_parity(uint32_t *res, int ticks)

{
  uint8_t construct[PLATFORM_MAX_MSG_SIZE];
  int s;

  s=sprintf((char *)construct,REMOTE_SWDP_IN_PAR_STR,ticks);
  platform_buffer_write(construct,s);

  s=platform_buffer_read(construct, PLATFORM_MAX_MSG_SIZE);
  if ((s<2) || (construct[0]==REMOTE_RESP_ERR))
    {
      fprintf(stderr,"swdptap_seq_in_parity failed, error %s\n",s?(char *)&(construct[1]):"short response");
      exit(-1);
    }

  *res=remotehston(-1,(char *)&construct[1]);
  return (construct[0]!=REMOTE_RESP_OK);
}


uint32_t swdptap_seq_in(int ticks)
{
  uint8_t construct[PLATFORM_MAX_MSG_SIZE];
  int s;

  s=sprintf((char *)construct,REMOTE_SWDP_IN_STR,ticks);
  platform_buffer_write(construct,s);

  s=platform_buffer_read(construct, PLATFORM_MAX_MSG_SIZE);
  if ((s<2) || (construct[0]==REMOTE_RESP_ERR))
    {
      fprintf(stderr,"swdptap_seq_in failed, error %s\n",s?(char *)&(construct[1]):"short response");
      exit(-1);
    }

  return remotehston(-1,(char *)&construct[1]);
}

void swdptap_seq_out(uint32_t MS, int ticks)
{
  uint8_t construct[PLATFORM_MAX_MSG_SIZE];
  int s;

  s=sprintf((char *)construct,REMOTE_SWDP_OUT_STR,ticks,MS);
  platform_buffer_write(construct,s);

  s=platform_buffer_read(construct, PLATFORM_MAX_MSG_SIZE);
  if ((s<1) || (construct[0]==REMOTE_RESP_ERR))
    {
      fprintf(stderr,"swdptap_seq_out failed, error %s\n",s?(char *)&(construct[1]):"short response");
      exit(-1);
    }
}


void swdptap_seq_out_parity(uint32_t MS, int ticks)
{
  uint8_t construct[PLATFORM_MAX_MSG_SIZE];
  int s;

  s=sprintf((char *)construct,REMOTE_SWDP_OUT_PAR_STR,ticks,MS);
  platform_buffer_write(construct,s);

  s=platform_buffer_read(construct, PLATFORM_MAX_MSG_SIZE);
  if ((s<1) || (construct[1]==REMOTE_RESP_ERR))
    {
      fprintf(stderr,"swdptap_seq_out_parity failed, error %s\n",s?(char *)&(construct[2]):"short response");
      exit(-1);
    }
}
