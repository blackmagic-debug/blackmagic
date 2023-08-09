/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2014  Richard Meadows <richardeoin>
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
 * This file implements Atmel SAM D target specific functions for
 * detecting the device, providing the XML memory map and Flash memory
 * programming.
 *
 * Tested with
 * * SAMD09D14A (rev B)
 * * SAMD20E17A (rev C)
 * * SAMD20J18A (rev B)
 * * SAMD21J18A (rev B)
 * * SAML21J17B (rev B)
 */

/*
 * Refer to the SAM D20 Datasheet:
 * http://www.atmel.com/Images/Atmel-42129-SAM-D20_Datasheet.pdf
 * particularly sections 12. DSU and 20. NVMCTRL
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"

static bool samd_flash_erase(target_flash_s *f, target_addr_t addr, size_t len);
static bool samd_flash_write(target_flash_s *f, target_addr_t dest, const void *src, size_t len);
/* NB: This is not marked static on purpose as it's used by samx5x.c. */
bool samd_mass_erase(target_s *t);

static bool samd_cmd_lock_flash(target_s *t, int argc, const char **argv);
static bool samd_cmd_unlock_flash(target_s *t, int argc, const char **argv);
static bool samd_cmd_unlock_bootprot(target_s *t, int argc, const char **argv);
static bool samd_cmd_lock_bootprot(target_s *t, int argc, const char **argv);
static bool samd_cmd_read_userrow(target_s *t, int argc, const char **argv);
static bool samd_cmd_serial(target_s *t, int argc, const char **argv);
static bool samd_cmd_mbist(target_s *t, int argc, const char **argv);
static bool samd_cmd_ssb(target_s *t, int argc, const char **argv);

const command_s samd_cmd_list[] = {
	{"lock_flash", samd_cmd_lock_flash, "Locks flash against spurious commands"},
	{"unlock_flash", samd_cmd_unlock_flash, "Unlocks flash"},
	{"lock_bootprot", samd_cmd_lock_bootprot, "Lock the boot protections to maximum"},
	{"unlock_bootprot", samd_cmd_unlock_bootprot, "Unlock the boot protections to minimum"},
	{"user_row", samd_cmd_read_userrow, "Prints user row from flash"},
	{"serial", samd_cmd_serial, "Prints serial number"},
	{"mbist", samd_cmd_mbist, "Runs the built-in memory test"},
	{"set_security_bit", samd_cmd_ssb, "Sets the Security Bit"},
	{NULL, NULL, NULL},
};

/* Non-Volatile Memory Controller (NVMC) Parameters */
#define SAMD_ROW_SIZE  256U
#define SAMD_PAGE_SIZE 64U

/* -------------------------------------------------------------------------- */
/* Non-Volatile Memory Controller (NVMC) Registers */
/* -------------------------------------------------------------------------- */

#define SAMD_NVMC         0x41004000U
#define SAMD_NVMC_CTRLA   (SAMD_NVMC + 0x00U)
#define SAMD_NVMC_CTRLB   (SAMD_NVMC + 0x04U)
#define SAMD_NVMC_PARAM   (SAMD_NVMC + 0x08U)
#define SAMD_NVMC_INTFLAG (SAMD_NVMC + 0x14U)
#define SAMD_NVMC_STATUS  (SAMD_NVMC + 0x18U)
#define SAMD_NVMC_ADDRESS (SAMD_NVMC + 0x1cU)

/* Control A Register (CTRLA) */
#define SAMD_CTRLA_CMD_KEY             0xa500U
#define SAMD_CTRLA_CMD_ERASEROW        0x0002U
#define SAMD_CTRLA_CMD_WRITEPAGE       0x0004U
#define SAMD_CTRLA_CMD_ERASEAUXROW     0x0005U
#define SAMD_CTRLA_CMD_WRITEAUXPAGE    0x0006U
#define SAMD_CTRLA_CMD_LOCK            0x0040U
#define SAMD_CTRLA_CMD_UNLOCK          0x0041U
#define SAMD_CTRLA_CMD_PAGEBUFFERCLEAR 0x0044U
#define SAMD_CTRLA_CMD_SSB             0x0045U
#define SAMD_CTRLA_CMD_INVALL          0x0046U

/* Interrupt Flag Register (INTFLAG) */
#define SAMD_NVMC_READY (1U << 0U)

