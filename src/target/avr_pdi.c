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
#define PDI_REPEAT	0xA0U
#define PDI_STCS	0xC0U
#define PDI_KEY		0xE0U

#define PDI_DATA_8	0x00U
#define PDI_DATA_16	0x01U
#define PDI_DATA_24	0x02U
#define PDI_DATA_32	0x03U

#define PDI_ADDR_8	0x00U
#define PDI_ADDR_16	0x04U
#define PDI_ADDR_24	0x08U
#define PDI_ADDR_32	0x0CU

#define PDI_MODE_IND_PTR	0x00U
#define PDI_MODE_IND_INCPTR	0x04U
#define PDI_MODE_DIR_PTR	0x08U
#define PDI_MODE_DIR_INCPTR	0x0CU // "Reserved"
#define PDI_MODE_MASK		0xF3U

#define PDI_REG_STATUS	0U
#define PDI_REG_RESET	1U
#define PDI_REG_CTRL	2U
#define PDI_REG_R3		3U
#define PDI_REG_R4		4U

#define PDI_RESET	0x59U

#define PDI_FLASH_OFFSET	0x00800000U

#define AVR_ADDR_DBG_CTR		0x00000000U
#define AVR_ADDR_DBG_PC			0x00000004U
#define AVR_ADDR_DBG_CTRL		0x0000000AU
#define AVR_ADDR_DBG_SPECIAL	0x0000000CU

#define AVR_ADDR_DBG_BREAK_BASE	0x00000020U
#define AVR_ADDR_DBG_BREAK_UNK1	0x00000040U
#define AVR_ADDR_DBG_BREAK_UNK2	0x00000044U
#define AVR_ADDR_DBG_BREAK_UNK3	0x00000048U

#define AVR_DBG_BREAK_ENABLED	0x80000000U
#define AVR_DBG_BREAK_MASK		0x00FFFFFFU

#define AVR_DBG_READ_REGS		0x11U
#define AVR_NUM_REGS			32

#define AVR_ADDR_CPU_SPL		0x0100003DU

#define AVR_ADDR_NVM			0x010001C0U
#define AVR_ADDR_NVM_DATA		0x010001C4U
#define AVR_ADDR_NVM_CMD		0x010001CAU
#define AVR_ADDR_NVM_STATUS		0x010001CFU

#define AVR_NVM_CMD_NOP					0x00U
#define AVR_NVM_CMD_ERASE_FLASH_BUFFER	0x26U
#define AVR_NVM_CMD_WRITE_FLASH_BUFFER	0x23U
#define AVR_NVM_CMD_ERASE_FLASH_PAGE	0x2BU
#define AVR_NVM_CMD_WRITE_FLASH_PAGE	0x2EU
#define AVR_NVM_CMD_READ_NVM			0x43U

#define AVR_NVM_BUSY			0x80U
#define AVR_NVM_FBUSY			0x40U

typedef enum
{
	PDI_NVM = 0x02U,
	PDI_DEBUG = 0x04U,
} pdi_key_e;

static const char pdi_key_nvm[] = {0xff, 0x88, 0xd8, 0xcd, 0x45, 0xab, 0x89, 0x12};
static const char pdi_key_debug[] = {0x21, 0x81, 0x7c, 0x9f, 0xd4, 0x2d, 0x21, 0x3a};

static bool avr_erase(target *t, int argc, char **argv);

const struct command_s avr_cmd_list[] = {
	{"erase", (cmd_handler)avr_erase, "Erase (part of) a device"},
	{NULL, NULL, NULL}
};

static void avr_reset(target *t);
static void avr_halt_request(target *t);
static void avr_halt_resume(target *t, bool step);
static enum target_halt_reason avr_halt_poll(target *t, target_addr *watch);

static bool avr_check_error(target *t);
static void avr_mem_read(target *t, void *dest, target_addr src, size_t len);

static void avr_regs_read(target *t, void *data);

