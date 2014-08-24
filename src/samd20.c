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

/* This file implements Atmel SAM D20 target specific functions for
 * detecting the device, providing the XML memory map and Flash memory
 * programming.
 */
/* Refer to the SAM D20 Datasheet:
 * http://www.atmel.com/Images/Atmel-42129-SAM-D20_Datasheet.pdf
 * particularly Sections 12. DSU and 20. NVMCTRL
 */
/* TODO: Support for the NVMCTRL Security Bit. If this is set then the
 * device will probably not even be detected.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "general.h"
#include "jtagtap.h"
#include "adiv5.h"
#include "target.h"
#include "command.h"
#include "gdb_packet.h"

static int samd20_flash_erase(struct target_s *target, uint32_t addr, int len);
static int samd20_flash_write(struct target_s *target, uint32_t dest,
			const uint8_t *src, int len);

static bool samd20_cmd_erase_all(target *t);
static bool samd20_cmd_lock_flash(target *t);
static bool samd20_cmd_unlock_flash(target *t);
static bool samd20_cmd_read_userrow(target *t);
static bool samd20_cmd_serial(target *t);
static bool samd20_cmd_mbist(target *t);

const struct command_s samd20_cmd_list[] = {
	{"erase_mass", (cmd_handler)samd20_cmd_erase_all, "Erase entire flash memory"},
	{"lock_flash", (cmd_handler)samd20_cmd_lock_flash, "Locks flash against spurious commands"},
	{"unlock_flash", (cmd_handler)samd20_cmd_unlock_flash, "Unlocks flash"},
	{"user_row", (cmd_handler)samd20_cmd_read_userrow, "Prints user row from flash"},
	{"serial", (cmd_handler)samd20_cmd_serial, "Prints serial number"},
	{"mbist", (cmd_handler)samd20_cmd_mbist, "Runs the built-in memory test"},
	{NULL, NULL, NULL}
};

/**
 * 256KB Flash Max., 32KB RAM Max. The smallest unit of erase is the
 * one row = 256 bytes.
 */
static const char samd20_xml_memory_map[] = "<?xml version=\"1.0\"?>"
/*	"<!DOCTYPE memory-map "
	"             PUBLIC \"+//IDN gnu.org//DTD GDB Memory Map V1.0//EN\""
	"                    \"http://sourceware.org/gdb/gdb-memory-map.dtd\">"*/
	"<memory-map>"
	"  <memory type=\"flash\" start=\"0x0\" length=\"0x40000\">"
	"    <property name=\"blocksize\">0x100</property>"
	"  </memory>"
	"  <memory type=\"ram\" start=\"0x20000000\" length=\"0x8000\"/>"
	"</memory-map>";

/* Non-Volatile Memory Controller (NVMC) Parameters */
#define SAMD20_ROW_SIZE			256
#define SAMD20_PAGE_SIZE		64

/* Non-Volatile Memory Controller (NVMC) Registers */
#define SAMD20_NVMC			0x41004000
#define SAMD20_NVMC_CMD			(SAMD20_NVMC + 0x0)
#define SAMD20_NVMC_PARAM		(SAMD20_NVMC + 0x08)
#define SAMD20_NVMC_INTFLAG		(SAMD20_NVMC + 0x14)
#define SAMD20_NVMC_STATUS		(SAMD20_NVMC + 0x18)
#define SAMD20_NVMC_ADDRESS		(SAMD20_NVMC + 0x1C)

/* Command Register (CMD) */
#define SAMD20_CMD_KEY			0xA500
#define SAMD20_CMD_ERASEROW		0x0002
#define SAMD20_CMD_WRITEPAGE		0x0004
#define SAMD20_CMD_ERASEAUXROW		0x0005
#define SAMD20_CMD_WRITEAUXPAGE		0x0006
#define SAMD20_CMD_LOCK			0x0040
#define SAMD20_CMD_UNLOCK		0x0041
#define SAMD20_CMD_PAGEBUFFERCLEAR	0x0044

