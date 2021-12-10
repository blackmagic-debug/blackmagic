#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "avr.h"
#include "exception.h"

#define IR_PDI		0x7U
#define PDI_BREAK	0xBBU
#define PDI_DELAY	0xDBU
#define PDI_EMPTY	0xEBU

#define PDI_LDCS	0x80U
#define PDI_STCS	0xC0U

static void avr_dp_unref(AVR_DP_t *dp)
{
	if (--(dp->refcnt) == 0)
		free(dp);
}

bool avr_dp_init(AVR_DP_t *dp)
{
	/* Check for a valid part number in the IDCode */
	if ((dp->idcode & 0x0FFFF000) == 0) {
		DEBUG_WARN("Invalid DP idcode %08" PRIx32 "\n", dp->idcode);
		free(dp);
		return false;
	}
	DEBUG_INFO("AVR ID 0x%08" PRIx32 " (v%d)\n", dp->idcode,
		(uint8_t)((dp->idcode >> 28U) & 0xfU));
	jtag_dev_write_ir(&jtag_proc, dp->dp_jd_index, IR_PDI);
	return true;
}

static bool avr_dev_shift_dr(jtag_proc_t *jp, uint8_t jd_index, uint8_t *dout, const uint8_t din)
{
	jtag_dev_t *d = &jtag_devs[jd_index];
	uint8_t result = 0;
	uint16_t request = 0, response = 0;
	uint8_t *data = (uint8_t *)&request;
	if (!dout)
		return false;
	jtagtap_shift_dr();
	jp->jtagtap_tdi_seq(0, ones, d->dr_prescan);
	data[0] = din;
	// Calculate the parity bit
	for (uint8_t i = 0; i < 8; ++i)
		data[1] ^= (din >> i) & 1U;
	jp->jtagtap_tdi_tdo_seq((uint8_t *)&response, 1, (uint8_t *)&request, 9);
	jp->jtagtap_tdi_seq(1, ones, d->dr_postscan);
	jtagtap_return_idle();
	data = (uint8_t *)&response;
	// Calculate the parity bit
	for (uint8_t i = 0; i < 8; ++i)
		result ^= (data[0] >> i) & 1U;
	*dout = data[0];
	return result == data[1];
}

bool avr_pdi_reg_write(AVR_DP_t *dp, uint8_t reg, uint8_t value)
{
	uint8_t result = 0, command = PDI_STCS | reg;
	if (reg >= 16 ||
		avr_dev_shift_dr(&jtag_proc, dp->dp_jd_index, &result, command) ||
		result != PDI_EMPTY ||
		avr_dev_shift_dr(&jtag_proc, dp->dp_jd_index, &result, value))
		return false;
	return result == PDI_EMPTY;
}

uint8_t avr_pdi_reg_read(AVR_DP_t *dp, uint8_t reg)
{
	uint8_t result = 0, command = PDI_LDCS | reg;
	if (reg >= 16 ||
		avr_dev_shift_dr(&jtag_proc, dp->dp_jd_index, &result, command) ||
		result != PDI_EMPTY ||
		!avr_dev_shift_dr(&jtag_proc, dp->dp_jd_index, &result, command))
		return 0xFFU; // TODO - figure out a better way to indicate failure.
	return result;
}
