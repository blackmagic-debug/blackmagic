/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2021 Uwe Bonnes (bon@elektron.ikp.physik.tu-darmstadt.de)
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holders nor the names of
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* This file implements Raspberry Pico (RP2040) target specific functions
 * for detecting the device, providing the XML memory map but not yet
 * Flash memory programming.
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"

#define BOOTROM_MAGIC ('M' | ('u' << 8) | (0x01 << 16))
#define BOOTROM_MAGIC_ADDR 0x00000010

bool rp_probe(target *t)
{
	/* Check bootrom magic*/
	uint32_t boot_magic = target_mem_read32(t, BOOTROM_MAGIC_ADDR);
	if ((boot_magic & 0x00ffffff) != BOOTROM_MAGIC) {
		DEBUG_WARN("Wrong Bootmagic %08" PRIx32 " found\n!", boot_magic);
		return false;
	}
	t->driver = "Raspberry RP2040";
	target_add_ram(t, 0x20000000, 0x40000);
	return true;
}

static bool rp_rescue_do_reset(target *t)
{
	ADIv5_AP_t *ap = (ADIv5_AP_t *)t->priv;
	ap->dp->low_access(ap->dp, ADIV5_LOW_WRITE, ADIV5_DP_CTRLSTAT,
						  ADIV5_DP_CTRLSTAT_CDBGPWRUPREQ);
	ap->dp->low_access(ap->dp, ADIV5_LOW_WRITE, ADIV5_DP_CTRLSTAT, 0);
	return false;
}

/* The RP Pico rescue DP provides no AP, so we need special handling
 *
 * Attach to this DP will do the reset, but will fail to attach!
 */
void rp_rescue_probe(ADIv5_AP_t *ap)
{
	target *t = target_new();
	if (!t) {
		return;
	}

	adiv5_ap_ref(ap);
	t->attach = (void*)rp_rescue_do_reset;
	t->priv = ap;
	t->priv_free = (void*)adiv5_ap_unref;
	t->driver = "Raspberry RP2040 Rescue(Attach to reset!)";

}