/* Interrupt Flag Register (INTFLAG) */
#define SAMD20_NVMC_READY		(1 << 0)

/* Non-Volatile Memory Calibration and Auxiliary Registers */
#define SAMD20_NVM_USER_ROW_LOW		0x00804000
#define SAMD20_NVM_USER_ROW_HIGH	0x00804004
#define SAMD20_NVM_CALIBRATION		0x00806020
#define SAMD20_NVM_SERIAL(n)		(0x0080A00C + (0x30 * ((n + 3) / 4)) + \
					 (0x4 * n))

/* Device Service Unit (DSU) Registers */
#define SAMD20_DSU			0x41002000
#define SAMD20_DSU_EXT_ACCESS		(SAMD20_DSU + 0x100)
#define SAMD20_DSU_CTRLSTAT		(SAMD20_DSU_EXT_ACCESS + 0x0)
#define SAMD20_DSU_ADDRESS		(SAMD20_DSU_EXT_ACCESS + 0x4)
#define SAMD20_DSU_LENGTH		(SAMD20_DSU_EXT_ACCESS + 0x8)
#define SAMD20_DSU_DID			(SAMD20_DSU_EXT_ACCESS + 0x018)
#define SAMD20_DSU_PID(n)		(SAMD20_DSU + 0x1FE0 + \
					 (0x4 * (n % 4)) - (0x10 * (n / 4)))
#define SAMD20_DSU_CID(n)		(SAMD20_DSU + 0x1FF0 + \
					 (0x4 * (n % 4)))

/* Control and Status Register (CTRLSTAT) */
#define SAMD20_CTRL_CHIP_ERASE		(1 << 4)
#define SAMD20_CTRL_MBIST		(1 << 3)
#define SAMD20_CTRL_CRC			(1 << 2)
#define SAMD20_STATUSA_PERR		(1 << 12)
#define SAMD20_STATUSA_FAIL		(1 << 11)
#define SAMD20_STATUSA_BERR		(1 << 10)
#define SAMD20_STATUSA_CRSTEXT		(1 << 9)
#define SAMD20_STATUSA_DONE		(1 << 8)

/* Device Identification Register (DID) */
#define SAMD20_DID_MASK			0xFFBF0000
#define SAMD20_DID_CONST_VALUE		0x10000000
#define SAMD20_DID_DEVSEL_MASK		0x0F
#define SAMD20_DID_DEVSEL_POS		0
#define SAMD20_DID_REVISION_MASK	0x0F
#define SAMD20_DID_REVISION_POS		8

/* Peripheral ID */
#define SAMD20_PID_MASK			0x00F7FFFF
#define SAMD20_PID_CONST_VALUE		0x0001FCD0

/* Component ID */
#define SAMD20_CID_VALUE		0xB105100D

#define CORTEXM_PPB_BASE	0xE0000000

#define CORTEXM_SCS_BASE	(CORTEXM_PPB_BASE + 0xE000)

#define CORTEXM_AIRCR		(CORTEXM_SCS_BASE + 0xD0C)
#define CORTEXM_CFSR		(CORTEXM_SCS_BASE + 0xD28)
#define CORTEXM_HFSR		(CORTEXM_SCS_BASE + 0xD2C)
#define CORTEXM_DFSR		(CORTEXM_SCS_BASE + 0xD30)
#define CORTEXM_CPACR		(CORTEXM_SCS_BASE + 0xD88)
#define CORTEXM_DHCSR		(CORTEXM_SCS_BASE + 0xDF0)
#define CORTEXM_DCRSR		(CORTEXM_SCS_BASE + 0xDF4)
#define CORTEXM_DCRDR		(CORTEXM_SCS_BASE + 0xDF8)
#define CORTEXM_DEMCR		(CORTEXM_SCS_BASE + 0xDFC)

