/*
 * This file is part of the Black Magic Debug project.
 *
 * MIT License
 *
 * Copyright (c) 2019 Roland Ruckerbauer <roland.rucky@gmail.com>
 * based on similar work by Gareth McMullin <gareth@blacksphere.co.nz>
 * Copyright (c) 2020-21 Uwe Bonnes <bon@elektron.ikp.physik.tu-darmstadt.de>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 + This file implements the JTAG-DP specific functions of the
 * RISC-V External Debug Support Version 0.13
 */

#include <limits.h>
#include "general.h"
#include "exception.h"
#include "jtag_scan.h"
#include "jtagtap.h"
#include "jtag_devs.h"
#include "morse.h"
#include "rvdbg.h"

typedef struct RVDBGv013_JTAG_s {
	RVDBGv013_DMI_t dmi;
	uint8_t dp_jd_index;
	uint64_t last_dmi;
} RVDBGv013_JTAG_t;

static void rvdbg_dmi_reset_jtag(RVDBGv013_DMI_t *dmi, bool hard_reset)
{
	RVDBGv013_JTAG_t *rvdbg_jtag = container_of(dmi, RVDBGv013_JTAG_t, dmi);

	jtag_dev_shift_ir(&jtag_proc, rvdbg_jtag->dp_jd_index, IR_DTMCS, NULL);

	uint64_t dtmcontrol =  /* uint64_t due to https://github.com/blacksphere/blackmagic/issues/542 */
		hard_reset ? DTMCS_DMIHARDRESET : DTMCS_DMIRESET;

	jtag_dev_shift_dr(&jtag_proc, rvdbg_jtag->dp_jd_index, (void*)&dtmcontrol, (void*)&dtmcontrol, 32);

	// DEBUG("after dmireset: dtmcs = 0x%08x\n", (uint32_t)dtmcontrol);

	// Switch to DMI register
	jtag_dev_shift_ir(&jtag_proc, rvdbg_jtag->dp_jd_index, IR_DMI, NULL);
}

// TODO: Rewrite to always use proper run/test/idle timing
static int rvdbg_dmi_low_access_jtag(RVDBGv013_DMI_t *dmi, uint32_t *dmi_data_out, uint64_t dmi_cmd)
{
	RVDBGv013_JTAG_t *rvdbg_jtag = container_of(dmi, RVDBGv013_JTAG_t, dmi);

	uint64_t dmi_ret;

retry:
	jtag_dev_shift_dr(&jtag_proc, rvdbg_jtag->dp_jd_index, (void*)&dmi_ret, (const void*)&dmi_cmd,
		DMI_BASE_BIT_COUNT + dmi->abits);
	switch (DMI_GET_OP(dmi_ret)) {
		case DMISTAT_OP_BUSY:
			// Retry after idling, restore last dmi
			rvdbg_dmi_reset_jtag(dmi, false);
			if (dmi->idle < 9) {
				dmi->idle ++;
				jtag_proc.idle_cycles = dmi->idle;
			} else {
				DEBUG_WARN("dmi_low_access idle cycle overflow\n");
				return -1;
			}
			jtag_dev_shift_dr(&jtag_proc, rvdbg_jtag->dp_jd_index, (void*)&dmi_ret, (const void*)&rvdbg_jtag->last_dmi,
				DMI_BASE_BIT_COUNT + dmi->abits);

			DEBUG_INFO("RISC-V DMI op interrupted ret = 0x%"PRIx64", idle now %d \n", dmi_ret, dmi->idle);
			goto retry;

		case DMISTAT_NO_ERROR:
			rvdbg_jtag->last_dmi = dmi_cmd;
			break;

		case DMISTAT_RESERVED:
		case DMISTAT_OP_FAILED:
		default:
			DEBUG_WARN("DMI returned error: %"PRIx64"\n", dmi_ret);
			// jtag_dev_shift_ir(&jtag_proc, rvdbg_jtag->dp_jd_index, IR_DMI);
			rvdbg_dmi_reset_jtag(dmi, false);
			// TODO: Support recovering?
			return -1;
	}

	if (dmi_data_out != NULL)
		*dmi_data_out = (dmi_ret >> 2);

	return 0;
}

static void rvdbg_dmi_free_jtag(RVDBGv013_DMI_t *dmi)
{
	RVDBGv013_JTAG_t *rvdbg_jtag = container_of(dmi, RVDBGv013_JTAG_t, dmi);
	free(rvdbg_jtag);
}

void rvdbg013_jtag_dp_handler(jtag_dev_t *jd)
{
	uint32_t dtmcontrol;
	uint8_t version;

	RVDBGv013_JTAG_t *rvdbg_jtag = calloc(1, sizeof(*rvdbg_jtag));
	if (!rvdbg_jtag) {
		DEBUG_WARN("calloc: failed in %s\n", __func__);
		return;
	}

	rvdbg_jtag->dp_jd_index = jd->jd_dev;
	rvdbg_jtag->dmi.descr = jtag_devs->jd_descr;
	rvdbg_jtag->dmi.rvdbg_dmi_low_access = rvdbg_dmi_low_access_jtag;
	rvdbg_jtag->dmi.rvdbg_dmi_reset = rvdbg_dmi_reset_jtag;
	rvdbg_jtag->dmi.rvdbg_dmi_free = rvdbg_dmi_free_jtag;

	DEBUG_INFO("RISC-V DTM id 0x%x detected: `%s`\n"
		"Scanning RISC-V target ...\n", jd->jd_idcode, rvdbg_jtag->dmi.descr);

	// Read from the DTM control and status register
	jtag_dev_shift_ir(&jtag_proc, rvdbg_jtag->dp_jd_index, IR_DTMCS, NULL);
	jtag_dev_shift_dr(&jtag_proc, rvdbg_jtag->dp_jd_index, (void*)&dtmcontrol,
		(void*)&dtmcontrol, 32);

	DEBUG_INFO("  dtmcs = 0x%08x\n", (uint32_t)dtmcontrol);

	version = DTMCS_GET_VERSION((uint32_t)dtmcontrol);
	if (rvdbg_set_debug_version(&rvdbg_jtag->dmi, version) < 0) {
		free(rvdbg_jtag);
		return;
	}

	rvdbg_jtag->dmi.idle = DTMCS_GET_IDLE(dtmcontrol);
	jtag_proc.idle_cycles = rvdbg_jtag->dmi.idle ;
	rvdbg_jtag->dmi.abits = DTMCS_GET_ABITS(dtmcontrol);

	if (rvdbg_dmi_init(&rvdbg_jtag->dmi) < 0) {
		free(rvdbg_jtag);
	}
}