/* Non-Volatile Memory Calibration and Auxiliary Registers */
#define SAMD_NVM_USER_ROW_LOW  0x00804000U
#define SAMD_NVM_USER_ROW_HIGH 0x00804004U
#define SAMD_NVM_CALIBRATION   0x00806020U
#define SAMD_NVM_SERIAL(n)     (0x0080a00cU + (0x30U * (((n) + 3U) / 4U)) + ((n)*4U))

/* -------------------------------------------------------------------------- */
/* Device Service Unit (DSU) Registers */
/* -------------------------------------------------------------------------- */

#define SAMD_DSU            0x41002000U
#define SAMD_DSU_EXT_ACCESS (SAMD_DSU + 0x100U)
#define SAMD_DSU_CTRLSTAT   (SAMD_DSU_EXT_ACCESS + 0x0U)
#define SAMD_DSU_ADDRESS    (SAMD_DSU_EXT_ACCESS + 0x4U)
#define SAMD_DSU_LENGTH     (SAMD_DSU_EXT_ACCESS + 0x8U)
#define SAMD_DSU_DID        (SAMD_DSU_EXT_ACCESS + 0x018U)
#define SAMD_DSU_PID        (SAMD_DSU + 0x1000U)
#define SAMD_DSU_CID        (SAMD_DSU + 0x1010U)

/* Control and Status Register (CTRLSTAT) */
#define SAMD_CTRL_CHIP_ERASE (1U << 4U)
#define SAMD_CTRL_MBIST      (1U << 3U)
#define SAMD_CTRL_CRC        (1U << 2U)
#define SAMD_STATUSA_PERR    (1U << 12U)
#define SAMD_STATUSA_FAIL    (1U << 11U)
#define SAMD_STATUSA_BERR    (1U << 10U)
#define SAMD_STATUSA_CRSTEXT (1U << 9U)
#define SAMD_STATUSA_DONE    (1U << 8U)
#define SAMD_STATUSB_PROT    (1U << 16U)

/* Device Identification Register (DID) */
#define SAMD_DID_MASK          0xff380000U
#define SAMD_DID_CONST_VALUE   0x10000000U
#define SAMD_DID_DEVSEL_MASK   0xffU
#define SAMD_DID_DEVSEL_POS    0U
#define SAMD_DID_REVISION_MASK 0x0fU
#define SAMD_DID_REVISION_POS  8U
#define SAMD_DID_SERIES_MASK   0x1fU
#define SAMD_DID_SERIES_POS    16U
#define SAMD_DID_FAMILY_MASK   0x3fU
#define SAMD_DID_FAMILY_POS    23U

/* Peripheral ID */
#define SAMD_PID_MASK        0x00f7ffffU
#define SAMD_PID_CONST_VALUE 0x0001fcd0U

/* Component ID */
#define SAMD_CID_VALUE 0xb105100dU

/* Family parts */
typedef struct samd_part {
	uint8_t devsel;
	char pin;
	uint8_t mem;
	uint8_t variant;
} samd_part_s;

static const samd_part_s samd_d21_parts[] = {
	{0x00, 'J', 18, 'A'}, /* SAMD21J18A */
	{0x01, 'J', 17, 'A'}, /* SAMD21J17A */
	{0x02, 'J', 16, 'A'}, /* SAMD21J16A */
	{0x03, 'J', 15, 'A'}, /* SAMD21J15A */
	{0x05, 'G', 18, 'A'}, /* SAMD21G18A */
	{0x06, 'G', 17, 'A'}, /* SAMD21G17A */
	{0x07, 'G', 16, 'A'}, /* SAMD21G16A */
	{0x08, 'G', 15, 'A'}, /* SAMD21G15A */
	{0x0a, 'E', 18, 'A'}, /* SAMD21E18A */
	{0x0b, 'E', 17, 'A'}, /* SAMD21E17A */
	{0x0c, 'E', 16, 'A'}, /* SAMD21E16A */
	{0x0d, 'E', 15, 'A'}, /* SAMD21E15A */
	{0x0f, 'G', 18, 'A'}, /* SAMD21G18A (WLCSP) */
	{0x10, 'G', 17, 'A'}, /* SAMD21G17A (WLCSP) */
	{0x20, 'J', 16, 'B'}, /* SAMD21J16B */
	{0x21, 'J', 15, 'B'}, /* SAMD21J15B */
	{0x23, 'G', 16, 'B'}, /* SAMD21G16B */
	{0x24, 'G', 15, 'B'}, /* SAMD21G15B */
	{0x26, 'E', 16, 'B'}, /* SAMD21E16B */
	{0x27, 'E', 15, 'B'}, /* SAMD21E15B */
	{0x55, 'E', 16, 'B'}, /* SAMD21E16B (WLCSP) */
	{0x56, 'E', 15, 'B'}, /* SAMD21E15B (WLCSP) */
	{0x62, 'E', 16, 'C'}, /* SAMD21E16C (WLCSP) */
	{0x63, 'E', 15, 'C'}, /* SAMD21E15C (WLCSP) */
	{0xff, 0, 0, 0},      /* Sentinel entry */
};

