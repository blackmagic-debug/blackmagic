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
#include "general.h"
#include "jtag_scan.h"
#include "gdb_packet.h"
#include "adiv5.h"
#include "target.h"

#ifndef DO_RESET_SEQ
#define DO_RESET_SEQ 0
#endif

static const char adiv5_driver_str[] = "ARM ADIv5 MEM-AP";

static int ap_check_error(struct target_s *target);

static int ap_mem_read_words(struct target_s *target, uint32_t *dest, uint32_t src, int len);
static int ap_mem_write_words(struct target_s *target, uint32_t dest, const uint32_t *src, int len);
static int ap_mem_read_halfwords(struct target_s *target, uint16_t *dest, uint32_t src, int len);
static int ap_mem_write_halfwords(struct target_s *target, uint32_t dest, const uint16_t *src, int len);
static int ap_mem_read_bytes(struct target_s *target, uint8_t *dest, uint32_t src, int len);
static int ap_mem_write_bytes(struct target_s *target, uint32_t dest, const uint8_t *src, int len);

void adiv5_dp_ref(ADIv5_DP_t *dp)
{
	dp->refcnt++;
}

void adiv5_ap_ref(ADIv5_AP_t *ap)
{
	ap->refcnt++;
}

void adiv5_dp_unref(ADIv5_DP_t *dp)
{
	if (--(dp->refcnt) == 0)
		free(dp);
}

void adiv5_ap_unref(ADIv5_AP_t *ap)
{
	if (--(ap->refcnt) == 0) {
		adiv5_dp_unref(ap->dp);
		if (ap->priv)
			ap->priv_free(ap->priv);
		free(ap);
	}
}

void adiv5_dp_init(ADIv5_DP_t *dp)
{
	uint32_t ctrlstat;

	adiv5_dp_ref(dp);

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
		ADIv5_AP_t *ap, tmpap;
		target *t;

		/* Assume valid and try to read IDR */
		memset(&tmpap, 0, sizeof(tmpap));
		tmpap.dp = dp;
		tmpap.apsel = i;
		tmpap.idr = adiv5_ap_read(&tmpap, ADIV5_AP_IDR);

		if(!tmpap.idr) /* IDR Invalid - Should we not continue here? */
			break;

		/* It's valid to so create a heap copy */
		ap = malloc(sizeof(*ap));
		memcpy(ap, &tmpap, sizeof(*ap));
		adiv5_dp_ref(dp);

		ap->cfg = adiv5_ap_read(ap, ADIV5_AP_CFG);
		ap->base = adiv5_ap_read(ap, ADIV5_AP_BASE);
		ap->csw = adiv5_ap_read(ap, ADIV5_AP_CSW) &
			~(ADIV5_AP_CSW_SIZE_MASK | ADIV5_AP_CSW_ADDRINC_MASK);

		/* Should probe further here to make sure it's a valid target.
		 * AP should be unref'd if not valid.
		 */

		/* Prepend to target list... */
		t = target_new(sizeof(*t));
		adiv5_ap_ref(ap);
		t->priv = ap;
		t->priv_free = (void (*)(void *))adiv5_ap_unref;

		t->driver = adiv5_driver_str;
		t->check_error = ap_check_error;

		t->mem_read_words = ap_mem_read_words;
		t->mem_write_words = ap_mem_write_words;
		t->mem_read_halfwords = ap_mem_read_halfwords;
		t->mem_write_halfwords = ap_mem_write_halfwords;
		t->mem_read_bytes = ap_mem_read_bytes;
		t->mem_write_bytes = ap_mem_write_bytes;

		/* The rest sould only be added after checking ROM table */
		cortexm_probe(t);
	}
	adiv5_dp_unref(dp);
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
	uint32_t osrc = src;

	len >>= 2;

	adiv5_ap_write(ap, ADIV5_AP_CSW, ap->csw |
		ADIV5_AP_CSW_SIZE_WORD | ADIV5_AP_CSW_ADDRINC_SINGLE);
	adiv5_dp_low_access(ap->dp, ADIV5_LOW_AP, ADIV5_LOW_WRITE,
					ADIV5_AP_TAR, src);
	adiv5_dp_low_access(ap->dp, ADIV5_LOW_AP, ADIV5_LOW_READ,
					ADIV5_AP_DRW, 0);
	while(--len) {
		*dest++ = adiv5_dp_low_access(ap->dp, ADIV5_LOW_AP,
					ADIV5_LOW_READ, ADIV5_AP_DRW, 0);
		src += 4;
		/* Check for 10 bit address overflow */
		if ((src ^ osrc) & 0xfffffc00) {
			osrc = src;
			adiv5_dp_low_access(ap->dp, ADIV5_LOW_AP,
					ADIV5_LOW_WRITE, ADIV5_AP_TAR, src);
			adiv5_dp_low_access(ap->dp, ADIV5_LOW_AP,
					ADIV5_LOW_READ, ADIV5_AP_DRW, 0);
		}

	}
	*dest++ = adiv5_dp_low_access(ap->dp, ADIV5_LOW_DP, ADIV5_LOW_READ,
					ADIV5_DP_RDBUFF, 0);

	return 0;
}