/* Application Interrupt and Reset Control Register (AIRCR) */
#define CORTEXM_AIRCR_VECTKEY		(0x05FA << 16)
/* Bits 31:16 - Read as VECTKETSTAT, 0xFA05 */
#define CORTEXM_AIRCR_ENDIANESS		(1 << 15)
/* Bits 15:11 - Unused, reserved */
#define CORTEXM_AIRCR_PRIGROUP		(7 << 8)
/* Bits 7:3 - Unused, reserved */
#define CORTEXM_AIRCR_SYSRESETREQ	(1 << 2)
#define CORTEXM_AIRCR_VECTCLRACTIVE	(1 << 1)
#define CORTEXM_AIRCR_VECTRESET		(1 << 0)

/* Debug Fault Status Register (DFSR) */
/* Bits 31:5 - Reserved */
#define CORTEXM_DFSR_RESETALL		0x1F
#define CORTEXM_DFSR_EXTERNAL		(1 << 4)
#define CORTEXM_DFSR_VCATCH		(1 << 3)
#define CORTEXM_DFSR_DWTTRAP		(1 << 2)
#define CORTEXM_DFSR_BKPT		(1 << 1)
#define CORTEXM_DFSR_HALTED		(1 << 0)

/* Debug Halting Control and Status Register (DHCSR) */
/* This key must be written to bits 31:16 for write to take effect */
#define CORTEXM_DHCSR_DBGKEY		0xA05F0000
/* Bits 31:26 - Reserved */
#define CORTEXM_DHCSR_S_RESET_ST	(1 << 25)
#define CORTEXM_DHCSR_S_RETIRE_ST	(1 << 24)
/* Bits 23:20 - Reserved */
#define CORTEXM_DHCSR_S_LOCKUP		(1 << 19)
#define CORTEXM_DHCSR_S_SLEEP		(1 << 18)
#define CORTEXM_DHCSR_S_HALT		(1 << 17)
#define CORTEXM_DHCSR_S_REGRDY		(1 << 16)
/* Bits 15:6 - Reserved */
#define CORTEXM_DHCSR_C_SNAPSTALL	(1 << 5)	/* v7m only */
/* Bit 4 - Reserved */
#define CORTEXM_DHCSR_C_MASKINTS	(1 << 3)
#define CORTEXM_DHCSR_C_STEP		(1 << 2)
#define CORTEXM_DHCSR_C_HALT		(1 << 1)
#define CORTEXM_DHCSR_C_DEBUGEN		(1 << 0)

/* Utility */
#define MINIMUM(a,b)			((a < b) ? a : b)

/**
 * Reads the SAM D20 Peripheral ID
 */
uint64_t samd20_read_pid(struct target_s *target)
{
	ADIv5_AP_t *ap = adiv5_target_ap(target);
	uint64_t pid = 0;
	uint8_t i, j;

	/* Five PID registers to read LSB first */
	for (i = 0, j = 0; i < 5; i++, j += 8)
		pid |= (adiv5_ap_mem_read(ap, SAMD20_DSU_PID(i)) & 0xFF) << j;

	return pid;
}
/**
 * Reads the SAM D20 Component ID
 */
uint32_t samd20_read_cid(struct target_s *target)
{
	ADIv5_AP_t *ap = adiv5_target_ap(target);
	uint64_t cid = 0;
	uint8_t i, j;

	/* Four CID registers to read LSB first */
	for (i = 0, j = 0; i < 4; i++, j += 8)
		cid |= (adiv5_ap_mem_read(ap, SAMD20_DSU_CID(i)) & 0xFF) << j;

	return cid;
}

/**
 * Overloads the default cortexm reset function with a version that
 * removes the target from extended reset where required.
 */
