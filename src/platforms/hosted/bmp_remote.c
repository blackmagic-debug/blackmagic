/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 * Additions by Dave Marples <dave@marples.net>
 * Additions by Uwe Bonnes (bon@elektron.ikp.physik.tu-darmstadt.de)
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
#include "gdb_if.h"
#include "version.h"
#include "platform.h"
#include "remote.h"
#include "target.h"
#include "bmp_remote.h"

#include <assert.h>
#include <sys/time.h>
#include <sys/time.h>
#include <errno.h>
#include <string.h>

#include "adiv5.h"

int remote_init(void)
{
	char construct[REMOTE_MAX_MSG_SIZE];
	int c = snprintf(construct, REMOTE_MAX_MSG_SIZE, "%s", REMOTE_START_STR);
	platform_buffer_write((uint8_t *)construct, c);
	c = platform_buffer_read((uint8_t *)construct, REMOTE_MAX_MSG_SIZE);

	if ((!c) || (construct[0] == REMOTE_RESP_ERR)) {
		fprintf(stderr,"Remote Start failed, error %s\n",
				c ? (char *)&(construct[1]) : "unknown");
      return -1;
    }

	printf("Remote is %s\n", &construct[1]);
	return 0;
}

bool remote_target_get_power(void)
{
	uint8_t construct[REMOTE_MAX_MSG_SIZE];
	int s;

	s=snprintf((char *)construct, REMOTE_MAX_MSG_SIZE, "%s",
			   REMOTE_PWR_GET_STR);
	platform_buffer_write(construct, s);

	s = platform_buffer_read(construct, REMOTE_MAX_MSG_SIZE);

	if ((!s) || (construct[0] == REMOTE_RESP_ERR)) {
      fprintf(stderr," platform_target_get_power failed, error %s\n",
			  s ? (char *)&(construct[1]) : "unknown");
      exit (-1);
    }

	return (construct[1] == '1');
}

void remote_target_set_power(bool power)
{
	uint8_t construct[REMOTE_MAX_MSG_SIZE];
	int s;

	s = snprintf((char *)construct, REMOTE_MAX_MSG_SIZE,REMOTE_PWR_SET_STR,
				 power ? '1' : '0');
	platform_buffer_write(construct, s);

	s = platform_buffer_read(construct, REMOTE_MAX_MSG_SIZE);

	if ((!s) || (construct[0] == REMOTE_RESP_ERR)) {
		fprintf(stderr, "platform_target_set_power failed, error %s\n",
				s ? (char *)&(construct[1]) : "unknown");
      exit(-1);
    }
}

void remote_srst_set_val(bool assert)
{
	uint8_t construct[REMOTE_MAX_MSG_SIZE];
	int s;

	s = snprintf((char *)construct, REMOTE_MAX_MSG_SIZE, REMOTE_SRST_SET_STR,
				 assert ? '1' : '0');
	platform_buffer_write(construct, s);

	s = platform_buffer_read(construct, REMOTE_MAX_MSG_SIZE);

	if ((!s) || (construct[0] == REMOTE_RESP_ERR)) {
		fprintf(stderr, "platform_srst_set_val failed, error %s\n",
				s ? (char *)&(construct[1]) : "unknown");
      exit(-1);
    }
}

bool remote_srst_get_val(void)
{
	uint8_t construct[REMOTE_MAX_MSG_SIZE];
	int s;

	s = snprintf((char *)construct, REMOTE_MAX_MSG_SIZE,"%s",
				 REMOTE_SRST_GET_STR);
	platform_buffer_write(construct, s);

	s = platform_buffer_read(construct, REMOTE_MAX_MSG_SIZE);

	if ((!s) || (construct[0] == REMOTE_RESP_ERR)) {
      fprintf(stderr, "platform_srst_set_val failed, error %s\n",
			  s ? (char *)&(construct[1]) : "unknown");
      exit(-1);
    }
	return (construct[1] == '1');
}

const char *remote_target_voltage(void)
{
	static uint8_t construct[REMOTE_MAX_MSG_SIZE];
	int s;

	s = snprintf((char *)construct, REMOTE_MAX_MSG_SIZE," %s",
				 REMOTE_VOLTAGE_STR);
	platform_buffer_write(construct, s);

	s = platform_buffer_read(construct, REMOTE_MAX_MSG_SIZE);

	if ((!s) || (construct[0] == REMOTE_RESP_ERR)) {
      fprintf(stderr, "platform_target_voltage failed, error %s\n",
			  s ? (char *)&(construct[1]) : "unknown");
      exit(- 1);
    }
	return (char *)&construct[1];
}