static int avr_flash_erase(struct target_flash *f, target_addr addr, size_t len);
static int avr_flash_write(struct target_flash *f, target_addr dest, const void *src, size_t len);

static int avr_breakwatch_set(target *t, struct breakwatch *bw);
static int avr_breakwatch_clear(target *t, struct breakwatch *bw);

typedef struct __attribute__((packed))
{
	uint8_t general[32]; // r0-r31
	uint8_t sreg; // r32
	uint16_t sp; // r33
	uint32_t pc; // r34
} avr_regs;

bool avr_pdi_init(avr_pdi_t *pdi)
{
	target *t;

	/* Check for a valid part number in the IDCode */
	if ((pdi->idcode & 0x0FFFF000) == 0) {
		DEBUG_WARN("Invalid PDI idcode %08" PRIx32 "\n", pdi->idcode);
		free(pdi);
		return false;
	}
	DEBUG_INFO("AVR ID 0x%08" PRIx32 " (v%d)\n", pdi->idcode,
		(uint8_t)((pdi->idcode >> 28U) & 0xfU));
	jtag_dev_write_ir(&jtag_proc, pdi->dp_jd_index, IR_BYPASS);

	t = target_new();
	if (!t)
		return false;

	t->cpuid = pdi->idcode;
	t->idcode = (pdi->idcode >> 12) & 0xFFFFU;
	t->driver = "Atmel AVR";
	t->core = "AVR";
	t->priv = pdi;
	t->priv_free = free;

	t->attach = avr_attach;
	t->detach = avr_detach;

	t->check_error = avr_check_error;
	t->mem_read = avr_mem_read;

	t->regs_read = avr_regs_read;

	t->reset = avr_reset;
	t->halt_request = avr_halt_request;
	t->halt_resume = avr_halt_resume;
	t->halt_poll = avr_halt_poll;
	// Unlike on an ARM processor, where this is the length of a table, here we return the size of
	// a suitable registers structure.
	t->regs_size = sizeof(avr_regs);

	t->breakwatch_set = avr_breakwatch_set;
	t->breakwatch_clear = avr_breakwatch_clear;

	target_add_commands(t, avr_cmd_list, "Atmel AVR");

	if (atxmega_probe(t))
		return true;
	pdi->halt_reason = TARGET_HALT_RUNNING;
	return true;
}

bool avr_pdi_reg_write(avr_pdi_t *pdi, uint8_t reg, uint8_t value)
{
	uint8_t result = 0, command = PDI_STCS | reg;
	if (reg >= 16 ||
		avr_jtag_shift_dr(&jtag_proc, pdi->dp_jd_index, &result, command) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(&jtag_proc, pdi->dp_jd_index, &result, value))
		return false;
	return result == PDI_EMPTY;
}

uint8_t avr_pdi_reg_read(avr_pdi_t *pdi, uint8_t reg)
{
	uint8_t result = 0, command = PDI_LDCS | reg;
	if (reg >= 16 ||
		avr_jtag_shift_dr(&jtag_proc, pdi->dp_jd_index, &result, command) || result != PDI_EMPTY ||
		!avr_jtag_shift_dr(&jtag_proc, pdi->dp_jd_index, &result, 0))
		return 0xFFU; // TODO - figure out a better way to indicate failure.
	return result;
}

static bool avr_pdi_write(avr_pdi_t *pdi, uint8_t bytes, uint32_t reg, uint32_t value)
{
	uint8_t result = 0;
	uint8_t command = PDI_STS | PDI_ADDR_32 | bytes;
	uint8_t data_bytes[4] = {
		value & 0xffU,
		(value >> 8U) & 0xffU,
		(value >> 16U) & 0xffU,
		(value >> 24U) & 0xffU
	};

	if (avr_jtag_shift_dr(&jtag_proc, pdi->dp_jd_index, &result, command) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(&jtag_proc, pdi->dp_jd_index, &result, reg & 0xffU) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(&jtag_proc, pdi->dp_jd_index, &result, (reg >> 8U) & 0xffU) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(&jtag_proc, pdi->dp_jd_index, &result, (reg >> 16U) & 0xffU) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(&jtag_proc, pdi->dp_jd_index, &result, (reg >> 24U) & 0xffU) || result != PDI_EMPTY)
		return false;
	// This is intentionally <= to avoid `bytes + 1` silliness
	for (uint8_t i = 0; i <= bytes; ++i)
	{
		if (avr_jtag_shift_dr(&jtag_proc, pdi->dp_jd_index, &result, data_bytes[i]) || result != PDI_EMPTY)
			return false;
	}
	return true;
}

