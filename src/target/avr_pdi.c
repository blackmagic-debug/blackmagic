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

#define PDI_LDS		0x00U
#define PDI_LD		0x20U
#define PDI_STS		0x40U
#define PDI_ST		0x60U
#define PDI_LDCS	0x80U
#define PDI_REPEAT	0xa0U
#define PDI_STCS	0xc0U
#define PDI_KEY		0xe0U

#define PDI_DATA_8	0x00U
#define PDI_DATA_16	0x01U
#define PDI_DATA_24	0x02U
#define PDI_DATA_32	0x03U

#define PDI_ADDR_8	0x00U
#define PDI_ADDR_16	0x04U
#define PDI_ADDR_24	0x08U
#define PDI_ADDR_32	0x0cU

#define PDI_MODE_IND_PTR	0x00U
#define PDI_MODE_IND_INCPTR	0x04U
#define PDI_MODE_DIR_PTR	0x08U
#define PDI_MODE_DIR_INCPTR	0x0cU // "Reserved"
#define PDI_MODE_MASK		0xf3U

#define PDI_REG_STATUS	0U
#define PDI_REG_RESET	1U
#define PDI_REG_CTRL	2U
#define PDI_REG_R3		3U
#define PDI_REG_R4		4U

#define PDI_RESET	0x59U

#define AVR_ADDR_DBG_CTR		0x00000000U
#define AVR_ADDR_DBG_PC			0x00000004U
#define AVR_ADDR_DBG_CTRL		0x0000000aU
#define AVR_ADDR_DBG_SPECIAL	0x0000000cU

#define AVR_DBG_READ_REGS	0x11U
#define AVR_NUM_REGS		32

#define AVR_ADDR_CPU		0x01000030U
#define AVR_ADDR_CPU_SPL	0xDU
#define AVR_ADDR_CPU_SREG	0xFU

typedef enum
{
	PDI_PROG = 0x02U,
	PDI_DEBUG = 0x04U,
} pdi_key_e;

static const char pdi_key_prog[] = {0xff, 0x88, 0xd8, 0xcd, 0x45, 0xab, 0x89, 0x12};
static const char pdi_key_debug[] = {0x21, 0x81, 0x7c, 0x9f, 0xd4, 0x2d, 0x21, 0x3a};

static void avr_reset(target *t);
static void avr_halt_request(target *t);
static enum target_halt_reason avr_halt_poll(target *t, target_addr *watch);

static void avr_regs_read(target *t, void *data);

typedef struct __attribute__((packed))
{
	uint8_t general[32]; // r0-r31
	uint8_t sreg; // r32
	uint16_t sp; // r33
	uint32_t pc; // r34
} avr_regs;

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

	t->reset = avr_reset;
	t->halt_request = avr_halt_request;
	t->halt_poll = avr_halt_poll;
	// Unlike on an ARM processor, where this is the length of a table, here we return the size of
	// a suitable registers structure.
	t->regs_size = sizeof(avr_regs);

	if (atxmega_probe(t))
		return true;
	dp->halt_reason = TARGET_HALT_RUNNING;
	return true;
}

bool avr_pdi_reg_write(AVR_DP_t *dp, uint8_t reg, uint8_t value)
{
	uint8_t result = 0, command = PDI_STCS | reg;
	if (reg >= 16 ||
		avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, command) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, value))
		return false;
	return result == PDI_EMPTY;
}

uint8_t avr_pdi_reg_read(AVR_DP_t *dp, uint8_t reg)
{
	uint8_t result = 0, command = PDI_LDCS | reg;
	if (reg >= 16 ||
		avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, command) || result != PDI_EMPTY ||
		!avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, 0))
		return 0xFFU; // TODO - figure out a better way to indicate failure.
	return result;
}

bool avr_pdi_st8(AVR_DP_t *dp, uint32_t reg, uint8_t data)
{
	uint8_t result = 0, command = PDI_STS | PDI_ADDR_32 | PDI_DATA_8;
	if (avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, command) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, reg & 0xffU) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, (reg >> 8U) & 0xffU) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, (reg >> 16U) & 0xffU) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, (reg >> 24U) & 0xffU) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, data))
		return false;
	return result == PDI_EMPTY;
}

