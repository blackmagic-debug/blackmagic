/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2015  Richard Meadows <richardeoin>
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

/* This file implements EFM32 target specific functions for
 * detecting the device, providing the XML memory map and Flash memory
 * programming.
 *
 * Both EFM32 (microcontroller only) and EZR32 (microcontroller+radio)
 * devices should be supported through this driver.
 *
 * Tested with:
 * * EZR32LG230 (EZR Leopard Gecko M3)
 * *
 */

/* Refer to the family reference manuals:
 *
 *
 * Also refer to AN0062 "Programming Internal Flash Over the Serial Wire Debug Interface"
 * http://www.silabs.com/Support%20Documents/TechnicalDocs/an0062.pdf
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"

#define SRAM_BASE		0x20000000
#define STUB_BUFFER_BASE	ALIGN(SRAM_BASE + sizeof(efm32_flash_write_stub), 4)

static int efm32_flash_erase(struct target_flash *t, target_addr addr, size_t len);
static int efm32_flash_write(struct target_flash *f,
			     target_addr dest, const void *src, size_t len);

static const uint16_t efm32_flash_write_stub[] = {
#include "flashstub/efm32.stub"
};

static bool efm32_cmd_erase_all(target *t);
static bool efm32_cmd_serial(target *t);

const struct command_s efm32_cmd_list[] = {
	{"erase_mass", (cmd_handler)efm32_cmd_erase_all, "Erase entire flash memory"},
	{"serial", (cmd_handler)efm32_cmd_serial, "Prints unique number"},
	{NULL, NULL, NULL}
};



/* -------------------------------------------------------------------------- */
/* Memory System Controller (MSC) Registers */
/* -------------------------------------------------------------------------- */

#define EFM32_MSC	       		0x400c0000
#define EFM32_MSC_WRITECTRL	     	(EFM32_MSC+0x008)
#define EFM32_MSC_WRITECMD	      	(EFM32_MSC+0x00c)
#define EFM32_MSC_ADDRB		 	(EFM32_MSC+0x010)
#define EFM32_MSC_WDATA		 	(EFM32_MSC+0x018)
#define EFM32_MSC_STATUS		(EFM32_MSC+0x01c)
#define EFM32_MSC_LOCK		  	(EFM32_MSC+0x03c)
#define EFM32_MSC_CMD		   	(EFM32_MSC+0x040)
#define EFM32_MSC_TIMEBASE	      	(EFM32_MSC+0x050)
#define EFM32_MSC_MASSLOCK	      	(EFM32_MSC+0x054)

#define EFM32_MSC_LOCK_LOCKKEY	  	0x1b71
#define EFM32_MSC_MASSLOCK_LOCKKEY	0x631a

#define EFM32_MSC_WRITECMD_LADDRIM	(1<<0)
#define EFM32_MSC_WRITECMD_ERASEPAGE	(1<<1)
#define EFM32_MSC_WRITECMD_WRITEEND	(1<<2)
#define EFM32_MSC_WRITECMD_WRITEONCE	(1<<3)
#define EFM32_MSC_WRITECMD_WRITETRIG	(1<<4)
#define EFM32_MSC_WRITECMD_ERASEABORT	(1<<5)
#define EFM32_MSC_WRITECMD_ERASEMAIN0	(1<<8)

#define EFM32_MSC_STATUS_BUSY		(1<<0)
#define EFM32_MSC_STATUS_LOCKED		(1<<1)
#define EFM32_MSC_STATUS_INVADDR	(1<<2)
#define EFM32_MSC_STATUS_WDATAREADY	(1<<3)


/* -------------------------------------------------------------------------- */
/* Flash Infomation Area */
/* -------------------------------------------------------------------------- */

#define EFM32_INFO			0x0fe00000
#define EFM32_USER_DATA			(EFM32_INFO+0x0000)
#define EFM32_LOCK_BITS			(EFM32_INFO+0x4000)
#define EFM32_DI			(EFM32_INFO+0x8000)


/* -------------------------------------------------------------------------- */
/* Device Information (DI) Area */
/* -------------------------------------------------------------------------- */

