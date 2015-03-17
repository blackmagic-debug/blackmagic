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

/* This file implements Atmel SAM D target specific functions for
 * detecting the device, providing the XML memory map and Flash memory
 * programming.
 *
 * Tested with
 * * SAMD20E17A (rev C)
 * * SAMD20J18A (rev B)
 * * SAMD21J18A (rev B)
 * *
 */
/* Refer to the SAM D20 Datasheet:
 * http://www.atmel.com/Images/Atmel-42129-SAM-D20_Datasheet.pdf
 * particularly Sections 12. DSU and 20. NVMCTRL
 */

#include "general.h"
#include "jtagtap.h"
#include "adiv5.h"
#include "target.h"
#include "command.h"
#include "gdb_packet.h"
#include "cortexm.h"

static int samd_flash_erase(struct target_s *target, uint32_t addr, size_t len);
static int samd_flash_write(struct target_s *target, uint32_t dest,
                            const uint8_t *src, size_t len);

static bool samd_cmd_erase_all(target *t);
static bool samd_cmd_lock_flash(target *t);
static bool samd_cmd_unlock_flash(target *t);
static bool samd_cmd_read_userrow(target *t);
static bool samd_cmd_serial(target *t);
static bool samd_cmd_mbist(target *t);
static bool samd_cmd_ssb(target *t);

const struct command_s samd_cmd_list[] = {
	{"erase_mass", (cmd_handler)samd_cmd_erase_all, "Erase entire flash memory"},
	{"lock_flash", (cmd_handler)samd_cmd_lock_flash, "Locks flash against spurious commands"},
	{"unlock_flash", (cmd_handler)samd_cmd_unlock_flash, "Unlocks flash"},
	{"user_row", (cmd_handler)samd_cmd_read_userrow, "Prints user row from flash"},
	{"serial", (cmd_handler)samd_cmd_serial, "Prints serial number"},
	{"mbist", (cmd_handler)samd_cmd_mbist, "Runs the built-in memory test"},
        {"set_security_bit", (cmd_handler)samd_cmd_ssb, "Sets the Security Bit"},
	{NULL, NULL, NULL}
};

/**
 * 256KB Flash Max., 32KB RAM Max. The smallest unit of erase is the
 * one row = 256 bytes.
 */
static const char samd_xml_memory_map[] = "<?xml version=\"1.0\"?>"
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
#define SAMD_ROW_SIZE			256
#define SAMD_PAGE_SIZE			64

/* -------------------------------------------------------------------------- */
/* Non-Volatile Memory Controller (NVMC) Registers */
/* -------------------------------------------------------------------------- */

#define SAMD_NVMC			0x41004000
#define SAMD_NVMC_CTRLA			(SAMD_NVMC + 0x0)
#define SAMD_NVMC_CTRLB			(SAMD_NVMC + 0x04)
#define SAMD_NVMC_PARAM			(SAMD_NVMC + 0x08)
#define SAMD_NVMC_INTFLAG		(SAMD_NVMC + 0x14)
#define SAMD_NVMC_STATUS		(SAMD_NVMC + 0x18)
#define SAMD_NVMC_ADDRESS		(SAMD_NVMC + 0x1C)

/* Control A Register (CTRLA) */
#define SAMD_CTRLA_CMD_KEY		0xA500
#define SAMD_CTRLA_CMD_ERASEROW		0x0002
#define SAMD_CTRLA_CMD_WRITEPAGE	0x0004
#define SAMD_CTRLA_CMD_ERASEAUXROW	0x0005
#define SAMD_CTRLA_CMD_WRITEAUXPAGE	0x0006
#define SAMD_CTRLA_CMD_LOCK		0x0040
#define SAMD_CTRLA_CMD_UNLOCK		0x0041
#define SAMD_CTRLA_CMD_PAGEBUFFERCLEAR	0x0044
#define SAMD_CTRLA_CMD_SSB		0x0045
#define SAMD_CTRLA_CMD_INVALL		0x0046

/* Interrupt Flag Register (INTFLAG) */
#define SAMD_NVMC_READY			(1 << 0)