static bool avr_pdi_read(avr_pdi_t *pdi, uint8_t bytes, uint32_t reg, uint32_t *value)
{
	uint8_t result = 0;
	uint8_t command = PDI_LDS | PDI_ADDR_32 | bytes;
	uint8_t data_bytes[4];
	uint32_t data = 0xffffffffU;
	if (avr_jtag_shift_dr(&jtag_proc, pdi->dp_jd_index, &result, command) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(&jtag_proc, pdi->dp_jd_index, &result, reg & 0xffU) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(&jtag_proc, pdi->dp_jd_index, &result, (reg >> 8U) & 0xffU) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(&jtag_proc, pdi->dp_jd_index, &result, (reg >> 16U) & 0xffU) || result != PDI_EMPTY ||
		avr_jtag_shift_dr(&jtag_proc, pdi->dp_jd_index, &result, (reg >> 24U) & 0xffU) || result != PDI_EMPTY)
		return false;
	for (uint8_t i = 0; i <= bytes; ++i)
	{
		if (!avr_jtag_shift_dr(&jtag_proc, pdi->dp_jd_index, &data_bytes[i], 0))
			return false;
	}
	data = data_bytes[0];
	if (bytes > PDI_DATA_8)
		data |= data_bytes[1] << 8U;
	if (bytes > PDI_DATA_16)
		data |= data_bytes[2] << 16U;
	if (bytes > PDI_DATA_24)
		data |= data_bytes[3] << 24U;
	*value = data;
	return true;
}

static inline bool avr_pdi_read8(avr_pdi_t *pdi, uint32_t reg, uint8_t *value)
{
	uint32_t data;
	const bool result = avr_pdi_read(pdi, PDI_DATA_8, reg, &data);
	if (result)
		*value = data;
	return result;
}

static inline bool avr_pdi_read16(avr_pdi_t *pdi, uint32_t reg, uint16_t *value)
{
	uint32_t data;
	const bool result = avr_pdi_read(pdi, PDI_DATA_16, reg, &data);
	if (result)
		*value = data;
	return result;
}

static inline bool avr_pdi_read24(avr_pdi_t *pdi, uint32_t reg, uint32_t *value)
	{ return avr_pdi_read(pdi, PDI_DATA_24, reg, value); }

static inline bool avr_pdi_read32(avr_pdi_t *pdi, uint32_t reg, uint32_t *value)
	{ return avr_pdi_read(pdi, PDI_DATA_32, reg, value); }

// Runs `st ptr <addr>`
static bool avr_pdi_write_ptr(const avr_pdi_t *const pdi, const uint32_t addr)
{
	const uint8_t command = PDI_ST | PDI_MODE_DIR_PTR | PDI_DATA_32;
	uint8_t result = 0;
	return !avr_jtag_shift_dr(&jtag_proc, pdi->dp_jd_index, &result, command) && result == PDI_EMPTY &&
		!avr_jtag_shift_dr(&jtag_proc, pdi->dp_jd_index, &result, addr & 0xffU) && result == PDI_EMPTY &&
		!avr_jtag_shift_dr(&jtag_proc, pdi->dp_jd_index, &result, (addr >> 8U) & 0xffU) && result == PDI_EMPTY &&
		!avr_jtag_shift_dr(&jtag_proc, pdi->dp_jd_index, &result, (addr >> 16U) & 0xffU) && result == PDI_EMPTY &&
		!avr_jtag_shift_dr(&jtag_proc, pdi->dp_jd_index, &result, (addr >> 24U) & 0xffU) && result == PDI_EMPTY;
}

