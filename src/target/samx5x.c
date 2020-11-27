/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2019  Ken Healy
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

/* This file implements Microchip SAM D5x/E5x target specific functions
 * for detecting the device, providing the XML memory map and Flash
 * memory programming.
 *
 * Tested with
 * * SAMD51G19A (rev A)
 * * SAMD51J19A (rev A)
 * *
 */
/* Refer to the SAM D5x/E5x Datasheet:
 * http://ww1.microchip.com/downloads/en/DeviceDoc/60001507E.pdf
 * particularly Sections 12. DSU and 25. NVMCTRL
 */

#include "general.h"
#include <ctype.h>

#include "target.h"
#include "target_internal.h"
#include "cortexm.h"

static int samx5x_flash_erase(struct target_flash *t, target_addr addr,
			      size_t len);
static int samx5x_flash_write(struct target_flash *f,
			      target_addr dest, const void *src, size_t len);
static bool samx5x_cmd_lock_flash(target *t, int argc, const char **argv);
static bool samx5x_cmd_unlock_flash(target *t, int argc, const char **argv);
static bool samx5x_cmd_unlock_bootprot(target *t, int argc, const char **argv);
static bool samx5x_cmd_lock_bootprot(target *t, int argc, const char **argv);
static bool samx5x_cmd_read_userpage(target *t, int argc, const char **argv);
static bool samx5x_cmd_serial(target *t, int argc, const char **argv);
static bool samx5x_cmd_ssb(target *t, int argc, const char **argv);
static bool samx5x_cmd_update_user_word(target *t, int argc, const char **argv);

/* (The SAM D1x/2x implementation of erase_all is reused as it's identical)*/
extern bool samd_cmd_erase_all(target *t, int argc, const char **argv);
#define samx5x_cmd_erase_all samd_cmd_erase_all


#ifdef SAMX5X_EXTRA_CMDS
static bool samx5x_cmd_mbist(target *t, int argc, const char **argv);
static bool samx5x_cmd_write8(target *t, int argc, const char **argv);
static bool samx5x_cmd_write16(target *t, int argc, const char **argv);
static bool samx5x_cmd_write32(target *t, int argc, const char **argv);
#endif

const struct command_s samx5x_cmd_list[] = {
	{"erase_mass", (cmd_handler)samx5x_cmd_erase_all,
	 "Erase entire flash memory"},
	{"lock_flash", (cmd_handler)samx5x_cmd_lock_flash,
	 "Locks flash against spurious commands"},
	{"unlock_flash", (cmd_handler)samx5x_cmd_unlock_flash,
	 "Unlocks flash"},
	{"lock_bootprot", (cmd_handler)samx5x_cmd_lock_bootprot,
	 "Lock the boot protections to maximum"},
	{"unlock_bootprot", (cmd_handler)samx5x_cmd_unlock_bootprot,
	 "Unlock the boot protections to minimum"},
	{"user_page", (cmd_handler)samx5x_cmd_read_userpage,
	 "Prints user page from flash"},
	{"serial", (cmd_handler)samx5x_cmd_serial,
	 "Prints serial number"},
	{"set_security_bit", (cmd_handler)samx5x_cmd_ssb,
	 "Sets the security bit"},
	{"update_user_word", (cmd_handler)samx5x_cmd_update_user_word,
	 "Sets 32-bits in the user page: <addr> <value>"},
#ifdef SAMX5X_EXTRA_CMDS
	{"mbist", (cmd_handler)samx5x_cmd_mbist,
	 "Runs the built-in memory test"},
	{"write8", (cmd_handler)samx5x_cmd_write8,
	 "Writes an 8-bit word: write8 <addr> <value>"},
	{"write16", (cmd_handler)samx5x_cmd_write16,
	 "Writes a 16-bit word: write16 <addr> <value>"},
	{"write32", (cmd_handler)samx5x_cmd_write32,
	 "Writes a 32-bit word: write32 <addr> <value>"},
#endif
	{NULL, NULL, NULL}
};

const struct command_s samx5x_protected_cmd_list[] = {
	{"erase_mass", (cmd_handler)samx5x_cmd_erase_all,
	 "Erase entire flash memory"},
	{NULL, NULL, NULL}
};

/* RAM Parameters */
#define SAMX5X_RAM_START			0x20000000

/* Non-Volatile Memory Controller (NVMC) Parameters */
#define SAMX5X_PAGE_SIZE			512
#define SAMX5X_BLOCK_SIZE			(SAMX5X_PAGE_SIZE * 16)

/* -------------------------------------------------------------------------- */
/* Non-Volatile Memory Controller (NVMC) Registers */
/* -------------------------------------------------------------------------- */