static void
samd20_reset(struct target_s *target)
{
	ADIv5_AP_t *ap = adiv5_target_ap(target);

	/**
	 * SRST is not asserted here as it appears to reset the adiv5
	 * logic, meaning that subsequent adiv5_* calls PLATFORM_FATAL_ERROR.
	 *
	 * This is ok as normally you can just connect the debugger and go,
	 * but if that's not possible (protection or SWCLK being used for
	 * something else) then having SWCLK low on reset should get you
	 * debug access (cold-plugging). TODO: Confirm this
	 *
	 * See the SAM D20 datasheet §12.6 Debug Operation for more
	 * details.
	 *
	 * jtagtap_srst(true);
	 * jtagtap_srst(false);
	 */

	/* Read DHCSR here to clear S_RESET_ST bit before reset */
	adiv5_ap_mem_read(ap, CORTEXM_DHCSR);

	/* Request system reset from NVIC: SRST doesn't work correctly */
	/* This could be VECTRESET: 0x05FA0001 (reset only core)
	 *          or SYSRESETREQ: 0x05FA0004 (system reset)
	 */
	adiv5_ap_mem_write(ap, CORTEXM_AIRCR,
			   CORTEXM_AIRCR_VECTKEY | CORTEXM_AIRCR_SYSRESETREQ);

	/* Exit extended reset */
	if (adiv5_ap_mem_read(ap, SAMD20_DSU_CTRLSTAT) &
	    SAMD20_STATUSA_CRSTEXT) {
		/* Write bit to clear from extended reset */
		adiv5_ap_mem_write(ap, SAMD20_DSU_CTRLSTAT,
				   SAMD20_STATUSA_CRSTEXT);
	}

	/* Poll for release from reset */
	while(adiv5_ap_mem_read(ap, CORTEXM_DHCSR) & CORTEXM_DHCSR_S_RESET_ST);

	/* Reset DFSR flags */
	adiv5_ap_mem_write(ap, CORTEXM_DFSR, CORTEXM_DFSR_RESETALL);

	/* Clear any target errors */
	target_check_error(target);
}

char variant_string[30];
bool samd20_probe(struct target_s *target)
{
	ADIv5_AP_t *ap = adiv5_target_ap(target);
	uint32_t cid = samd20_read_cid(target);
	uint32_t pid = samd20_read_pid(target);

	/* Check the ARM Coresight Component and Perhiperal IDs */
	if (cid == SAMD20_CID_VALUE &&
	    (pid & SAMD20_PID_MASK) == SAMD20_PID_CONST_VALUE) {

		/* Read the Device ID */
		uint32_t did = adiv5_ap_mem_read(ap, SAMD20_DSU_DID);

		/* If the Device ID matches */
		if ((did & SAMD20_DID_MASK) == SAMD20_DID_CONST_VALUE) {

			uint8_t devsel = (did >> SAMD20_DID_DEVSEL_POS)
			  & SAMD20_DID_DEVSEL_MASK;
			uint8_t revision = (did >> SAMD20_DID_REVISION_POS)
			  & SAMD20_DID_REVISION_MASK;

			/* Pin Variant */
			char pin_variant;
			switch (devsel / 5) {
				case 0: pin_variant = 'J'; break;
				case 1: pin_variant = 'G'; break;
				case 2: pin_variant = 'E'; break;
				default: pin_variant = 'u'; break;
			}

			/* Mem Variant */
			uint8_t mem_variant = 18 - (devsel % 5);

			/* Revision */
			char revision_variant = 'A' + revision;

			/* Part String */
			sprintf(variant_string, "Atmel SAMD20%c%dA (rev %c)",
				pin_variant, mem_variant, revision_variant);

			/* Setup Target */
			target->driver = variant_string;
			target->reset = samd20_reset;
			target->xml_mem_map = samd20_xml_memory_map;
			target->flash_erase = samd20_flash_erase;
			target->flash_write = samd20_flash_write;
			target_add_commands(target, samd20_cmd_list, "SAMD20");

			/* If we're not in reset here */
			if (!connect_assert_srst) {
			  /* We'll have to release the target from
			   * extended reset to make attach possible */
			  if (adiv5_ap_mem_read(ap, SAMD20_DSU_CTRLSTAT) &
			      SAMD20_STATUSA_CRSTEXT) {

			    /* Write bit to clear from extended reset */
			    adiv5_ap_mem_write(ap, SAMD20_DSU_CTRLSTAT,
					       SAMD20_STATUSA_CRSTEXT);
			  }
			}

			return true;
		}
	}

	return false;
}