// Runs `repeat <count - 1>`
static bool avr_pdi_repeat(const avr_pdi_t *const pdi, const uint32_t count)
{
	const uint32_t repeat = count - 1U;
	const uint8_t command = PDI_REPEAT | PDI_DATA_32;
	uint8_t result = 0;
	return !avr_jtag_shift_dr(&jtag_proc, pdi->dp_jd_index, &result, command) && result == PDI_EMPTY &&
		!avr_jtag_shift_dr(&jtag_proc, pdi->dp_jd_index, &result, repeat & 0xffU) && result == PDI_EMPTY &&
		!avr_jtag_shift_dr(&jtag_proc, pdi->dp_jd_index, &result, (repeat >> 8U) & 0xffU) && result == PDI_EMPTY &&
		!avr_jtag_shift_dr(&jtag_proc, pdi->dp_jd_index, &result, (repeat >> 16U) & 0xffU) && result == PDI_EMPTY &&
		!avr_jtag_shift_dr(&jtag_proc, pdi->dp_jd_index, &result, (repeat >> 24U) & 0xffU) && result == PDI_EMPTY;
}

static bool avr_pdi_read_ind(const avr_pdi_t *const pdi, const uint32_t addr, const uint8_t ptr_mode,
	void *const dst, const uint32_t count)
{
	const uint8_t command = PDI_LD | ptr_mode;
	uint8_t result = 0;
	uint8_t *const data = (uint8_t *)dst;
	if ((ptr_mode & PDI_MODE_MASK) || !count ||
		!avr_pdi_write_ptr(pdi, addr) ||
		!avr_pdi_repeat(pdi, count))
		return false;
	// Run `ld <ptr_mode>`
	if (avr_jtag_shift_dr(&jtag_proc, pdi->dp_jd_index, &result, command) || result != PDI_EMPTY)
		return false;
	for (uint32_t i = 0; i < count; ++i)
	{
		if (!avr_jtag_shift_dr(&jtag_proc, pdi->dp_jd_index, data + i, 0))
			return false;
	}
	return true;
}

static bool avr_pdi_write_ind(const avr_pdi_t *const pdi, const uint32_t addr, const uint8_t ptr_mode,
	const void *const src, const uint32_t count)
{
	const uint8_t command = PDI_ST | ptr_mode;
	uint8_t result = 0;
	const uint8_t *const data = (const uint8_t *)src;
	if ((ptr_mode & PDI_MODE_MASK) || !count ||
		!avr_pdi_write_ptr(pdi, addr) ||
		!avr_pdi_repeat(pdi, count))
		return false;
	// Run `st <ptr_mode>`
	if (avr_jtag_shift_dr(&jtag_proc, pdi->dp_jd_index, &result, command) || result != PDI_EMPTY)
		return false;
	for (uint32_t i = 0; i < count; ++i)
	{
		if (avr_jtag_shift_dr(&jtag_proc, pdi->dp_jd_index, &result, data[i]) || result != PDI_EMPTY)
			return false;
	}
	return true;
}

static bool avr_enable(avr_pdi_t *pdi, pdi_key_e what)
{
	const char *const key = what == PDI_DEBUG ? pdi_key_debug : pdi_key_nvm;
	uint8_t result = 0;
	if (avr_jtag_shift_dr(&jtag_proc, pdi->dp_jd_index, &result, PDI_KEY) || result != PDI_EMPTY)
		return false;
	for (uint8_t i = 0; i < 8; ++i)
	{
		if (avr_jtag_shift_dr(&jtag_proc, pdi->dp_jd_index, &result, key[i]) || result != PDI_EMPTY)
			return false;
	}
	if (what == PDI_NVM)
	{
		while ((avr_pdi_reg_read(pdi, PDI_REG_STATUS) & what) != what)
			continue;
		return true;
	}
	else
		return (avr_pdi_reg_read(pdi, PDI_REG_STATUS) & what) == what;
}