uint8_t avr_pdi_ld8(AVR_DP_t *dp, uint32_t reg)
{
	uint8_t result = 0, command = PDI_LDS | PDI_ADDR_32 | PDI_DATA_8;
	uint8_t data[1];
	if (avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, command) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, reg & 0xffU) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, (reg >> 8U) & 0xffU) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, (reg >> 16U) & 0xffU) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, (reg >> 24U) & 0xffU) || result != PDI_EMPTY ||
		!avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, data, 0))
		return 0xFFU; // TODO - figure out a better way to indicate failure.
	return data[0];
}

bool avr_pdi_st16(AVR_DP_t *dp, uint32_t reg, uint16_t data)
{
	uint8_t result = 0, command = PDI_STS | PDI_ADDR_32 | PDI_DATA_16;
	if (avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, command) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, reg & 0xffU) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, (reg >> 8U) & 0xffU) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, (reg >> 16U) & 0xffU) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, (reg >> 24U) & 0xffU) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, data & 0xffU) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, (data >> 8U) & 0xffU))
		return false;
	return result == PDI_EMPTY;
}

uint16_t avr_pdi_ld16(AVR_DP_t *dp, uint32_t reg)
{
	uint8_t result = 0, command = PDI_LDS | PDI_ADDR_32 | PDI_DATA_16;
	uint8_t data[2];
	if (avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, command) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, reg & 0xffU) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, (reg >> 8U) & 0xffU) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, (reg >> 16U) & 0xffU) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, (reg >> 24U) & 0xffU) || result != PDI_EMPTY ||
		!avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, data + 0, 0) ||
		!avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, data + 1, 0))
		return 0xFFFFU; // TODO - figure out a better way to indicate failure.
	return data[0] | (data[1] << 8U);
}

bool avr_pdi_st32(AVR_DP_t *dp, uint32_t reg, uint32_t data)
{
	uint8_t result = 0, command = PDI_STS | PDI_ADDR_32 | PDI_DATA_32;
	if (avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, command) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, reg & 0xffU) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, (reg >> 8U) & 0xffU) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, (reg >> 16U) & 0xffU) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, (reg >> 24U) & 0xffU) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, data & 0xffU) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, (data >> 8U) & 0xffU) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, (data >> 16U) & 0xffU) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, (data >> 24U) & 0xffU))
		return false;
	return result == PDI_EMPTY;
}

uint32_t avr_pdi_ld32(AVR_DP_t *dp, uint32_t reg)
{
	uint8_t result = 0, command = PDI_LDS | PDI_ADDR_32 | PDI_DATA_32;
	uint8_t data[4];
	if (avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, command) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, reg & 0xffU) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, (reg >> 8U) & 0xffU) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, (reg >> 16U) & 0xffU) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, (reg >> 24U) & 0xffU) || result != PDI_EMPTY ||
		!avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, data + 0, 0) ||
		!avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, data + 1, 0) ||
		!avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, data + 2, 0) ||
		!avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, data + 3, 0))
		return 0xFFFFU; // TODO - figure out a better way to indicate failure.
	return data[0] | (data[1] << 8U) | (data[2] << 16U) | (data[3] << 24U);
}

bool avr_pdi_read(AVR_DP_t *dp, uint32_t addr, uint8_t ptr_mode, void *dst, uint32_t count)
{
	const uint32_t repeat = count - 1U;
	uint8_t result = 0, command;
	uint8_t *data = (uint8_t *)dst;
	if ((ptr_mode & PDI_MODE_MASK) || !count)
		return false;
	// Run `st ptr <addr>`
	command = PDI_ST | PDI_MODE_DIR_PTR | PDI_DATA_32;
	if (avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, command) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, addr & 0xffU) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, (addr >> 8U) & 0xffU) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, (addr >> 16U) & 0xffU) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, (addr >> 24U) & 0xffU) || result != PDI_EMPTY)
		return false;
	// Run `repeat <count - 1>`
	command = PDI_REPEAT | PDI_DATA_32;
	if (avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, command) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, repeat & 0xffU) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, (repeat >> 8U) & 0xffU) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, (repeat >> 16U) & 0xffU) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, (repeat >> 24U) & 0xffU) || result != PDI_EMPTY)
		return false;
	// Run `ld <ptr_mode>`
	command = PDI_LD | ptr_mode;
	if (avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, command) || result != PDI_EMPTY)
		return false;
	for (uint32_t i = 0; i < count; ++i)
	{
		if (!avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &data[i], 0))
			return false;
	}
	return true;
}

