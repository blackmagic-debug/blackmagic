/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2015, 2017 - 2022  Uwe Bonnes
 *                             <bon@elektron.ikp.physik.tu-darmstadt.de>
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

/*
 * This file implements STM32L4 target specific functions for detecting
 * the device, providing the XML memory map and Flash memory programming.
 *
 * On L4, flash and options are written in DWORDs (8-Byte) only.
 *
 * References:
 * RM0351 STM32L4x5 and STM32L4x6 advanced ARM®-based 32-bit MCUs Rev. 5
 * - https://www.st.com/resource/en/reference_manual/rm0351-stm32l47xxx-stm32l48xxx-stm32l49xxx-and-stm32l4axxx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf
 * RM0394 STM32L43xxx STM32L44xxx STM32L45xxx STM32L46xxxx advanced
 *  ARM®-based 32-bit MCUs Rev.3
 * RM0432 STM32L4Rxxx and STM32L4Sxxx advanced Arm®-based 32-bit MCU. Rev 1
 * RM0440 STM32G4 Series advanced Arm®-based 32-bit MCU. Rev 6
 * RM0434 Multiprotocol wireless 32-bit MCU Arm®-based Cortex®-M4 with
 *        FPU, Bluetooth® Low-Energy and 802.15.4 radio solution
 * RM0453 STM32WL5x advanced Arm®-based 32-bit MCUswith sub-GHz radio solution
 */

#include <limits.h>
#include <assert.h>
#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"
#include "gdb_packet.h"

static bool stm32l4_cmd_erase_bank1(target *t, int argc, const char **argv);
static bool stm32l4_cmd_erase_bank2(target *t, int argc, const char **argv);
static bool stm32l4_cmd_option(target *t, int argc, const char **argv);

static bool stm32l4_attach(target *t);
static void stm32l4_detach(target *t);
static bool stm32l4_flash_erase(target_flash_s *f, target_addr_t addr, size_t len);
static bool stm32l4_flash_write(target_flash_s *f, target_addr_t dest, const void *src, size_t len);
static bool stm32l4_mass_erase(target *t);

const struct command_s stm32l4_cmd_list[] = {
	{"erase_bank1", stm32l4_cmd_erase_bank1, "Erase entire bank1 flash memory"},
	{"erase_bank2", stm32l4_cmd_erase_bank2, "Erase entire bank2 flash memory"},
	{"option", stm32l4_cmd_option, "Manipulate option bytes"},
	{NULL, NULL, NULL},
};

/* Flash Program ad Erase Controller Register Map */
#define STM32L4_FPEC_BASE 0x40022000U
#define STM32L5_FPEC_BASE 0x40022000U
#define STM32WL_FPEC_BASE 0x58004000U
#define STM32WB_FPEC_BASE 0x58004000U

#define STM32L5_FLASH_OPTR_TZEN (1U << 31U)

#define FLASH_OPTR_ESE (1U << 8U)
#define PWR_CR4        0x5800040cU
#define PWR_CR4_C2BOOT (1U << 15U)

#define FLASH_CR_PG         (1U << 0U)
#define FLASH_CR_PER        (1U << 1U)
#define FLASH_CR_MER1       (1U << 2U)
#define FLASH_CR_PAGE_SHIFT 3U
#define FLASH_CR_BKER       (1U << 11U)
#define FLASH_CR_MER2       (1U << 15U)
#define FLASH_CR_STRT       (1U << 16U)
#define FLASH_CR_OPTSTRT    (1U << 17U)
#define FLASH_CR_FSTPG      (1U << 18U)
#define FLASH_CR_EOPIE      (1U << 24U)
#define FLASH_CR_ERRIE      (1U << 25U)
#define FLASH_CR_OBL_LAUNCH (1U << 27U)
#define FLASH_CR_OPTLOCK    (1U << 30U)
#define FLASH_CR_LOCK       (1U << 31U)

#define FLASH_SR_EOP        (1U << 0U)
#define FLASH_SR_OPERR      (1U << 1U)
#define FLASH_SR_PROGERR    (1U << 3U)
#define FLASH_SR_WRPERR     (1U << 4U)
#define FLASH_SR_PGAERR     (1U << 5U)
#define FLASH_SR_SIZERR     (1U << 6U)
#define FLASH_SR_PGSERR     (1U << 7U)
#define FLASH_SR_MSERR      (1U << 8U)
#define FLASH_SR_FASTERR    (1U << 9U)
#define FLASH_SR_RDERR      (1U << 14U)
#define FLASH_SR_OPTVERR    (1U << 15U)
#define FLASH_SR_ERROR_MASK 0xc3faU
#define FLASH_SR_BSY        (1U << 16U)

#define STM32L4_FLASH_BANK_1_BASE 0x08000000U
#define FLASH_SIZE_MAX_G4_CAT4    (512U * 1024U) // 512kiB

#define KEY1 0x45670123U
#define KEY2 0xcdef89abU

#define OPTKEY1 0x08192a3bU
#define OPTKEY2 0x4c5d6e7fU

#define SR_ERROR_MASK 0xf2U