static bool avr_disable(avr_pdi_t *pdi, pdi_key_e what)
{
	return avr_pdi_reg_write(pdi, PDI_REG_STATUS, ~what);
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
	f->erase = avr_flash_erase;
	f->write = avr_flash_write;
	f->erased = 0xff;
	target_add_flash(t, f);
}

static bool avr_ensure_nvm_idle(avr_pdi_t *pdi)
{
	return avr_pdi_write(pdi, PDI_DATA_8, AVR_ADDR_NVM_CMD, 0) &&
		avr_pdi_write(pdi, PDI_DATA_8, AVR_ADDR_NVM_DATA, 0xFFU);
}

static bool avr_config_breakpoints(avr_pdi_t *pdi, const bool step)
{
	const uint32_t addr_breakpoint_counter = AVR_ADDR_DBG_BREAK_BASE + (pdi->hw_breakpoints_max * 4);
	const uint16_t breakpoint_count = step ? 0U : (pdi->hw_breakpoints_enabled << 8U);
	if (step)
	{
		for (uint8_t i = 0; i < pdi->hw_breakpoints_max; ++i)
		{
			if (!avr_pdi_write(pdi, PDI_DATA_32, AVR_ADDR_DBG_BREAK_BASE + (i * 4), 0U))
				return false;
		}
	}
	else
	{
		for (uint8_t i = 0; i < pdi->hw_breakpoints_max; ++i)
		{
			if (!avr_pdi_write(pdi, PDI_DATA_32, AVR_ADDR_DBG_BREAK_BASE + (i * 4),
					pdi->hw_breakpoint[i] & AVR_DBG_BREAK_MASK))
				return false;
		}
	}
	if (!avr_pdi_write(pdi, PDI_DATA_8, AVR_ADDR_DBG_BREAK_UNK1, 0) ||
		!avr_pdi_write(pdi, PDI_DATA_8, AVR_ADDR_DBG_BREAK_UNK2, 0) ||
		!avr_pdi_write(pdi, PDI_DATA_16, addr_breakpoint_counter, breakpoint_count) ||
		!avr_pdi_write(pdi, PDI_DATA_8, AVR_ADDR_DBG_BREAK_UNK3, 0))
		return false;
	return true;
}

bool avr_attach(target *t)
{
	avr_pdi_t *pdi = t->priv;
	volatile struct exception e;
	jtag_dev_write_ir(&jtag_proc, pdi->dp_jd_index, IR_PDI);

	TRY_CATCH (e, EXCEPTION_ALL) {
		target_reset(t);
		if (!avr_enable(pdi, PDI_DEBUG))
			return false;
		target_halt_request(t);
		if (!avr_enable(pdi, PDI_NVM) ||
			!avr_ensure_nvm_idle(pdi) ||
			avr_pdi_reg_read(pdi, PDI_REG_R3) != 0x04U)
			return false;
	}
	return !e.type;
}

void avr_detach(target *t)
{
	avr_pdi_t *pdi = t->priv;

	avr_disable(pdi, PDI_NVM);
	avr_disable(pdi, PDI_DEBUG);
	jtag_dev_write_ir(&jtag_proc, pdi->dp_jd_index, IR_BYPASS);
}

static void avr_reset(target *t)
{
	avr_pdi_t *pdi = t->priv;
	// We only actually want to do this if the target is not presently attached as this resets the NVM and debug enables
	if (target_attached(t))
		return;
	if (!avr_pdi_reg_write(pdi, PDI_REG_RESET, PDI_RESET))
		raise_exception(EXCEPTION_ERROR, "Error resetting device, device in incorrect state");
	if (avr_pdi_reg_read(pdi, PDI_REG_STATUS) != 0x00)
	{
		avr_disable(pdi, PDI_NVM);
		avr_disable(pdi, PDI_DEBUG);
	}
	pdi->programCounter = 0;
}