/* Non-Volatile Memory Calibration and Auxiliary Registers */
#define SAMD_NVM_USER_ROW_LOW		0x00804000
#define SAMD_NVM_USER_ROW_HIGH		0x00804004
#define SAMD_NVM_CALIBRATION		0x00806020
#define SAMD_NVM_SERIAL(n)		(0x0080A00C + (0x30 * ((n + 3) / 4)) + \
					 (0x4 * n))

/* -------------------------------------------------------------------------- */
/* Device Service Unit (DSU) Registers */
/* -------------------------------------------------------------------------- */

#define SAMD_DSU			0x41002000
#define SAMD_DSU_EXT_ACCESS		(SAMD_DSU + 0x100)
#define SAMD_DSU_CTRLSTAT		(SAMD_DSU_EXT_ACCESS + 0x0)
#define SAMD_DSU_ADDRESS		(SAMD_DSU_EXT_ACCESS + 0x4)
#define SAMD_DSU_LENGTH			(SAMD_DSU_EXT_ACCESS + 0x8)
#define SAMD_DSU_DID			(SAMD_DSU_EXT_ACCESS + 0x018)
#define SAMD_DSU_PID(n)			(SAMD_DSU + 0x1FE0 + \
					 (0x4 * (n % 4)) - (0x10 * (n / 4)))
#define SAMD_DSU_CID(n)			(SAMD_DSU + 0x1FF0 + \
					 (0x4 * (n % 4)))

/* Control and Status Register (CTRLSTAT) */
#define SAMD_CTRL_CHIP_ERASE		(1 << 4)
#define SAMD_CTRL_MBIST			(1 << 3)
#define SAMD_CTRL_CRC			(1 << 2)
#define SAMD_STATUSA_PERR		(1 << 12)
#define SAMD_STATUSA_FAIL		(1 << 11)
#define SAMD_STATUSA_BERR		(1 << 10)
#define SAMD_STATUSA_CRSTEXT		(1 << 9)
#define SAMD_STATUSA_DONE		(1 << 8)
#define SAMD_STATUSB_PROT		(1 << 16)

/* Device Identification Register (DID) */
#define SAMD_DID_MASK			0xFFBC0000
#define SAMD_DID_CONST_VALUE		0x10000000
#define SAMD_DID_DEVSEL_MASK		0x0F
#define SAMD_DID_DEVSEL_POS		0
#define SAMD_DID_REVISION_MASK		0x0F
#define SAMD_DID_REVISION_POS		8
#define SAMD_DID_SERIES_MASK		0x03
#define SAMD_DID_SERIES_POS		16

/* Peripheral ID */
#define SAMD_PID_MASK			0x00F7FFFF
#define SAMD_PID_CONST_VALUE		0x0001FCD0

/* Component ID */
#define SAMD_CID_VALUE			0xB105100D

/* Utility */
#define MINIMUM(a,b)			((a < b) ? a : b)

/**
 * Reads the SAM D20 Peripheral ID
 */
uint64_t samd_read_pid(struct target_s *target)
{
	uint64_t pid = 0;
	uint8_t i, j;

	/* Five PID registers to read LSB first */
	for (i = 0, j = 0; i < 5; i++, j += 8)
		pid |= (target_mem_read32(target, SAMD_DSU_PID(i)) & 0xFF) << j;

	return pid;
}
/**
 * Reads the SAM D20 Component ID
 */
uint32_t samd_read_cid(struct target_s *target)
{
	uint64_t cid = 0;
	uint8_t i, j;

	/* Four CID registers to read LSB first */
	for (i = 0, j = 0; i < 4; i++, j += 8)
		cid |= (target_mem_read32(target, SAMD_DSU_CID(i)) & 0xFF) << j;

	return cid;
}

/**
 * Overloads the default cortexm reset function with a version that
 * removes the target from extended reset where required.
 */
