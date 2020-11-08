/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 * Additions by Dave Marples <dave@marples.net>
 * Modifications (C) 2020 Uwe Bonnes (bon@elektron.ikp.physik.tu-darmstadt.de)
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
#include "cl_utils.h"
#include "hex_utils.h"

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
		DEBUG_WARN("Remote Start failed, error %s\n",
				c ? (char *)&(construct[1]) : "unknown");
      return -1;
    }
	DEBUG_PROBE("Remote is %s\n", &construct[1]);
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
      DEBUG_WARN(" platform_target_get_power failed, error %s\n",
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
		DEBUG_WARN("platform_target_set_power failed, error %s\n",
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
		DEBUG_WARN("platform_srst_set_val failed, error %s\n",
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
		DEBUG_WARN("platform_srst_set_val failed, error %s\n",
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
      DEBUG_WARN("platform_target_voltage failed, error %s\n",
			  s ? (char *)&(construct[1]) : "unknown");
      exit(- 1);
    }
	return (char *)&construct[1];
}

static uint32_t remote_adiv5_dp_read(ADIv5_DP_t *dp, uint16_t addr)
{
	(void)dp;
	uint8_t construct[REMOTE_MAX_MSG_SIZE];
	int s = snprintf((char *)construct, REMOTE_MAX_MSG_SIZE, REMOTE_DP_READ_STR,
					 dp->dp_jd_index, addr);
	platform_buffer_write(construct, s);
	s = platform_buffer_read(construct, REMOTE_MAX_MSG_SIZE);
	if ((!s) || (construct[0] == REMOTE_RESP_ERR)) {
		DEBUG_WARN("%s error %d\n", __func__, s);
	}
    uint32_t dest[1];
	unhexify(dest, (const char*)&construct[1], 4);
	DEBUG_PROBE("dp_read addr %04x: %08" PRIx32 "\n", dest[0]);
	return dest[0];
}

static uint32_t remote_adiv5_low_access(
	ADIv5_DP_t *dp, uint8_t RnW, uint16_t addr, uint32_t value)
{
	(void)dp;
	uint8_t construct[REMOTE_MAX_MSG_SIZE];
	int s = snprintf((char *)construct, REMOTE_MAX_MSG_SIZE,
					 REMOTE_LOW_ACCESS_STR, dp->dp_jd_index, RnW, addr, value);
	platform_buffer_write(construct, s);
	s = platform_buffer_read(construct, REMOTE_MAX_MSG_SIZE);
	if ((!s) || (construct[0] == REMOTE_RESP_ERR)) {
		DEBUG_WARN("%s error %d\n", __func__, s);
	}
    uint32_t dest[1];
	unhexify(dest, (const char*)&construct[1], 4);
	return dest[0];
}

static uint32_t remote_adiv5_ap_read(ADIv5_AP_t *ap, uint16_t addr)
{
	uint8_t construct[REMOTE_MAX_MSG_SIZE];
	int s = snprintf((char *)construct, REMOTE_MAX_MSG_SIZE,REMOTE_AP_READ_STR,
					 ap->dp->dp_jd_index, ap->apsel, addr);
	platform_buffer_write(construct, s);
	s = platform_buffer_read(construct, REMOTE_MAX_MSG_SIZE);
	if ((!s) || (construct[0] == REMOTE_RESP_ERR)) {
		DEBUG_WARN("%s error %d\n", __func__, s);
	}
    uint32_t dest[1];
	unhexify(dest, (const char*)&construct[1], 4);
	return dest[0];
}

static void remote_adiv5_ap_write(ADIv5_AP_t *ap, uint16_t addr, uint32_t value)
{
	uint8_t construct[REMOTE_MAX_MSG_SIZE];
	int s = snprintf((char *)construct, REMOTE_MAX_MSG_SIZE,REMOTE_AP_WRITE_STR,
					ap->dp->dp_jd_index,  ap->apsel, addr, value);
	platform_buffer_write(construct, s);
	s = platform_buffer_read(construct, REMOTE_MAX_MSG_SIZE);
	if ((!s) || (construct[0] == REMOTE_RESP_ERR)) {
		DEBUG_WARN("%s error %d\n", __func__, s);
	}
	return;
}

#if 0
static void remote_mem_read(
	ADIv5_AP_t *ap, void *dest, uint32_t src, size_t len)
{
	(void)ap;
	if (len == 0)
		return;
	DEBUG_WIRE("memread @ %" PRIx32 " len %ld, start: \n",
			   src, len);
	uint8_t construct[REMOTE_MAX_MSG_SIZE];
	int s;
	int batchsize = (REMOTE_MAX_MSG_SIZE - 32) / 2;
	while(len) {
		int count = len;
		if (count > batchsize)
			count = batchsize;
		s = snprintf(construct, REMOTE_MAX_MSG_SIZE,
					 REMOTE_MEM_READ_STR, src, count);
		platform_buffer_write(construct, s);

		s = platform_buffer_read(construct, REMOTE_MAX_MSG_SIZE);
		if ((s > 0) && (construct[0] == REMOTE_RESP_OK)) {
			unhexify(dest, (const char*)&construct[1], count);
			src += count;
			dest += count;
			len -= count;

			continue;
		} else {
			if(construct[0] == REMOTE_RESP_ERR) {
				ap->dp->fault = 1;
				DEBUG_WARN("%s returned REMOTE_RESP_ERR at addr: 0x%08x\n",
					   __func__, src);
				break;
			} else {
				DEBUG_WARN("%s error %d\n", __func__, s);
				break;
			}
		}
	}
}
#endif

static void remote_ap_mem_read(
	ADIv5_AP_t *ap, void *dest, uint32_t src, size_t len)
{
	(void)ap;
	if (len == 0)
		return;
	char construct[REMOTE_MAX_MSG_SIZE];
	int batchsize = (REMOTE_MAX_MSG_SIZE - 0x20) / 2;
	while(len) {
		int s;
		int count = len;
		if (count > batchsize)
			count = batchsize;
		s = snprintf(construct, REMOTE_MAX_MSG_SIZE,
					 REMOTE_AP_MEM_READ_STR, ap->dp->dp_jd_index, ap->apsel, ap->csw, src, count);
		platform_buffer_write((uint8_t*)construct, s);
		s = platform_buffer_read((uint8_t*)construct, REMOTE_MAX_MSG_SIZE);
		if ((s > 0) && (construct[0] == REMOTE_RESP_OK)) {
			unhexify(dest, (const char*)&construct[1], count);
			src  += count;
			dest += count;
			len  -= count;
			continue;
		} else {
			if(construct[0] == REMOTE_RESP_ERR) {
				ap->dp->fault = 1;
				DEBUG_WARN("%s returned REMOTE_RESP_ERR at apsel %d, "
					   "addr: 0x%08" PRIx32 "\n", __func__, ap->apsel, src);
				break;
			} else {
				DEBUG_WARN("%s error %d around 0x%08" PRIx32 "\n",
					   __func__, s, src);
				break;
			}
		}
	}
}

static void remote_ap_mem_write_sized(
	ADIv5_AP_t *ap, uint32_t dest, const void *src, size_t len,
	enum align align)
{
	(void)ap;
	if (len == 0)
		return;
	char construct[REMOTE_MAX_MSG_SIZE];
	/* (5 * 1 (char)) + (2 * 2 (bytes)) + (3 * 8 (words)) */
	int batchsize = (REMOTE_MAX_MSG_SIZE - 0x30) / 2;
	while (len) {
		int count = len;
		if (count > batchsize)
			count = batchsize;
		int s = snprintf(construct, REMOTE_MAX_MSG_SIZE,
						 REMOTE_AP_MEM_WRITE_SIZED_STR,
						 ap->dp->dp_jd_index, ap->apsel, ap->csw, align, dest, count);
		char *p = construct + s;
		hexify(p, src, count);
		p += 2 * count;
		src  += count;
		dest += count;
		len  -= count;
		*p++ = REMOTE_EOM;
		*p   = 0;
		platform_buffer_write((uint8_t*)construct, p - construct);

		s = platform_buffer_read((uint8_t*)construct, REMOTE_MAX_MSG_SIZE);
		if ((s > 0) && (construct[0] == REMOTE_RESP_OK))
			continue;
		if ((s > 0) && (construct[0] == REMOTE_RESP_ERR)) {
			ap->dp->fault = 1;
			DEBUG_WARN("%s returned REMOTE_RESP_ERR at apsel %d, "
				   "addr: 0x%08x\n", __func__, ap->apsel, dest);
		} else {
			DEBUG_WARN("%s error %d around address 0x%08" PRIx32 "\n",
				   __func__, s, dest);
			break;
		}
	}
}

void remote_adiv5_dp_defaults(ADIv5_DP_t *dp)
{
	uint8_t construct[REMOTE_MAX_MSG_SIZE];
	int s = snprintf((char *)construct, REMOTE_MAX_MSG_SIZE, "%s",
					 REMOTE_HL_CHECK_STR);
	platform_buffer_write(construct, s);
	s = platform_buffer_read(construct, REMOTE_MAX_MSG_SIZE);
	if ((!s) || (construct[0] == REMOTE_RESP_ERR) ||
		((construct[1] - '0') <  REMOTE_HL_VERSION)) {
		DEBUG_WARN(
			"Please update BMP firmware for substantial speed increase!\n");
		return;
	}
	dp->low_access = remote_adiv5_low_access;
	dp->dp_read    = remote_adiv5_dp_read;
	dp->ap_write   = remote_adiv5_ap_write;
	dp->ap_read    = remote_adiv5_ap_read;
	dp->mem_read   = remote_ap_mem_read;
	dp->mem_write_sized = remote_ap_mem_write_sized;
}

void remote_add_jtag_dev(int i, const jtag_dev_t *jtag_dev)
{
	uint8_t construct[REMOTE_MAX_MSG_SIZE];
	int s = snprintf((char *)construct, REMOTE_MAX_MSG_SIZE,
					 REMOTE_JTAG_ADD_DEV_STR,
					 i,
					 jtag_dev->dr_prescan,
					 jtag_dev->dr_postscan,
					 jtag_dev->ir_len,
					 jtag_dev->ir_prescan,
					 jtag_dev->ir_postscan,
					 jtag_dev->current_ir);
	platform_buffer_write(construct, s);
	s = platform_buffer_read(construct, REMOTE_MAX_MSG_SIZE);
	/* No check for error here. Done in remote_adiv5_dp_defaults!*/
}