static const samd_part_s samd_l21_parts[] = {
	{0x00, 'J', 18, 'A'}, /* SAML21J18A */
	{0x01, 'J', 17, 'A'}, /* SAML21J17A */
	{0x02, 'J', 16, 'A'}, /* SAML21J16A */
	{0x05, 'G', 18, 'A'}, /* SAML21G18A */
	{0x06, 'G', 17, 'A'}, /* SAML21G17A */
	{0x07, 'G', 16, 'A'}, /* SAML21G16A */
	{0x0a, 'E', 18, 'A'}, /* SAML21E18A */
	{0x0b, 'E', 17, 'A'}, /* SAML21E17A */
	{0x0c, 'E', 16, 'A'}, /* SAML21E16A */
	{0x0d, 'E', 15, 'A'}, /* SAML21E15A */
	{0x0f, 'J', 18, 'B'}, /* SAML21J18B */
	{0x10, 'J', 17, 'B'}, /* SAML21J17B */
	{0x11, 'J', 16, 'B'}, /* SAML21J16B */
	{0x14, 'G', 18, 'B'}, /* SAML21G18B */
	{0x15, 'G', 17, 'B'}, /* SAML21G17B */
	{0x16, 'G', 16, 'B'}, /* SAML21G16B */
	{0x19, 'E', 18, 'B'}, /* SAML21E18B */
	{0x1a, 'E', 17, 'B'}, /* SAML21E17B */
	{0x1b, 'E', 16, 'B'}, /* SAML21E16B */
	{0x1c, 'E', 15, 'B'}, /* SAML21E15B */
	{0xff, 0, 0, 0},      /* Sentinel entry */
};

static const samd_part_s samd_l22_parts[] = {
	{0x00, 'N', 18, 'A'}, /* SAML22N18 */
	{0x01, 'N', 17, 'A'}, /* SAML22N17 */
	{0x02, 'N', 16, 'A'}, /* SAML22N16 */
	{0x05, 'J', 18, 'A'}, /* SAML22J18 */
	{0x06, 'J', 17, 'A'}, /* SAML22J17 */
	{0x07, 'J', 16, 'A'}, /* SAML22J16 */
	{0x0a, 'G', 18, 'A'}, /* SAML22G18 */
	{0x0b, 'G', 17, 'A'}, /* SAML22G17 */
	{0x0c, 'G', 16, 'A'}, /* SAML22G16 */
	{0xff, 0, 0, 0},      /* Sentinel entry */
};

/*
 * Overloads the default cortexm reset function with a version that
 * removes the target from extended reset where required.
 */
void samd_reset(target_s *t)
{
	/*
	 * nRST is not asserted here as it appears to reset the adiv5
	 * logic, meaning that subsequent adiv5_* calls PLATFORM_FATAL_ERROR.
	 *
	 * This is ok as normally you can just connect the debugger and go,
	 * but if that's not possible (protection or SWCLK being used for
	 * something else) then having SWCLK low on reset should get you
	 * debug access (cold-plugging). TODO: Confirm this
	 *
	 * See the SAM D20 datasheet §12.6 Debug Operation for more details.
	 *
	 * jtagtap_nrst(true);
	 * jtagtap_nrst(false);
	 *
	 * XXX: Should this actually call cortexm_reset()?
	 */

	/* Read DHCSR here to clear S_RESET_ST bit before reset */
	target_mem_read32(t, CORTEXM_DHCSR);

	/*
	 * Request System Reset from NVIC: nRST doesn't work correctly
	 * This could be VECTRESET: 0x05fa0001 (reset only core)
	 *          or SYSRESETREQ: 0x05fa0004 (system reset)
	 */
	target_mem_write32(t, CORTEXM_AIRCR, CORTEXM_AIRCR_VECTKEY | CORTEXM_AIRCR_SYSRESETREQ);

	/* Exit extended reset */
	if (target_mem_read32(t, SAMD_DSU_CTRLSTAT) & SAMD_STATUSA_CRSTEXT)
		/* Write bit to clear from extended reset */
		target_mem_write32(t, SAMD_DSU_CTRLSTAT, SAMD_STATUSA_CRSTEXT);

	/* Poll for release from reset */
	while (target_mem_read32(t, CORTEXM_DHCSR) & CORTEXM_DHCSR_S_RESET_ST)
		continue;

	/* Reset DFSR flags */
	target_mem_write32(t, CORTEXM_DFSR, CORTEXM_DFSR_RESETALL);

	/* Clear any target errors */
	target_check_error(t);
}