static void avr_halt_request(target *t)
{
	avr_pdi_t *pdi = t->priv;
	/* To halt the processor we go through a few really specific steps:
	 * Write r4 to 1 to indicate we want to put the processor into debug-based pause
	 * Read r3 and check it's 0x10 which indicates the processor is held in reset and no debugging is active
	 * Releae reset
	 * Read r3 twice more, the first time should respond 0x14 to indicate the processor is still reset
	 * but that debug pause is requested, and the second should respond 0x04 to indicate the processor is now
	 * in debug pause state (halted)
	 */
	if (!avr_pdi_reg_write(pdi, PDI_REG_R4, 1) ||
		avr_pdi_reg_read(pdi, PDI_REG_R3) != 0x10U ||
		!avr_pdi_reg_write(pdi, PDI_REG_RESET, 0) ||
		avr_pdi_reg_read(pdi, PDI_REG_R3) != 0x14U ||
		avr_pdi_reg_read(pdi, PDI_REG_R3) != 0x04U)
		raise_exception(EXCEPTION_ERROR, "Error halting device, device in incorrect state");
	pdi->halt_reason = TARGET_HALT_REQUEST;
}

static void avr_halt_resume(target *t, const bool step)
{
	avr_pdi_t *pdi = t->priv;
	if (step)
	{
		const target_addr currentPC = pdi->programCounter;
		const target_addr nextPC = currentPC + 1U;
		/* To do a single step, we run the following steps:
		 * Write the debug control register to 4, which puts the processor in a temporary breakpoint mode
		 * Write the debug counter register with the address to stop execution on
		 * Write the program counter with the address to resume execution on
		 */
		if (avr_pdi_reg_read(pdi, PDI_REG_R3) != 0x04U ||
			!avr_config_breakpoints(pdi, step) ||
			!avr_pdi_write(pdi, PDI_DATA_8, AVR_ADDR_DBG_CTRL, 4) ||
			!avr_pdi_write(pdi, PDI_DATA_32, AVR_ADDR_DBG_CTR, nextPC) ||
			!avr_pdi_write(pdi, PDI_DATA_32, AVR_ADDR_DBG_PC, currentPC) ||
			!avr_pdi_reg_write(pdi, PDI_REG_R4, 1) ||
			avr_pdi_reg_read(pdi, PDI_REG_R3) != 0x04U ||
			avr_pdi_reg_read(pdi, PDI_REG_STATUS) != 0x06U)
			raise_exception(EXCEPTION_ERROR, "Error stepping device, device in incorrect state");
		pdi->halt_reason = TARGET_HALT_STEPPING;
	}
	else
	{
		/* To resume the processor we go through the following specific steps:
		 * Write the program counter to ensure we start where we expect
		 * Then we release the externally (PDI) applied reset
		 * We then poke the debug control register to indicate debug-supervised run
		 * Ensure that PDI is still in debug mode (r4 = 1)
		 * Read r3 to see that the processor is resuming
		 */
		if (avr_pdi_reg_read(pdi, PDI_REG_R3) != 0x04U ||
			!avr_config_breakpoints(pdi, step) ||
			!avr_pdi_write(pdi, PDI_DATA_32, AVR_ADDR_DBG_PC, pdi->programCounter) ||
			!avr_pdi_reg_write(pdi, PDI_REG_RESET, 0) ||
			!avr_pdi_write(pdi, PDI_DATA_8, AVR_ADDR_DBG_CTRL, 0) ||
			!avr_pdi_reg_write(pdi, PDI_REG_R4, 1))
			raise_exception(EXCEPTION_ERROR, "Error resuming device, device in incorrect state");
		pdi->halt_reason = TARGET_HALT_RUNNING;
	}
}