#define EFM32_DI_CMU_LFRCOCTRL 		(EFM32_DI+0x020)
#define EFM32_DI_CMU_HFRCOCTRL 		(EFM32_DI+0x028)
#define EFM32_DI_CMU_AUXHFRCOCTRL 	(EFM32_DI+0x030)
#define EFM32_DI_ADC0_CAL 		(EFM32_DI+0x040)
#define EFM32_DI_ADC0_BIASPROG 		(EFM32_DI+0x048)
#define EFM32_DI_DAC0_CAL 		(EFM32_DI+0x050)
#define EFM32_DI_DAC0_BIASPROG 		(EFM32_DI+0x058)
#define EFM32_DI_ACMP0_CTRL 		(EFM32_DI+0x060)
#define EFM32_DI_ACMP1_CTRL 		(EFM32_DI+0x068)
#define EFM32_DI_CMU_LCDCTRL 		(EFM32_DI+0x078)
#define EFM32_DI_DAC0_OPACTRL 		(EFM32_DI+0x0A0)
#define EFM32_DI_DAC0_OPAOFFSET 	(EFM32_DI+0x0A8)
#define EFM32_DI_EMU_BUINACT 		(EFM32_DI+0x0B0)
#define EFM32_DI_EMU_BUACT 		(EFM32_DI+0x0B8)
#define EFM32_DI_EMU_BUBODBUVINCAL 	(EFM32_DI+0x0C0)
#define EFM32_DI_EMU_BUBODUNREGCAL 	(EFM32_DI+0x0C8)
#define EFM32_DI_MCM_REV_MIN 		(EFM32_DI+0x1AA)
#define EFM32_DI_MCM_REV_MAJ 		(EFM32_DI+0x1AB)
#define EFM32_DI_RADIO_REV_MIN 		(EFM32_DI+0x1AC)
#define EFM32_DI_RADIO_REV_MAJ 		(EFM32_DI+0x1AD)
#define EFM32_DI_RADIO_OPN 		(EFM32_DI+0x1AE)
#define EFM32_DI_DI_CRC 		(EFM32_DI+0x1B0)
#define EFM32_DI_CAL_TEMP_0 		(EFM32_DI+0x1B2)
#define EFM32_DI_ADC0_CAL_1V25 		(EFM32_DI+0x1B4)
#define EFM32_DI_ADC0_CAL_2V5 		(EFM32_DI+0x1B6)
#define EFM32_DI_ADC0_CAL_VDD 		(EFM32_DI+0x1B8)
#define EFM32_DI_ADC0_CAL_5VDIFF 	(EFM32_DI+0x1BA)
#define EFM32_DI_ADC0_CAL_2XVDD 	(EFM32_DI+0x1BC)
#define EFM32_DI_ADC0_TEMP_0_READ_1V25	(EFM32_DI+0x1BE)
#define EFM32_DI_DAC0_CAL_1V25 		(EFM32_DI+0x1C8)
#define EFM32_DI_DAC0_CAL_2V5 		(EFM32_DI+0x1CC)
#define EFM32_DI_DAC0_CAL_VDD 		(EFM32_DI+0x1D0)
#define EFM32_DI_AUXHFRCO_CALIB_BAND_1 	(EFM32_DI+0x1D4)
#define EFM32_DI_AUXHFRCO_CALIB_BAND_7 	(EFM32_DI+0x1D5)
#define EFM32_DI_AUXHFRCO_CALIB_BAND_11 (EFM32_DI+0x1D6)
#define EFM32_DI_AUXHFRCO_CALIB_BAND_14 (EFM32_DI+0x1D7)
#define EFM32_DI_AUXHFRCO_CALIB_BAND_21 (EFM32_DI+0x1D8)
#define EFM32_DI_AUXHFRCO_CALIB_BAND_28 (EFM32_DI+0x1D9)
#define EFM32_DI_HFRCO_CALIB_BAND_1 	(EFM32_DI+0x1DC)
#define EFM32_DI_HFRCO_CALIB_BAND_7 	(EFM32_DI+0x1DD)
#define EFM32_DI_HFRCO_CALIB_BAND_11 	(EFM32_DI+0x1DE)
#define EFM32_DI_HFRCO_CALIB_BAND_14 	(EFM32_DI+0x1DF)
#define EFM32_DI_HFRCO_CALIB_BAND_21 	(EFM32_DI+0x1E0)
#define EFM32_DI_HFRCO_CALIB_BAND_28 	(EFM32_DI+0x1E1)
#define EFM32_DI_MEM_INFO_PAGE_SIZE 	(EFM32_DI+0x1E7)
#define EFM32_DI_RADIO_ID 		(EFM32_DI+0x1EE)
#define EFM32_DI_EUI64_0 		(EFM32_DI+0x1F0)
#define EFM32_DI_EUI64_1 		(EFM32_DI+0x1F4)
#define EFM32_DI_MEM_INFO_FLASH 	(EFM32_DI+0x1F8)
#define EFM32_DI_MEM_INFO_RAM 		(EFM32_DI+0x1FA)
#define EFM32_DI_PART_NUMBER 		(EFM32_DI+0x1FC)
#define EFM32_DI_PART_FAMILY 		(EFM32_DI+0x1FE)
#define EFM32_DI_PROD_REV 		(EFM32_DI+0x1FF)

/* top 24 bits of eui */
#define EFM32_DI_EUI_SILABS	0x000b57

