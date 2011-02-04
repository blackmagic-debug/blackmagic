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

#include "cortexm3.h"

#ifndef DO_RESET_SEQ
#define DO_RESET_SEQ 0
#endif

/* Bits in the DP_CTRLSTAT register */
#define CSYSPWRUPACK	0x80000000L
#define CSYSPWRUPREQ	0x40000000L
#define CDBGPWRUPACK	0x20000000L
#define CDBGPWRUPREQ	0x10000000L
#define CDBGRSTACK	0x08000000L
#define CDBGRSTREQ	0x04000000L

/* This belongs elsewhere... */
target *target_list = NULL;
target *cur_target = NULL;
target *last_target = NULL;

static const char adiv5_driver_str[] = "ARM ADIv5 MEM-AP";

ADIv5_DP_t *adiv5_dp_list;
/*
ADIv5_DP_t adiv5_dps[5];
int adiv5_dp_count;
*/

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

	ctrlstat = adiv5_dp_read(dp, DP_CTRLSTAT);

	/* Write request for system and debug power up */
	adiv5_dp_write(dp, DP_CTRLSTAT, ctrlstat |= CSYSPWRUPREQ | CDBGPWRUPREQ);
	/* Wait for acknowledge */
	while(((ctrlstat = adiv5_dp_read(dp, DP_CTRLSTAT)) & 
		(CSYSPWRUPACK | CDBGPWRUPACK)) != (CSYSPWRUPACK | CDBGPWRUPACK));

	if(DO_RESET_SEQ) {
		/* This AP reset logic is described in ADIv5, but fails to work 
		 * correctly on STM32.  CDBGRSTACK is never asserted, and we 
		 * just wait forever. 
		 */

		/* Write request for debug reset */
		adiv5_dp_write(dp, DP_CTRLSTAT, ctrlstat |= CDBGRSTREQ);
		/* Wait for acknowledge */
		while(!((ctrlstat = adiv5_dp_read(dp, DP_CTRLSTAT)) & CDBGRSTACK));

		/* Write request for debug reset release */
		adiv5_dp_write(dp, DP_CTRLSTAT, ctrlstat &= ~CDBGRSTREQ);
		/* Wait for acknowledge */
		while(adiv5_dp_read(dp, DP_CTRLSTAT) & CDBGRSTACK);
	}

	/* Probe for APs on this DP */
	for(int i = 0; i < 256; i++) {
		uint32_t idr;

		adiv5_dp_write(dp, DP_SELECT, ((uint32_t)i << 24) | 0xF0);
		idr = adiv5_dp_read_ap(dp, 0x0C); /* attempt to read IDR */

		if(idr) {	/* We have a valid AP, adding to list */
			target *t;

			adiv5_aps[adiv5_ap_count].dp = dp;
			adiv5_aps[adiv5_ap_count].apsel = i;
			adiv5_aps[adiv5_ap_count].idr = idr;
			adiv5_aps[adiv5_ap_count].cfg = adiv5_dp_read_ap(dp, 0x04);
			adiv5_aps[adiv5_ap_count].base = adiv5_dp_read_ap(dp, 0x08);
			/* Should probe further here... */

			/* Prepend to target list... */
			t = target_list;
			target_list = (void*)calloc(1, sizeof(struct target_ap_s));
			target_list->next = t;
			((struct target_ap_s *)target_list)->ap = &adiv5_aps[adiv5_ap_count];

			target_list->driver = adiv5_driver_str;
			target_list->check_error = ap_check_error;

			target_list->mem_read_words = ap_mem_read_words;
			target_list->mem_write_words = ap_mem_write_words;
			target_list->mem_read_bytes = ap_mem_read_bytes;
			target_list->mem_write_bytes = ap_mem_write_bytes;

			/* The rest sould only be added after checking ROM table */
			cm3_probe((void*)target_list);

			adiv5_ap_count++;
		} else break;
	}
}


static int 
ap_check_error(struct target_s *target)
{
	struct target_ap_s *t = (void *)target;
	return adiv5_dp_error(t->ap->dp);
}