#define SAMX5X_NVMC				0x41004000
#define SAMX5X_NVMC_CTRLA			(SAMX5X_NVMC + 0x0)
#define SAMX5X_NVMC_CTRLB			(SAMX5X_NVMC + 0x04)
#define SAMX5X_NVMC_PARAM			(SAMX5X_NVMC + 0x08)
#define SAMX5X_NVMC_INTFLAG			(SAMX5X_NVMC + 0x10)
#define SAMX5X_NVMC_STATUS			(SAMX5X_NVMC + 0x12)
#define SAMX5X_NVMC_ADDRESS			(SAMX5X_NVMC + 0x14)
#define SAMX5X_NVMC_RUNLOCK			(SAMX5X_NVMC + 0x18)

/* Control B Register (CTRLB) */
#define SAMX5X_CTRLB_CMD_KEY			0xA500
#define SAMX5X_CTRLB_CMD_ERASEPAGE		0x0000
#define SAMX5X_CTRLB_CMD_ERASEBLOCK		0x0001
#define SAMX5X_CTRLB_CMD_WRITEPAGE		0x0003
#define SAMX5X_CTRLB_CMD_WRITEQUADWORD		0x0004
#define SAMX5X_CTRLB_CMD_LOCK			0x0011
#define SAMX5X_CTRLB_CMD_UNLOCK		0x0012
#define SAMX5X_CTRLB_CMD_PAGEBUFFERCLEAR	0x0015
#define SAMX5X_CTRLB_CMD_SSB			0x0016

/* Interrupt Flag Register (INTFLAG) */
#define SAMX5X_INTFLAG_DONE			(1 << 0)
#define SAMX5X_INTFLAG_ADDRE			(1 << 1)
#define SAMX5X_INTFLAG_PROGE			(1 << 2)
#define SAMX5X_INTFLAG_LOCKE			(1 << 3)
#define SAMX5X_INTFLAG_ECCSE			(1 << 4)
#define SAMX5X_INTFLAG_ECCDE			(1 << 5)
#define SAMX5X_INTFLAG_NVME			(1 << 6)
#define SAMX5X_INTFLAG_SUSP			(1 << 7)
#define SAMX5X_INTFLAG_SEESFULL			(1 << 8)
#define SAMX5X_INTFLAG_SEESOVF			(1 << 9)

/* Status Register (STATUS) */
#define SAMX5X_STATUS_READY			(1 << 0)

/* Non-Volatile Memory Calibration and Auxiliary Registers */
#define SAMX5X_NVM_USER_PAGE			0x00804000
#define SAMX5X_NVM_CALIBRATION			0x00800000
#define SAMX5X_NVM_SERIAL(n)			(0x0080600C + \
						 (n == 0 ? 0x1F0 : n * 4))

#define SAMX5X_USER_PAGE_OFFSET_LOCK		0x08
#define SAMX5X_USER_PAGE_OFFSET_BOOTPROT	0x03
#define SAMX5X_USER_PAGE_MASK_BOOTPROT		0x3C
#define SAMX5X_USER_PAGE_SHIFT_BOOTPROT	2

/* -------------------------------------------------------------------------- */
/* Device Service Unit (DSU) Registers */
/* -------------------------------------------------------------------------- */

#define SAMX5X_DSU				0x41002000
#define SAMX5X_DSU_EXT_ACCESS			(SAMX5X_DSU + 0x100)
#define SAMX5X_DSU_CTRLSTAT			(SAMX5X_DSU_EXT_ACCESS + 0x00)
#define SAMX5X_DSU_ADDRESS			(SAMX5X_DSU_EXT_ACCESS + 0x04)
#define SAMX5X_DSU_LENGTH			(SAMX5X_DSU_EXT_ACCESS + 0x08)
#define SAMX5X_DSU_DATA				(SAMX5X_DSU_EXT_ACCESS + 0x0C)
#define SAMX5X_DSU_DID				(SAMX5X_DSU_EXT_ACCESS + 0x18)
#define SAMX5X_DSU_PID  			(SAMX5X_DSU + 0x1000)
#define SAMX5X_DSU_CID  			(SAMX5X_DSU + 0x1010)

/* Control and Status Register (CTRLSTAT) */
#define SAMX5X_CTRL_CHIP_ERASE			(1 << 4)
#define SAMX5X_CTRL_MBIST			(1 << 3)
#define SAMX5X_CTRL_CRC				(1 << 2)
#define SAMX5X_STATUSA_PERR			(1 << 12)
#define SAMX5X_STATUSA_FAIL			(1 << 11)
#define SAMX5X_STATUSA_BERR			(1 << 10)
#define SAMX5X_STATUSA_CRSTEXT			(1 << 9)
#define SAMX5X_STATUSA_DONE			(1 << 8)
#define SAMX5X_STATUSB_PROT			(1 << 16)


/* Device Identification Register (DID)

   Bits 31-17

   SAME54 0110 0001 1000 0100
   SAME53 0110 0001 1000 0011
   SAME51 0110 0001 1000 0001
   SAMD51 0110 0000 0000 0110

   Common
   mask   1111 1110 0111 1000

   Masked common
   value  0110 0000 0000 0000 == 0x6000
*/

