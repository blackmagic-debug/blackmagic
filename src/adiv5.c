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

/* This file implements the transport generic functions of the 
 * ARM Debug Interface v5 Architecure Specification, ARM doc IHI0031A.
 *
 * Issues:
 * Currently doesn't use ROM table for introspection, just assumes
 * the device is Cortex-M3.
 */

#include <stdio.h>
#include <stdlib.h>

#include "general.h"
#include "jtag_scan.h"
#include "gdb_packet.h"
#include "adiv5.h"

#include "target.h"

#ifndef DO_RESET_SEQ
#define DO_RESET_SEQ 0
#endif

static const char adiv5_driver_str[] = "ARM ADIv5 MEM-AP";

ADIv5_DP_t *adiv5_dp_list;
ADIv5_AP_t adiv5_aps[5];
int adiv5_ap_count;

static int ap_check_error(struct target_s *target);

static int ap_mem_read_words(struct target_s *target, uint32_t *dest, uint32_t src, int len);
static int ap_mem_write_words(struct target_s *target, uint32_t dest, const uint32_t *src, int len);
static int ap_mem_read_bytes(struct target_s *target, uint8_t *dest, uint32_t src, int len);
static int ap_mem_write_bytes(struct target_s *target, uint32_t dest, const uint8_t *src, int len);

void adiv5_free_all(void)
{
	ADIv5_DP_t *dp;

	while(adiv5_dp_list) {
		dp = adiv5_dp_list->next;
		free(adiv5_dp_list);
		adiv5_dp_list = dp;
	}

	adiv5_ap_count = 0;
}


void adiv5_dp_init(ADIv5_DP_t *dp)
{
	uint32_t ctrlstat;

	dp->next = adiv5_dp_list;
	adiv5_dp_list = dp;

	ctrlstat = adiv5_dp_read(dp, ADIV5_DP_CTRLSTAT);

	/* Write request for system and debug power up */
	adiv5_dp_write(dp, ADIV5_DP_CTRLSTAT, 
			ctrlstat |= ADIV5_DP_CTRLSTAT_CSYSPWRUPREQ | 
				ADIV5_DP_CTRLSTAT_CDBGPWRUPREQ);
	/* Wait for acknowledge */
	while(((ctrlstat = adiv5_dp_read(dp, ADIV5_DP_CTRLSTAT)) & 
		(ADIV5_DP_CTRLSTAT_CSYSPWRUPACK | ADIV5_DP_CTRLSTAT_CDBGPWRUPACK)) != 
		(ADIV5_DP_CTRLSTAT_CSYSPWRUPACK | ADIV5_DP_CTRLSTAT_CDBGPWRUPACK));

	if(DO_RESET_SEQ) {
		/* This AP reset logic is described in ADIv5, but fails to work 
		 * correctly on STM32.  CDBGRSTACK is never asserted, and we 
		 * just wait forever. 
		 */

		/* Write request for debug reset */
		adiv5_dp_write(dp, ADIV5_DP_CTRLSTAT, 
				ctrlstat |= ADIV5_DP_CTRLSTAT_CDBGRSTREQ);
		/* Wait for acknowledge */
		while(!((ctrlstat = adiv5_dp_read(dp, ADIV5_DP_CTRLSTAT)) & 
				ADIV5_DP_CTRLSTAT_CDBGRSTACK));

		/* Write request for debug reset release */
		adiv5_dp_write(dp, ADIV5_DP_CTRLSTAT, 
				ctrlstat &= ~ADIV5_DP_CTRLSTAT_CDBGRSTREQ);
		/* Wait for acknowledge */
		while(adiv5_dp_read(dp, ADIV5_DP_CTRLSTAT) & 
				ADIV5_DP_CTRLSTAT_CDBGRSTACK);
	}

	/* Probe for APs on this DP */
	for(int i = 0; i < 256; i++) {
		ADIv5_AP_t *ap = &adiv5_aps[adiv5_ap_count];
		target *t;

		/* Assume valid and try to read IDR */
		ap->dp = dp;
		ap->apsel = i;
		ap->idr = adiv5_ap_read(ap, ADIV5_AP_IDR);

		if(!ap->idr) /* IDR Invalid - Should we not continue here? */
			break;

		/* We have a valid AP, adding to list */
		ap->cfg = adiv5_ap_read(ap, ADIV5_AP_CFG);
		ap->base = adiv5_ap_read(ap, ADIV5_AP_BASE);
		/* Should probe further here... */

		/* Prepend to target list... */
		t = target_new(sizeof(struct target_ap_s));
		((struct target_ap_s *)t)->ap = ap;

		t->driver = adiv5_driver_str;
		t->check_error = ap_check_error;

		t->mem_read_words = ap_mem_read_words;
		t->mem_write_words = ap_mem_write_words;
		t->mem_read_bytes = ap_mem_read_bytes;
		t->mem_write_bytes = ap_mem_write_bytes;

		/* The rest sould only be added after checking ROM table */
		cortexm_probe(t);

		adiv5_ap_count++;
	}
}

void adiv5_dp_write_ap(ADIv5_DP_t *dp, uint8_t addr, uint32_t value)
{
	adiv5_dp_low_access(dp, ADIV5_LOW_AP, ADIV5_LOW_WRITE, addr, value);
}

uint32_t adiv5_dp_read_ap(ADIv5_DP_t *dp, uint8_t addr)
{
	uint32_t ret;

	adiv5_dp_low_access(dp, ADIV5_LOW_AP, ADIV5_LOW_READ, addr, 0);
	ret = adiv5_dp_low_access(dp, ADIV5_LOW_DP, ADIV5_LOW_READ, 
				ADIV5_DP_RDBUFF, 0);

	return ret;
}