/**
 * Temporary (until next reset) flash memory locking / unlocking
 */
static void samd20_lock_current_address(struct target_s *target)
{
	ADIv5_AP_t *ap = adiv5_target_ap(target);

	/* Issue the unlock command */
	adiv5_ap_mem_write(ap, SAMD20_NVMC_CMD, SAMD20_CMD_KEY | SAMD20_CMD_LOCK);
}
static void samd20_unlock_current_address(struct target_s *target)
{
	ADIv5_AP_t *ap = adiv5_target_ap(target);

	/* Issue the unlock command */
	adiv5_ap_mem_write(ap, SAMD20_NVMC_CMD, SAMD20_CMD_KEY | SAMD20_CMD_UNLOCK);
}

/**
 * Erase flash row by row
 */
static int samd20_flash_erase(struct target_s *target, uint32_t addr, int len)
{
	ADIv5_AP_t *ap = adiv5_target_ap(target);

	addr &= ~(SAMD20_ROW_SIZE - 1);
	len &= ~(SAMD20_ROW_SIZE - 1);

	while (len) {
		/* Write address of first word in row to erase it */
		/* Must be shifted right for 16-bit address, see Datasheet §20.8.8 Address */
		adiv5_ap_mem_write(ap, SAMD20_NVMC_ADDRESS, addr >> 1);

		/* Unlock */
		samd20_unlock_current_address(target);

		/* Issue the erase command */
		adiv5_ap_mem_write(ap, SAMD20_NVMC_CMD, SAMD20_CMD_KEY | SAMD20_CMD_ERASEROW);
		/* Poll for NVM Ready */
		while ((adiv5_ap_mem_read(ap, SAMD20_NVMC_INTFLAG) & SAMD20_NVMC_READY) == 0)
			if(target_check_error(target))
				return -1;

		/* Lock */
		samd20_lock_current_address(target);

		addr += SAMD20_ROW_SIZE;
		len -= SAMD20_ROW_SIZE;
	}

	return 0;
}

/**
 * Write flash page by page
 */