/*
 * Overloads the default cortexm detached function with a version that
 * removes the target from extended reset where required.
 *
 * Only required for SAM D20 _Revision B_ Silicon
 */
static void samd20_revB_detach(target_s *t)
{
	cortexm_detach(t);

	/* Exit extended reset */
	if (target_mem_read32(t, SAMD_DSU_CTRLSTAT) & SAMD_STATUSA_CRSTEXT)
		/* Write bit to clear from extended reset */
		target_mem_write32(t, SAMD_DSU_CTRLSTAT, SAMD_STATUSA_CRSTEXT);
}

/*
 * Overloads the default cortexm halt_resume function with a version
 * that removes the target from extended reset where required.
 *
 * Only required for SAM D20 _Revision B_ Silicon
 */
static void samd20_revB_halt_resume(target_s *t, bool step)
{
	cortexm_halt_resume(t, step);

	/* Exit extended reset */
	if (target_mem_read32(t, SAMD_DSU_CTRLSTAT) & SAMD_STATUSA_CRSTEXT)
		/* Write bit to clear from extended reset */
		target_mem_write32(t, SAMD_DSU_CTRLSTAT, SAMD_STATUSA_CRSTEXT);
}

/*
 * Release the target from extended reset before running the normal cortexm_attach routine.
 * This prevents tripping up over errata ref 9905
 *
 * Only required for SAM D11 silicon.
 */
static bool samd11_attach(target_s *t)
{
	/* Exit extended reset */
	if (target_mem_read32(t, SAMD_DSU_CTRLSTAT) & SAMD_STATUSA_CRSTEXT)
		/* Write bit to clear from extended reset */
		target_mem_write32(t, SAMD_DSU_CTRLSTAT, SAMD_STATUSA_CRSTEXT);

	return cortexm_attach(t);
}

/*
 * Overload the default cortexm attach for when the samd is protected.
 *
 * If the samd is protected then the default cortexm attach will
 * fail as the S_HALT bit in the DHCSR will never go high. This
 * function allows users to attach on a temporary basis so they can
 * rescue the device.
 */
bool samd_protected_attach(target_s *t)
{
	tc_printf(t, "Attached in protected mode, please issue 'monitor erase_mass' to regain chip access\n");
	/* Patch back in the normal cortexm attach for next time */
	t->attach = cortexm_attach;

	/* Allow attach this time */
	return true;
}

/*
 * Use the DSU Device Identification Register to populate a struct
 * describing the SAM D device.
 */
typedef struct samd_descr {
	char family;
	uint8_t series;
	char revision;
	char pin;
	uint32_t ram_size;
	uint32_t flash_size;
	uint8_t mem;
	char variant;
	char package[3];
} samd_descr_s;