bool avr_enable(AVR_DP_t *dp, pdi_key_e what)
{
	const char *const key = what == PDI_DEBUG ? pdi_key_debug : pdi_key_prog;
	uint8_t result = 0;
	if (avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, PDI_KEY) || result != PDI_EMPTY)
		return false;
	for (uint8_t i = 0; i < 8; ++i)
	{
		if (avr_jtag_shift_dr(&jtag_proc, dp->dp_jd_index, &result, key[i]) || result != PDI_EMPTY)
			return false;
	}
	return (avr_pdi_reg_read(dp, PDI_REG_STATUS) & what) == what;
}

bool avr_disable(AVR_DP_t *dp, pdi_key_e what)
{
	return avr_pdi_reg_write(dp, PDI_REG_STATUS, ~what);
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
	volatile struct exception e;
	jtag_dev_write_ir(&jtag_proc, dp->dp_jd_index, IR_PDI);

	TRY_CATCH (e, EXCEPTION_ALL) {
		target_reset(t);
		if (!avr_enable(dp, PDI_DEBUG))
			return false;
		target_halt_request(t);
	}
	return !e.type;
}

void avr_detach(target *t)
{
	AVR_DP_t *dp = t->priv;

	avr_disable(dp, PDI_DEBUG);
	jtag_dev_write_ir(&jtag_proc, dp->dp_jd_index, IR_BYPASS);
}

static void avr_reset(target *t)
{
	AVR_DP_t *dp = t->priv;
	if (!avr_pdi_reg_write(dp, PDI_REG_RESET, PDI_RESET) ||
		avr_pdi_reg_read(dp, PDI_REG_STATUS) != 0x00)
		raise_exception(EXCEPTION_ERROR, "Error resetting device, device in incorrect state");
}

static void avr_halt_request(target *t)
{
	AVR_DP_t *dp = t->priv;
	/* To halt the processor we go through a few really specific steps:
	 * Write r4 to 1 to indicate we want to put the processor into debug-based pause
	 * Read r3 and check it's 0x10 which indicates the processor is held in reset and no debugging is active
	 * Releae reset
	 * Read r3 twice more, the first time should respond 0x14 to indicate the processor is still reset
	 * but that debug pause is requested, and the second should respond 0x04 to indicate the processor is now
	 * in debug pause state (halted)
	 */
	if (!avr_pdi_reg_write(dp, PDI_REG_R4, 1) ||
		avr_pdi_reg_read(dp, PDI_REG_R3) != 0x10U ||
		!avr_pdi_reg_write(dp, PDI_REG_RESET, 0) ||
		avr_pdi_reg_read(dp, PDI_REG_R3) != 0x14U ||
		avr_pdi_reg_read(dp, PDI_REG_R3) != 0x04U)
		raise_exception(EXCEPTION_ERROR, "Error halting device, device in incorrect state");
	dp->halt_reason = TARGET_HALT_REQUEST;
}

static enum target_halt_reason avr_halt_poll(target *t, target_addr *watch)
{
	AVR_DP_t *dp = t->priv;
	(void)watch;
	return dp->halt_reason;
}

static void avr_regs_read(target *t, void *data)
{
	AVR_DP_t *dp = t->priv;
	avr_regs *regs = (avr_regs *)data;
	regs->pc = avr_pdi_ld32(dp, AVR_ADDR_DBG_PC);
	regs->sp = avr_pdi_ld16(dp, AVR_ADDR_CPU | AVR_ADDR_CPU_SPL);
	regs->sreg = avr_pdi_ld16(dp, AVR_ADDR_CPU | AVR_ADDR_CPU_SREG);

	if (!avr_pdi_st8(dp, AVR_ADDR_DBG_CTRL, AVR_DBG_READ_REGS) ||
		!avr_pdi_st32(dp, AVR_ADDR_DBG_CTR, AVR_NUM_REGS) ||
		!avr_pdi_reg_write(dp, PDI_REG_R4, 1) ||
		!avr_pdi_read(dp, AVR_ADDR_DBG_SPECIAL, PDI_MODE_IND_PTR, regs->general, 32))
		raise_exception(EXCEPTION_ERROR, "Error reading registers");
}