#define SAMX5X_DID_MASK				0xFE780000
#define SAMX5X_DID_CONST_VALUE			0x60000000
#define SAMX5X_DID_DEVSEL_MASK			0xFF
#define SAMX5X_DID_DEVSEL_POS			0
#define SAMX5X_DID_REVISION_MASK		0x0F
#define SAMX5X_DID_REVISION_POS			8
#define SAMX5X_DID_SERIES_MASK			0x3F
#define SAMX5X_DID_SERIES_POS			16

/* Peripheral ID */
#define SAMX5X_PID_MASK				0x00F7FFFF
#define SAMX5X_PID_CONST_VALUE			0x0001FCD0

/* Component ID */
#define SAMX5X_CID_VALUE			0xB105100D

/**
 * Overloads the default cortexm reset function with a version that
 * removes the target from extended reset where required.
 *
 * (Reuses the SAM D1x/2x implementation as it is identical)
 */
extern void samd_reset(target *t);
#define samx5x_reset samd_reset

/**
 * Overload the default cortexm attach for when the samd is protected.
 *
 * If the samd is protected then the default cortexm attach will
 * fail as the S_HALT bit in the DHCSR will never go high. This
 * function allows users to attach on a temporary basis so they can
 * rescue the device.
 *
 * (Reuses the SAM D1x/2x implementation as it is identical)
 */
extern bool samd_protected_attach(target *t);
#define samx5x_protected_attach samd_protected_attach

/**
 * Use the DSU Device Indentification Register to populate a struct
 * describing the SAM D device.
 */
struct samx5x_descr {
	char series_letter;
	uint8_t series_number;
	char revision;
	char pin;
	uint8_t mem;
	char package[3];
};
struct samx5x_descr samx5x_parse_device_id(uint32_t did)
{
	struct samx5x_descr samd;
	memset(samd.package, 0, 3);

	uint8_t series = (did >> SAMX5X_DID_SERIES_POS)
		& SAMX5X_DID_SERIES_MASK;
	uint8_t revision = (did >> SAMX5X_DID_REVISION_POS)
		& SAMX5X_DID_REVISION_MASK;
	uint8_t devsel = (did >> SAMX5X_DID_DEVSEL_POS)
		& SAMX5X_DID_DEVSEL_MASK;

	/* Series */
	switch (series) {
	case 1:
		samd.series_letter = 'E';
		samd.series_number = 51;
		break;
	case 6:
		samd.series_letter = 'D';
		samd.series_number = 51;
		break;
	case 3:
		samd.series_letter = 'E';
		samd.series_number = 53;
		break;
	case 4:
		samd.series_letter = 'E';
		samd.series_number = 54;
		break;
	}
	/* Revision */
	samd.revision = 'A' + revision;

	switch(devsel) {
	case 0:
		samd.pin = 'P';
		samd.mem = 20;
		break;
	case 1:
		samd.pin = 'P';
		samd.mem = 19;
		break;
	case 2:
		samd.pin = 'N';
		samd.mem = 20;
		break;
	case 3:
		samd.pin = 'N';
		samd.mem = 19;
		break;
	case 4:
		samd.pin = 'J';
		samd.mem = 20;
		break;
	case 5:
		samd.pin = 'J';
		samd.mem = 19;
		break;
	case 6:
		samd.pin = 'J';
		samd.mem = 18;
		break;
	case 7:
		samd.pin = 'G';
		samd.mem = 19;
		break;
	case 8:
		samd.pin = 'G';
		samd.mem = 18;
		break;
	}

	return samd;
}

static void samx5x_add_flash(target *t, uint32_t addr, size_t length,
			     size_t erase_block_size, size_t write_page_size)
{
	struct target_flash *f = calloc(1, sizeof(*f));
	if (!f) {			/* calloc failed: heap exhaustion */
		DEBUG_INFO("calloc: failed in %s\n", __func__);
		return;
	}

	f->start = addr;
	f->length = length;
	f->blocksize = erase_block_size;
	f->erase = samx5x_flash_erase;
	f->write = samx5x_flash_write;
	f->buf_size = write_page_size;
	target_add_flash(t, f);
}

