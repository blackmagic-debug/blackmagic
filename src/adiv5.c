/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2015  Black Sphere Technologies Ltd.
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

static bool ap_check_error(target *t);

static void ap_mem_read(target *t, void *dest, uint32_t src, size_t len);
static void ap_mem_write(target *t, uint32_t dest, const void *src, size_t len);

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

void adiv5_dp_write(ADIv5_DP_t *dp, uint16_t addr, uint32_t value)
{
	dp->low_access(dp, ADIV5_LOW_WRITE, addr, value);
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

		t->mem_read = ap_mem_read;
		t->mem_write = ap_mem_write;

		/* The rest sould only be added after checking ROM table */
		cortexm_probe(t);
	}
	adiv5_dp_unref(dp);
}

static bool ap_check_error(target *t)
{
	ADIv5_AP_t *ap = adiv5_target_ap(t);
	return adiv5_dp_error(ap->dp) != 0;
}

enum align {
	ALIGN_BYTE =  0,
	ALIGN_HALFWORD = 1,
	ALIGN_WORD = 2
};
#define ALIGNOF(x) (((x) & 3) == 0 ? ALIGN_WORD : \
                    (((x) & 1) == 0 ? ALIGN_HALFWORD : ALIGN_BYTE))

/* Program the CSW and TAR for sequencial access at a given width */
static void ap_mem_access_setup(ADIv5_AP_t *ap, uint32_t addr, enum align align)
{
	uint32_t csw = ap->csw | ADIV5_AP_CSW_ADDRINC_SINGLE;

	switch (align) {
	case ALIGN_BYTE:
		csw |= ADIV5_AP_CSW_SIZE_BYTE;
		break;
	case ALIGN_HALFWORD:
		csw |= ADIV5_AP_CSW_SIZE_HALFWORD;
		break;
	case ALIGN_WORD:
		csw |= ADIV5_AP_CSW_SIZE_WORD;
		break;
	}
	adiv5_ap_write(ap, ADIV5_AP_CSW, csw);
	adiv5_dp_low_access(ap->dp, ADIV5_LOW_WRITE, ADIV5_AP_TAR, addr);
}

/* Extract read data from data lane based on align and src address */
static void * extract(void *dest, uint32_t src, uint32_t val, enum align align)
{
	switch (align) {
	case ALIGN_BYTE:
		*(uint8_t *)dest = (val >> ((src & 0x3) << 3) & 0xFF);
		break;
	case ALIGN_HALFWORD:
		*(uint16_t *)dest = (val >> ((src & 0x2) << 3) & 0xFFFF);
		break;
	case ALIGN_WORD:
		*(uint32_t *)dest = val;
		break;
	}
	return (uint8_t *)dest + (1 << align);
}

static void
ap_mem_read(target *t, void *dest, uint32_t src, size_t len)
{
	ADIv5_AP_t *ap = adiv5_target_ap(t);
	uint32_t tmp;
	uint32_t osrc = src;
	enum align align = MIN(ALIGNOF(src), ALIGNOF(len));

	len >>= align;
	ap_mem_access_setup(ap, src, align);
	adiv5_dp_low_access(ap->dp, ADIV5_LOW_READ, ADIV5_AP_DRW, 0);
	while (--len) {
		tmp = adiv5_dp_low_access(ap->dp, ADIV5_LOW_READ, ADIV5_AP_DRW, 0);
		dest = extract(dest, src, tmp, align);

		src += (1 << align);
		/* Check for 10 bit address overflow */
		if ((src ^ osrc) & 0xfffffc00) {
			osrc = src;
			adiv5_dp_low_access(ap->dp,
					ADIV5_LOW_WRITE, ADIV5_AP_TAR, src);
			adiv5_dp_low_access(ap->dp,
					ADIV5_LOW_READ, ADIV5_AP_DRW, 0);
		}
	}
	tmp = adiv5_dp_low_access(ap->dp, ADIV5_LOW_READ, ADIV5_DP_RDBUFF, 0);
	extract(dest, src, tmp, align);
}

static void
ap_mem_write(target *t, uint32_t dest, const void *src, size_t len)
{
	ADIv5_AP_t *ap = adiv5_target_ap(t);
	uint32_t odest = dest;
	enum align align = MIN(ALIGNOF(dest), ALIGNOF(len));

	len >>= align;
	ap_mem_access_setup(ap, dest, align);
	while (len--) {
		uint32_t tmp = 0;
		/* Pack data into correct data lane */
		switch (align) {
		case ALIGN_BYTE:
			tmp = ((uint32_t)*(uint8_t *)src) << ((dest & 3) << 3);
			break;
		case ALIGN_HALFWORD:
			tmp = ((uint32_t)*(uint16_t *)src) << ((dest & 2) << 3);
			break;
		case ALIGN_WORD:
			tmp = *(uint32_t *)src;
			break;
		}
		src = (uint8_t *)src + (1 << align);
		dest += (1 << align);
		adiv5_dp_low_access(ap->dp, ADIV5_LOW_WRITE, ADIV5_AP_DRW, tmp);

		/* Check for 10 bit address overflow */
		if ((dest ^ odest) & 0xfffffc00) {
			odest = dest;
			adiv5_dp_low_access(ap->dp,
					ADIV5_LOW_WRITE, ADIV5_AP_TAR, dest);
		}
	}
}

void adiv5_ap_write(ADIv5_AP_t *ap, uint16_t addr, uint32_t value)
{
	adiv5_dp_write(ap->dp, ADIV5_DP_SELECT,
			((uint32_t)ap->apsel << 24)|(addr & 0xF0));
	adiv5_dp_write(ap->dp, addr, value);
}

uint32_t adiv5_ap_read(ADIv5_AP_t *ap, uint16_t addr)
{
	uint32_t ret;
	adiv5_dp_write(ap->dp, ADIV5_DP_SELECT,
			((uint32_t)ap->apsel << 24)|(addr & 0xF0));
	ret = adiv5_dp_read(ap->dp, addr);
	return ret;
}