static int samd20_flash_write(struct target_s *target, uint32_t dest,
			  const uint8_t *src, int len)
{
	ADIv5_AP_t *ap = adiv5_target_ap(target);

	/* Find the size of our 32-bit data buffer */
	uint32_t offset = dest % 4;
	uint32_t words = (offset + len + 3) / 4;
	uint32_t data[words], i = 0;

	/* Populate the data buffer */
	memset((uint8_t *)data, 0xFF, words * 4);
	memcpy((uint8_t *)data + offset, src, len);

	/* The address of the first word involved in the write */
	uint32_t addr = dest & ~0x3;
	/* The address of the last word involved in the write */
	uint32_t end = (dest + len - 1) & ~0x3;

	/* The start address of the first page involved in the write */
	uint32_t first_page = dest & ~(SAMD20_PAGE_SIZE - 1);
	/* The start address of the last page involved in the write */
	uint32_t last_page = (dest + len - 1) & ~(SAMD20_PAGE_SIZE - 1);
	uint32_t end_of_this_page;


	for (uint32_t page = first_page; page <= last_page; page += SAMD20_PAGE_SIZE) {
		end_of_this_page = page + (SAMD20_PAGE_SIZE - 4);

		if (addr > page || (page == last_page && end < end_of_this_page)) {
			/* Setup write */
			adiv5_ap_write(ap, ADIV5_AP_CSW, ap->csw |
				       ADIV5_AP_CSW_SIZE_WORD | ADIV5_AP_CSW_ADDRINC_SINGLE);
			adiv5_ap_write(ap, ADIV5_AP_TAR, addr);
			adiv5_dp_write(ap->dp, ADIV5_DP_SELECT,
				       ((uint32_t)ap->apsel << 24)|(ADIV5_AP_DRW & 0xF0));

			/* Partial, manual page write */
			for (; addr <= MINIMUM(end, end_of_this_page); addr += 4, i++) {
				adiv5_dp_write_ap(ap->dp, ADIV5_AP_DRW, data[i]);
			}

			/* Unlock */
			samd20_unlock_current_address(target);

			/* Issue the write page command */
			adiv5_ap_mem_write(ap, SAMD20_NVMC_CMD,
					   SAMD20_CMD_KEY | SAMD20_CMD_WRITEPAGE);
		} else {
			/* Write first word to set address */
			adiv5_ap_mem_write(ap, addr, data[i]); addr += 4; i++;

			/* Unlock */
			samd20_unlock_current_address(target);

			/* Set up write */
			adiv5_ap_write(ap, ADIV5_AP_CSW, ap->csw |
				       ADIV5_AP_CSW_SIZE_WORD | ADIV5_AP_CSW_ADDRINC_SINGLE);
			adiv5_ap_write(ap, ADIV5_AP_TAR, addr);
			adiv5_dp_write(ap->dp, ADIV5_DP_SELECT,
				       ((uint32_t)ap->apsel << 24)|(ADIV5_AP_DRW & 0xF0));

			/* Full, automatic page write */
			for (; addr < page + SAMD20_PAGE_SIZE; addr += 4, i++) {
				adiv5_dp_write_ap(ap->dp, ADIV5_AP_DRW, data[i]);
			}
		}

		/* Poll for NVM Ready */
		while ((adiv5_ap_mem_read(ap, SAMD20_NVMC_INTFLAG) & SAMD20_NVMC_READY) == 0)
			if(target_check_error(target))
				return -1;

		/* Lock */
		samd20_lock_current_address(target);
	}

	return 0;
}

/**
 * Uses the Device Service Unit to erase the entire flash
 */
static bool samd20_cmd_erase_all(target *t)
{
	ADIv5_AP_t *ap = adiv5_target_ap(t);

	/* Erase all */
	adiv5_ap_mem_write(ap, SAMD20_DSU_CTRLSTAT, SAMD20_CTRL_CHIP_ERASE);

	/* Poll for DSU Ready */
	while ((adiv5_ap_mem_read(ap, SAMD20_DSU_CTRLSTAT) & SAMD20_STATUSA_DONE) == 0)
		if(target_check_error(t))
			return false;

	return true;
}

/**
 * Sets the NVM region lock bits in the User Row. This value is read
 * at startup as the default value for the lock bits, and hence does
 * not take effect until a reset.
 *
 * 0x0000 = Lock, 0xFFFF = Unlock (default)
 */