static char samx5x_variant_string[60];
bool samx5x_probe(target *t)
{
	ADIv5_AP_t *ap = cortexm_ap(t);
	uint32_t cid = adiv5_ap_read_pidr(ap, SAMX5X_DSU_CID);
	uint32_t pid = adiv5_ap_read_pidr(ap, SAMX5X_DSU_PID);

	/* Check the ARM Coresight Component and Perhiperal IDs */
	if ((cid != SAMX5X_CID_VALUE) ||
	    ((pid & SAMX5X_PID_MASK) != SAMX5X_PID_CONST_VALUE))
		return false;

	/* Read the Device ID */
	uint32_t did = target_mem_read32(t, SAMX5X_DSU_DID);

	/* If the Device ID matches */
	if ((did & SAMX5X_DID_MASK) != SAMX5X_DID_CONST_VALUE)
		return false;

	uint32_t ctrlstat = target_mem_read32(t, SAMX5X_DSU_CTRLSTAT);
	struct samx5x_descr samx5x = samx5x_parse_device_id(did);

	/* Protected? */
	bool protected = (ctrlstat & SAMX5X_STATUSB_PROT);

	/* Part String */
	if (protected) {
		snprintf(samx5x_variant_string, sizeof(samx5x_variant_string),
			 "Microchip SAM%c%d%c%dA (rev %c) (PROT=1)",
			 samx5x.series_letter, samx5x.series_number,
			 samx5x.pin, samx5x.mem, samx5x.revision);
	} else {
		snprintf(samx5x_variant_string, sizeof(samx5x_variant_string),
			 "Microchip SAM%c%d%c%dA (rev %c)",
			 samx5x.series_letter, samx5x.series_number,
			 samx5x.pin, samx5x.mem, samx5x.revision);
	}

	/* Setup Target */
	t->driver = samx5x_variant_string;
	t->reset = samx5x_reset;

	if (protected) {
		/**
		 * Overload the default cortexm attach
		 * for when the samx5x is protected.
		 * This function allows users to
		 * attach on a temporary basis so they
		 * can rescue the device.
		 */
		t->attach = samx5x_protected_attach;
	}
	switch(samx5x.mem) {
	default:
	case 18:
		target_add_ram(t, 0x20000000, 0x20000);
		samx5x_add_flash(t, 0x00000000, 0x40000,
				 SAMX5X_BLOCK_SIZE,
				 SAMX5X_PAGE_SIZE);
		break;
	case 19:
		target_add_ram(t, 0x20000000, 0x30000);
		samx5x_add_flash(t, 0x00000000, 0x80000,
				 SAMX5X_BLOCK_SIZE,
				 SAMX5X_PAGE_SIZE);
		break;
	case 20:
		target_add_ram(t, 0x20000000, 0x40000);
		samx5x_add_flash(t, 0x00000000, 0x100000,
				 SAMX5X_BLOCK_SIZE,
				 SAMX5X_PAGE_SIZE);
		break;
	}

	if (protected)
		target_add_commands(t, samx5x_protected_cmd_list,
				    "SAMD5x/E5x (protected)");
	else
		target_add_commands(t, samx5x_cmd_list, "SAMD5x/E5x");

	/* If we're not in reset here */
	if (!platform_srst_get_val()) {
		/* We'll have to release the target from
		 * extended reset to make attach possible */
		if (target_mem_read32(t, SAMX5X_DSU_CTRLSTAT) &
		    SAMX5X_STATUSA_CRSTEXT) {

			/* Write bit to clear from extended reset */
			target_mem_write32(t, SAMX5X_DSU_CTRLSTAT,
					   SAMX5X_STATUSA_CRSTEXT);
		}
	}

	return true;
}

/**
 * Temporary (until next reset) flash memory locking / unlocking
 */
static void samx5x_lock_current_address(target *t)
{
	/* Issue the unlock command */
	target_mem_write32(t, SAMX5X_NVMC_CTRLB,
			   SAMX5X_CTRLB_CMD_KEY | SAMX5X_CTRLB_CMD_LOCK);
}
static void samx5x_unlock_current_address(target *t)
{
	/* Issue the unlock command */
	target_mem_write32(t, SAMX5X_NVMC_CTRLB,
			   SAMX5X_CTRLB_CMD_KEY | SAMX5X_CTRLB_CMD_UNLOCK);
}

/**
 * Check for NVM errors and print debug messages
 */
static void samx5x_print_nvm_error(uint16_t errs)
{
	if (errs & SAMX5X_INTFLAG_ADDRE) {
		DEBUG_WARN(" ADDRE");
	}
	if (errs & SAMX5X_INTFLAG_PROGE) {
		DEBUG_WARN(" PROGE");
	}
	if (errs & SAMX5X_INTFLAG_LOCKE) {
		DEBUG_WARN(" LOCKE");
	}
	if (errs & SAMX5X_INTFLAG_NVME) {
		DEBUG_WARN(" NVME");
	}

	DEBUG_WARN("\n");
}

static int samx5x_read_nvm_error(target *t)
{
	uint16_t intflag = target_mem_read16(t, SAMX5X_NVMC_INTFLAG);

	return intflag & (SAMX5X_INTFLAG_ADDRE | SAMX5X_INTFLAG_PROGE |
			  SAMX5X_INTFLAG_LOCKE | SAMX5X_INTFLAG_NVME);
}

static void samx5x_clear_nvm_error(target *t)
{
	target_mem_write16(t, SAMX5X_NVMC_INTFLAG,
			   SAMX5X_INTFLAG_ADDRE | SAMX5X_INTFLAG_PROGE |
			   SAMX5X_INTFLAG_LOCKE | SAMX5X_INTFLAG_NVME);
}

static int samx5x_check_nvm_error(target *t)
{
	uint16_t errs = samx5x_read_nvm_error(t);

	if (!errs)
		return 0;

	DEBUG_WARN("NVM error(s) detected:");
	samx5x_print_nvm_error(errs);
	return -1;
}

