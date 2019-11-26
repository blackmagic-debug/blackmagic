/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2008  Black Sphere Technologies Ltd.
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

/* Low level JTAG implementation using FT2232 with libftdi.
 *
 * Issues:
 * Should share interface with swdptap.c or at least clean up...
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include <assert.h>

#include "general.h"
#include "remote.h"
#include "jtagtap.h"

/* See remote.c/.h for protocol information */

int jtagtap_init(void)
{
  uint8_t construct[PLATFORM_MAX_MSG_SIZE];
  int s;

  s=snprintf((char *)construct,PLATFORM_MAX_MSG_SIZE,"%s",REMOTE_JTAG_INIT_STR);
  platform_buffer_write(construct,s);

  s=platform_buffer_read(construct, PLATFORM_MAX_MSG_SIZE);
  if ((!s) || (construct[0]==REMOTE_RESP_ERR))
    {
      fprintf(stderr,"jtagtap_init failed, error %s\n",s?(char *)&(construct[1]):"unknown");
      exit(-1);
    }

  return 0;
}

void jtagtap_reset(void)
{
  uint8_t construct[PLATFORM_MAX_MSG_SIZE];
  int s;

  s=snprintf((char *)construct,PLATFORM_MAX_MSG_SIZE,"%s",REMOTE_JTAG_RESET_STR);
  platform_buffer_write(construct,s);

  s=platform_buffer_read(construct, PLATFORM_MAX_MSG_SIZE);
  if ((!s) || (construct[0]==REMOTE_RESP_ERR))
    {
      fprintf(stderr,"jtagtap_reset failed, error %s\n",s?(char *)&(construct[1]):"unknown");
      exit(-1);
    }
}

void jtagtap_tms_seq(uint32_t MS, int ticks)

{
  uint8_t construct[PLATFORM_MAX_MSG_SIZE];
  int s;

  s=snprintf((char *)construct,PLATFORM_MAX_MSG_SIZE,REMOTE_JTAG_TMS_STR,ticks,MS);
  platform_buffer_write(construct,s);

  s=platform_buffer_read(construct, PLATFORM_MAX_MSG_SIZE);
  if ((!s) || (construct[0]==REMOTE_RESP_ERR))
    {
      fprintf(stderr,"jtagtap_tms_seq failed, error %s\n",s?(char *)&(construct[1]):"unknown");
      exit(-1);
    }
}

void jtagtap_tdi_tdo_seq(uint8_t *DO, const uint8_t final_tms, const uint8_t *DI, int ticks)
{
  uint8_t construct[PLATFORM_MAX_MSG_SIZE];
  int s;

  uint64_t DIl=*(uint64_t *)DI;

  if(!ticks || !DI) return;

  /* Reduce the length of DI according to the bits we're transmitting */
  DIl&=(1L<<(ticks+1))-1;

  s=snprintf((char *)construct,PLATFORM_MAX_MSG_SIZE,REMOTE_JTAG_TDIDO_STR,final_tms?REMOTE_TDITDO_TMS:REMOTE_TDITDO_NOTMS,ticks,DIl);
  platform_buffer_write(construct,s);

  s=platform_buffer_read(construct, PLATFORM_MAX_MSG_SIZE);
  if ((!s) || (construct[0]==REMOTE_RESP_ERR))
    {
      fprintf(stderr,"jtagtap_tms_seq failed, error %s\n",s?(char *)&(construct[1]):"unknown");
      exit(-1);
    }

  if (DO) {
      for (unsigned int i = 1; i*8 <= (unsigned int)ticks; i++)
          DO[i - 1] = remotehston(2 , (char *)&construct[s - (i * 2)]);
  }
}

void jtagtap_tdi_seq(const uint8_t final_tms, const uint8_t *DI, int ticks)

{
  return jtagtap_tdi_tdo_seq(NULL,  final_tms, DI, ticks);
}


uint8_t jtagtap_next(uint8_t dTMS, uint8_t dTDI)

{
  uint8_t construct[PLATFORM_MAX_MSG_SIZE];
  int s;

  s=snprintf((char *)construct,PLATFORM_MAX_MSG_SIZE,REMOTE_JTAG_NEXT,dTMS?'1':'0',dTDI?'1':'0');

  platform_buffer_write(construct,s);

  s=platform_buffer_read(construct, PLATFORM_MAX_MSG_SIZE);
  if ((!s) || (construct[0]==REMOTE_RESP_ERR))
    {
      fprintf(stderr,"jtagtap_next failed, error %s\n",s?(char *)&(construct[1]):"unknown");
      exit(-1);
    }

  return remotehston(-1,(char *)&construct[1]);
}