samd_descr_s samd_parse_device_id(uint32_t did)
{
	samd_descr_s samd = {0};
	const samd_part_s *parts = samd_d21_parts;
	samd.ram_size = 0x8000;
	samd.flash_size = 0x40000;

	/* Family */
	const uint8_t family = (did >> SAMD_DID_FAMILY_POS) & SAMD_DID_FAMILY_MASK;
	switch (family) {
	case 0:
		samd.family = 'D';
		break;
	case 1:
		samd.family = 'L';
		parts = samd_l21_parts;
		break;
	case 2:
		samd.family = 'C';
		break;
	}
	/* Series */
	const uint8_t series = (did >> SAMD_DID_SERIES_POS) & SAMD_DID_SERIES_MASK;
	switch (series) {
	case 0:
		samd.series = 20;
		break;
	case 1:
		samd.series = 21;
		break;
	case 2:
		if (family == 1) {
			samd.series = 22;
			parts = samd_l22_parts;
		} else
			samd.series = 10;
		break;
	case 3:
		samd.series = 11;
		break;
	case 4:
		samd.series = 9;
		break;
	default:
		samd.series = 0;
		break;
	}
	/* Revision */
	const uint8_t revision = (did >> SAMD_DID_REVISION_POS) & SAMD_DID_REVISION_MASK;
	samd.revision = (char)('A' + revision);

	const uint8_t devsel = (did >> SAMD_DID_DEVSEL_POS) & SAMD_DID_DEVSEL_MASK;
	switch (samd.series) {
	case 20U: /* SAM D20 */
		switch (devsel / 5U) {
		case 0U:
			samd.pin = 'J';
			break;
		case 1U:
			samd.pin = 'G';
			break;
		case 2U:
			samd.pin = 'E';
			break;
		default:
			samd.pin = 'u';
			break;
		}
		samd.mem = 18U - (devsel % 5U);
		samd.variant = 'A';
		break;
	case 21U: /* SAM D21/L21 */
	case 22U: /* SAM L22 */
		for (size_t i = 0; parts[i].devsel != 0xffU; ++i) {
			if (parts[i].devsel == devsel) {
				samd.pin = parts[i].pin;
				samd.mem = parts[i].mem;
				samd.variant = parts[i].variant;
				break;
			}
		}
		break;
	case 10U: /* SAM D10 */
	case 11U: /* SAM D11 */
		switch (devsel / 3U) {
		case 0U:
			samd.package[0] = 'M';
			break;
		case 1U:
			samd.package[0] = 'S';
			samd.package[1] = 'S';
			break;
		}
		samd.pin = 'D';
		samd.mem = 14U - (devsel % 3U);
		samd.variant = 'A';
		break;
	case 9U: /* SAM D09 */
		samd.ram_size = 4096;
		switch (devsel) {
		case 0U:
			samd.pin = 'D';
			samd.mem = 14;
			samd.flash_size = 16384;
			samd.package[0] = 'M';
			break;
		case 7U:
			samd.pin = 'C';
			samd.mem = 13;
			samd.flash_size = 8192;
			break;
		}
		samd.variant = 'A';
		break;
	}

	return samd;
}

static void samd_add_flash(target_s *t, uint32_t addr, size_t length)
{
	target_flash_s *f = calloc(1, sizeof(*f));
	if (!f) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return;
	}

	f->start = addr;
	f->length = length;
	f->blocksize = SAMD_ROW_SIZE;
	f->erase = samd_flash_erase;
	f->write = samd_flash_write;
	f->writesize = SAMD_PAGE_SIZE;
	target_add_flash(t, f);
}

#define SAMD_VARIANT_STR_LENGTH 60U

typedef struct samd_priv {
	char samd_variant_string[SAMD_VARIANT_STR_LENGTH];
} samd_priv_s;

bool samd_probe(target_s *t)
{
	adiv5_access_port_s *ap = cortex_ap(t);
	const uint32_t cid = adiv5_ap_read_pidr(ap, SAMD_DSU_CID);
	const uint32_t pid = adiv5_ap_read_pidr(ap, SAMD_DSU_PID);

	/* Check the ARM Coresight Component and Peripheral IDs */
	if (cid != SAMD_CID_VALUE || (pid & SAMD_PID_MASK) != SAMD_PID_CONST_VALUE)
		return false;

	/* Read the Device ID */
	const uint32_t did = target_mem_read32(t, SAMD_DSU_DID);

	/* If the Device ID matches */
	if ((did & SAMD_DID_MASK) != SAMD_DID_CONST_VALUE)
		return false;

	samd_priv_s *priv_storage = calloc(1, sizeof(*priv_storage));
	t->target_storage = priv_storage;

	const uint32_t ctrlstat = target_mem_read32(t, SAMD_DSU_CTRLSTAT);
	const samd_descr_s samd = samd_parse_device_id(did);

	/* Protected? */
	const bool protected = (ctrlstat & SAMD_STATUSB_PROT);

	snprintf(priv_storage->samd_variant_string, SAMD_VARIANT_STR_LENGTH, "Atmel SAM%c%02d%c%d%c%s (rev %c)%s",
		samd.family, samd.series, samd.pin, samd.mem, samd.variant, samd.package, samd.revision,
		protected ? " protected" : "");

	/* Setup Target */
	t->driver = priv_storage->samd_variant_string;
	t->reset = samd_reset;
	t->mass_erase = samd_mass_erase;

	if (samd.series == 20 && samd.revision == 'B') {
		/*
		 * These functions check for an extended reset.
		 * Appears to be related to Errata 35.4.1 ref 12015
		 */
		t->detach = samd20_revB_detach;
		t->halt_resume = samd20_revB_halt_resume;
	} else if (samd.series == 11) {
		/*
		 * Attach routine that checks for an extended reset and releases it.
		 * This works around Errata 38.2.5 ref 9905
		 */
		t->attach = samd11_attach;
	}

	if (protected) {
		/*
		 * Overload the default cortexm attach
		 * for when the samd is protected.
		 * This function allows users to
		 * attach on a temporary basis so they
		 * can rescue the device.
		 */
		t->attach = samd_protected_attach;
	}

	target_add_ram(t, 0x20000000, samd.ram_size);
	samd_add_flash(t, 0x00000000, samd.flash_size);
	target_add_commands(t, samd_cmd_list, "SAMD");

	/* If we're not in reset here */
	if (!platform_nrst_get_val()) {
		/* We'll have to release the target from
		 * extended reset to make attach possible */
		if (target_mem_read32(t, SAMD_DSU_CTRLSTAT) & SAMD_STATUSA_CRSTEXT)
			/* Write bit to clear from extended reset */
			target_mem_write32(t, SAMD_DSU_CTRLSTAT, SAMD_STATUSA_CRSTEXT);
	}

	return true;
}