static enum target_halt_reason avr_halt_poll(target *t, target_addr *watch)
{
	avr_pdi_t *pdi = t->priv;
	if (pdi->halt_reason == TARGET_HALT_RUNNING &&
		avr_pdi_reg_read(pdi, PDI_REG_R3) == 0x04U)
		pdi->halt_reason = TARGET_HALT_BREAKPOINT;
	(void)watch;
	return pdi->halt_reason;
}

static bool avr_check_error(target *t)
{
	avr_pdi_t *pdi = t->priv;
	return pdi->error_state != pdi_ok;
}

static void avr_mem_read(target *t, void *dest, target_addr src, size_t len)
{
	avr_pdi_t *pdi = t->priv;
	if (target_flash_for_addr(t, src) != NULL)
	{
		// This presently assumes src is a Flash address.
		if (!avr_pdi_write(pdi, PDI_DATA_8, AVR_ADDR_NVM_CMD, AVR_NVM_CMD_READ_NVM) ||
			!avr_pdi_read_ind(pdi, src | PDI_FLASH_OFFSET, PDI_MODE_IND_INCPTR, dest, len) ||
			!avr_ensure_nvm_idle(pdi))
			pdi->error_state = pdi_failure;
	}
	else if (src >= 0x00800000U)
	{
		if (!avr_pdi_read_ind(pdi, src + 0x00800000U, PDI_MODE_IND_INCPTR, dest, len))
			pdi->error_state = pdi_failure;
	}
}

static void avr_regs_read(target *t, void *data)
{
	avr_pdi_t *pdi = t->priv;
	avr_regs *regs = (avr_regs *)data;
	uint8_t status[3];
	uint32_t pc = 0;
	if (!avr_pdi_read32(pdi, AVR_ADDR_DBG_PC, &pc) ||
		!avr_pdi_read_ind(pdi, AVR_ADDR_CPU_SPL, PDI_MODE_IND_INCPTR, status, 3) ||
		!avr_pdi_write(pdi, PDI_DATA_32, AVR_ADDR_DBG_PC, 0) ||
		!avr_pdi_write(pdi, PDI_DATA_32, AVR_ADDR_DBG_CTR, AVR_NUM_REGS) ||
		!avr_pdi_write(pdi, PDI_DATA_8, AVR_ADDR_DBG_CTRL, AVR_DBG_READ_REGS) ||
		!avr_pdi_reg_write(pdi, PDI_REG_R4, 1) ||
		!avr_pdi_read_ind(pdi, AVR_ADDR_DBG_SPECIAL, PDI_MODE_IND_PTR, regs->general, 32) ||
		avr_pdi_reg_read(pdi, PDI_REG_R3) != 0x04U)
		raise_exception(EXCEPTION_ERROR, "Error reading registers");
	// These aren't in the reads above because regs is a packed struct, which results in compiler errors
	// Additionally, the program counter is stored in words and points to the next instruction to be executed
	// So we substract 1 and double.
	regs->pc = (pc - 1) << 1;
	regs->sp = status[0] | (status[1] << 8);
	regs->sreg = status[2];
	pdi->programCounter = pc - 1;
}

static int avr_flash_erase(struct target_flash *f, target_addr addr, size_t len)
{
	avr_pdi_t *pdi = f->t->priv;
	for (size_t i = 0; i < len; i += f->blocksize)
	{
		uint8_t status = 0;
		if (!avr_pdi_write(pdi, PDI_DATA_8, AVR_ADDR_NVM_CMD, AVR_NVM_CMD_ERASE_FLASH_PAGE) ||
			!avr_pdi_write(pdi, PDI_DATA_8, (addr + i) | PDI_FLASH_OFFSET, 0x55U))
			return 1;

		while (avr_pdi_read8(pdi, AVR_ADDR_NVM_STATUS, &status) &&
			(status & (AVR_NVM_BUSY | AVR_NVM_FBUSY)) == (AVR_NVM_BUSY | AVR_NVM_FBUSY))
			continue;

		if (!avr_ensure_nvm_idle(pdi) ||
			// This can only happen if the read failed.
			(status & (AVR_NVM_BUSY | AVR_NVM_FBUSY)) != 0)
			return 1;
	}
	return 0;
}

