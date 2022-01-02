/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2022 Uwe Bonnes (bon@elektron.ikp.physik.tu-darmstadt.de)
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

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"

#define  LPC55_DMAP_IDR  0x002A0000
#define  LPC5XXXX_CHIPID 0x50000ff8

#define LPC55_DMAP_BULK_ERASE 0x02
#define LPC55_DMAP_START_DEBUG_SESSION 0x07

static uint32_t lpc55_dmap_cmd(ADIv5_AP_t *ap, uint32_t cmd)
{
	uint32_t value;
	platform_timeout timeout;
	platform_timeout_set(&timeout, 20);
	do {
		/* Check that mailbox is not busy*/
		value = adiv5_ap_read(ap, ADIV5_AP_CSW);
		if (platform_timeout_is_expired(&timeout))
			break;
	} while (value);
	platform_timeout_set(&timeout, 20);
	if (value)
		return value;
	adiv5_ap_write(ap, ADIV5_AP_TAR, cmd);
	platform_timeout_set(&timeout, 20);
	do {
		value = adiv5_ap_read(ap, ADIV5_AP_DRW) & 0xffff;
		if (platform_timeout_is_expired(&timeout))
			break;
	} while(value);
	if (value) {
		DEBUG_WARN("LPC55 cms %x failed\n", cmd);
	}
	return value;
}

void lpc55_ap_prepare(ADIv5_DP_t *dp)
{
	/* Reading targetid again here upsets the LPC55 ans STM32U5!*/
	/* UM11126, 51.6.1
	 * Debug session with uninitialized/invalid flash image or ISP mode
	 */
	adiv5_dp_abort(dp, ADIV5_DP_ABORT_DAPABORT);
	ADIv5_AP_t tmpap;
	memset(&tmpap, 0, sizeof(tmpap));
	tmpap.dp = dp;
	tmpap.apsel = 2;
	tmpap.idr = adiv5_ap_read(&tmpap, ADIV5_AP_IDR);
	if (tmpap.idr == LPC55_DMAP_IDR) {
		/* All fine when AP0 is visible*/
		tmpap.apsel = 0;
		tmpap.idr = adiv5_ap_read(&tmpap, ADIV5_AP_IDR);
		if (!tmpap.idr) {
			DEBUG_INFO("LPC55 activation sequence\n");
			/* AP0 not visible, activation sequence needed*/
			tmpap.apsel = 2;
			adiv5_ap_write(&tmpap, ADIV5_AP_CSW, 0x21);
			lpc55_dmap_cmd(&tmpap, LPC55_DMAP_START_DEBUG_SESSION);
		}
	} else {
		DEBUG_WARN("LPC55 AP2 not visible %08" PRIx32 "\n", tmpap.idr);
	}
}

bool lpc55xx_probe(target *t)
{
	/* Syscon access does not work
	uint32_t chipid = target_mem_read32(t, LPC5XXXX_CHIPID);
	DEBUG_WARN("Chipid %08" PRIx32 "\n", chipid);
	*/
	target_add_ram(t, 0x20000000, 0x44000);
	t->target_options |= CORTEXM_TOPT_INHIBIT_SRST;
	switch (t->cpuid & CPUID_PATCH_MASK) {
	case 3:
		t->driver = "LPC55";
		break;
	case 4:
		t->driver = "LPC551x";
		break;
	}
	return true;
}

static bool lpc55_dmap_cmd_device_erase(target *t, int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	ADIv5_AP_t *ap = t->priv;
	if (lpc55_dmap_cmd(ap, LPC55_DMAP_BULK_ERASE))
		return false;
	return true;
}

const struct command_s lpc55_dmap_cmd_list[] = {
	{"erase_mass", (cmd_handler)lpc55_dmap_cmd_device_erase,
	 "Erase entire flash memory"},
	{NULL, NULL, NULL}
};

void lpc55_dmap_probe(ADIv5_AP_t *ap)
{
	switch(ap->idr) {
	case LPC55_DMAP_IDR:
		break;
	default:
		return;
	}
	target *t = target_new();
	if (!t) {
		DEBUG_WARN("nxp_dmap_probe target_new failed\n");
		return;
	}
	adiv5_ap_ref(ap);
	t->priv = ap;
	t->priv_free = (void*)adiv5_ap_unref;

	t->driver = "LPC55 Debugger Mailbox";
	t->regs_size = 4;
	target_add_commands(t, lpc55_dmap_cmd_list, t->driver);
}