static int
ap_mem_read_halfwords(struct target_s *target, uint16_t *dest, uint32_t src, int len)
{
	ADIv5_AP_t *ap = adiv5_target_ap(target);
	uint32_t tmp;
	uint32_t osrc = src;

	len >>= 1;

	adiv5_ap_write(ap, ADIV5_AP_CSW, ap->csw |
		ADIV5_AP_CSW_SIZE_HALFWORD | ADIV5_AP_CSW_ADDRINC_SINGLE);
	adiv5_dp_low_access(ap->dp, ADIV5_LOW_AP, ADIV5_LOW_WRITE,
					ADIV5_AP_TAR, src);
	adiv5_dp_low_access(ap->dp, ADIV5_LOW_AP, ADIV5_LOW_READ,
					ADIV5_AP_DRW, 0);
	while(--len) {
		tmp = adiv5_dp_low_access(ap->dp, ADIV5_LOW_AP, ADIV5_LOW_READ,
					ADIV5_AP_DRW, 0);
		*dest++ = (tmp >> ((src & 0x2) << 3) & 0xFFFF);

		src += 2;
		/* Check for 10 bit address overflow */
		if ((src ^ osrc) & 0xfffffc00) {
			osrc = src;
			adiv5_dp_low_access(ap->dp, ADIV5_LOW_AP,
					ADIV5_LOW_WRITE, ADIV5_AP_TAR, src);
			adiv5_dp_low_access(ap->dp, ADIV5_LOW_AP,
					ADIV5_LOW_READ, ADIV5_AP_DRW, 0);
		}
	}
	tmp = adiv5_dp_low_access(ap->dp, 0, 1, ADIV5_DP_RDBUFF, 0);
	*dest++ = (tmp >> ((src & 0x2) << 3) & 0xFFFF);

	return 0;
}

static int
ap_mem_read_bytes(struct target_s *target, uint8_t *dest, uint32_t src, int len)
{
	ADIv5_AP_t *ap = adiv5_target_ap(target);
	uint32_t tmp;
	uint32_t osrc = src;

	adiv5_ap_write(ap, ADIV5_AP_CSW, ap->csw |
		ADIV5_AP_CSW_SIZE_BYTE | ADIV5_AP_CSW_ADDRINC_SINGLE);
	adiv5_dp_low_access(ap->dp, ADIV5_LOW_AP, ADIV5_LOW_WRITE,
					ADIV5_AP_TAR, src);
	adiv5_dp_low_access(ap->dp, ADIV5_LOW_AP, ADIV5_LOW_READ,
					ADIV5_AP_DRW, 0);
	while(--len) {
		tmp = adiv5_dp_low_access(ap->dp, 1, 1, ADIV5_AP_DRW, 0);
		*dest++ = (tmp >> ((src & 0x3) << 3) & 0xFF);

		src++;
		/* Check for 10 bit address overflow */
		if ((src ^ osrc) & 0xfffffc00) {
			osrc = src;
			adiv5_dp_low_access(ap->dp, ADIV5_LOW_AP,
					ADIV5_LOW_WRITE, ADIV5_AP_TAR, src);
			adiv5_dp_low_access(ap->dp, ADIV5_LOW_AP,
					ADIV5_LOW_READ, ADIV5_AP_DRW, 0);
		}
	}
	tmp = adiv5_dp_low_access(ap->dp, 0, 1, ADIV5_DP_RDBUFF, 0);
	*dest++ = (tmp >> ((src++ & 0x3) << 3) & 0xFF);

	return 0;
}


static int
ap_mem_write_words(struct target_s *target, uint32_t dest, const uint32_t *src, int len)
{
	ADIv5_AP_t *ap = adiv5_target_ap(target);
	uint32_t odest = dest;

	len >>= 2;

	adiv5_ap_write(ap, ADIV5_AP_CSW, ap->csw |
		ADIV5_AP_CSW_SIZE_WORD | ADIV5_AP_CSW_ADDRINC_SINGLE);
	adiv5_dp_low_access(ap->dp, ADIV5_LOW_AP, ADIV5_LOW_WRITE,
					ADIV5_AP_TAR, dest);
	while(len--) {
		adiv5_dp_low_access(ap->dp, ADIV5_LOW_AP, ADIV5_LOW_WRITE,
					ADIV5_AP_DRW, *src++);
		dest += 4;
		/* Check for 10 bit address overflow */
		if ((dest ^ odest) & 0xfffffc00) {
			odest = dest;
			adiv5_dp_low_access(ap->dp, ADIV5_LOW_AP,
					ADIV5_LOW_WRITE, ADIV5_AP_TAR, dest);
		}
	}

	return 0;
}