/* Used in STM32L47*/
#define OR_DUALBANK (1U << 21U)
/* Used in STM32L47R*/
#define OR_DB1M (1U << 21U)
/* Used in STM32L47R, STM32G47 and STM32L55*/
#define OR_DBANK (1U << 22U)

#define DBGMCU_CR(reg_base)   ((reg_base) + 0x04U)
#define DBGMCU_CR_DBG_SLEEP   (1U << 0U)
#define DBGMCU_CR_DBG_STOP    (1U << 1U)
#define DBGMCU_CR_DBG_STANDBY (1U << 2U)

#define STM32L4_DBGMCU_IDCODE_PHYS 0xe0042000U
#define STM32L5_DBGMCU_IDCODE_PHYS 0xe0044000U

#define STM32L4_FLASH_SIZE_REG 0x1fff75e0U
#define STM32L5_FLASH_SIZE_REG 0x0bfa05e0U

#define STM32L5_RCC_APB1ENR1       0x50021058U
#define STM32L5_RCC_APB1ENR1_PWREN (1U << 28U)
#define STM32L5_PWR_CR1            0x50007000U
#define STM32L5_PWR_CR1_VOS        (3U << 9U)

#define DUAL_BANK     0x80U
#define RAM_COUNT_MSK 0x07U

typedef enum stm32l4_device_id {
	ID_STM32L41 = 0x464U,  /* RM0394, Rev.4 */
	ID_STM32L43 = 0x435U,  /* RM0394, Rev.4 */
	ID_STM32L45 = 0x462U,  /* RM0394, Rev.4 */
	ID_STM32L47 = 0x415U,  /* RM0351, Rev.5 */
	ID_STM32L49 = 0x461U,  /* RM0351, Rev.5 */
	ID_STM32L4R = 0x470U,  /* RM0432, Rev.5 */
	ID_STM32G43 = 0x468U,  /* RM0440, Rev.1 */
	ID_STM32G47 = 0x469U,  /* RM0440, Rev.1 */
	ID_STM32G49 = 0x479U,  /* RM0440, Rev.6 */
	ID_STM32L55 = 0x472U,  /* RM0438, Rev.4 */
	ID_STM32WLXX = 0x497U, /* RM0461, Rev.3, RM453, Rev.1 */
	ID_STM32WBXX = 0x495U, /* RM0434, Rev.9 */
} stm32l4_device_id_e;

typedef enum stm32l4_family {
	STM32L4_FAMILY_L4xx,
	STM32L4_FAMILY_L4Rx,
	STM32L4_FAMILY_WBxx,
	STM32L4_FAMILY_G4xx,
	STM32L4_FAMILY_L55x,
	STM32L4_FAMILY_WLxx,
} stm32l4_family_e;

typedef struct stm32l4_device_info {
	const char *designator;
	uint16_t sram1; /* Normal SRAM mapped at 0x20000000 */
	uint16_t sram2; /* SRAM at 0x10000000, mapped after sram1 (not L47) */
	uint16_t sram3; /* SRAM mapped after SRAM1 and SRAM2 */
	uint8_t flags;  /* Only DUAL_BANK is evaluated for now. */
	stm32l4_device_id_e device_id;
	stm32l4_family_e family;
	const uint32_t *flash_regs_map;
} stm32l4_device_info_s;

typedef struct stm32l4_flash {
	target_flash_s f;
	uint32_t bank1_start;
} stm32l4_flash_s;

typedef struct stm32l4_priv {
	const stm32l4_device_info_s *device;
	uint32_t dbgmcu_cr;
} stm32l4_priv_s;

typedef struct stm32l4_option_bytes_info {
	const uint8_t *offsets;
	const uint32_t *default_values;
	const uint8_t word_count;
} stm32l4_option_bytes_info_s;

typedef enum stm32l4_flash_reg {
	FLASH_KEYR,
	FLASH_OPTKEYR,
	FLASH_SR,
	FLASH_CR,
	FLASH_OPTR,
	FLASHSIZE,
	FLASH_REGS_COUNT
} stm32l4_flash_reg_e;

static const uint32_t stm32l4_flash_regs_map[FLASH_REGS_COUNT] = {
	STM32L4_FPEC_BASE + 0x08, /* KEYR */
	STM32L4_FPEC_BASE + 0x0c, /* OPTKEYR */
	STM32L4_FPEC_BASE + 0x10, /* SR */
	STM32L4_FPEC_BASE + 0x14, /* CR */
	STM32L4_FPEC_BASE + 0x20, /* OPTR */
	STM32L4_FLASH_SIZE_REG,   /* FLASHSIZE */
};

static const uint32_t stm32l5_flash_regs_map[FLASH_REGS_COUNT] = {
	STM32L5_FPEC_BASE + 0x08, /* KEYR */
	STM32L5_FPEC_BASE + 0x10, /* OPTKEYR */
	STM32L5_FPEC_BASE + 0x20, /* SR */
	STM32L5_FPEC_BASE + 0x28, /* CR */
	STM32L5_FPEC_BASE + 0x40, /* OPTR */
	STM32L5_FLASH_SIZE_REG,   /* FLASHSIZE */
};