/* Temporary (until next reset) flash memory locking */
static void samd_lock_current_address(target_s *t)
{
	/* Issue the lock command */
	target_mem_write32(t, SAMD_NVMC_CTRLA, SAMD_CTRLA_CMD_KEY | SAMD_CTRLA_CMD_LOCK);
}

/* Temporary (until next reset) flash memory unlocking */
static void samd_unlock_current_address(target_s *t)
{
	/* Issue the unlock command */
	target_mem_write32(t, SAMD_NVMC_CTRLA, SAMD_CTRLA_CMD_KEY | SAMD_CTRLA_CMD_UNLOCK);
}

static bool samd_wait_nvm_ready(target_s *t)
{
	/* Poll for NVM Ready */
	while ((target_mem_read32(t, SAMD_NVMC_INTFLAG) & SAMD_NVMC_READY) == 0) {
		if (target_check_error(t))
			return false;
	}
	return true;
}

static bool samd_wait_dsu_ready(target_s *const t, uint32_t *const result, platform_timeout_s *const timeout)
{
	uint32_t status = 0;
	while ((status & (SAMD_STATUSA_DONE | SAMD_STATUSA_PERR | SAMD_STATUSA_FAIL)) == 0) {
		status = target_mem_read32(t, SAMD_DSU_CTRLSTAT);
		if (target_check_error(t))
			return false;
		if (timeout)
			target_print_progress(timeout);
	}
	*result = status;
	return true;
}

/* Erase flash row by row */
static bool samd_flash_erase(target_flash_s *const f, const target_addr_t addr, const size_t len)
{
	target_s *t = f->t;
	for (size_t offset = 0; offset < len; offset += f->blocksize) {
		/*
		 * Write address of first word in row to erase it
		 * Must be shifted right for 16-bit address, see Datasheet §20.8.8 Address
		 */
		target_mem_write32(t, SAMD_NVMC_ADDRESS, (addr + offset) >> 1U);

		/* Unlock */
		samd_unlock_current_address(t);

		/* Issue the erase command */
		target_mem_write32(t, SAMD_NVMC_CTRLA, SAMD_CTRLA_CMD_KEY | SAMD_CTRLA_CMD_ERASEROW);
		if (!samd_wait_nvm_ready(t))
			return false;

		/* Lock */
		samd_lock_current_address(t);
	}

	return true;
}

/*
 * Write flash page by page
 */
static bool samd_flash_write(target_flash_s *f, target_addr_t dest, const void *src, size_t len)
{
	target_s *t = f->t;

	/* Write within a single page. This may be part or all of the page */
	target_mem_write(t, dest, src, len);

	/* Unlock */
	samd_unlock_current_address(t);

	/* Issue the write page command */
	target_mem_write32(t, SAMD_NVMC_CTRLA, SAMD_CTRLA_CMD_KEY | SAMD_CTRLA_CMD_WRITEPAGE);
	if (!samd_wait_nvm_ready(t))
		return false;

	/* Lock */
	samd_lock_current_address(t);

	return true;
}