static int
ap_mem_write_halfwords(struct target_s *target, uint32_t dest, const uint16_t *src, int len)
{
	ADIv5_AP_t *ap = adiv5_target_ap(target);
	uint32_t odest = dest;

	len >>= 1;

	adiv5_ap_write(ap, ADIV5_AP_CSW, ap->csw |
		ADIV5_AP_CSW_SIZE_HALFWORD | ADIV5_AP_CSW_ADDRINC_SINGLE);
	adiv5_dp_low_access(ap->dp, ADIV5_LOW_AP, ADIV5_LOW_WRITE,
					ADIV5_AP_TAR, dest);
	while(len--) {
		uint32_t tmp = (uint32_t)*src++ << ((dest & 2) << 3);
		adiv5_dp_low_access(ap->dp, ADIV5_LOW_AP, ADIV5_LOW_WRITE,
					ADIV5_AP_DRW, tmp);
		dest += 2;
		/* Check for 10 bit address overflow */
		if ((dest ^ odest) & 0xfffffc00) {
			odest = dest;
			adiv5_dp_low_access(ap->dp, ADIV5_LOW_AP,
					ADIV5_LOW_WRITE, ADIV5_AP_TAR, dest);
		}
	}
	return 0;
}

static int
ap_mem_write_bytes(struct target_s *target, uint32_t dest, const uint8_t *src, int len)
{
	ADIv5_AP_t *ap = adiv5_target_ap(target);
	uint32_t odest = dest;

	adiv5_ap_write(ap, ADIV5_AP_CSW, ap->csw |
		ADIV5_AP_CSW_SIZE_BYTE | ADIV5_AP_CSW_ADDRINC_SINGLE);
	adiv5_dp_low_access(ap->dp, ADIV5_LOW_AP, ADIV5_LOW_WRITE,
					ADIV5_AP_TAR, dest);
	while(len--) {
		uint32_t tmp = (uint32_t)*src++ << ((dest++ & 3) << 3);
		adiv5_dp_low_access(ap->dp, ADIV5_LOW_AP, ADIV5_LOW_WRITE,
					ADIV5_AP_DRW, tmp);

		/* Check for 10 bit address overflow */
		if ((dest ^ odest) & 0xfffffc00) {
			odest = dest;
			adiv5_dp_low_access(ap->dp, ADIV5_LOW_AP,
					ADIV5_LOW_WRITE, ADIV5_AP_TAR, dest);
		}
	}
	return 0;
}



uint32_t adiv5_ap_mem_read(ADIv5_AP_t *ap, uint32_t addr)
{
	adiv5_ap_write(ap, ADIV5_AP_CSW, ap->csw |
		ADIV5_AP_CSW_SIZE_WORD | ADIV5_AP_CSW_ADDRINC_SINGLE);
	adiv5_ap_write(ap, ADIV5_AP_TAR, addr);
	return adiv5_ap_read(ap, ADIV5_AP_DRW);
}

void adiv5_ap_mem_write(ADIv5_AP_t *ap, uint32_t addr, uint32_t value)
{
	adiv5_ap_write(ap, ADIV5_AP_CSW, ap->csw |
		ADIV5_AP_CSW_SIZE_WORD | ADIV5_AP_CSW_ADDRINC_SINGLE);
	adiv5_ap_write(ap, ADIV5_AP_TAR, addr);
	adiv5_ap_write(ap, ADIV5_AP_DRW, value);
}

uint16_t adiv5_ap_mem_read_halfword(ADIv5_AP_t *ap, uint32_t addr)
{
	adiv5_ap_write(ap, ADIV5_AP_CSW, ap->csw |
		ADIV5_AP_CSW_SIZE_HALFWORD | ADIV5_AP_CSW_ADDRINC_SINGLE);
	adiv5_ap_write(ap, ADIV5_AP_TAR, addr);
	uint32_t v = adiv5_ap_read(ap, ADIV5_AP_DRW);
	if (addr & 2)
		return v >> 16;
	else
		return v & 0xFFFF;
}

void adiv5_ap_mem_write_halfword(ADIv5_AP_t *ap, uint32_t addr, uint16_t value)
{
	uint32_t v = value;
	if (addr & 2)
		v <<= 16;

	adiv5_ap_write(ap, ADIV5_AP_CSW, ap->csw |
		ADIV5_AP_CSW_SIZE_HALFWORD | ADIV5_AP_CSW_ADDRINC_SINGLE);
	adiv5_ap_write(ap, ADIV5_AP_TAR, addr);
	adiv5_ap_write(ap, ADIV5_AP_DRW, v);
}

uint8_t adiv5_ap_mem_read_byte(ADIv5_AP_t *ap, uint32_t addr)
{
	adiv5_ap_write(ap, ADIV5_AP_CSW, ap->csw |
		ADIV5_AP_CSW_SIZE_BYTE | ADIV5_AP_CSW_ADDRINC_SINGLE);
	adiv5_ap_write(ap, ADIV5_AP_TAR, addr);
	uint32_t v = adiv5_ap_read(ap, ADIV5_AP_DRW);

	return v >> ((addr & 3) * 8);
}

void adiv5_ap_mem_write_byte(ADIv5_AP_t *ap, uint32_t addr, uint8_t value)
{
	uint32_t v = value << ((addr & 3) * 8);

	adiv5_ap_write(ap, ADIV5_AP_CSW, ap->csw |
		ADIV5_AP_CSW_SIZE_BYTE | ADIV5_AP_CSW_ADDRINC_SINGLE);
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