static const uint32_t stm32wl_flash_regs_map[FLASH_REGS_COUNT] = {
	STM32WL_FPEC_BASE + 0x08, /* KEYR */
	STM32WL_FPEC_BASE + 0x0c, /* OPTKEYR */
	STM32WL_FPEC_BASE + 0x10, /* SR */
	STM32WL_FPEC_BASE + 0x14, /* CR */
	STM32WL_FPEC_BASE + 0x20, /* OPTR */
	STM32L4_FLASH_SIZE_REG,   /* FLASHSIZE */
};

static const uint32_t stm32wb_flash_regs_map[FLASH_REGS_COUNT] = {
	STM32WB_FPEC_BASE + 0x08, /* KEYR */
	STM32WB_FPEC_BASE + 0x0c, /* OPTKEYR */
	STM32WB_FPEC_BASE + 0x10, /* SR */
	STM32WB_FPEC_BASE + 0x14, /* CR */
	STM32WB_FPEC_BASE + 0x20, /* OPTR */
	STM32L4_FLASH_SIZE_REG,   /* FLASHSIZE */
};

static stm32l4_device_info_s const stm32l4_device_info[] = {
	{
		.device_id = ID_STM32L41,
		.family = STM32L4_FAMILY_L4xx,
		.designator = "STM32L41x",
		.sram1 = 32U,
		.sram2 = 8U,
		.flags = 2,
		.flash_regs_map = stm32l4_flash_regs_map,
	},
	{
		.device_id = ID_STM32L43,
		.family = STM32L4_FAMILY_L4xx,
		.designator = "STM32L43x",
		.sram1 = 48U,
		.sram2 = 16U,
		.flags = 2,
		.flash_regs_map = stm32l4_flash_regs_map,
	},
	{
		.device_id = ID_STM32L45,
		.family = STM32L4_FAMILY_L4xx,
		.designator = "STM32L45x",
		.sram1 = 128U,
		.sram2 = 32U,
		.flags = 2,
		.flash_regs_map = stm32l4_flash_regs_map,
	},
	{
		.device_id = ID_STM32L47,
		.family = STM32L4_FAMILY_L4xx,
		.designator = "STM32L47x",
		.sram1 = 96U,
		.sram2 = 32U,
		.flags = 2 | DUAL_BANK,
		.flash_regs_map = stm32l4_flash_regs_map,
	},
	{
		.device_id = ID_STM32L49,
		.family = STM32L4_FAMILY_L4xx,
		.designator = "STM32L49x",
		.sram1 = 256U,
		.sram2 = 64U,
		.flags = 2 | DUAL_BANK,
		.flash_regs_map = stm32l4_flash_regs_map,
	},
	{
		.device_id = ID_STM32L4R,
		.family = STM32L4_FAMILY_L4Rx,
		.designator = "STM32L4Rx",
		.sram1 = 192U,
		.sram2 = 64U,
		.sram3 = 384U,
		.flags = 3 | DUAL_BANK,
		.flash_regs_map = stm32l4_flash_regs_map,
	},
	{
		.device_id = ID_STM32G43,
		.family = STM32L4_FAMILY_G4xx,
		.designator = "STM32G43",
		.sram1 = 22U,
		.sram2 = 10U,
		.flash_regs_map = stm32l4_flash_regs_map,
	},
	{
		.device_id = ID_STM32G47,
		.family = STM32L4_FAMILY_G4xx,
		.designator = "STM32G47",
		.sram1 = 96U, /* SRAM1 and SRAM2 are mapped continuous */
		.sram2 = 32U, /* CCM SRAM is mapped as per SRAM2 on G4 */
		.flags = 2,
		.flash_regs_map = stm32l4_flash_regs_map,
	},
	{
		.device_id = ID_STM32G49,
		.family = STM32L4_FAMILY_G4xx,
		.designator = "STM32G49",
		.sram1 = 96U, /* SRAM1 and SRAM2 are mapped continuously */
		.sram2 = 16U, /* CCM SRAM is mapped as per SRAM2 on G4 */
		.flags = 2,
		.flash_regs_map = stm32l4_flash_regs_map,
	},
	{
		.device_id = ID_STM32L55,
		.family = STM32L4_FAMILY_L55x,
		.designator = "STM32L55",
		.sram1 = 192U, /* SRAM1 and SRAM2 are mapped continuous */
		.sram2 = 64U,
		.flags = 2,
		.flash_regs_map = stm32l5_flash_regs_map,
	},
	{
		.device_id = ID_STM32WLXX,
		.family = STM32L4_FAMILY_WLxx,
		.designator = "STM32WLxx",
		.sram1 = 32U,
		.sram2 = 32U,
		.flags = 2,
		.flash_regs_map = stm32wl_flash_regs_map,
	},
	{
		.device_id = ID_STM32WBXX,
		.family = STM32L4_FAMILY_WBxx,
		.designator = "STM32WBxx",
		.sram1 = 192U,
		.sram2 = 64U,
		.flags = 2,
		.flash_regs_map = stm32wb_flash_regs_map,
	},
	{
		/* Sentinel entry */
		.device_id = 0,
	},
};