static void
samd_reset(struct target_s *target)
{
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
	target_mem_read32(target, CORTEXM_DHCSR);

	/* Request system reset from NVIC: SRST doesn't work correctly */
	/* This could be VECTRESET: 0x05FA0001 (reset only core)
	 *          or SYSRESETREQ: 0x05FA0004 (system reset)
	 */
	target_mem_write32(target, CORTEXM_AIRCR,
			   CORTEXM_AIRCR_VECTKEY | CORTEXM_AIRCR_SYSRESETREQ);

	/* Exit extended reset */
	if (target_mem_read32(target, SAMD_DSU_CTRLSTAT) &
	    SAMD_STATUSA_CRSTEXT) {
		/* Write bit to clear from extended reset */
		target_mem_write32(target, SAMD_DSU_CTRLSTAT,
				   SAMD_STATUSA_CRSTEXT);
	}

	/* Poll for release from reset */
	while (target_mem_read32(target, CORTEXM_DHCSR) & CORTEXM_DHCSR_S_RESET_ST);

	/* Reset DFSR flags */
	target_mem_write32(target, CORTEXM_DFSR, CORTEXM_DFSR_RESETALL);

	/* Clear any target errors */
	target_check_error(target);
}

/**
 * Overloads the default cortexm detached function with a version that
 * removes the target from extended reset where required.
 *
 * Only required for SAM D20 _Revision B_ Silicon
 */
static void
samd20_revB_detach(struct target_s *target)
{
	cortexm_detach(target);

	/* ---- Additional ---- */
	/* Exit extended reset */
	if (target_mem_read32(target, SAMD_DSU_CTRLSTAT) &
	    SAMD_STATUSA_CRSTEXT) {
		/* Write bit to clear from extended reset */
		target_mem_write32(target, SAMD_DSU_CTRLSTAT,
		                   SAMD_STATUSA_CRSTEXT);
	}
}

/**
 * Overloads the default cortexm halt_resume function with a version
 * that removes the target from extended reset where required.
 *
 * Only required for SAM D20 _Revision B_ Silicon
 */
static void
samd20_revB_halt_resume(struct target_s *target, bool step)
{
	cortexm_halt_resume(target, step);

	/* ---- Additional ---- */
	/* Exit extended reset */
	if (target_mem_read32(target, SAMD_DSU_CTRLSTAT) &
	    SAMD_STATUSA_CRSTEXT) {
		/* Write bit to clear from extended reset */
		target_mem_write32(target, SAMD_DSU_CTRLSTAT,
		                   SAMD_STATUSA_CRSTEXT);
	}
}

/**
 * Overload the default cortexm attach for when the samd is protected.
 *
 * If the samd is protected then the default cortexm attach will
 * fail as the S_HALT bit in the DHCSR will never go high. This
 * function allows users to attach on a temporary basis so they can
 * rescue the device.
 */
static bool
samd_protected_attach(struct target_s *target)
{
	/**
	 * TODO: Notify the user that we're not really attached and
	 * they should issue the 'monitor erase_mass' command to
	 * regain access to the chip.
	 */

        /* Patch back in the normal cortexm attach for next time */
        target->attach = cortexm_attach;

        /* Allow attach this time */
	return true;
}

/**
 * Use the DSU Device Indentification Register to populate a struct
 * describing the SAM D device.
 */
struct samd_descr {
	uint8_t series;
	char revision;
	char pin;
	uint8_t mem;
	char package[3];
};
struct samd_descr samd_parse_device_id(uint32_t did)
{
	struct samd_descr samd;
	memset(samd.package, 0, 3);

	uint8_t series = (did >> SAMD_DID_SERIES_POS)
	  & SAMD_DID_SERIES_MASK;
	uint8_t revision = (did >> SAMD_DID_REVISION_POS)
	  & SAMD_DID_REVISION_MASK;
	uint8_t devsel = (did >> SAMD_DID_DEVSEL_POS)
	  & SAMD_DID_DEVSEL_MASK;

	/* Series */
	switch (series) {
		case 0: samd.series = 20; break;
		case 1: samd.series = 21; break;
		case 2: samd.series = 10; break;
		case 3: samd.series = 11; break;
	}
	/* Revision */
	samd.revision = 'A' + revision;

	switch (samd.series) {
	case 20: /* SAM D20 */
	case 21: /* SAM D21 */
		switch (devsel / 5) {
			case 0: samd.pin = 'J'; break;
			case 1: samd.pin = 'G'; break;
			case 2: samd.pin = 'E'; break;
			default: samd.pin = 'u'; break;
		}
		samd.mem = 18 - (devsel % 5);
		break;
	case 10: /* SAM D10 */
	case 11: /* SAM D11 */
		switch (devsel / 3) {
			case 0: samd.package[0] = 'M'; break;
			case 1: samd.package[0] = 'S'; samd.package[1] = 'S'; break;
		}
		samd.pin = 'D';
		samd.mem = 14 - (devsel % 3);
		break;
	}