#define NVM_ERROR_BITS_MSG						\
	"Warning: Found NVM error bits set while preparing to %s\n"	\
	"         flash block at 0x%08"PRIx32" (length 0x%zx).\n"	\
	"         Clearing these before proceeding:\n"			\
	"             "

/**
 * Erase flash block by block
 */
static int samx5x_flash_erase(struct target_flash *f, target_addr addr,
			      size_t len)
{
	target *t = f->t;
	uint16_t errs = samx5x_read_nvm_error(t);
	if (errs) {
		DEBUG_INFO(NVM_ERROR_BITS_MSG, "erase", addr, len);
		samx5x_print_nvm_error(errs);
		samx5x_clear_nvm_error(t);
	}

	/* Check if the bootprot or region lock settings
	 * are going to prevent erasing flash. */
	uint32_t bootprot, runlock, flash_size, lock_region_size;
	bootprot = (target_mem_read16(t, SAMX5X_NVMC_STATUS) >> 8) & 0xf;
	runlock = target_mem_read32(t, SAMX5X_NVMC_RUNLOCK);
	flash_size = (target_mem_read32(t, SAMX5X_NVMC_PARAM) & 0xffff) *
		SAMX5X_PAGE_SIZE;
	lock_region_size = flash_size >> 5;

	if (addr < (15 - bootprot) * 8192)
		return -1;

	if (~runlock & (1 << addr / lock_region_size))
		return -1;

	while (len) {
		target_mem_write32(t, SAMX5X_NVMC_ADDRESS, addr);

		/* Unlock */
		samx5x_unlock_current_address(t);

		/* Issue the erase command */
		target_mem_write32(t, SAMX5X_NVMC_CTRLB,
				   SAMX5X_CTRLB_CMD_KEY |
				   SAMX5X_CTRLB_CMD_ERASEBLOCK);

		/* Poll for NVM Ready */
		while ((target_mem_read32(t, SAMX5X_NVMC_STATUS) &
			SAMX5X_STATUS_READY) == 0)
			if (target_check_error(t) || samx5x_check_nvm_error(t))
				return -1;

		if (target_check_error(t) || samx5x_check_nvm_error(t))
			return -1;

		/* Lock */
		samx5x_lock_current_address(t);

		addr += f->blocksize;
		len -= f->blocksize;
	}

	return 0;
}

/**
 * Write flash page by page
 */
static int samx5x_flash_write(struct target_flash *f,
			      target_addr dest, const void *src, size_t len)
{
	target *t = f->t;
	bool error = false;
	uint16_t errs = samx5x_read_nvm_error(t);
	if (errs) {
		DEBUG_INFO(NVM_ERROR_BITS_MSG, "write", dest, len);
		samx5x_print_nvm_error(errs);
		samx5x_clear_nvm_error(t);
	}

	/* Unlock */
	target_mem_write32(t, SAMX5X_NVMC_ADDRESS, dest);
	samx5x_unlock_current_address(t);

	/* Write within a single page. This may be part or all of the page */
	target_mem_write(t, dest, src, len);

	/* Issue the write page command */
	target_mem_write32(t, SAMX5X_NVMC_CTRLB,
			   SAMX5X_CTRLB_CMD_KEY | SAMX5X_CTRLB_CMD_WRITEPAGE);

	/* Poll for NVM Ready */
	while ((target_mem_read32(t, SAMX5X_NVMC_STATUS) &
		SAMX5X_STATUS_READY) == 0)
		if (target_check_error(t) || samx5x_check_nvm_error(t)) {
			error = true;
			break;
		}

	if (error || target_check_error(t) || samx5x_check_nvm_error(t)) {
		DEBUG_WARN("Error writing flash page at 0x%08"PRIx32
		      " (len 0x%08zx)\n",  dest, len);
		return -1;
	}

	/* Lock */
	samx5x_lock_current_address(t);

	return 0;
}

/**
 * Erase and write the NVM user page
 */
static int samx5x_write_user_page(target *t, uint8_t *buffer)
{
	uint16_t errs = samx5x_read_nvm_error(t);
	if (errs) {
		DEBUG_INFO(NVM_ERROR_BITS_MSG, "erase and write",
				   (uint32_t)SAMX5X_NVM_USER_PAGE,
				   (size_t)SAMX5X_PAGE_SIZE);
		samx5x_print_nvm_error(errs);
		samx5x_clear_nvm_error(t);
	}

	/* Erase the user page */
	target_mem_write32(t, SAMX5X_NVMC_ADDRESS, SAMX5X_NVM_USER_PAGE);
	/* Issue the erase command */
	target_mem_write32(t, SAMX5X_NVMC_CTRLB,
			   SAMX5X_CTRLB_CMD_KEY | SAMX5X_CTRLB_CMD_ERASEPAGE);

	/* Poll for NVM Ready */
	while ((target_mem_read32(t, SAMX5X_NVMC_STATUS) &
		SAMX5X_STATUS_READY) == 0)
		if (target_check_error(t) || samx5x_check_nvm_error(t))
			return -1;

	/* Write back */
	for (uint32_t offset = 0; offset < SAMX5X_PAGE_SIZE; offset += 16) {
		target_mem_write(t, SAMX5X_NVM_USER_PAGE + offset,
				 buffer + offset, 16);

		/* Issue the write page command */
		target_mem_write32(t, SAMX5X_NVMC_CTRLB,
				   SAMX5X_CTRLB_CMD_KEY |
				   SAMX5X_CTRLB_CMD_WRITEQUADWORD);

		/* Poll for NVM Ready */
		while ((target_mem_read32(t, SAMX5X_NVMC_STATUS) &
			SAMX5X_STATUS_READY) == 0)
			if (target_check_error(t) ||
			    samx5x_check_nvm_error(t))
				return -2;
	}
	return 0;
}