static const uint8_t stm32l4_opt_reg_offsets[9] = {0x20, 0x24, 0x28, 0x2c, 0x30, 0x44, 0x48, 0x4c, 0x50};
static const uint8_t stm32g4_opt_reg_offsets[11] = {0x20, 0x24, 0x28, 0x2c, 0x30, 0x70, 0x44, 0x48, 0x4c, 0x50, 0x74};
static const uint8_t stm32wl_opt_reg_offsets[7] = {0x20, 0x24, 0x28, 0x2c, 0x30, 0x34, 0x38};

static const uint32_t stm32l4_default_options_values[9] = {
	0xffeff8aaU,
	0xffffffffU,
	0x00000000U,
	0x000000ffU,
	0x000000ffU,
	0xffffffffU,
	0x00000000U,
	0x000000ffU,
	0x000000ffU,
};

/*
 * SEC_SIZE1 occupies 9 bits on G49/G4A (cat 4),
 * 8 bits on cat 3 and 7 bits on cat 2.
 * It is safe to write 0xFF00FE00 (cat 4 value) in FLASH_SEC1R
 */
static const uint32_t stm32g4_default_options_values[11] = {
	0xffeff8aaU,
	0xffffffffU,
	0x00ff0000U,
	0xff00ffffU,
	0xff00ffffU,
	0xff00fe00U,
	0xffffffffU,
	0xffffffffU,
	0xff00ffffU,
	0xff00ffffU,
	0xff00ff00U,
};

static const uint32_t stm32wl_default_options_values[7] = {
	0x3feff0aaU,
	0xffffffffU,
	0xffffff00U,
	0xff80ffffU,
	0xff80ffffU,
	0xffffffffU,
	0xffffff00U,
};

static_assert(ARRAY_LENGTH(stm32l4_opt_reg_offsets) == ARRAY_LENGTH(stm32l4_default_options_values),
	"Number of stm32l4 option registers must match number of default values");
static_assert(ARRAY_LENGTH(stm32g4_opt_reg_offsets) == ARRAY_LENGTH(stm32g4_default_options_values),
	"Number of stm32g4 option registers must match number of default values");
static_assert(ARRAY_LENGTH(stm32wl_opt_reg_offsets) == ARRAY_LENGTH(stm32wl_default_options_values),
	"Number of stm32wl option registers must match number of default values");

/* Retrieve device basic information, just add to the vector to extend */
static const stm32l4_device_info_s *stm32l4_get_device_info(const uint16_t device_id)
{
	const stm32l4_device_info_s *device_info = stm32l4_device_info;
	for (; device_info->device_id; ++device_info) {
		if (device_info->device_id == device_id)
			break;
	}
	/* If we haven't found a valid entry this returns the sentinel */
	return device_info;
}

static inline uint32_t stm32l4_flash_read16(target *const t, const stm32l4_flash_reg_e reg)
{
	stm32l4_priv_s *ps = (stm32l4_priv_s *)t->target_storage;
	const stm32l4_device_info_s *const device = ps->device;
	return target_mem_read16(t, device->flash_regs_map[reg]);
}

static inline uint32_t stm32l4_flash_read32(target *const t, const stm32l4_flash_reg_e reg)
{
	stm32l4_priv_s *ps = (stm32l4_priv_s *)t->target_storage;
	const stm32l4_device_info_s *const device = ps->device;
	return target_mem_read32(t, device->flash_regs_map[reg]);
}

static inline void stm32l4_flash_write32(target *const t, const stm32l4_flash_reg_e reg, const uint32_t value)
{
	stm32l4_priv_s *ps = (stm32l4_priv_s *)t->target_storage;
	const stm32l4_device_info_s *const device = ps->device;
	target_mem_write32(t, device->flash_regs_map[reg], value);
}

static void stm32l4_add_flash(
	target *const t, const uint32_t addr, const size_t length, const size_t blocksize, const uint32_t bank1_start)
{
	stm32l4_flash_s *sf = calloc(1, sizeof(*sf));
	if (!sf) { /* calloc failed: heap exhaustion */
		DEBUG_WARN("calloc: failed in %s\n", __func__);
		return;
	}

	target_flash_s *f = &sf->f;
	f->start = addr;
	f->length = length;
	f->blocksize = blocksize;
	f->erase = stm32l4_flash_erase;
	f->write = stm32l4_flash_write;
	f->writesize = 2048;
	f->erased = 0xffU;
	sf->bank1_start = bank1_start;
	target_add_flash(t, f);
}

/* For flash programming, L5 needs to be in VOS 0 or 1 while reset set 2 (or even 3?) */
static void stm32l5_flash_enable(target *t)
{
	target_mem_write32(t, STM32L5_RCC_APB1ENR1, STM32L5_RCC_APB1ENR1_PWREN);
	const uint32_t pwr_ctrl1 = target_mem_read32(t, STM32L5_PWR_CR1) & ~STM32L5_PWR_CR1_VOS;
	target_mem_write32(t, STM32L5_PWR_CR1, pwr_ctrl1);
}

static uint32_t stm32l4_idcode_reg_address(target *const t)
{
	const stm32l4_priv_s *const priv = (const stm32l4_priv_s *)t->target_storage;
	const stm32l4_device_info_s *const device = priv->device;
	if (device->family == STM32L4_FAMILY_L55x) {
		stm32l5_flash_enable(t);
		return STM32L5_DBGMCU_IDCODE_PHYS;
	}
	return STM32L4_DBGMCU_IDCODE_PHYS;
}

