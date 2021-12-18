#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "avr.h"
#include "exception.h"
#include "gdb_packet.h"

#define IR_PDI		0x7U
#define IR_BYPASS	0xFU

#define PDI_BREAK	0xBBU
#define PDI_DELAY	0xDBU
#define PDI_EMPTY	0xEBU

#define PDI_LDCS	0x80U
#define PDI_STCS	0xC0U

#define PDI_REG_STATUS	0U
#define PDI_REG_RESET	1U
#define PDI_REG_CTRL	2U
#define PDI_REG_R3		3U
#define PDI_REG_R4		4U

#define PDI_RESET	0x59U

static void avr_reset(target *t);
static enum target_halt_reason avr_halt_poll(target *t, target_addr *watch);

static void avr_dp_ref(AVR_DP_t *dp)
{
	dp->refcnt++;
}

static void avr_dp_unref(AVR_DP_t *dp)
{
	if (--(dp->refcnt) == 0)
		free(dp);
}

bool avr_dp_init(AVR_DP_t *dp)
{
	target *t;

	/* Check for a valid part number in the IDCode */
	if ((dp->idcode & 0x0FFFF000) == 0) {
		DEBUG_WARN("Invalid DP idcode %08" PRIx32 "\n", dp->idcode);
		free(dp);
		return false;
	}
	DEBUG_INFO("AVR ID 0x%08" PRIx32 " (v%d)\n", dp->idcode,
		(uint8_t)((dp->idcode >> 28U) & 0xfU));
	jtag_dev_write_ir(&jtag_proc, dp->dp_jd_index, IR_BYPASS);

	t = target_new();
	if (!t)
		return false;
	avr_dp_ref(dp);

	t->cpuid = dp->idcode;
	t->idcode = (dp->idcode >> 12) & 0xFFFFU;
	t->priv = dp;
	t->driver = "Atmel AVR";
	t->core = "AVR";

	t->attach = avr_attach;
	t->detach = avr_detach;
	t->halt_poll = avr_halt_poll;

	if (atxmega_probe(t))
		return true;
	dp->halt_reason = TARGET_HALT_RUNNING;
	return true;
}

bool avr_pdi_reg_write(AVR_DP_t *dp, uint8_t reg, uint8_t value)
{
	uint8_t result = 0, command = PDI_STCS | reg;
	if (reg >= 16 ||
		avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, command) ||
		result != PDI_EMPTY ||
		avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, value))
		return false;
	return result == PDI_EMPTY;
}

uint8_t avr_pdi_reg_read(AVR_DP_t *dp, uint8_t reg)
{
	uint8_t result = 0, command = PDI_LDCS | reg;
	if (reg >= 16 ||
		avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, command) ||
		result != PDI_EMPTY ||
		!avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, command))
		return 0xFFU; // TODO - figure out a better way to indicate failure.
	return result;
}

void avr_add_flash(target *t, uint32_t start, size_t length)
{
	struct target_flash *f = calloc(1, sizeof(*f));
	if (!f) {			/* calloc failed: heap exhaustion */
		DEBUG_WARN("calloc: failed in %s\n", __func__);
		return;
	}

	f->start = start;
	f->length = length;
	f->blocksize = 0x100;
	f->erased = 0xff;
	target_add_flash(t, f);
}

bool avr_attach(target *t)
{
	AVR_DP_t *dp = t->priv;
	jtag_dev_write_ir(&jtag_proc, dp->dp_jd_index, IR_PDI);

	return true;
}

void avr_detach(target *t)
{
	AVR_DP_t *dp = t->priv;
	jtag_dev_write_ir(&jtag_proc, dp->dp_jd_index, IR_BYPASS);
}

static void avr_reset(target *t)
{
	AVR_DP_t *dp = t->priv;
	if (!avr_pdi_reg_write(dp, PDI_REG_RESET, PDI_RESET) ||
		avr_pdi_reg_read(dp, PDI_REG_STATUS) != 0x00)
		raise_exception(EXCEPTION_ERROR, "Error resetting device, device in incorrect state\n");
}

static enum target_halt_reason avr_halt_poll(target *t, target_addr *watch)
{
	AVR_DP_t *dp = t->priv;
	(void)watch;
	return dp->halt_reason;
}