/* Uses the Device Service Unit to erase the entire flash */
bool samd_mass_erase(target_s *t)
{
	/* Clear the DSU status bits */
	target_mem_write32(t, SAMD_DSU_CTRLSTAT, SAMD_STATUSA_DONE | SAMD_STATUSA_PERR | SAMD_STATUSA_FAIL);

	/* Erase all */
	target_mem_write32(t, SAMD_DSU_CTRLSTAT, SAMD_CTRL_CHIP_ERASE);

	uint32_t status = 0;
	platform_timeout_s timeout;
	platform_timeout_set(&timeout, 500);
	if (!samd_wait_dsu_ready(t, &status, &timeout))
		return false;

	/* Test the protection error bit in Status A */
	if (status & SAMD_STATUSA_PERR) {
		tc_printf(t, "Erase failed due to a protection error.\n");
		return true;
	}

	/* Test the fail bit in Status A */
	return !(status & SAMD_STATUSA_FAIL);
}

/*
 * Sets the NVM region lock bits in the User Row. This value is read
 * at startup as the default value for the lock bits, and hence does
 * not take effect until a reset.
 *
 * 0x0000 = Lock, 0xffff = Unlock (default)
 */
static bool samd_set_flashlock(target_s *t, uint16_t value, const char **argv)
{
	(void)argv;
	uint32_t high = target_mem_read32(t, SAMD_NVM_USER_ROW_HIGH);
	uint32_t low = target_mem_read32(t, SAMD_NVM_USER_ROW_LOW);

	/* Write address of a word in the row to erase it */
	/* Must be shifted right for 16-bit address, see Datasheet §20.8.8 Address */
	target_mem_write32(t, SAMD_NVMC_ADDRESS, SAMD_NVM_USER_ROW_LOW >> 1U);

	/* Issue the erase command */
	target_mem_write32(t, SAMD_NVMC_CTRLA, SAMD_CTRLA_CMD_KEY | SAMD_CTRLA_CMD_ERASEAUXROW);
	if (!samd_wait_nvm_ready(t))
		return false;

	/* Modify the high byte of the user row */
	high = (high & 0x0000ffffU) | ((value << 16U) & 0xffff0000U);

	/* Write back */
	target_mem_write32(t, SAMD_NVM_USER_ROW_LOW, low);
	target_mem_write32(t, SAMD_NVM_USER_ROW_HIGH, high);

	/* Issue the page write command */
	target_mem_write32(t, SAMD_NVMC_CTRLA, SAMD_CTRLA_CMD_KEY | SAMD_CTRLA_CMD_WRITEAUXPAGE);

	return true;
}

static bool parse_unsigned(const char *str, uint32_t *val)
{
	int result;
	unsigned long num;

	size_t len = strlen(str);
	// TODO: port to use substrate::toInt_t<> style parser for robustness and smaller code size
	if (len > 2U && str[0] == '0' && (str[1] == 'x' || str[1] == 'X'))
		result = sscanf(str + 2, "%lx", &num);
	else
		result = sscanf(str, "%lu", &num);

	if (result < 1)
		return false;

	*val = (uint32_t)num;
	return true;
}

static bool samd_cmd_lock_flash(target_s *t, int argc, const char **argv)
{
	if (argc > 2) {
		tc_printf(t, "usage: monitor lock_flash [number]\n");
		return false;
	}
	if (argc == 2) {
		uint32_t val = 0;
		if (!parse_unsigned(argv[1], &val)) {
			tc_printf(t, "number must be either decimal or 0x prefixed hexadecimal\n");
			return false;
		}

		if (val > 0xffffU) {
			tc_printf(t, "number must be between 0 and 65535\n");
			return false;
		}

		return samd_set_flashlock(t, (uint16_t)val, NULL);
	}
	return samd_set_flashlock(t, 0x0000, NULL);
}

static bool samd_cmd_unlock_flash(target_s *t, int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	return samd_set_flashlock(t, 0xffff, NULL);
}

static bool samd_set_bootprot(target_s *t, uint16_t value, const char **argv)
{
	(void)argv;
	const uint32_t high = target_mem_read32(t, SAMD_NVM_USER_ROW_HIGH);
	uint32_t low = target_mem_read32(t, SAMD_NVM_USER_ROW_LOW);

	/*
	 * Write address of a word in the row to erase it
	 * Must be shifted right for 16-bit address, see Datasheet §20.8.8 Address
	 */
	target_mem_write32(t, SAMD_NVMC_ADDRESS, SAMD_NVM_USER_ROW_LOW >> 1U);

	/* Issue the erase command */
	target_mem_write32(t, SAMD_NVMC_CTRLA, SAMD_CTRLA_CMD_KEY | SAMD_CTRLA_CMD_ERASEAUXROW);
	if (!samd_wait_nvm_ready(t))
		return false;

	/* Modify the low word of the user row */
	low = (low & 0xfffffff8U) | ((value << 0U) & 0x00000007U);

	/* Write back */
	target_mem_write32(t, SAMD_NVM_USER_ROW_LOW, low);
	target_mem_write32(t, SAMD_NVM_USER_ROW_HIGH, high);

	/* Issue the page write command */
	target_mem_write32(t, SAMD_NVMC_CTRLA, SAMD_CTRLA_CMD_KEY | SAMD_CTRLA_CMD_WRITEAUXPAGE);
	return true;
}