static uint32_t stm32l4_main_sram_length(const target *const t)
{
	const stm32l4_priv_s *const priv = (const stm32l4_priv_s *)t->target_storage;
	const stm32l4_device_info_s *const device = priv->device;
	/* All L4 beside L47 alias SRAM2 after SRAM1.*/
	if (t->part_id == ID_STM32L47)
		return device->sram1 * 1024U;
	return (device->sram1 + device->sram2 + device->sram3) * 1024U;
}

bool stm32l4_probe(target *const t)
{
	adiv5_access_port_s *ap = cortexm_ap(t);
	uint32_t device_id;
	if (ap->dp->version >= 2 && ap->dp->target_partno > 1) /* STM32L552 has invalid TARGETID 1 */
		/* FIXME: ids likely no longer match and need fixing */
		device_id = ap->dp->target_partno;
	else {
		uint32_t idcode_addr = STM32L4_DBGMCU_IDCODE_PHYS;
		/* FIXME: we probaly want to check if this is a C-M33 via cpuid */
		if (ap->dp->partno == 0xbe)
			idcode_addr = STM32L5_DBGMCU_IDCODE_PHYS;
		device_id = target_mem_read32(t, idcode_addr) & 0xfffU;
		DEBUG_INFO("IDCode %08" PRIx32 "\n", device_id);
	}

	const stm32l4_device_info_s *device = stm32l4_get_device_info(device_id);
	/* If the call returned the sentinel, it's not a supported L4 device */
	if (!device->device_id)
		return false;

	/* Save private storage */
	stm32l4_priv_s *priv_storage = calloc(1, sizeof(*priv_storage));
	priv_storage->device = device;
	t->target_storage = (void *)priv_storage;

	t->driver = device->designator;
	switch (device_id) {
	case ID_STM32WLXX:
	case ID_STM32WBXX:
		if ((stm32l4_flash_read32(t, FLASH_OPTR)) & FLASH_OPTR_ESE) {
			DEBUG_WARN("STM32W security enabled\n");
			t->driver = device_id == ID_STM32WLXX ? "STM32WLxx (secure)" : "STM32WBxx (secure)";
		}
		if (ap->apsel == 0) {
			/*
			 * Enable CPU2 from CPU1.
			 * CPU2 does not boot after reset w/o C2BOOT set.
			 * RM0453/RM0434, §6.6.4. PWR control register 4 (PWR_CR4)
			 */
			const uint32_t pwr_ctrl4 = target_mem_read32(t, PWR_CR4);
			target_mem_write32(t, PWR_CR4, pwr_ctrl4 | PWR_CR4_C2BOOT);
		}
		break;
	case ID_STM32L55:
		if ((stm32l4_flash_read32(t, FLASH_OPTR)) & STM32L5_FLASH_OPTR_TZEN) {
			DEBUG_WARN("STM32L5 Trust Zone enabled\n");
			t->core = "M33+TZ";
			break;
		}
	}
	t->mass_erase = stm32l4_mass_erase;
	t->attach = stm32l4_attach;
	t->detach = stm32l4_detach;
	target_add_commands(t, stm32l4_cmd_list, device->designator);
	return true;
}