static int avr_flash_write(struct target_flash *f, target_addr dest, const void *src, size_t len)
{
	avr_pdi_t *pdi = f->t->priv;
	const uint8_t *const buffer = (const uint8_t *)src;
	for (size_t i = 0; i < len; i += f->blocksize)
	{
		uint8_t status = 0;
		const size_t amount = MIN(f->blocksize, len - i);
		const uint32_t addr = (dest + i) | PDI_FLASH_OFFSET;
		if (!avr_pdi_write(pdi, PDI_DATA_8, AVR_ADDR_NVM_CMD, AVR_NVM_CMD_WRITE_FLASH_BUFFER) ||
			!avr_pdi_write_ind(pdi, addr, PDI_MODE_IND_INCPTR, buffer + i, amount) ||
			!avr_pdi_write(pdi, PDI_DATA_8, AVR_ADDR_NVM_CMD, AVR_NVM_CMD_WRITE_FLASH_PAGE) ||
			!avr_pdi_write(pdi, PDI_DATA_8, addr, 0xFFU))
			return 1;

		while (avr_pdi_read8(pdi, AVR_ADDR_NVM_STATUS, &status) &&
			(status & (AVR_NVM_BUSY | AVR_NVM_FBUSY)) == (AVR_NVM_BUSY | AVR_NVM_FBUSY))
			continue;

		if (!avr_ensure_nvm_idle(pdi) ||
			// This can only happen if the read failed.
			(status & (AVR_NVM_BUSY | AVR_NVM_FBUSY)) != 0)
			return 1;
	}
	return 0;
}

static int avr_breakwatch_set(target *t, struct breakwatch *bw)
{
	avr_pdi_t *pdi = t->priv;
	switch (bw->type)
	{
		case TARGET_BREAK_HARD:
		{
			const uint8_t bp = pdi->hw_breakpoints_enabled;
			if (bp == pdi->hw_breakpoints_max)
				return -1;
			pdi->hw_breakpoint[bp] = AVR_DBG_BREAK_ENABLED | bw->addr;
			bw->reserved[0] = pdi->hw_breakpoint[bp];
			++pdi->hw_breakpoints_enabled;
			return 0;
		}
		default:
			return 1;
	}
}

static int avr_breakwatch_clear(target *t, struct breakwatch *bw)
{
	avr_pdi_t *pdi = t->priv;
	switch (bw->type)
	{
		case TARGET_BREAK_HARD:
		{
			uint8_t bp = 0;
			// Locate the breakpoint
			for (; bp < pdi->hw_breakpoints_max; ++bp)
			{
				if (pdi->hw_breakpoint[bp] == bw->reserved[0])
					break;
			}
			// Fail if we cannot find it
			if (bp == pdi->hw_breakpoints_max)
				return -1;
			// Shuffle the remaining breakpoints.
			for (; bp < (pdi->hw_breakpoints_enabled - 1U); ++bp)
				pdi->hw_breakpoint[bp] = pdi->hw_breakpoint[bp + 1];
			// Cleanup by disabling the breakpoint and fixing the count
			pdi->hw_breakpoint[bp] = 0;
			bw->reserved[0] = 0;
			--pdi->hw_breakpoints_enabled;
			return 0;
		}
		default:
			return 1;
	}
}

static bool avr_erase(target *t, int argc, char **argv)
{
	if (argc < 2)
		tc_printf(t, "usage: monitor erase (<start_address>) <length>\n");
	else
	{
		target_addr begin = 0;
		size_t length = 0;
		if (argc >= 3)
		{
			begin = atol(argv[1]);
			length = atol(argv[2]);
		}
		else
			length = atol(argv[1]);
		target_flash_erase(t, begin, length);
	}
	return true;
}