#define EFM32_DI_PART_FAMILY_GECKO		71
#define EFM32_DI_PART_FAMILY_GIANT_GECKO	72
#define EFM32_DI_PART_FAMILY_TINY_GECKO		73
#define EFM32_DI_PART_FAMILY_LEOPARD_GECKO	74
#define EFM32_DI_PART_FAMILY_WONDER_GECKO	75
#define EFM32_DI_PART_FAMILY_ZERO_GECKO		76
#define EFM32_DI_PART_FAMILY_HAPPY_GECKO	77
#define EFM32_DI_PART_FAMILY_EZR_WONDER_GECKO	120
#define EFM32_DI_PART_FAMILY_EZR_LEOPARD_GECKO	121

/* -------------------------------------------------------------------------- */
/* Helper functions */
/* -------------------------------------------------------------------------- */

/**
 * Reads the EFM32 Extended Unique Identifier
 */
	uint64_t efm32_read_eui(target *t)
	{
		uint64_t eui;

		eui  = (uint64_t)target_mem_read32(t, EFM32_DI_EUI64_1) << 32;
		eui |= (uint64_t)target_mem_read32(t, EFM32_DI_EUI64_0) <<  0;

		return eui;
	}
/**
 * Reads the EFM32 flash size in kiB
 */
uint16_t efm32_read_flash_size(target *t)
{
	return target_mem_read16(t, EFM32_DI_MEM_INFO_FLASH);
}
/**
 * Reads the EFM32 RAM size in kiB
 */
uint16_t efm32_read_ram_size(target *t)
{
	return target_mem_read16(t, EFM32_DI_MEM_INFO_RAM);
}
/**
 * Reads the EFM32 Part Number
 */
uint16_t efm32_read_part_number(target *t)
{
	return target_mem_read16(t, EFM32_DI_PART_NUMBER);
}
/**
 * Reads the EFM32 Part Family
 */
uint8_t efm32_read_part_family(target *t)
{
	return target_mem_read8(t, EFM32_DI_PART_FAMILY);
}
/**
 * Reads the EFM32 Radio part number (EZR parts only)
 */
uint16_t efm32_read_radio_part_number(target *t)
{
	return target_mem_read16(t, EFM32_DI_RADIO_OPN);
}




static void efm32_add_flash(target *t, target_addr addr, size_t length,
			    size_t page_size)
{
	struct target_flash *f = calloc(1, sizeof(*f));
	f->start = addr;
	f->length = length;
	f->blocksize = page_size;
	f->erase = efm32_flash_erase;
	f->write = efm32_flash_write;
	f->buf_size = page_size;
	target_add_flash(t, f);
}

char variant_string[40];
bool efm32_probe(target *t)
{
	/* Read the IDCODE register from the SW-DP */
	ADIv5_AP_t *ap = cortexm_ap(t);
	uint32_t ap_idcode = ap->dp->idcode;

	/* Check the idcode is silabs. See AN0062 Section 2.2 */
	if (ap_idcode == 0x2BA01477) {
		/* Cortex M3, Cortex M4 */
	} else if (ap_idcode == 0x0BC11477) {
		/* Cortex M0+ */
	} else {
		return false;
	}

	/* Read the part number and family */
	uint16_t part_number = efm32_read_part_number(t);
	uint8_t part_family = efm32_read_part_family(t);
	uint16_t radio_number, radio_number_short;  /* optional, for ezr parts */
	uint32_t flash_page_size; uint16_t flash_kb;

	switch(part_family) {
		case EFM32_DI_PART_FAMILY_GECKO:
			sprintf(variant_string,
				"EFM32 Gecko");
			flash_page_size = 512;
			break;
		case EFM32_DI_PART_FAMILY_GIANT_GECKO:
			sprintf(variant_string,
				"EFM32 Giant Gecko");
			flash_page_size = 2048; /* Could be 2048 or 4096, assume 2048 */
			break;
		case EFM32_DI_PART_FAMILY_TINY_GECKO:
			sprintf(variant_string,
				"EFM32 Tiny Gecko");
			flash_page_size = 512;
			break;
		case EFM32_DI_PART_FAMILY_LEOPARD_GECKO:
			sprintf(variant_string,
				"EFM32 Leopard Gecko");
			flash_page_size = 2048; /* Could be 2048 or 4096, assume 2048 */
			break;
		case EFM32_DI_PART_FAMILY_WONDER_GECKO:
			sprintf(variant_string,
				"EFM32 Wonder Gecko");
			flash_page_size = 2048;
			break;
		case EFM32_DI_PART_FAMILY_ZERO_GECKO:
			sprintf(variant_string,
				"EFM32 Zero Gecko");
			flash_page_size = 1024;
			break;
		case EFM32_DI_PART_FAMILY_HAPPY_GECKO:
			sprintf(variant_string,
				"EFM32 Happy Gecko");
			flash_page_size = 1024;
			break;
		case EFM32_DI_PART_FAMILY_EZR_WONDER_GECKO:
			radio_number = efm32_read_radio_part_number(t); /* on-chip radio */
			radio_number_short = radio_number % 100;
			flash_kb = efm32_read_flash_size(t);

			sprintf(variant_string,
				"EZR32WG%dF%dR%d (radio si%d)",
				part_number, flash_kb,
				radio_number_short, radio_number);

			flash_page_size = 2048;
			break;
		case EFM32_DI_PART_FAMILY_EZR_LEOPARD_GECKO:
			radio_number = efm32_read_radio_part_number(t); /* on-chip radio */
			radio_number_short = radio_number % 100;
			flash_kb = efm32_read_flash_size(t);

			sprintf(variant_string,
				"EZR32LG%dF%dR%d (radio si%d)",
				part_number, flash_kb,
				radio_number_short, radio_number);

			flash_page_size = 2048;
			break;
		default:	/* Unknown family */
			return false;
	}

	/* Read memory sizes, convert to bytes */
	uint32_t flash_size = efm32_read_flash_size(t) * 0x400;
	uint32_t ram_size   = efm32_read_ram_size(t)   * 0x400;

	/* Setup Target */
	t->target_options |= CORTEXM_TOPT_INHIBIT_SRST;
	t->driver = variant_string;
	tc_printf(t, "flash size %d page size %d\n", flash_size, flash_page_size);
	target_add_ram (t, SRAM_BASE, ram_size);
	efm32_add_flash(t, 0x00000000, flash_size, flash_page_size);
	target_add_commands(t, efm32_cmd_list, "EFM32");

	return true;
}