static bool stm32l4_attach(target *const t)
{
	if (!cortexm_attach(t))
		return false;

	/* Retrive device information, and locate the device ID register */
	const stm32l4_device_info_s *device = stm32l4_get_device_info(t->part_id);
	const uint32_t idcode_addr = stm32l4_idcode_reg_address(t);

	/* Save DBGMCU_CR to restore it when detaching */
	stm32l4_priv_s *const priv_storage = (stm32l4_priv_s *)t->target_storage;
	priv_storage->dbgmcu_cr = target_mem_read32(t, DBGMCU_CR(idcode_addr));

	/* Enable debugging during all low power modes */
	target_mem_write32(t, DBGMCU_CR(idcode_addr), DBGMCU_CR_DBG_SLEEP | DBGMCU_CR_DBG_STANDBY | DBGMCU_CR_DBG_STOP);

	/* Free any previously built memory map */
	target_mem_map_free(t);
	/* And rebuild the RAM map */
	if (device->family == STM32L4_FAMILY_L55x)
		target_add_ram(t, 0x0A000000, device->sram1 + device->sram2);
	else
		target_add_ram(t, 0x10000000, device->sram2);
	target_add_ram(t, 0x20000000, stm32l4_main_sram_length(t));

	const uint16_t flash_len = stm32l4_flash_read16(t, FLASHSIZE);
	const uint32_t options = stm32l4_flash_read32(t, FLASH_OPTR);

	/* Now we have a base RAM map, rebuild the Flash map */
	if (device->family == STM32L4_FAMILY_WBxx)
		stm32l4_add_flash(t, STM32L4_FLASH_BANK_1_BASE, flash_len * 1024U, 0x1000, UINT32_MAX);
	else if (device->family == STM32L4_FAMILY_L4Rx) {
		/* RM0432 Rev. 2 does not mention 1MiB devices or explain DB1M.*/
		if (options & OR_DBANK) {
			stm32l4_add_flash(t, STM32L4_FLASH_BANK_1_BASE, 0x00100000, 0x1000, 0x08100000);
			stm32l4_add_flash(t, 0x08100000, 0x00100000, 0x1000, 0x08100000);
		} else
			stm32l4_add_flash(t, STM32L4_FLASH_BANK_1_BASE, 0x00200000, 0x2000, UINT32_MAX);
	} else if (device->family == STM32L4_FAMILY_L55x) {
		/* FIXME: Test behaviour on 256kiB devices */
		if (options & OR_DBANK) {
			stm32l4_add_flash(t, STM32L4_FLASH_BANK_1_BASE, 0x00040000, 0x0800, 0x08040000);
			stm32l4_add_flash(t, 0x08040000, 0x00040000, 0x0800, 0x08040000);
		} else
			stm32l4_add_flash(t, STM32L4_FLASH_BANK_1_BASE, 0x00080000, 0x0800, UINT32_MAX);
	} else if (device->family == STM32L4_FAMILY_G4xx) {
		/*
		 * RM0440 describes G43x/G44x as Category 2, G47x/G48x as Category 3 and G49x/G4Ax as Category 4 devices
		 * Cat 2 is always 128kiB with 2kiB pages, single bank
		 * Cat 3 is dual bank with an option bit to choose single 512kiB bank with 4kiB pages or
		 *     dual bank as 2x256kiB with 2kiB pages
		 * Cat 4 is single bank with up to 512kiB of 2kiB pages
		 */
		if (device->device_id == ID_STM32G43) {
			const uint32_t bank_len = flash_len * 1024U;
			stm32l4_add_flash(t, STM32L4_FLASH_BANK_1_BASE, bank_len, 0x0800, UINT32_MAX);
		} else if (device->device_id == ID_STM32G49) {
			/* Announce maximum possible flash length on this device */
			stm32l4_add_flash(t, STM32L4_FLASH_BANK_1_BASE, FLASH_SIZE_MAX_G4_CAT4, 0x0800, UINT32_MAX);
		} else {
			if (options & OR_DBANK) {
				const uint32_t bank_len = flash_len * 512U;
				stm32l4_add_flash(t, STM32L4_FLASH_BANK_1_BASE, bank_len, 0x0800, STM32L4_FLASH_BANK_1_BASE + bank_len);
				stm32l4_add_flash(
					t, STM32L4_FLASH_BANK_1_BASE + bank_len, bank_len, 0x0800, STM32L4_FLASH_BANK_1_BASE + bank_len);
			} else {
				const uint32_t bank_len = flash_len * 1024U;
				stm32l4_add_flash(t, STM32L4_FLASH_BANK_1_BASE, bank_len, 0x1000, UINT32_MAX);
			}
		}
	} else if (device->flags & DUAL_BANK) {
		if (options & OR_DUALBANK) {
			const uint32_t bank_len = flash_len * 512U;
			stm32l4_add_flash(t, STM32L4_FLASH_BANK_1_BASE, bank_len, 0x0800, STM32L4_FLASH_BANK_1_BASE + bank_len);
			stm32l4_add_flash(
				t, STM32L4_FLASH_BANK_1_BASE + bank_len, bank_len, 0x0800, STM32L4_FLASH_BANK_1_BASE + bank_len);
		} else {
			const uint32_t bank_len = flash_len * 1024U;
			stm32l4_add_flash(t, STM32L4_FLASH_BANK_1_BASE, bank_len, 0x0800, UINT32_MAX);
		}
	} else
		stm32l4_add_flash(t, STM32L4_FLASH_BANK_1_BASE, flash_len * 1024U, 0x800, UINT32_MAX);

	/* Clear all errors in the status register. */
	stm32l4_flash_write32(t, FLASH_SR, stm32l4_flash_read32(t, FLASH_SR));
	return true;
}

static void stm32l4_detach(target *const t)
{
	const stm32l4_priv_s *const ps = (stm32l4_priv_s *)t->target_storage;

	/*reverse all changes to DBGMCU_CR*/
	target_mem_write32(t, DBGMCU_CR(STM32L4_DBGMCU_IDCODE_PHYS), ps->dbgmcu_cr);
	cortexm_detach(t);
}

static void stm32l4_flash_unlock(target *const t)
{
	if ((stm32l4_flash_read32(t, FLASH_CR)) & FLASH_CR_LOCK) {
		/* Enable FPEC controller access */
		stm32l4_flash_write32(t, FLASH_KEYR, KEY1);
		stm32l4_flash_write32(t, FLASH_KEYR, KEY2);
	}
}

static bool stm32l4_flash_busy_wait(target *const t, platform_timeout *timeout)
{
	/* Read FLASH_SR to poll for BSY bit */
	uint32_t status = FLASH_SR_BSY;
	while (status & FLASH_SR_BSY) {
		status = stm32l4_flash_read32(t, FLASH_SR);
		if ((status & FLASH_SR_ERROR_MASK) || target_check_error(t)) {
			DEBUG_WARN("stm32l4 Flash error: status 0x%" PRIx32 "\n", status);
			return false;
		}
		if (timeout)
			target_print_progress(timeout);
	}
	return true;
}