static int samx5x_update_user_word(target *t, uint32_t addr, uint32_t value,
				   uint32_t *value_written, bool force)
{
	uint8_t factory_bits[] = {
		/* 0     8    16    24    32    40    48    56 */
		0x00, 0x80, 0xFF, 0xC3, 0x00, 0xFF, 0x00, 0x80,

		/*64    72    80    88    96   104   112   120 */
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

		/*128  136   144   152 */
		0xFF, 0xFF, 0xFF, 0xFF };

	uint8_t buffer[SAMX5X_PAGE_SIZE];
	uint32_t current_word, new_word;

	target_mem_read(t, buffer, SAMX5X_NVM_USER_PAGE, SAMX5X_PAGE_SIZE);
	memcpy(&current_word, buffer + addr, 4);

	uint32_t factory_word = 0;

	for (int i = 0; !force && i < 4 && addr + i < 20; i++)
		factory_word |= (uint32_t)factory_bits[addr + i] << (i * 8);

	new_word = current_word & factory_word;
	new_word |= value & ~factory_word;
	if (value_written != NULL)
		*value_written = new_word;

	if (new_word != current_word) {
		DEBUG_INFO("Writing user page word 0x%08"PRIx32
				   " at offset 0x%03"PRIx32"\n", new_word, addr);
		memcpy(buffer + addr, &new_word, 4);
		return samx5x_write_user_page(t, buffer);
	}
	else {
		DEBUG_INFO("Skipping user page write as no change would be made");
	}

	return 0;
}

/**
 * Sets the NVM region lock bits in the User Page. This value is read
 * at startup as the default value for the lock bits, and hence does
 * not take effect until a reset.
 *
 * 0x00000000 = Lock, 0xFFFFFFFF = Unlock (default)
 */
static int samx5x_set_flashlock(target *t, uint32_t value)
{
	uint8_t buffer[SAMX5X_PAGE_SIZE];
	target_mem_read(t, buffer, SAMX5X_NVM_USER_PAGE, SAMX5X_PAGE_SIZE);

	uint32_t current_value;
	memcpy(&current_value, buffer + SAMX5X_USER_PAGE_OFFSET_LOCK, 4);

	if (value != current_value)
		return samx5x_update_user_word(t, SAMX5X_USER_PAGE_OFFSET_LOCK,
					       value, NULL, false);

	return 0;
}

static bool samx5x_cmd_lock_flash(target *t, int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	if (samx5x_set_flashlock(t, 0x00000000)) {
		tc_printf(t, "Error writing NVM page\n");
		return false;
	}
	tc_printf(t, "Flash locked. The target must be reset for "
		  "this to take effect.\n");
	return true;
}

static bool samx5x_cmd_unlock_flash(target *t, int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	if (samx5x_set_flashlock(t, 0xFFFFFFFF)) {
		tc_printf(t, "Error writing NVM page\n");
		return false;
	}
	tc_printf(t, "Flash unlocked. The target must be reset for "
		  "this to take effect.\n");
	return true;
}

/**
 * Sets the BOOTPROT bits in the User Page. This value is read at
 * startup as the default value for BOOTPROT, and hence does not
 * take effect until a reset.
 *
 * Size of protected region at beginning of flash:
 *     (15 - BOOTPROT) * 8192
 */
static int samx5x_set_bootprot(target *t, uint8_t value)
{
	uint8_t buffer[SAMX5X_PAGE_SIZE];
	target_mem_read(t, buffer, SAMX5X_NVM_USER_PAGE, SAMX5X_PAGE_SIZE);

	uint32_t current_value, new_value;
	memcpy(&current_value, buffer + SAMX5X_USER_PAGE_OFFSET_BOOTPROT, 4);

	new_value = current_value & ~SAMX5X_USER_PAGE_MASK_BOOTPROT;
	new_value |= (value << SAMX5X_USER_PAGE_SHIFT_BOOTPROT) &
		SAMX5X_USER_PAGE_MASK_BOOTPROT;

	if (new_value != current_value)
		return samx5x_update_user_word(t,
					       SAMX5X_USER_PAGE_OFFSET_BOOTPROT,
					       new_value, NULL, false);

	return 0;
}