static int 
ap_check_error(struct target_s *target)
{
	ADIv5_AP_t *ap = adiv5_target_ap(target);
	return adiv5_dp_error(ap->dp);
}

static int 
ap_mem_read_words(struct target_s *target, uint32_t *dest, uint32_t src, int len)
{
	ADIv5_AP_t *ap = adiv5_target_ap(target);

	len >>= 2;

	adiv5_ap_write(ap, ADIV5_AP_CSW, 0xA2000052);
	adiv5_dp_low_access(ap->dp, ADIV5_LOW_AP, ADIV5_LOW_WRITE, 
					ADIV5_AP_TAR, src);
	adiv5_dp_low_access(ap->dp, ADIV5_LOW_AP, ADIV5_LOW_READ, 
					ADIV5_AP_DRW, 0);
	while(--len) 
		*dest++ = adiv5_dp_low_access(ap->dp, ADIV5_LOW_AP, 
					ADIV5_LOW_READ, ADIV5_AP_DRW, 0);
	
	*dest++ = adiv5_dp_low_access(ap->dp, ADIV5_LOW_DP, ADIV5_LOW_READ, 
					ADIV5_DP_RDBUFF, 0);

	return 0;
}

static int 
ap_mem_read_bytes(struct target_s *target, uint8_t *dest, uint32_t src, int len)
{
	ADIv5_AP_t *ap = adiv5_target_ap(target);
	uint32_t tmp = src;

	adiv5_ap_write(ap, ADIV5_AP_CSW, 0xA2000050);
	adiv5_dp_low_access(ap->dp, ADIV5_LOW_AP, ADIV5_LOW_WRITE, 
					ADIV5_AP_TAR, src);
	adiv5_dp_low_access(ap->dp, ADIV5_LOW_AP, ADIV5_LOW_READ, 
					ADIV5_AP_DRW, 0);
	while(--len) {
		tmp = adiv5_dp_low_access(ap->dp, 1, 1, ADIV5_AP_DRW, 0);
		*dest++ = (tmp >> ((src++ & 0x3) << 3) & 0xFF);
	}
	tmp = adiv5_dp_low_access(ap->dp, 0, 1, ADIV5_DP_RDBUFF, 0);
	*dest++ = (tmp >> ((src++ & 0x3) << 3) & 0xFF);

	return 0;
}


static int 
ap_mem_write_words(struct target_s *target, uint32_t dest, const uint32_t *src, int len)
{
	ADIv5_AP_t *ap = adiv5_target_ap(target);

	len >>= 2;

	adiv5_ap_write(ap, ADIV5_AP_CSW, 0xA2000052);
	adiv5_dp_low_access(ap->dp, ADIV5_LOW_AP, ADIV5_LOW_WRITE, 
					ADIV5_AP_TAR, dest);
	while(len--) 
		adiv5_dp_low_access(ap->dp, ADIV5_LOW_AP, ADIV5_LOW_WRITE, 
					ADIV5_AP_DRW, *src++);
	
	return 0;
}

static int 
ap_mem_write_bytes(struct target_s *target, uint32_t dest, const uint8_t *src, int len)
{
	ADIv5_AP_t *ap = adiv5_target_ap(target);

	adiv5_ap_write(ap, ADIV5_AP_CSW, 0xA2000050);
	adiv5_dp_low_access(ap->dp, ADIV5_LOW_AP, ADIV5_LOW_WRITE, 
					ADIV5_AP_TAR, dest);
	while(len--) {
		uint32_t tmp = (uint32_t)*src++ << ((dest++ & 3) << 3);
		adiv5_dp_low_access(ap->dp, ADIV5_LOW_AP, ADIV5_LOW_WRITE, 
					ADIV5_AP_DRW, tmp);
	}
	return 0;
}



uint32_t adiv5_ap_mem_read(ADIv5_AP_t *ap, uint32_t addr)
{
	adiv5_ap_write(ap, ADIV5_AP_CSW, 0xA2000052);
	adiv5_ap_write(ap, ADIV5_AP_TAR, addr);
	return adiv5_ap_read(ap, ADIV5_AP_DRW);
}

void adiv5_ap_mem_write(ADIv5_AP_t *ap, uint32_t addr, uint32_t value)
{
	adiv5_ap_write(ap, ADIV5_AP_CSW, 0xA2000052);
	adiv5_ap_write(ap, ADIV5_AP_TAR, addr);
	adiv5_ap_write(ap, ADIV5_AP_DRW, value);
}

void adiv5_ap_mem_write_halfword(ADIv5_AP_t *ap, uint32_t addr, uint16_t value)
{
	uint32_t v = value;
	if (addr & 2)
		v <<= 16;

	adiv5_ap_write(ap, ADIV5_AP_CSW, 0xA2000051);
	adiv5_ap_write(ap, ADIV5_AP_TAR, addr);
	adiv5_ap_write(ap, ADIV5_AP_DRW, v);
}

void adiv5_ap_write(ADIv5_AP_t *ap, uint8_t addr, uint32_t value)
{
	adiv5_dp_write(ap->dp, ADIV5_DP_SELECT, 
			((uint32_t)ap->apsel << 24)|(addr & 0xF0));
	adiv5_dp_write_ap(ap->dp, addr, value);
}

uint32_t adiv5_ap_read(ADIv5_AP_t *ap, uint8_t addr)
{
	uint32_t ret;
	adiv5_dp_write(ap->dp, ADIV5_DP_SELECT, 
			((uint32_t)ap->apsel << 24)|(addr & 0xF0));
	ret = adiv5_dp_read_ap(ap->dp, addr);
	return ret;
}