static bool stm32l4_flash_erase(target_flash_s *const f, const target_addr_t addr, const size_t len)
{
	target *t = f->t;
	const stm32l4_flash_s *const sf = (stm32l4_flash_s *)f;
	/* Unlock the Flash and wait for the operation to complete, reporting any errors */
	stm32l4_flash_unlock(t);
	if (!stm32l4_flash_busy_wait(t, NULL))
		return false;

	/* Fixme: OPTVER always set after reset! Wrong option defaults? */
	stm32l4_flash_write32(t, FLASH_SR, stm32l4_flash_read32(t, FLASH_SR));

	/* Erase the requested chunk of flash, one page at a time. */
	for (size_t offset = 0; offset < len; offset += f->blocksize) {
		const uint32_t page = (addr + offset - STM32L4_FLASH_BANK_1_BASE) / f->blocksize;
		const uint32_t bank_flags = addr + offset >= sf->bank1_start ? FLASH_CR_BKER : 0;
		const uint32_t ctrl = FLASH_CR_PER | (page << FLASH_CR_PAGE_SHIFT) | bank_flags;
		/* Flash page erase instruction */
		stm32l4_flash_write32(t, FLASH_CR, ctrl);
		/* write address to FMA */
		stm32l4_flash_write32(t, FLASH_CR, ctrl | FLASH_CR_STRT);

		/* Wait for completion or an error */
		if (!stm32l4_flash_busy_wait(t, NULL))
			return false;
	}
	return true;
}

static bool stm32l4_flash_write(target_flash_s *f, target_addr_t dest, const void *src, size_t len)
{
	target *t = f->t;
	stm32l4_flash_write32(t, FLASH_CR, FLASH_CR_PG);
	target_mem_write(t, dest, src, len);

	/* Wait for completion or an error */
	return stm32l4_flash_busy_wait(t, NULL);
}

static bool stm32l4_cmd_erase(target *const t, const uint32_t action)
{
	stm32l4_flash_unlock(t);
	/* Erase time is 25 ms. Timeout logic shouldn't get fired.*/
	/* Flash erase action start instruction */
	stm32l4_flash_write32(t, FLASH_CR, action);
	stm32l4_flash_write32(t, FLASH_CR, action | FLASH_CR_STRT);

	platform_timeout timeout;
	platform_timeout_set(&timeout, 500);
	/* Wait for completion or an error */
	return stm32l4_flash_busy_wait(t, &timeout);
}

static bool stm32l4_mass_erase(target *const t)
{
	return stm32l4_cmd_erase(t, FLASH_CR_MER1 | FLASH_CR_MER2);
}

static bool stm32l4_cmd_erase_bank1(target *const t, const int argc, const char **const argv)
{
	(void)argc;
	(void)argv;
	gdb_out("Erasing bank 1: ");
	const bool result = stm32l4_cmd_erase(t, FLASH_CR_MER1);
	gdb_out("done\n");
	return result;
}

static bool stm32l4_cmd_erase_bank2(target *const t, const int argc, const char **const argv)
{
	(void)argc;
	(void)argv;
	gdb_out("Erasing bank 2: ");
	const bool result = stm32l4_cmd_erase(t, FLASH_CR_MER2);
	gdb_out("done\n");
	return result;
}

static bool stm32l4_option_write(target *const t, const uint32_t *const values, const size_t len,
	const uint32_t fpec_base, const uint8_t *const opt_reg_offsets)
{
	/* Unlock the option registers Flash */
	stm32l4_flash_unlock(t);
	stm32l4_flash_write32(t, FLASH_OPTKEYR, OPTKEY1);
	stm32l4_flash_write32(t, FLASH_OPTKEYR, OPTKEY2);
	/* Wait for the operation to complete and report any errors */
	if (!stm32l4_flash_busy_wait(t, NULL))
		return true;

	/* Write the new option register values and begin the programming operation */
	for (size_t i = 0; i < len; i++)
		target_mem_write32(t, fpec_base + opt_reg_offsets[i], values[i]);
	stm32l4_flash_write32(t, FLASH_CR, FLASH_CR_OPTSTRT);
	/* Wait for the operation to complete and report any errors */
	if (!stm32l4_flash_busy_wait(t, NULL))
		return false;

	tc_printf(t, "Scan and attach again\n");
	/* Ask the device to reload its options bytes */
	stm32l4_flash_write32(t, FLASH_CR, FLASH_CR_OBL_LAUNCH);
	while (stm32l4_flash_read32(t, FLASH_CR) & FLASH_CR_OBL_LAUNCH) {
		if (target_check_error(t))
			return true;
	}
	/* Re-lock Flash */
	stm32l4_flash_write32(t, FLASH_CR, FLASH_CR_LOCK);
	return false;
}

static uint32_t stm32l4_fpec_base_addr(const target *const t)
{
	if (t->part_id == ID_STM32WLXX)
		return STM32WL_FPEC_BASE;
	return STM32L4_FPEC_BASE;
}