static bool samx5x_cmd_lock_bootprot(target *t, int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	if (samx5x_set_bootprot(t, 0)) {
		tc_printf(t, "Error writing NVM page\n");
		return false;
	}
	tc_printf(t, "Bootprot locked. The target must be reset for "
		  "this to take effect.\n");
	return true;
}

static bool samx5x_cmd_unlock_bootprot(target *t, int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	if (samx5x_set_bootprot(t, 0xf)) {
		tc_printf(t, "Error writing NVM page\n");
		return false;
	}
	tc_printf(t, "Bootprot unlocked. The target must be reset for "
		  "this to take effect.\n");
	return true;
}

static bool samx5x_cmd_read_userpage(target *t, int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	uint8_t buffer[SAMX5X_PAGE_SIZE];
	int i = 0;

	target_mem_read(t, buffer, SAMX5X_NVM_USER_PAGE, SAMX5X_PAGE_SIZE);

	tc_printf(t, "User Page:\n");
	while (i < SAMX5X_PAGE_SIZE) {
		tc_printf(t, "%02x%c", buffer[i],
			  (i + 1) % 16 == 0 ? '\n' : ' ');
		i++;
	}
	return true;
}

/**
 * Reads the 128-bit serial number from the NVM
 */
static bool samx5x_cmd_serial(target *t, int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	tc_printf(t, "Serial Number: 0x");

	for (uint32_t i = 0; i < 4; i++) {
		tc_printf(t, "%08x",
			  target_mem_read32(t, SAMX5X_NVM_SERIAL(i)));
	}

	tc_printf(t, "\n");

	return true;
}

/**
 * Sets the security bit
 */
static bool samx5x_cmd_ssb(target *t, int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	/* Issue the ssb command */
	target_mem_write32(t, SAMX5X_NVMC_CTRLB,
			   SAMX5X_CTRLB_CMD_KEY | SAMX5X_CTRLB_CMD_SSB);

	/* Poll for NVM Ready */
	while ((target_mem_read32(t, SAMX5X_NVMC_STATUS) &
		SAMX5X_STATUS_READY) == 0)
		if (target_check_error(t))
			return -1;

	tc_printf(t, "Set the security bit! "
		  "You will need to issue 'monitor erase_mass' "
		  "to clear this.\n");

	return true;
}

#define FACTORY_BITS_MSG						\
	"Warning: the value provided would have modified factory\n"	\
	"         setting bits that should not be changed. The\n"	\
	"         actual value written was: 0x%08"PRIx32"\n"		\
	"To override this protection to write the factory setting\n"	\
	"bits, use: update_user_word <addr> <value> force\n"

/**
 * Updates a 32-bit word in the NVM user page. Factory setting bits are
 * not modified unless the "force" argument is provided.
 */
static bool samx5x_cmd_update_user_word(target *t, int argc, const char **argv)
{
	if (argc < 3 || argc > 4) {
		tc_printf(t, "Error: incorrect number of arguments\n");
		return false;
	}

	char *addr_end, *value_end;
	uint32_t addr = strtoul(argv[1], &addr_end, 0);
	uint32_t value = strtoul(argv[2], &value_end, 0);
	uint32_t actual_value;

	if (addr_end == argv[1] || (!addr && *(addr_end - 1) != '0') ||
	    value_end == argv[2] || (!value && *(value_end - 1) != '0') ||
	    (argc == 4 && strcmp(argv[3], "force"))) {
		tc_printf(t, "Error: unrecognized arguments\n");
		return false;
	}

	if (addr > 0x1fc) {
		tc_printf(t, "Error: address out of range. "
			  "User page is 512 bytes.\n");
		return false;
	}

	if (samx5x_update_user_word(t, addr, value, &actual_value, argc == 4)) {
		tc_printf(t, "Error updating NVM page\n");
		return false;
	}

	if (argc != 4 && value != actual_value)
		tc_printf(t, FACTORY_BITS_MSG, actual_value);

	tc_printf(t, "User page updated.");
	if (addr < 12)
		tc_printf(t,
			  " The target must be reset for the new config "
			  "settings\n"
			  "(bootprot, wdt, etc.) to take effect.");
	tc_printf(t, "\n");

	return true;
}

#ifdef SAMX5X_EXTRA_CMDS

/**
 * Returns the size (in bytes) of the RAM.
 */
static uint32_t samx5x_ram_size(target *t)
{
	/* Read the Device ID */
	uint32_t did = target_mem_read32(t, SAMX5X_DSU_DID);

	/* Mask off the device select bits */
	struct samx5x_descr samx5x = samx5x_parse_device_id(did);

	/* Adjust the maximum ram size (256KB) down as appropriate */
	return (0x40000 - 0x10000 * (20 - samx5x.mem));
}

/**
 * Runs the Memory Built In Self Test (MBIST)
 */