	return samd;
}


char variant_string[40];
bool samd_probe(struct target_s *target)
{
	uint32_t cid = samd_read_cid(target);
	uint32_t pid = samd_read_pid(target);

	/* Check the ARM Coresight Component and Perhiperal IDs */
	if (cid == SAMD_CID_VALUE &&
	    (pid & SAMD_PID_MASK) == SAMD_PID_CONST_VALUE) {

		/* Read the Device ID */
		uint32_t did = target_mem_read32(target, SAMD_DSU_DID);

		/* If the Device ID matches */
		if ((did & SAMD_DID_MASK) == SAMD_DID_CONST_VALUE) {

			uint32_t ctrlstat = target_mem_read32(target,
			                                      SAMD_DSU_CTRLSTAT);
			struct samd_descr samd = samd_parse_device_id(did);

                        /* Protected? */
                        int protected = (ctrlstat & SAMD_STATUSB_PROT);

			/* Part String */
                        if (protected) {
				sprintf(variant_string,
					"Atmel SAMD%d%c%dA%s (rev %c) (PROT=1)",
					samd.series, samd.pin, samd.mem,
					samd.package, samd.revision);
			} else {
				sprintf(variant_string,
					"Atmel SAMD%d%c%dA%s (rev %c)",
					samd.series, samd.pin, samd.mem,
					samd.package, samd.revision);
                        }

			/* Setup Target */
			target->driver = variant_string;
			target->reset = samd_reset;

			if (samd.series == 20 && samd.revision == 'B') {
				/**
				 * These functions check for and
				 * extended reset. Appears to be
				 * related to Errata 35.4.1 ref 12015
				 */
				target->detach      = samd20_revB_detach;
				target->halt_resume = samd20_revB_halt_resume;
			}
			if (protected) {
				/**
				 * Overload the default cortexm attach
				 * for when the samd is protected.
				 * This function allows users to
				 * attach on a temporary basis so they
				 * can rescue the device.
				 */
				target->attach = samd_protected_attach;
			}

			target->xml_mem_map = samd_xml_memory_map;
			target->flash_erase = samd_flash_erase;
			target->flash_write = samd_flash_write;
			target_add_commands(target, samd_cmd_list, "SAMD");

			/* If we're not in reset here */
			if (!connect_assert_srst) {
			  /* We'll have to release the target from
			   * extended reset to make attach possible */
			  if (target_mem_read32(target, SAMD_DSU_CTRLSTAT) &
			      SAMD_STATUSA_CRSTEXT) {

			    /* Write bit to clear from extended reset */
			    target_mem_write32(target, SAMD_DSU_CTRLSTAT,
					       SAMD_STATUSA_CRSTEXT);
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
static void samd_lock_current_address(struct target_s *target)
{
	/* Issue the unlock command */
	target_mem_write32(target, SAMD_NVMC_CTRLA,
	                   SAMD_CTRLA_CMD_KEY | SAMD_CTRLA_CMD_LOCK);
}
static void samd_unlock_current_address(struct target_s *target)
{
	/* Issue the unlock command */
	target_mem_write32(target, SAMD_NVMC_CTRLA,
	                   SAMD_CTRLA_CMD_KEY | SAMD_CTRLA_CMD_UNLOCK);
}

/**
 * Erase flash row by row
 */
static int samd_flash_erase(struct target_s *target, uint32_t addr, size_t len)
{
	addr &= ~(SAMD_ROW_SIZE - 1);
	len &= ~(SAMD_ROW_SIZE - 1);

	while (len) {
		/* Write address of first word in row to erase it */
		/* Must be shifted right for 16-bit address, see Datasheet §20.8.8 Address */
		target_mem_write32(target, SAMD_NVMC_ADDRESS, addr >> 1);

		/* Unlock */
		samd_unlock_current_address(target);

		/* Issue the erase command */
		target_mem_write32(target, SAMD_NVMC_CTRLA,
		                   SAMD_CTRLA_CMD_KEY | SAMD_CTRLA_CMD_ERASEROW);
		/* Poll for NVM Ready */
		while ((target_mem_read32(target, SAMD_NVMC_INTFLAG) & SAMD_NVMC_READY) == 0)
			if(target_check_error(target))
				return -1;

		/* Lock */
		samd_lock_current_address(target);

		addr += SAMD_ROW_SIZE;
		len -= SAMD_ROW_SIZE;
	}

	return 0;
}

/**
 * Write flash page by page
 */
static int samd_flash_write(struct target_s *target, uint32_t dest,
                            const uint8_t *src, size_t len)
{
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
	uint32_t first_page = dest & ~(SAMD_PAGE_SIZE - 1);
	/* The start address of the last page involved in the write */
	uint32_t last_page = (dest + len - 1) & ~(SAMD_PAGE_SIZE - 1);
	uint32_t next_page;
	uint32_t length;

	for (uint32_t page = first_page; page <= last_page; page += SAMD_PAGE_SIZE) {
		next_page = page + SAMD_PAGE_SIZE;
		length = MINIMUM(end + 4, next_page) - addr;

		/* Write within a single page. This may be part or all of the page */
                target_mem_write(target, addr, &data[i], length);
                addr += length; i += (length >> 2);

                /* If MANW=0 (default) we may have triggered an automatic
                 * write. Ignore this */

                /* Unlock */
                samd_unlock_current_address(target);

                /* Issue the write page command */
                target_mem_write32(target, SAMD_NVMC_CTRLA,
				   SAMD_CTRLA_CMD_KEY | SAMD_CTRLA_CMD_WRITEPAGE);

		/* Poll for NVM Ready */
		while ((target_mem_read32(target, SAMD_NVMC_INTFLAG) & SAMD_NVMC_READY) == 0)
			if(target_check_error(target))
				return -1;

		/* Lock */
		samd_lock_current_address(target);
	}

	return 0;
}

/**
 * Uses the Device Service Unit to erase the entire flash
 */
static bool samd_cmd_erase_all(target *t)
{
  /* Clear the DSU status bits */
	target_mem_write32(t, SAMD_DSU_CTRLSTAT,
			   (SAMD_STATUSA_DONE | SAMD_STATUSA_PERR | SAMD_STATUSA_FAIL));

	/* Erase all */
	target_mem_write32(t, SAMD_DSU_CTRLSTAT, SAMD_CTRL_CHIP_ERASE);

	/* Poll for DSU Ready */
	uint32_t status;
	while (((status = target_mem_read32(t, SAMD_DSU_CTRLSTAT)) &
		(SAMD_STATUSA_DONE | SAMD_STATUSA_PERR | SAMD_STATUSA_FAIL)) == 0)
		if(target_check_error(t))
			return false;

	/* Test the protection error bit in Status A */
	if (status & SAMD_STATUSA_PERR) {
		gdb_outf("Erase failed due to a protection error.\n");
		return true;
	}

	/* Test the fail bit in Status A */
	if (status & SAMD_STATUSA_FAIL) {
		gdb_outf("Erase failed.\n");
		return true;
	}

	gdb_outf("Erase successful!\n");

	return true;
}

/**
 * Sets the NVM region lock bits in the User Row. This value is read
 * at startup as the default value for the lock bits, and hence does
 * not take effect until a reset.
 *
 * 0x0000 = Lock, 0xFFFF = Unlock (default)
 */
static bool samd_set_flashlock(target *t, uint16_t value)
{
	uint32_t high = target_mem_read32(t, SAMD_NVM_USER_ROW_HIGH);
	uint32_t low = target_mem_read32(t, SAMD_NVM_USER_ROW_LOW);

	/* Write address of a word in the row to erase it */
	/* Must be shifted right for 16-bit address, see Datasheet §20.8.8 Address */
	target_mem_write32(t, SAMD_NVMC_ADDRESS, SAMD_NVM_USER_ROW_LOW >> 1);

	/* Issue the erase command */
	target_mem_write32(t, SAMD_NVMC_CTRLA,
	                   SAMD_CTRLA_CMD_KEY | SAMD_CTRLA_CMD_ERASEAUXROW);

	/* Poll for NVM Ready */
	while ((target_mem_read32(t, SAMD_NVMC_INTFLAG) & SAMD_NVMC_READY) == 0)
		if(target_check_error(t))
			return -1;

	/* Modify the high byte of the user row */
	high = (high & 0x0000FFFF) | ((value << 16) & 0xFFFF0000);

	/* Write back */
	target_mem_write32(t, SAMD_NVM_USER_ROW_LOW, low);
	target_mem_write32(t, SAMD_NVM_USER_ROW_HIGH, high);

	/* Issue the page write command */
	target_mem_write32(t, SAMD_NVMC_CTRLA,
	                   SAMD_CTRLA_CMD_KEY | SAMD_CTRLA_CMD_WRITEAUXPAGE);

  	return true;
}
static bool samd_cmd_lock_flash(target *t)
{
	return samd_set_flashlock(t, 0x0000);
}
static bool samd_cmd_unlock_flash(target *t)
{
	return samd_set_flashlock(t, 0xFFFF);
}
static bool samd_cmd_read_userrow(target *t)
{
	gdb_outf("User Row: 0x%08x%08x\n",
		target_mem_read32(t, SAMD_NVM_USER_ROW_HIGH),
		target_mem_read32(t, SAMD_NVM_USER_ROW_LOW));

	return true;
}
/**
 * Reads the 128-bit serial number from the NVM
 */
static bool samd_cmd_serial(target *t)
{
	gdb_outf("Serial Number: 0x");

	for (uint32_t i = 0; i < 4; i++) {
		gdb_outf("%08x", target_mem_read32(t, SAMD_NVM_SERIAL(i)));
	}

	gdb_outf("\n");

	return true;
}
/**
 * Returns the size (in bytes) of the current SAM D20's flash memory.
 */
static uint32_t samd_flash_size(target *t)
{
	/* Read the Device ID */
	uint32_t did = target_mem_read32(t, SAMD_DSU_DID);

	/* Mask off the device select bits */
	uint8_t devsel = did & SAMD_DID_DEVSEL_MASK;

	/* Shift the maximum flash size (256KB) down as appropriate */
	return (0x40000 >> (devsel % 5));
}
/**
 * Runs the Memory Built In Self Test (MBIST)
 */
static bool samd_cmd_mbist(target *t)
{
	/* Write the memory parameters to the DSU */
	target_mem_write32(t, SAMD_DSU_ADDRESS, 0);
	target_mem_write32(t, SAMD_DSU_LENGTH, samd_flash_size(t));

	/* Clear the fail bit */
	target_mem_write32(t, SAMD_DSU_CTRLSTAT, SAMD_STATUSA_FAIL);

	/* Write the MBIST command */
	target_mem_write32(t, SAMD_DSU_CTRLSTAT, SAMD_CTRL_MBIST);

	/* Poll for DSU Ready */
	uint32_t status;
	while (((status = target_mem_read32(t, SAMD_DSU_CTRLSTAT)) &
		(SAMD_STATUSA_DONE | SAMD_STATUSA_PERR | SAMD_STATUSA_FAIL)) == 0)
		if(target_check_error(t))
			return false;

	/* Test the protection error bit in Status A */
	if (status & SAMD_STATUSA_PERR) {
		gdb_outf("MBIST not run due to protection error.\n");
		return true;
	}

	/* Test the fail bit in Status A */
	if (status & SAMD_STATUSA_FAIL) {
		gdb_outf("MBIST Fail @ 0x%08x\n",
			 target_mem_read32(t, SAMD_DSU_ADDRESS));
	} else {
		gdb_outf("MBIST Passed!\n");
	}

	return true;
}
/**
 * Sets the security bit
 */
static bool samd_cmd_ssb(target *t)
{
	/* Issue the ssb command */
	target_mem_write32(t, SAMD_NVMC_CTRLA,
	                   SAMD_CTRLA_CMD_KEY | SAMD_CTRLA_CMD_SSB);

        /* Poll for NVM Ready */
        while ((target_mem_read32(t, SAMD_NVMC_INTFLAG) & SAMD_NVMC_READY) == 0)
        	if(target_check_error(t))
			return -1;

	gdb_outf("Set the security bit! "
		 "You will need to issue 'monitor erase_mass' to clear this.\n");

	return true;
}