static bool samd20_set_flashlock(target *t, uint16_t value)
{
	ADIv5_AP_t *ap = adiv5_target_ap(t);

	uint32_t high = adiv5_ap_mem_read(ap, SAMD20_NVM_USER_ROW_HIGH);
	uint32_t low = adiv5_ap_mem_read(ap, SAMD20_NVM_USER_ROW_LOW);

	/* Write address of a word in the row to erase it */
	/* Must be shifted right for 16-bit address, see Datasheet §20.8.8 Address */
	adiv5_ap_mem_write(ap, SAMD20_NVMC_ADDRESS, SAMD20_NVM_USER_ROW_LOW >> 1);

	/* Issue the erase command */
	adiv5_ap_mem_write(ap, SAMD20_NVMC_CMD, SAMD20_CMD_KEY | SAMD20_CMD_ERASEAUXROW);

	/* Poll for NVM Ready */
	while ((adiv5_ap_mem_read(ap, SAMD20_NVMC_INTFLAG) & SAMD20_NVMC_READY) == 0)
		if(target_check_error(t))
			return -1;

	/* Modify the high byte of the user row */
	high = (high & 0x0000FFFF) | ((value << 16) & 0xFFFF0000);

	/* Write back */
	adiv5_ap_mem_write(ap, SAMD20_NVM_USER_ROW_LOW, low);
	adiv5_ap_mem_write(ap, SAMD20_NVM_USER_ROW_HIGH, high);

	/* Issue the page write command */
	adiv5_ap_mem_write(ap, SAMD20_NVMC_CMD,
		   SAMD20_CMD_KEY | SAMD20_CMD_WRITEAUXPAGE);

  	return true;
}
static bool samd20_cmd_lock_flash(target *t)
{
	return samd20_set_flashlock(t, 0x0000);
}
static bool samd20_cmd_unlock_flash(target *t)
{
	return samd20_set_flashlock(t, 0xFFFF);
}
static bool samd20_cmd_read_userrow(target *t)
{
	ADIv5_AP_t *ap = adiv5_target_ap(t);

	gdb_outf("User Row: 0x%08x%08x\n",
		adiv5_ap_mem_read(ap, SAMD20_NVM_USER_ROW_HIGH),
		 adiv5_ap_mem_read(ap, SAMD20_NVM_USER_ROW_LOW));

	return true;
}
/**
 * Reads the 128-bit serial number from the NVM
 */
static bool samd20_cmd_serial(target *t)
{
	ADIv5_AP_t *ap = adiv5_target_ap(t);

	gdb_outf("Serial Number: 0x");

	for (uint32_t i = 0; i < 4; i++) {
		gdb_outf("%08x", adiv5_ap_mem_read(ap, SAMD20_NVM_SERIAL(i)));
	}

	gdb_outf("\n");

	return true;
}
/**
 * Returns the size (in bytes) of the current SAM D20's flash memory.
 */
static uint32_t samd20_flash_size(target *t)
{
	ADIv5_AP_t *ap = adiv5_target_ap(t);

	/* Read the Device ID */
	uint32_t did = adiv5_ap_mem_read(ap, SAMD20_DSU_DID);

	/* Mask off the device select bits */
	uint8_t devsel = did & SAMD20_DID_DEVSEL_MASK;

	/* Shift the maximum flash size (256KB) down as appropriate */
	return (0x40000 >> (devsel % 5));
}
/**
 * Runs the Memory Built In Self Test (MBIST)
 */
static bool samd20_cmd_mbist(target *t)
{
	ADIv5_AP_t *ap = adiv5_target_ap(t);

	/* Write the memory parameters to the DSU */
	adiv5_ap_mem_write(ap, SAMD20_DSU_ADDRESS, 0);
	adiv5_ap_mem_write(ap, SAMD20_DSU_LENGTH, samd20_flash_size(t));

	/* Clear the fail bit */
	adiv5_ap_mem_write(ap, SAMD20_DSU_CTRLSTAT, SAMD20_STATUSA_FAIL);

	/* Write the MBIST command */
	adiv5_ap_mem_write(ap, SAMD20_DSU_CTRLSTAT, SAMD20_CTRL_MBIST);

	/* Poll for DSU Ready */
	uint32_t status;
	while (((status = adiv5_ap_mem_read(ap, SAMD20_DSU_CTRLSTAT)) &
		(SAMD20_STATUSA_DONE | SAMD20_STATUSA_PERR | SAMD20_STATUSA_FAIL)) == 0)
		if(target_check_error(t))
			return false;

	/* Test the protection error bit in Status A */
	if (status & SAMD20_STATUSA_PERR) {
		gdb_outf("MBIST not run due to protection error.\n");
		return true;
	}

	/* Test the fail bit in Status A */
	if (status & SAMD20_STATUSA_FAIL) {
		gdb_outf("MBIST Fail @ 0x%08x\n",
			 adiv5_ap_mem_read(ap, SAMD20_DSU_ADDRESS));
	} else {
		gdb_outf("MBIST Passed!\n");
	}

	return true;
}