static stm32l4_option_bytes_info_s stm32l4_get_opt_bytes_info(const uint16_t part_id)
{
	switch (part_id) {
	case ID_STM32L43:
		return (stm32l4_option_bytes_info_s){
			.word_count = 5,
			.offsets = stm32l4_opt_reg_offsets,
			.default_values = stm32l4_default_options_values,
		};
	case ID_STM32G47:
		return (stm32l4_option_bytes_info_s){
			.word_count = ARRAY_LENGTH(stm32g4_default_options_values),
			.offsets = stm32g4_opt_reg_offsets,
			.default_values = stm32g4_default_options_values,
		};
	case ID_STM32G43:
	case ID_STM32G49:
		return (stm32l4_option_bytes_info_s){
			.word_count = 6,
			.offsets = stm32g4_opt_reg_offsets,
			.default_values = stm32g4_default_options_values,
		};
	case ID_STM32WLXX:
		return (stm32l4_option_bytes_info_s){
			.word_count = ARRAY_LENGTH(stm32wl_default_options_values),
			.offsets = stm32wl_opt_reg_offsets,
			.default_values = stm32wl_default_options_values,
		};
	default:
		return (stm32l4_option_bytes_info_s){
			.word_count = ARRAY_LENGTH(stm32l4_default_options_values),
			.offsets = stm32l4_opt_reg_offsets,
			.default_values = stm32l4_default_options_values,
		};
	}
}

/*
 * Chip:      L43X/mask  L43x/def   L47x/mask  L47x/def   G47x/mask  G47x/def
 *                                  L49x/mask  L49x/def   G48x/mask  G48x/def
 * Address
 * 0x1fff7800 0x0f8f77ff 0xffeff8aa 0x0fdf77ff 0xffeff8aa 0x0fdf77ff 0xffeff8aa
 * 0x1fff7808 0x0000ffff 0xffffffff 0x0000ffff 0xffffffff 0x00007fff 0xffffffff
 * 0x1fff7810 0x8000ffff 0          0x8000ffff 0          0x80007fff 0x00ff0000
 * 0x1fff7818 0x00ff00ff 0x000000ff 0x00ff00ff 0x000000ff 0x007f007f 0xff00ffff
 * 0x1fff7820 0x00ff00ff 0x000000ff 0x00ff00ff 0x000000ff 0x007f007f 0xff00ffff
 * 0x1fff7828 0          0          0          0          0x000100ff 0xff00ff00
 * 0x1ffff808 0          0          0x8000ffff 0xffffffff 0x00007fff 0xffffffff
 * 0x1ffff810 0          0          0x8000ffff 0          0x00007fff 0xffffffff
 * 0x1ffff818 0          0          0x00ff00ff 0          0x00ff00ff 0xff00ffff
 * 0x1ffff820 0          0          0x00ff00ff 0x000000ff 0x00ff00ff 0xff00ffff
 * 0x1ffff828 0          0          0          0          0x000000ff 0xff00ff00
 */

static bool stm32l4_cmd_option(target *t, int argc, const char **argv)
{
	if (t->part_id == ID_STM32L55) {
		tc_printf(t, "STM32L5 options not implemented!\n");
		return false;
	}
	if (t->part_id == ID_STM32WBXX) {
		tc_printf(t, "STM32WBxx options not implemented!\n");
		return false;
	}
	if (t->part_id == ID_STM32WLXX) {
		tc_printf(t, "STM32WLxx options not implemented!\n");
		return false;
	}

	const stm32l4_option_bytes_info_s info = stm32l4_get_opt_bytes_info(t->part_id);
	const uint32_t fpec_base = stm32l4_fpec_base_addr(t);
	const uint8_t *const opt_reg_offsets = info.offsets;

	const size_t word_count = info.word_count;
	uint32_t values[11] = {};
	for (size_t i = 0; i < word_count; ++i)
		values[i] = info.default_values[i];

	bool result = false;
	if (argc == 2 && strcmp(argv[1], "erase") == 0)
		result = stm32l4_option_write(t, values, word_count, fpec_base, opt_reg_offsets);
	else if (argc > 2 && strcmp(argv[1], "write") == 0) {
		const size_t option_words = MIN((size_t)argc - 2U, word_count);
		for (size_t i = 0; i < option_words; ++i)
			values[i] = strtoul(argv[i + 2U], NULL, 0);

		for (size_t i = option_words; i < word_count; ++i)
			values[i] = target_mem_read32(t, fpec_base + opt_reg_offsets[i]);

		if ((values[0] & 0xffU) == 0xccU) {
			++values[0];
			tc_printf(t, "Changing level 2 protection request to level 1!");
		}
		result = stm32l4_option_write(t, values, word_count, fpec_base, opt_reg_offsets);
	} else {
		tc_printf(t, "usage: monitor option erase\n");
		tc_printf(t, "usage: monitor option write <value> ...\n");
	}

	if (result) {
		tc_printf(t, "Writing options failed!\n");
		return false;
	}

	for (size_t i = 0; i < word_count; ++i) {
		const uint32_t addr = fpec_base + opt_reg_offsets[i];
		const uint32_t val = target_mem_read32(t, fpec_base + opt_reg_offsets[i]);
		tc_printf(t, "0x%08X: 0x%08X\n", addr, val);
	}
	return true;
}