/**
 * Erase flash row by row
 */
static int efm32_flash_erase(struct target_flash *f, target_addr addr, size_t len)
{
	target *t = f->t;

	/* Set WREN bit to enabel MSC write and erase functionality */
	target_mem_write32(t, EFM32_MSC_WRITECTRL, 1);

	while (len) {
		/* Write address of first word in row to erase it */
		target_mem_write32(t, EFM32_MSC_ADDRB, addr);
		target_mem_write32(t, EFM32_MSC_WRITECMD, EFM32_MSC_WRITECMD_LADDRIM);

		/* Issue the erase command */
		target_mem_write32(t, EFM32_MSC_WRITECMD, EFM32_MSC_WRITECMD_ERASEPAGE );

		/* Poll MSC Busy */
		while ((target_mem_read32(t, EFM32_MSC_STATUS) & EFM32_MSC_STATUS_BUSY)) {
			if (target_check_error(t))
				return -1;
		}

		addr += f->blocksize;
		len -= f->blocksize;
	}

	return 0;
}

/**
 * Write flash page by page
 */
static int efm32_flash_write(struct target_flash *f,
			     target_addr dest, const void *src, size_t len)
{
	(void)len;
	target *t = f->t;

	/* Write flashloader */
	target_mem_write(t, SRAM_BASE, efm32_flash_write_stub,
			 sizeof(efm32_flash_write_stub));
	/* Write Buffer */
	target_mem_write(t, STUB_BUFFER_BASE, src, len);
	/* Run flashloader */
	return cortexm_run_stub(t, SRAM_BASE, dest, STUB_BUFFER_BASE, len, 0);

	return 0;
}

/**
 * Uses the MSC ERASEMAIN0 command to erase the entire flash
 */
static bool efm32_cmd_erase_all(target *t)
{
	/* Set WREN bit to enabel MSC write and erase functionality */
	target_mem_write32(t, EFM32_MSC_WRITECTRL, 1);

	/* Unlock mass erase */
	target_mem_write32(t, EFM32_MSC_MASSLOCK, EFM32_MSC_MASSLOCK_LOCKKEY);

	/* Erase operation */
	target_mem_write32(t, EFM32_MSC_WRITECMD, EFM32_MSC_WRITECMD_ERASEMAIN0);

	/* Poll MSC Busy */
	while ((target_mem_read32(t, EFM32_MSC_STATUS) & EFM32_MSC_STATUS_BUSY)) {
		if (target_check_error(t))
			return false;
	}

	/* Relock mass erase */
	target_mem_write32(t, EFM32_MSC_MASSLOCK, 0);

	tc_printf(t, "Erase successful!\n");

	return true;
}

/**
 * Reads the 40-bit unique number
 */
static bool efm32_cmd_serial(target *t)
{
	/* Read the extended unique identifier */
	uint64_t eui = efm32_read_eui(t);

	/* 64 bits of unique number */
	tc_printf(t, "Unique Number: 0x%016llx\n", eui);

	return true;
}