static int 
ap_mem_read_words(struct target_s *target, uint32_t *dest, uint32_t src, int len)
{
	struct target_ap_s *t = (void *)target;

	len >>= 2;

	adiv5_ap_write(t->ap, 0x00, 0xA2000052);
	adiv5_dp_low_access(t->ap->dp, 1, 0, 0x04, src);
	adiv5_dp_low_access(t->ap->dp, 1, 1, 0x0C, 0);
	while(--len) 
		*dest++ = adiv5_dp_low_access(t->ap->dp, 1, 1, 0x0C, 0);
	
	*dest++ = adiv5_dp_low_access(t->ap->dp, 0, 1, DP_RDBUFF, 0);

	return 0;
}

static int 
ap_mem_read_bytes(struct target_s *target, uint8_t *dest, uint32_t src, int len)
{
	struct target_ap_s *t = (void *)target;
	uint32_t tmp = src;

	adiv5_ap_write(t->ap, 0x00, 0xA2000050);
	adiv5_dp_low_access(t->ap->dp, 1, 0, 0x04, src);
	adiv5_dp_low_access(t->ap->dp, 1, 1, 0x0C, 0);
	while(--len) {
		tmp = adiv5_dp_low_access(t->ap->dp, 1, 1, 0x0C, 0);
		*dest++ = (tmp >> ((src++ & 0x3) << 3) & 0xFF);
	}
	tmp = adiv5_dp_low_access(t->ap->dp, 0, 1, DP_RDBUFF, 0);
	*dest++ = (tmp >> ((src++ & 0x3) << 3) & 0xFF);

	return 0;
}


static int 
ap_mem_write_words(struct target_s *target, uint32_t dest, const uint32_t *src, int len)
{
	struct target_ap_s *t = (void *)target;

	len >>= 2;

	adiv5_ap_write(t->ap, 0x00, 0xA2000052);
	adiv5_dp_low_access(t->ap->dp, 1, 0, 0x04, dest);
	while(len--) 
		adiv5_dp_low_access(t->ap->dp, 1, 0, 0x0C, *src++);
	
	return 0;
}

static int 
ap_mem_write_bytes(struct target_s *target, uint32_t dest, const uint8_t *src, int len)
{
	struct target_ap_s *t = (void *)target;
	uint32_t tmp;

	adiv5_ap_write(t->ap, 0x00, 0xA2000050);
	adiv5_dp_low_access(t->ap->dp, 1, 0, 0x04, dest);
	while(len--) {
		tmp = (uint32_t)*src++ << ((dest++ & 3) << 3);
		adiv5_dp_low_access(t->ap->dp, 1, 0, 0x0C, tmp);
	}
	return 0;
}



uint32_t adiv5_ap_mem_read(ADIv5_AP_t *ap, uint32_t addr)
{
	adiv5_ap_write(ap, 0x00, 0xA2000052);
	adiv5_ap_write(ap, 0x04, addr);
	return adiv5_ap_read(ap, 0x0C);
}

void adiv5_ap_mem_write(ADIv5_AP_t *ap, uint32_t addr, uint32_t value)
{
	adiv5_ap_write(ap, 0x00, 0xA2000052);
	adiv5_ap_write(ap, 0x04, addr);
	adiv5_ap_write(ap, 0x0C, value);
}

void adiv5_ap_write(ADIv5_AP_t *ap, uint8_t addr, uint32_t value)
{
	adiv5_dp_write(ap->dp, DP_SELECT, 
			((uint32_t)ap->apsel << 24)|(addr & 0xF0));
	adiv5_dp_write_ap(ap->dp, addr, value);
}

uint32_t adiv5_ap_read(ADIv5_AP_t *ap, uint8_t addr)
{
	uint32_t ret;
	adiv5_dp_write(ap->dp, DP_SELECT, 
			((uint32_t)ap->apsel << 24)|(addr & 0xF0));
	ret = adiv5_dp_read_ap(ap->dp, addr);
	return ret;
}