static bool samd_cmd_lock_bootprot(target_s *t, int argc, const char **argv)
{
	/* Locks first 0x7 .. 0, 0x6 .. 512, 0x5 .. 1024, ..., 0x0 .. 32768 bytes of flash*/
	if (argc > 2) {
		tc_printf(t, "usage: monitor lock_bootprot [number]\n");
		return false;
	}
	if (argc == 2) {
		uint32_t val = 0;
		if (!parse_unsigned(argv[1], &val)) {
			tc_printf(t, "number must be either decimal or 0x prefixed hexadecimal\n");
			return false;
		}

		if (val > 7U) {
			tc_printf(t, "number must be between 0 and 7\n");
			return false;
		}

		return samd_set_bootprot(t, (uint16_t)val, NULL);
	}
	return samd_set_bootprot(t, 0, NULL);
}

static bool samd_cmd_unlock_bootprot(target_s *t, int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	return samd_set_bootprot(t, 7, NULL);
}

static bool samd_cmd_read_userrow(target_s *t, int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	tc_printf(t, "User Row: 0x%08" PRIx32 "%08" PRIx32 "\n", target_mem_read32(t, SAMD_NVM_USER_ROW_HIGH),
		target_mem_read32(t, SAMD_NVM_USER_ROW_LOW));

	return true;
}

/* Reads the 128-bit serial number from the NVM */
static bool samd_cmd_serial(target_s *t, int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	tc_printf(t, "Serial Number: 0x");

	for (size_t i = 0; i < 4U; ++i)
		tc_printf(t, "%08x", target_mem_read32(t, SAMD_NVM_SERIAL(i)));
	tc_printf(t, "\n");
	return true;
}

/* Returns the size (in bytes) of the current SAM D20's flash memory. */
static uint32_t samd_flash_size(target_s *t)
{
	/* Read the Device ID */
	const uint32_t did = target_mem_read32(t, SAMD_DSU_DID);
	/* Mask off the device select bits */
	const uint8_t devsel = did & SAMD_DID_DEVSEL_MASK;
	/* Shift the maximum flash size (256KB) down as appropriate */
	return (0x40000U >> (devsel % 5U));
}

/* Runs the Memory Built In Self Test (MBIST) */
static bool samd_cmd_mbist(target_s *t, int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	/* Write the memory parameters to the DSU */
	target_mem_write32(t, SAMD_DSU_ADDRESS, 0);
	target_mem_write32(t, SAMD_DSU_LENGTH, samd_flash_size(t));

	/* Clear the fail bit */
	target_mem_write32(t, SAMD_DSU_CTRLSTAT, SAMD_STATUSA_FAIL);

	/* Write the MBIST command */
	target_mem_write32(t, SAMD_DSU_CTRLSTAT, SAMD_CTRL_MBIST);

	uint32_t status = 0;
	if (!samd_wait_dsu_ready(t, &status, NULL))
		return false;

	/* Test the protection error bit in Status A */
	if (status & SAMD_STATUSA_PERR) {
		tc_printf(t, "MBIST not run due to protection error.\n");
		return true;
	}

	/* Test the fail bit in Status A */
	if (status & SAMD_STATUSA_FAIL)
		tc_printf(t, "MBIST Fail @ 0x%08" PRIx32 "\n", target_mem_read32(t, SAMD_DSU_ADDRESS));
	else
		tc_printf(t, "MBIST Passed!\n");
	return true;
}

/*
 * Sets the security bit
 */
static bool samd_cmd_ssb(target_s *t, int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	/* Issue the ssb command */
	target_mem_write32(t, SAMD_NVMC_CTRLA, SAMD_CTRLA_CMD_KEY | SAMD_CTRLA_CMD_SSB);
	if (!samd_wait_nvm_ready(t))
		return false;

	tc_printf(t, "Security bit set!\nScan again, attach and issue 'monitor erase_mass' to reset.\n");

	target_reset(t);
	return true;
}