static bool samx5x_cmd_mbist(target *t, int argc, const char **argv)
{
	(void)argc;
	(void)argv;

	DEBUG_INFO("Running MBIST for memory range 0x%08x-%08"PRIx32"\n",
			   SAMX5X_RAM_START, samx5x_ram_size(t));

	/* Write the memory parameters to the DSU
	 * Note that the two least significant bits of the address are
	 * the access mode, so the actual starting address should be
	 * left shifted by 2
	 *
	 * Similarly, the length must also be left shifted by 2 as the
	 * two least significant bits of that register are unused */
	target_mem_write32(t, SAMX5X_DSU_ADDRESS, SAMX5X_RAM_START);
	target_mem_write32(t, SAMX5X_DSU_LENGTH, samx5x_ram_size(t) << 2);

	/* Clear the fail and protection error bits */
	target_mem_write32(t, SAMX5X_DSU_CTRLSTAT, SAMX5X_STATUSA_FAIL |
			   SAMX5X_STATUSA_PERR);

	/* Write the MBIST command */
	target_mem_write32(t, SAMX5X_DSU_CTRLSTAT, SAMX5X_CTRL_MBIST);

	/* Poll for DSU Ready */
	uint32_t status;
	while (((status = target_mem_read32(t, SAMX5X_DSU_CTRLSTAT)) &
		(SAMX5X_STATUSA_DONE | SAMX5X_STATUSA_PERR |
		 SAMX5X_STATUSA_FAIL)) == 0)
		if (target_check_error(t))
			return false;

	/* Test the protection error bit in Status A */
	if (status & SAMX5X_STATUSA_PERR) {
		tc_printf(t, "MBIST not run due to protection error.\n");
		return true;
	}

	/* Test the fail bit in Status A */
	if (status & SAMX5X_STATUSA_FAIL) {
		uint32_t data = target_mem_read32(t, SAMX5X_DSU_DATA);
		tc_printf(t, "MBIST Fail @ 0x%08x (bit %d in phase %d)\n",
			  target_mem_read32(t, SAMX5X_DSU_ADDRESS),
			  data & 0x1f, data >> 8);
	} else {
		tc_printf(t, "MBIST Passed!\n");
	}

	return true;
}

/**
 * Writes an 8-bit word to the specified address
 */
static bool samx5x_cmd_write8(target *t, int argc, const char **argv)
{
	if (argc != 3) {
		tc_printf(t, "Error: incorrect number of arguments\n");
		return false;
	}

	char *addr_end, *value_end;
	uint32_t addr = strtoul(argv[1], &addr_end, 0);
	uint32_t value = strtoul(argv[2], &value_end, 0);

	if (addr_end == argv[1] || (!addr && *(addr_end - 1) != '0') ||
	    value_end == argv[2] || (!value && *(value_end - 1) != '0')) {
		tc_printf(t, "Error: unrecognized arguments\n");
		return false;
	}

	if (value > 0xff) {
		tc_printf(t, "Error: value out of range\n");
		return false;
	}

	DEBUG_INFO("Writing 8-bit value 0x%02"PRIx32" at address 0x%08"PRIx32"\n",
			   value, addr);
	target_mem_write8(t, addr, (uint8_t)value);

	return true;
}

/**
 * Writes a 16-bit word to the specified address
 */
static bool samx5x_cmd_write16(target *t, int argc, const char **argv)
{
	if (argc != 3) {
		tc_printf(t, "Error: incorrect number of arguments\n");
		return false;
	}

	char *addr_end, *value_end;
	uint32_t addr = strtoul(argv[1], &addr_end, 0);
	uint32_t value = strtoul(argv[2], &value_end, 0);

	if (addr_end == argv[1] || (!addr && *(addr_end - 1) != '0') ||
	    value_end == argv[2] || (!value && *(value_end - 1) != '0')) {
		tc_printf(t, "Error: unrecognized arguments\n");
		return false;
	}

	if (value > 0xffff) {
		tc_printf(t, "Error: value out of range\n");
		return false;
	}

	DEBUG_INFO("Writing 16-bit value 0x%04"PRIx32" at address 0x%08"PRIx32"\n",
			   value, addr);
	target_mem_write16(t, addr, (uint16_t)value);

	return true;
}

/**
 * Writes a 32-bit word to the specified address
 */
static bool samx5x_cmd_write32(target *t, int argc, const char **argv)
{
	if (argc != 3) {
		tc_printf(t, "Error: incorrect number of arguments\n");
		return false;
	}

	char *addr_end, *value_end;
	uint32_t addr = strtoul(argv[1], &addr_end, 0);
	uint32_t value = strtoul(argv[2], &value_end, 0);

	if (addr_end == argv[1] || (!addr && *(addr_end - 1) != '0') ||
	    value_end == argv[2] || (!value && *(value_end - 1) != '0')) {
		tc_printf(t, "Error: unrecognized arguments\n");
		return false;
	}

	DEBUG_INFO("Writing 32-bit value 0x%08"PRIx32" at address 0x%08"PRIx32"\n",
			   value, addr);
	target_mem_write32(t, addr, value);

	return true;
}

#endif
