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
 * detecting the device, providing the memory map and Flash memory
 * programming.
 *
 * EFM32, EZR32 and EFR32 devices are all currently supported through
 * this driver.
 *
 * Tested with:
 * * EZR32LG230 (EZR Leopard Gecko M3)
 * * EFR32BG13P532F512GM32 (EFR Blue Gecko)
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

static bool efm32_cmd_erase_all(target *t, int argc, const char **argv);
static bool efm32_cmd_serial(target *t, int argc, const char **argv);
static bool efm32_cmd_efm_info(target *t, int argc, const char **argv);
static bool efm32_cmd_bootloader(target *t, int argc, const char **argv);

const struct command_s efm32_cmd_list[] = {
	{"erase_mass", (cmd_handler)efm32_cmd_erase_all, "Erase entire flash memory"},
	{"serial", (cmd_handler)efm32_cmd_serial, "Prints unique number"},
	{"efm_info", (cmd_handler)efm32_cmd_efm_info, "Prints information about the device"},
	{"bootloader", (cmd_handler)efm32_cmd_bootloader, "Bootloader status in CLW0"},
	{NULL, NULL, NULL}
};



/* -------------------------------------------------------------------------- */
/* Memory System Controller (MSC) Registers */
/* -------------------------------------------------------------------------- */

#define EFM32_MSC_WRITECTRL(msc)	   	(msc+0x008)
#define EFM32_MSC_WRITECMD(msc)	      	(msc+0x00c)
#define EFM32_MSC_ADDRB(msc)		 	(msc+0x010)
#define EFM32_MSC_WDATA(msc)		 	(msc+0x018)
#define EFM32_MSC_STATUS(msc)			(msc+0x01c)
#define EFM32_MSC_IF(msc)				(msc+0x030)
#define EFM32_MSC_LOCK(msc)				(msc+(msc == 0x400c0000?0x3c:0x40))
#define EFM32_MSC_MASSLOCK(msc)	      	(msc+0x054)

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
#define EFM32_USER_DATA		(EFM32_INFO+0x0000)
#define EFM32_LOCK_BITS		(EFM32_INFO+0x4000)
#define EFM32_V1_DI			(EFM32_INFO+0x8000)
#define EFM32_V2_DI			(EFM32_INFO+0x81B0)


/* -------------------------------------------------------------------------- */
/* Lock Bits (LB) */
/* -------------------------------------------------------------------------- */

#define EFM32_LOCK_BITS_DLW		(EFM32_LOCK_BITS+(4*127))
#define EFM32_LOCK_BITS_ULW		(EFM32_LOCK_BITS+(4*126))
#define EFM32_LOCK_BITS_MLW		(EFM32_LOCK_BITS+(4*125))
#define EFM32_LOCK_BITS_CLW0	(EFM32_LOCK_BITS+(4*122))

#define EFM32_CLW0_BOOTLOADER_ENABLE	(1<<1)
#define EFM32_CLW0_PINRESETSOFT			(1<<2)

/* -------------------------------------------------------------------------- */
/* Device Information (DI) Area - Version 1 V1 */
/* -------------------------------------------------------------------------- */

#define EFM32_V1_DI_CMU_LFRCOCTRL 		(EFM32_V1_DI+0x020)
#define EFM32_V1_DI_CMU_HFRCOCTRL 		(EFM32_V1_DI+0x028)
#define EFM32_V1_DI_CMU_AUXHFRCOCTRL 	(EFM32_V1_DI+0x030)
#define EFM32_V1_DI_ADC0_CAL 		(EFM32_V1_DI+0x040)
#define EFM32_V1_DI_ADC0_BIASPROG 		(EFM32_V1_DI+0x048)
#define EFM32_V1_DI_DAC0_CAL 		(EFM32_V1_DI+0x050)
#define EFM32_V1_DI_DAC0_BIASPROG 		(EFM32_V1_DI+0x058)
#define EFM32_V1_DI_ACMP0_CTRL 		(EFM32_V1_DI+0x060)
#define EFM32_V1_DI_ACMP1_CTRL 		(EFM32_V1_DI+0x068)
#define EFM32_V1_DI_CMU_LCDCTRL 		(EFM32_V1_DI+0x078)
#define EFM32_V1_DI_DAC0_OPACTRL 		(EFM32_V1_DI+0x0A0)
#define EFM32_V1_DI_DAC0_OPAOFFSET 	(EFM32_V1_DI+0x0A8)
#define EFM32_V1_DI_EMU_BUINACT 		(EFM32_V1_DI+0x0B0)
#define EFM32_V1_DI_EMU_BUACT 		(EFM32_V1_DI+0x0B8)
#define EFM32_V1_DI_EMU_BUBODBUVINCAL 	(EFM32_V1_DI+0x0C0)
#define EFM32_V1_DI_EMU_BUBODUNREGCAL 	(EFM32_V1_DI+0x0C8)
#define EFM32_V1_DI_MCM_REV_MIN 		(EFM32_V1_DI+0x1AA)
#define EFM32_V1_DI_MCM_REV_MAJ 		(EFM32_V1_DI+0x1AB)
#define EFM32_V1_DI_RADIO_REV_MIN 		(EFM32_V1_DI+0x1AC)
#define EFM32_V1_DI_RADIO_REV_MAJ 		(EFM32_V1_DI+0x1AD)
#define EFM32_V1_DI_RADIO_OPN 		(EFM32_V1_DI+0x1AE)
#define EFM32_V1_DI_V1_DI_CRC 		(EFM32_V1_DI+0x1B0)
#define EFM32_V1_DI_CAL_TEMP_0 		(EFM32_V1_DI+0x1B2)
#define EFM32_V1_DI_ADC0_CAL_1V25 		(EFM32_V1_DI+0x1B4)
#define EFM32_V1_DI_ADC0_CAL_2V5 		(EFM32_V1_DI+0x1B6)
#define EFM32_V1_DI_ADC0_CAL_VDD 		(EFM32_V1_DI+0x1B8)
#define EFM32_V1_DI_ADC0_CAL_5VDIFF 	(EFM32_V1_DI+0x1BA)
#define EFM32_V1_DI_ADC0_CAL_2XVDD 	(EFM32_V1_DI+0x1BC)
#define EFM32_V1_DI_ADC0_TEMP_0_READ_1V25	(EFM32_V1_DI+0x1BE)
#define EFM32_V1_DI_DAC0_CAL_1V25 		(EFM32_V1_DI+0x1C8)
#define EFM32_V1_DI_DAC0_CAL_2V5 		(EFM32_V1_DI+0x1CC)
#define EFM32_V1_DI_DAC0_CAL_VDD 		(EFM32_V1_DI+0x1D0)
#define EFM32_V1_DI_AUXHFRCO_CALIB_BAND_1 	(EFM32_V1_DI+0x1D4)
#define EFM32_V1_DI_AUXHFRCO_CALIB_BAND_7 	(EFM32_V1_DI+0x1D5)
#define EFM32_V1_DI_AUXHFRCO_CALIB_BAND_11 (EFM32_V1_DI+0x1D6)
#define EFM32_V1_DI_AUXHFRCO_CALIB_BAND_14 (EFM32_V1_DI+0x1D7)
#define EFM32_V1_DI_AUXHFRCO_CALIB_BAND_21 (EFM32_V1_DI+0x1D8)
#define EFM32_V1_DI_AUXHFRCO_CALIB_BAND_28 (EFM32_V1_DI+0x1D9)
#define EFM32_V1_DI_HFRCO_CALIB_BAND_1 	(EFM32_V1_DI+0x1DC)
#define EFM32_V1_DI_HFRCO_CALIB_BAND_7 	(EFM32_V1_DI+0x1DD)
#define EFM32_V1_DI_HFRCO_CALIB_BAND_11 	(EFM32_V1_DI+0x1DE)
#define EFM32_V1_DI_HFRCO_CALIB_BAND_14 	(EFM32_V1_DI+0x1DF)
#define EFM32_V1_DI_HFRCO_CALIB_BAND_21 	(EFM32_V1_DI+0x1E0)
#define EFM32_V1_DI_HFRCO_CALIB_BAND_28 	(EFM32_V1_DI+0x1E1)
#define EFM32_V1_DI_MEM_INFO_PAGE_SIZE 	(EFM32_V1_DI+0x1E7)
#define EFM32_V1_DI_RADIO_ID 		(EFM32_V1_DI+0x1EE)
#define EFM32_V1_DI_EUI64_0 		(EFM32_V1_DI+0x1F0)
#define EFM32_V1_DI_EUI64_1 		(EFM32_V1_DI+0x1F4)
#define EFM32_V1_DI_MEM_INFO_FLASH 	(EFM32_V1_DI+0x1F8)
#define EFM32_V1_DI_MEM_INFO_RAM 		(EFM32_V1_DI+0x1FA)
#define EFM32_V1_DI_PART_NUMBER 		(EFM32_V1_DI+0x1FC)
#define EFM32_V1_DI_PART_FAMILY 		(EFM32_V1_DI+0x1FE)
#define EFM32_V1_DI_PROD_REV 		(EFM32_V1_DI+0x1FF)

/* top 24 bits of eui */
#define EFM32_V1_DI_EUI_SILABS	0x000b57

/* -------------------------------------------------------------------------- */
/* Device Information (DI) Area - Version 2 V2 */
/* -------------------------------------------------------------------------- */

#define EFM32_V2_DI_CAL	(EFM32_V2_DI+0x000)	/* CRC of DI-page and calibration temperature */
#define EFM32_V2_DI_EXTINFO	(EFM32_V2_DI+0x020)	/* External Component description */
#define EFM32_V2_DI_EUI48L	(EFM32_V2_DI+0x028)	/* EUI48 OUI and Unique identifier */
#define EFM32_V2_DI_EUI48H	(EFM32_V2_DI+0x02C)	/* OUI */
#define EFM32_V2_DI_CUSTOMINFO	(EFM32_V2_DI+0x030)	/* Custom information */
#define EFM32_V2_DI_MEMINFO	(EFM32_V2_DI+0x034)	/* Flash page size and misc. chip information */
#define EFM32_V2_DI_UNIQUEL	(EFM32_V2_DI+0x040)	/* Low 32 bits of device unique number */
#define EFM32_V2_DI_UNIQUEH	(EFM32_V2_DI+0x044)	/* High 32 bits of device unique number */
#define EFM32_V2_DI_MSIZE	(EFM32_V2_DI+0x048)	/* Flash and SRAM Memory size in kB */
#define EFM32_V2_DI_PART	(EFM32_V2_DI+0x04C)	/* Part description */
#define EFM32_V2_DI_DEVINFOREV	(EFM32_V2_DI+0x050)	/* Device information page revision */
#define EFM32_V2_DI_EMUTEMP	(EFM32_V2_DI+0x054)	/* EMU Temperature Calibration Information */
#define EFM32_V2_DI_ADC0CAL0	(EFM32_V2_DI+0x060)	/* ADC0 calibration register 0 */
#define EFM32_V2_DI_ADC0CAL1	(EFM32_V2_DI+0x064)	/* ADC0 calibration register 1 */
#define EFM32_V2_DI_ADC0CAL2	(EFM32_V2_DI+0x068)	/* ADC0 calibration register 2 */
#define EFM32_V2_DI_ADC0CAL3	(EFM32_V2_DI+0x06C)	/* ADC0 calibration register 3 */
#define EFM32_V2_DI_HFRCOCAL0	(EFM32_V2_DI+0x080)	/* HFRCO Calibration Register (4 MHz) */
#define EFM32_V2_DI_HFRCOCAL3	(EFM32_V2_DI+0x08C)	/* HFRCO Calibration Register (7 MHz) */
#define EFM32_V2_DI_HFRCOCAL6	(EFM32_V2_DI+0x098)	/* HFRCO Calibration Register (13 MHz) */
#define EFM32_V2_DI_HFRCOCAL7	(EFM32_V2_DI+0x09C)	/* HFRCO Calibration Register (16 MHz)  */
#define EFM32_V2_DI_HFRCOCAL8	(EFM32_V2_DI+0x0A0)
#define EFM32_V2_DI_HFRCOCAL10	(EFM32_V2_DI+0x0A8)
#define EFM32_V2_DI_HFRCOCAL11	(EFM32_V2_DI+0x0AC)
#define EFM32_V2_DI_HFRCOCAL12	(EFM32_V2_DI+0x0B0)
#define EFM32_V2_DI_AUXHFRCOCAL0	(EFM32_V2_DI+0x0E0)
#define EFM32_V2_DI_AUXHFRCOCAL3	(EFM32_V2_DI+0x0EC)
#define EFM32_V2_DI_AUXHFRCOCAL6	(EFM32_V2_DI+0x0F8)
#define EFM32_V2_DI_AUXHFRCOCAL7	(EFM32_V2_DI+0x0FC)
#define EFM32_V2_DI_AUXHFRCOCAL8	(EFM32_V2_DI+0x100)
#define EFM32_V2_DI_AUXHFRCOCAL10	(EFM32_V2_DI+0x108)
#define EFM32_V2_DI_AUXHFRCOCAL11	(EFM32_V2_DI+0x10C)
#define EFM32_V2_DI_AUXHFRCOCAL12	(EFM32_V2_DI+0x110)
#define EFM32_V2_DI_VMONCAL0	(EFM32_V2_DI+0x140)
#define EFM32_V2_DI_VMONCAL1	(EFM32_V2_DI+0x144)	/* VMON Calibration Register 1 */
#define EFM32_V2_DI_VMONCAL2	(EFM32_V2_DI+0x148)	/* VMON Calibration Register 2 */
#define EFM32_V2_DI_IDAC0CAL0	(EFM32_V2_DI+0x158)	/* IDAC0 Calibration Register 0 */
#define EFM32_V2_DI_IDAC0CAL1	(EFM32_V2_DI+0x15C)	/* IDAC0 Calibration Register 1 */
#define EFM32_V2_DI_DCDCLNVCTRL0	(EFM32_V2_DI+0x168)	/* DCDC Low-noise VREF Trim Register 0 */
#define EFM32_V2_DI_DCDCLPVCTRL0	(EFM32_V2_DI+0x16C)	/* DCDC Low-power VREF Trim Register 0 */
#define EFM32_V2_DI_DCDCLPVCTRL1	(EFM32_V2_DI+0x170)	/* DCDC Low-power VREF Trim Register 1 */
#define EFM32_V2_DI_DCDCLPVCTRL2	(EFM32_V2_DI+0x174)	/* DCDC Low-power VREF Trim Register 2 */
#define EFM32_V2_DI_DCDCLPVCTRL3	(EFM32_V2_DI+0x178)	/* DCDC Low-power VREF Trim Register 3 */
#define EFM32_V2_DI_DCDCLPCMPHYSSEL0	(EFM32_V2_DI+0x17C)	/* DCDC LPCMPHYSSEL Trim Register 0 */
#define EFM32_V2_DI_DCDCLPCMPHYSSEL1	(EFM32_V2_DI+0x180)	/* DCDC LPCMPHYSSEL Trim Register 1 */
#define EFM32_V2_DI_VDAC0MAINCAL	(EFM32_V2_DI+0x184)	/* VDAC0 Cals for Main Path */
#define EFM32_V2_DI_VDAC0ALTCAL	(EFM32_V2_DI+0x188)	/* VDAC0 Cals for Alternate Path */
#define EFM32_V2_DI_VDAC0CH1CAL	(EFM32_V2_DI+0x18C)	/* VDAC0 CH1 Error Cal */
#define EFM32_V2_DI_OPA0CAL0	(EFM32_V2_DI+0x190)	/* OPA0 Calibration Register for DRIVESTRENGTH 0, INCBW=1 */
#define EFM32_V2_DI_OPA0CAL1	(EFM32_V2_DI+0x194)	/* OPA0 Calibration Register for DRIVESTRENGTH 1, INCBW=1 */
#define EFM32_V2_DI_OPA0CAL2	(EFM32_V2_DI+0x198)	/* OPA0 Calibration Register for DRIVESTRENGTH 2, INCBW=1 */
#define EFM32_V2_DI_OPA0CAL3	(EFM32_V2_DI+0x19C)	/* OPA0 Calibration Register for DRIVESTRENGTH 3, INCBW=1 */
#define EFM32_V2_DI_OPA1CAL0	(EFM32_V2_DI+0x1A0)	/* OPA1 Calibration Register for DRIVESTRENGTH 0, INCBW=1 */
#define EFM32_V2_DI_OPA1CAL1	(EFM32_V2_DI+0x1A4)	/* OPA1 Calibration Register for DRIVESTRENGTH 1, INCBW=1 */
#define EFM32_V2_DI_OPA1CAL2	(EFM32_V2_DI+0x1A8)
#define EFM32_V2_DI_OPA1CAL3	(EFM32_V2_DI+0x1AC)
#define EFM32_V2_DI_OPA2CAL0	(EFM32_V2_DI+0x1B0)
#define EFM32_V2_DI_OPA2CAL1	(EFM32_V2_DI+0x1B4)
#define EFM32_V2_DI_OPA2CAL2	(EFM32_V2_DI+0x1B8)
#define EFM32_V2_DI_OPA2CAL3	(EFM32_V2_DI+0x1BC)
#define EFM32_V2_DI_CSENGAINCAL	(EFM32_V2_DI+0x1C0)
#define EFM32_V2_DI_OPA0CAL4	(EFM32_V2_DI+0x1D0)
#define EFM32_V2_DI_OPA0CAL5	(EFM32_V2_DI+0x1D4)
#define EFM32_V2_DI_OPA0CAL6	(EFM32_V2_DI+0x1D8)
#define EFM32_V2_DI_OPA0CAL7	(EFM32_V2_DI+0x1DC)
#define EFM32_V2_DI_OPA1CAL4	(EFM32_V2_DI+0x1E0)
#define EFM32_V2_DI_OPA1CAL5	(EFM32_V2_DI+0x1E4)
#define EFM32_V2_DI_OPA1CAL6	(EFM32_V2_DI+0x1E8)
#define EFM32_V2_DI_OPA1CAL7	(EFM32_V2_DI+0x1EC)
#define EFM32_V2_DI_OPA2CAL4	(EFM32_V2_DI+0x1F0)
#define EFM32_V2_DI_OPA2CAL5	(EFM32_V2_DI+0x1F4)
#define EFM32_V2_DI_OPA2CAL6	(EFM32_V2_DI+0x1F8)	/* OPA2 Calibration Register for DRIVESTRENGTH 2, INCBW=0 */
#define EFM32_V2_DI_OPA2CAL7	(EFM32_V2_DI+0x1FC)	/* OPA2 Calibration Register for DRIVESTRENGTH 3, INCBW=0 */

/* top 24 bits of eui */
#define EFM32_V2_DI_EUI_ENERGYMICRO	0xd0cf5e

/* -------------------------------------------------------------------------- */
/* Constants */
/* -------------------------------------------------------------------------- */

typedef struct efm32_device_t {
	uint16_t family_id;	/* Family for device matching */
	char* name;			/* Friendly device family name */
	uint32_t flash_page_size;	/* Flash page size */
	uint32_t msc_addr;			/* MSC Address */
	bool has_radio;		   /* Indicates a device has attached radio */
	uint32_t user_data_size;	/* User Data (UD) region size */
	uint32_t bootloader_size;	/* Bootloader (BL) region size (may be 0 for no BL region) */
	char* description;	   /* Human-readable description */
} efm32_device_t;

efm32_device_t const efm32_devices[] = {
	/*  First gen micros */
	{71, "EFM32G", 512, 0x400c0000, false, 512, 0, "Gecko"},
	{72, "EFM32GG", 2048, 0x400c0000, false, 4096, 0, "Giant Gecko"},
	{73, "EFM32TG", 512, 0x400c0000, false, 512, 0, "Tiny Gecko"},
	{74, "EFM32LG", 2048, 0x400c0000, false, 2048, 0, "Leopard Gecko"},
	{75, "EFM32WG", 2048, 0x400c0000, false, 2048, 0, "Wonder Gecko"},
	{76, "EFM32ZG", 1024, 0x400c0000, false, 1024, 0, "Zero Gecko"},
	{77, "EFM32HG", 1024, 0x400c0000, false, 1024, 0, "Happy Gecko"},
	/*  First (1.5) gen micro + radio */
	{120, "EZR32WG", 2048, 0x400c0000, true, 2048, 0, "EZR Wonder Gecko"},
	{121, "EZR32LG", 2048, 0x400c0000, true, 2048, 0, "EZR Leopard Gecko"},
	{122, "EZR32HG", 1024, 0x400c0000, true, 1024, 0, "EZR Happy Gecko"},
	/*  Second gen micros */
	{81, "EFM32PG1B", 2048, 0x400e0000, false, 2048, 10240, "Pearl Gecko"},
	{83, "EFM32JG1B", 2048, 0x400e0000, false, 2048, 10240, "Jade Gecko"},
	{85, "EFM32PG12B", 2048, 0x400e0000, false, 2048, 32768,"Pearl Gecko 12"},
	{87, "EFM32JG12B", 2048, 0x400e0000, false, 2048, 32768, "Jade Gecko 12"},
	/*  Second (2.5) gen micros, with re-located MSC */
	{100, "EFM32GG11B", 4096, 0x40000000, false, 4096, 32768, "Giant Gecko 11"},
	{103, "EFM32TG11B", 2048, 0x40000000, false, 2048, 18432, "Tiny Gecko 11"},
	{106, "EFM32GG12B", 2048, 0x40000000, false, 2048, 32768, "Giant Gecko 12"},
	/*  Second gen devices micro + radio */
	{16, "EFR32MG1P", 2048, 0x400e0000, true, 2048, 10240, "Mighty Gecko"},
	{17, "EFR32MG1B", 2048, 0x400e0000, true, 2048, 10240, "Mighty Gecko"},
	{18, "EFR32MG1V", 2048, 0x400e0000, true, 2048, 10240, "Mighty Gecko"},
	{19, "EFR32BG1P", 2048, 0x400e0000, true, 2048, 10240, "Blue Gecko"},
	{20, "EFR32BG1B", 2048, 0x400e0000, true, 2048, 10240, "Blue Gecko"},
	{21, "EFR32BG1V", 2048, 0x400e0000, true, 2048, 10240, "Blue Gecko"},
	{25, "EFR32FG1P", 2048, 0x400e0000, true, 2048, 10240, "Flex Gecko"},
	{26, "EFR32FG1B", 2048, 0x400e0000, true, 2048, 10240, "Flex Gecko"},
	{27, "EFR32FG1V", 2048, 0x400e0000, true, 2048, 10240, "Flex Gecko"},
	{28, "EFR32MG12P", 2048, 0x400e0000, true, 2048, 32768, "Mighty Gecko"},
	{29, "EFR32MG12B", 2048, 0x400e0000, true, 2048, 32768, "Mighty Gecko"},
	{30, "EFR32MG12V", 2048, 0x400e0000, true, 2048, 32768, "Mighty Gecko"},
	{31, "EFR32BG12P", 2048, 0x400e0000, true, 2048, 32768, "Blue Gecko"},
	{32, "EFR32BG12B", 2048, 0x400e0000, true, 2048, 32768, "Blue Gecko"},
	{33, "EFR32BG12V", 2048, 0x400e0000, true, 2048, 32768, "Blue Gecko"},
	{37, "EFR32FG12P", 2048, 0x400e0000, true, 2048, 32768, "Flex Gecko"},
	{38, "EFR32FG12B", 2048, 0x400e0000, true, 2048, 32768, "Flex Gecko"},
	{39, "EFR32FG12V", 2048, 0x400e0000, true, 2048, 32768, "Flex Gecko"},
	{40, "EFR32MG13P", 2048, 0x400e0000, true, 2048, 16384, "Mighty Gecko"},
	{41, "EFR32MG13B", 2048, 0x400e0000, true, 2048, 16384, "Mighty Gecko"},
	{42, "EFR32MG13V", 2048, 0x400e0000, true, 2048, 16384, "Mighty Gecko"},
	{43, "EFR32BG13P", 2048, 0x400e0000, true, 2048, 16384, "Blue Gecko"},
	{44, "EFR32BG13B", 2048, 0x400e0000, true, 2048, 16384, "Blue Gecko"},
	{45, "EFR32BG13V", 2048, 0x400e0000, true, 2048, 16384, "Blue Gecko"},
	{49, "EFR32FG13P", 2048, 0x400e0000, true, 2048, 16384, "Flex Gecko"},
	{50, "EFR32FG13B", 2048, 0x400e0000, true, 2048, 16384, "Flex Gecko"},
	{51, "EFR32FG13V", 2048, 0x400e0000, true, 2048, 16384, "Flex Gecko"},
	{52, "EFR32MG14P", 2048, 0x400e0000, true, 2048, 16384, "Mighty Gecko"},
	{53, "EFR32MG14B", 2048, 0x400e0000, true, 2048, 16384, "Mighty Gecko"},
	{54, "EFR32MG14V", 2048, 0x400e0000, true, 2048, 16384, "Mighty Gecko"},
	{55, "EFR32BG14P", 2048, 0x400e0000, true, 2048, 16384, "Blue Gecko"},
	{56, "EFR32BG14B", 2048, 0x400e0000, true, 2048, 16384, "Blue Gecko"},
	{57, "EFR32BG14V", 2048, 0x400e0000, true, 2048, 16384, "Blue Gecko"},
	{61, "EFR32FG14P", 2048, 0x400e0000, true, 2048, 16384, "Flex Gecko"},
	{62, "EFR32FG14B", 2048, 0x400e0000, true, 2048, 16384, "Flex Gecko"},
	{63, "EFR32FG14V", 2048, 0x400e0000, true, 2048, 16384, "Flex Gecko"},
};


/* miscchip */
typedef struct efm32_v2_di_miscchip_t {
	uint8_t pincount;
	uint8_t pkgtype;
	uint8_t tempgrade;
} efm32_v2_di_miscchip_t;

/* pkgtype */
typedef struct efm32_v2_di_pkgtype_t {
	uint8_t pkgtype;
	char* name;
} efm32_v2_di_pkgtype_t;

efm32_v2_di_pkgtype_t const efm32_v2_di_pkgtypes[] = {
	{74, "WLCSP"},	/* WLCSP package */
	{76, "BGA"},	/* BGA package */
	{77, "QFN"},	/* QFN package */
	{81, "QFxP"},	/* QFP package */
};

/* tempgrade */
typedef struct efm32_v2_di_tempgrade_t {
	uint8_t tempgrade;
	char* name;
} efm32_v2_di_tempgrade_t;

efm32_v2_di_tempgrade_t const efm32_v2_di_tempgrades[] = {
	{0, "-40 to 85degC"},
	{1, "-40 to 125degC"},
	{2, "-40 to 105degC"},
	{3, "0 to 70degC"}
};

/* -------------------------------------------------------------------------- */
/* Helper functions - Version 1 V1 */
/* -------------------------------------------------------------------------- */

/**
 * Reads the EFM32 Extended Unique Identifier EUI64 (V1)
 */
static uint64_t efm32_v1_read_eui64(target *t)
{
	uint64_t eui;

	eui  = (uint64_t)target_mem_read32(t, EFM32_V1_DI_EUI64_1) << 32;
	eui |= (uint64_t)target_mem_read32(t, EFM32_V1_DI_EUI64_0) <<  0;

	return eui;
}

#if 0
/**
 * Reads the EFM32 Extended Unique Identifier EUI48 (V2)
 */
static uint64_t efm32_v2_read_eui48(target *t)
{
	uint64_t eui;

	eui  = (uint64_t)target_mem_read32(t, EFM32_V2_DI_EUI48H) << 32;
	eui |= (uint64_t)target_mem_read32(t, EFM32_V2_DI_EUI48L) <<  0;

	return eui;
}
#endif
/**
 * Reads the Unique Number (DI V2 only)
 */
static uint64_t efm32_v2_read_unique(target *t, uint8_t di_version)
{
	uint64_t unique;
	switch (di_version) {
	case 2:
		unique  = (uint64_t)target_mem_read32(t, EFM32_V2_DI_UNIQUEH) << 32;
		unique |= (uint64_t)target_mem_read32(t, EFM32_V2_DI_UNIQUEL) << 0;
		return unique;
	default:
		return 0;
	}
}

/**
 * Reads the EFM32 flash size in kiB
 */
static uint16_t efm32_read_flash_size(target *t, uint8_t di_version)
{
	switch (di_version) {
	case 1:
		return target_mem_read16(t, EFM32_V1_DI_MEM_INFO_FLASH);
	case 2:
		return (target_mem_read32(t, EFM32_V2_DI_MSIZE) >> 0) & 0xFFFF;
	default:
		return 0;
	}
}
/**
 * Reads the EFM32 RAM size in kiB
 */
static uint16_t efm32_read_ram_size(target *t, uint8_t di_version)
{
	switch (di_version) {
	case 1:
		return target_mem_read16(t, EFM32_V1_DI_MEM_INFO_RAM);
	case 2:
		return (target_mem_read32(t, EFM32_V2_DI_MSIZE) >> 16) & 0xFFFF;
	default:
		return 0;
	}
}
/**
 * Reads the EFM32 reported flash page size in bytes.  Note: This
 * driver ignores this value, and uses a conservative hard-coded
 * value. There are errata on the value reported by the EFM32
 * eg. DI_101
 */
static uint32_t efm32_flash_page_size(target *t, uint8_t di_version)
{
	uint8_t mem_info_page_size;

	switch (di_version) {
	case 1:
		mem_info_page_size = target_mem_read8(t, EFM32_V1_DI_MEM_INFO_PAGE_SIZE);
		break;
	case 2:
		mem_info_page_size = (target_mem_read32(t, EFM32_V2_DI_MEMINFO) >> 24) & 0xFF;
		break;
	default:
		return 0;
	}

	return (1 << (mem_info_page_size + 10)); /* uint8_t ovf here */
}


/**
 * Reads the EFM32 Part Number
 */
static uint16_t efm32_read_part_number(target *t, uint8_t di_version)
{
	switch (di_version) {
	case 1:
		return target_mem_read8(t, EFM32_V1_DI_PART_NUMBER);
	case 2:
		return target_mem_read32(t, EFM32_V2_DI_PART) & 0xFFFF;
	default:
		return 0;
	}
}
/**
 * Reads the EFM32 Part Family
 */
static uint8_t efm32_read_part_family(target *t, uint8_t di_version)
{
	switch (di_version) {
	case 1:
		return target_mem_read8(t, EFM32_V1_DI_PART_FAMILY);
	case 2:
		return (target_mem_read32(t, EFM32_V2_DI_PART) >> 16) & 0xFF;
	default:
		return 0;
	}
}
/**
 * Reads the EFM32 Radio part number (EZR parts with V1 DI only)
 */
static uint16_t efm32_read_radio_part_number(target *t, uint8_t di_version)
{
	switch (di_version) {
	case 1:
		return target_mem_read16(t, EFM32_V1_DI_RADIO_OPN);
	default:
		return 0;
	}
}

/**
 * Reads the EFM32 Misc. Chip definitions
 */
static efm32_v2_di_miscchip_t efm32_v2_read_miscchip(target *t, uint8_t di_version)
{
	uint32_t meminfo;
	efm32_v2_di_miscchip_t miscchip;
	memset(&miscchip, 0, sizeof(efm32_v2_di_miscchip_t) / sizeof(char));

	switch (di_version) {
	case 2:
		meminfo = target_mem_read32(t, EFM32_V2_DI_MEMINFO);
		miscchip.pincount  = (meminfo >> 16) & 0xFF;
		miscchip.pkgtype   = (meminfo >>  8) & 0xFF;
		miscchip.tempgrade = (meminfo >>  0) & 0xFF;
	}

	return miscchip;
}

/* -------------------------------------------------------------------------- */
/* Shared Functions */
/* -------------------------------------------------------------------------- */


static void efm32_add_flash(target *t, target_addr addr, size_t length,
			    size_t page_size)
{
	struct target_flash *f = calloc(1, sizeof(*f));
	if (!f) {			/* calloc failed: heap exhaustion */
		DEBUG_WARN("calloc: failed in %s\n", __func__);
		return;
	}

	f->start = addr;
	f->length = length;
	f->blocksize = page_size;
	f->erase = efm32_flash_erase;
	f->write = efm32_flash_write;
	f->buf_size = page_size;
	target_add_flash(t, f);
}

/**
 * Lookup device
 */
static size_t efm32_lookup_device_index(target *t, uint8_t di_version)
{
	uint8_t part_family = efm32_read_part_family(t, di_version);

	/* Search for family */
	for (size_t i = 0; i < (sizeof(efm32_devices) / sizeof(efm32_device_t)); i++) {
		if (efm32_devices[i].family_id == part_family) {
			return i;
		}
	}
	/* Unknown family */
	return 9999;
}

static efm32_device_t const * efm32_get_device(size_t index)
{
	if (index >= (sizeof(efm32_devices) / sizeof(efm32_device_t))) {
		return NULL;
	}
	return &efm32_devices[index];
}

/**
 * Probe
 */
static char efm32_variant_string[60];
bool efm32_probe(target *t)
{
	uint8_t di_version = 1;

	/* Read the IDCODE register from the SW-DP */
	ADIv5_AP_t *ap = cortexm_ap(t);
	uint32_t ap_idcode = ap->dp->idcode;

	/* Check the idcode. See AN0062 Section 2.2 */
	if (ap_idcode == 0x2BA01477) {
		/* Cortex M3, Cortex M4 */
	} else if (ap_idcode == 0x0BC11477) {
		/* Cortex M0+ */
	} else {
		return false;
	}

	/* Check the OUI in the EUI is silabs or energymicro.
	 * Use this to identify the Device Identification (DI) version */
	uint64_t oui24 = ((efm32_v1_read_eui64(t) >> 40) & 0xFFFFFF);
	if (oui24 == EFM32_V1_DI_EUI_SILABS) {
		/* Device Identification (DI) version 1 */
		di_version = 1;
	} else if (oui24 == EFM32_V2_DI_EUI_ENERGYMICRO) {
		/* Device Identification (DI) version 2 */
		di_version = 2;
	} else {
		/* Unknown OUI - assume version 1 */
		di_version = 1;
	}

	/* Read the part family, and reject if unknown */
	size_t device_index  = efm32_lookup_device_index(t, di_version);
	if (device_index > (127-32)) {
		/* unknown device family */
		return false;
	}
	efm32_device_t const* device = &efm32_devices[device_index];
	if (device == NULL) {
		return false;
	}

	uint16_t part_number = efm32_read_part_number(t, di_version);

	/* Read memory sizes, convert to bytes */
	uint16_t flash_kib  = efm32_read_flash_size(t, di_version);
	uint32_t flash_size = flash_kib * 0x400;
	uint16_t ram_kib    = efm32_read_ram_size(t, di_version);
	uint32_t ram_size   = ram_kib   * 0x400;
	uint32_t flash_page_size = device->flash_page_size;

	snprintf(efm32_variant_string, sizeof(efm32_variant_string), "%c\b%c\b%s %d F%d %s",
			di_version + 48, (uint8_t)device_index + 32,
			device->name, part_number, flash_kib, device->description);

	/* Setup Target */
	t->target_options |= CORTEXM_TOPT_INHIBIT_SRST;
	t->driver = efm32_variant_string;
	tc_printf(t, "flash size %d page size %d\n", flash_size, flash_page_size);
	target_add_ram (t, SRAM_BASE, ram_size);
	efm32_add_flash(t, 0x00000000, flash_size, flash_page_size);
	if (device->user_data_size) { /* optional User Data (UD) section */
		efm32_add_flash(t, 0x0fe00000, device->user_data_size, flash_page_size);
	}
	if (device->bootloader_size) { /* optional Bootloader (BL) section */
		efm32_add_flash(t, 0x0fe10000, device->bootloader_size, flash_page_size);
	}
	target_add_commands(t, efm32_cmd_list, "EFM32");

	return true;
}

/**
 * Erase flash row by row
 */
static int efm32_flash_erase(struct target_flash *f, target_addr addr, size_t len)
{
	target *t = f->t;
	efm32_device_t const* device = efm32_get_device(t->driver[2] - 32);
	if (device == NULL) {
		return true;
	}
	uint32_t msc = device->msc_addr;

	/* Unlock */
	target_mem_write32(t, EFM32_MSC_LOCK(msc), EFM32_MSC_LOCK_LOCKKEY);

	/* Set WREN bit to enabel MSC write and erase functionality */
	target_mem_write32(t, EFM32_MSC_WRITECTRL(msc), 1);

	while (len) {
		/* Write address of first word in row to erase it */
		target_mem_write32(t, EFM32_MSC_ADDRB(msc), addr);
		target_mem_write32(t, EFM32_MSC_WRITECMD(msc), EFM32_MSC_WRITECMD_LADDRIM);

		/* Issue the erase command */
		target_mem_write32(t, EFM32_MSC_WRITECMD(msc), EFM32_MSC_WRITECMD_ERASEPAGE );

		/* Poll MSC Busy */
		while ((target_mem_read32(t, EFM32_MSC_STATUS(msc)) & EFM32_MSC_STATUS_BUSY)) {
			if (target_check_error(t))
				return -1;
		}

		addr += f->blocksize;
		if (len > f->blocksize)
			len -= f->blocksize;
		else
			len = 0;
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
	efm32_device_t const* device = efm32_get_device(t->driver[2] - 32);
	if (device == NULL) {
		return true;
	}
	/* Write flashloader */
	target_mem_write(t, SRAM_BASE, efm32_flash_write_stub,
			 sizeof(efm32_flash_write_stub));
	/* Write Buffer */
	target_mem_write(t, STUB_BUFFER_BASE, src, len);
	/* Run flashloader */
	int ret = cortexm_run_stub(t, SRAM_BASE, dest, STUB_BUFFER_BASE, len,
							   device->msc_addr);

#ifdef ENABLE_DEBUG
	/* Check the MSC_IF */
	uint32_t msc = device->msc_addr;
	uint32_t msc_if = target_mem_read32(t, EFM32_MSC_IF(msc));
	DEBUG_INFO("EFM32: Flash write done MSC_IF=%08"PRIx32"\n", msc_if);
#endif
	return ret;
}

/**
 * Uses the MSC ERASEMAIN0 command to erase the entire flash
 */
static bool efm32_cmd_erase_all(target *t, int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	efm32_device_t const* device = efm32_get_device(t->driver[2] - 32);
	if (device == NULL) {
		return true;
	}
	uint32_t msc = device->msc_addr;

	/* Set WREN bit to enabel MSC write and erase functionality */
	target_mem_write32(t, EFM32_MSC_WRITECTRL(msc), 1);

	/* Unlock mass erase */
	target_mem_write32(t, EFM32_MSC_MASSLOCK(msc), EFM32_MSC_MASSLOCK_LOCKKEY);

	/* Erase operation */
	target_mem_write32(t, EFM32_MSC_WRITECMD(msc), EFM32_MSC_WRITECMD_ERASEMAIN0);

	/* Poll MSC Busy */
	while ((target_mem_read32(t, EFM32_MSC_STATUS(msc)) & EFM32_MSC_STATUS_BUSY)) {
		if (target_check_error(t))
			return false;
	}

	/* Relock mass erase */
	target_mem_write32(t, EFM32_MSC_MASSLOCK(msc), 0);

	tc_printf(t, "Erase successful!\n");

	return true;
}

/**
 * Reads the 40-bit unique number
 */
static bool efm32_cmd_serial(target *t, int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	uint64_t unique = 0;
	uint8_t di_version = t->driver[0] - 48; /* di version hidden in driver str */

	switch (di_version) {
	case 1:
		/* Read the eui */
		unique = efm32_v1_read_eui64(t);
		break;
	case 2:
		/* Read unique number */
		unique = efm32_v2_read_unique(t, di_version);
		break;
	}

	tc_printf(t, "Unique Number: 0x%016llx\n", unique);

	return true;
}
/**
 * Prints various information we know about the device
 */
static bool efm32_cmd_efm_info(target *t, int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	uint8_t di_version  = t->driver[0] - 48; /* hidden in driver str */

	switch (di_version) {
	case 1:
		tc_printf(t, "DI version 1 (silabs remix?) base 0x%08x\n\n", EFM32_V1_DI);
		break;
	case 2:
		tc_printf(t, "DI version 2 (energy micro remix?) base 0x%08x\n\n", EFM32_V2_DI);
		break;
	default:
		tc_printf(t, "Bad DI version %d! This driver doesn't know about this DI version\n", di_version);
		return true;			/* finish */
	}

	/* lookup device and part number */
	efm32_device_t const* device = efm32_get_device(t->driver[2] - 32);
	if (device == NULL) {
		return true;
	}
	uint16_t part_number = efm32_read_part_number(t, di_version);

	/* Read memory sizes, convert to bytes */
	uint16_t flash_kib  = efm32_read_flash_size(t, di_version);
	uint16_t ram_kib    = efm32_read_ram_size(t, di_version);
	uint32_t flash_page_size_reported = efm32_flash_page_size(t, di_version);
	uint32_t flash_page_size          = device->flash_page_size;

	tc_printf(t, "%s %d F%d = %s %dkiB flash, %dkiB ram\n",
			  device->name,    part_number, flash_kib,
			  device->description, flash_kib, ram_kib);
	tc_printf(t, "Device says flash page size is %d bytes, we're using %d bytes\n",
			  flash_page_size_reported, flash_page_size);
	if (flash_page_size_reported < flash_page_size) {
		tc_printf(t, "This is bad, flash writes may be corrupted\n");
	}
	tc_printf(t, "\n");

	if (di_version == 2) {
		efm32_v2_di_miscchip_t miscchip = efm32_v2_read_miscchip(t, di_version);
		efm32_v2_di_pkgtype_t const* pkgtype = NULL;
		efm32_v2_di_tempgrade_t const* tempgrade;

		for (size_t i = 0; i < (sizeof(efm32_v2_di_pkgtypes) /
								sizeof(efm32_v2_di_pkgtype_t)); i++) {
			if (efm32_v2_di_pkgtypes[i].pkgtype == miscchip.pkgtype) {
				pkgtype = &efm32_v2_di_pkgtypes[i];
			}
		}
		for (size_t i = 0; i < (sizeof(efm32_v2_di_tempgrades) /
								sizeof(efm32_v2_di_tempgrade_t)); i++) {
			if (efm32_v2_di_tempgrades[i].tempgrade == miscchip.tempgrade) {
				tempgrade = &efm32_v2_di_tempgrades[i];
			}
		}

		tc_printf(t, "Package %s %d pins\n", pkgtype->name, miscchip.pincount);
		tc_printf(t, "Temperature grade %s\n", tempgrade->name);
		tc_printf(t, "\n");
	}

	if (di_version == 1 && device->has_radio) {
		uint16_t radio_number = efm32_read_radio_part_number(t, di_version); /* on-chip radio */
		tc_printf(t, "Radio si%d\n", radio_number);
		tc_printf(t, "\n");
	}

	return true;
}

/**
 * Bootloader status in CLW0, if applicable.
 *
 * This is a bit in flash, so it is possible to clear it only once.
 */
static bool efm32_cmd_bootloader(target *t, int argc, const char **argv)
{
	/* lookup device and part number */
	efm32_device_t const* device = efm32_get_device(t->driver[2] - 32);
	if (device == NULL) {
		return true;
	}
	uint32_t msc = device->msc_addr;

	if (device->bootloader_size == 0) {
		tc_printf(t, "This device has no bootloader.\n");
		return false;
	}

	uint32_t clw0 = target_mem_read32(t, EFM32_LOCK_BITS_CLW0);
	bool bootloader_status = (clw0 & EFM32_CLW0_BOOTLOADER_ENABLE)?1:0;

	if (argc == 1) {
		tc_printf(t, "Bootloader %s\n",
			  bootloader_status ? "enabled" : "disabled");
		return true;
	} else {
		bootloader_status = (argv[1][0] == 'e');
	}

	/* Modify bootloader enable bit */
	clw0 &= bootloader_status?~0:~EFM32_CLW0_BOOTLOADER_ENABLE;

	/* Unlock */
	target_mem_write32(t, EFM32_MSC_LOCK(msc), EFM32_MSC_LOCK_LOCKKEY);

	/* Set WREN bit to enabel MSC write and erase functionality */
	target_mem_write32(t, EFM32_MSC_WRITECTRL(msc), 1);

	/* Write address of CLW0 */
	target_mem_write32(t, EFM32_MSC_ADDRB(msc), EFM32_LOCK_BITS_CLW0);
	target_mem_write32(t, EFM32_MSC_WRITECMD(msc), EFM32_MSC_WRITECMD_LADDRIM);

	/* Issue the write */
	target_mem_write32(t, EFM32_MSC_WDATA(msc), clw0);
	target_mem_write32(t, EFM32_MSC_WRITECMD(msc), EFM32_MSC_WRITECMD_WRITEONCE);

	/* Poll MSC Busy */
	while ((target_mem_read32(t, EFM32_MSC_STATUS(msc)) & EFM32_MSC_STATUS_BUSY)) {
		if (target_check_error(t))
			return false;
	}

	return true;
}


/*** Authentication Access Port (AAP) **/

/* There's an additional AP on the SW-DP is accessable when the part
 * is almost entirely locked.
 *
 * The AAP can be used to issue a DEVICEERASE command, which erases:
 * * Flash
 * * SRAM
 * * Lock Bit (LB) page
 *
 * It does _not_ erase:
 * * User Data (UD) page
 * * Bootloader (BL) if present
 *
 * Once the DEVICEERASE command has completed, the main AP will be
 * accessable again. If the device has a bootloader, it will attempt
 * to boot from this. If you have just unlocked the device the
 * bootloader could be anything (even garbage, if the bootloader
 * wasn't used before the DEVICEERASE). Therefore you may want to
 * connect under srst and use the bootloader command to disable it.
 *
 * It is possible to lock the AAP itself by clearing the AAP Lock Word
 * (ALW). In this case the part is unrecoverable (unless you glitch
 * it, please try glitching it).
 */

#include "adiv5.h"

/* IDR revision [31:28] jes106 [27:17] class [16:13] res [12:8]
 * variant [7:4] type [3:0] */
#define EFM32_AAP_IDR      0x06E60001
#define EFM32_APP_IDR_MASK 0x0FFFFF0F

#define AAP_CMD      ADIV5_AP_REG(0x00)
#define AAP_CMDKEY   ADIV5_AP_REG(0x04)
#define AAP_STATUS   ADIV5_AP_REG(0x08)

#define AAP_STATUS_LOCKED    (1 << 1)
#define AAP_STATUS_ERASEBUSY (1 << 0)

#define CMDKEY 0xCFACC118

static bool efm32_aap_cmd_device_erase(target *t, int argc, const char **argv);

const struct command_s efm32_aap_cmd_list[] = {
	{"erase_mass", (cmd_handler)efm32_aap_cmd_device_erase, "Erase entire flash memory"},
	{NULL, NULL, NULL}
};

static bool nop_function(void)
{
	return true;
}

/**
 * AAP Probe
 */
char aap_driver_string[42];
void efm32_aap_probe(ADIv5_AP_t *ap)
{
	if ((ap->idr & EFM32_APP_IDR_MASK) == EFM32_AAP_IDR) {
		/* It's an EFM32 AAP! */
		DEBUG_INFO("EFM32: Found EFM32 AAP\n");
	} else {
		return;
	}

	/* Both revsion 1 and revision 2 devices seen in the wild */
	uint16_t aap_revision = (uint16_t)((ap->idr & 0xF0000000) >> 28);

	/* New target */
	target *t = target_new();
	adiv5_ap_ref(ap);
	t->priv = ap;
	t->priv_free = (void*)adiv5_ap_unref;

	//efm32_aap_cmd_device_erase(t);

	/* Read status */
	DEBUG_INFO("EFM32: AAP STATUS=%08"PRIx32"\n", adiv5_ap_read(ap, AAP_STATUS));

	sprintf(aap_driver_string,
			"EFM32 Authentication Access Port rev.%d",
			aap_revision);
	t->driver = aap_driver_string;
	t->attach = (void*)nop_function;
	t->detach = (void*)nop_function;
	t->check_error = (void*)nop_function;
	t->mem_read = (void*)nop_function;
	t->mem_write = (void*)nop_function;
	t->regs_size = 4;
	t->regs_read = (void*)nop_function;
	t->regs_write = (void*)nop_function;
	t->reset = (void*)nop_function;
	t->halt_request = (void*)nop_function;
	t->halt_resume = (void*)nop_function;

	target_add_commands(t, efm32_aap_cmd_list, t->driver);
}

static bool efm32_aap_cmd_device_erase(target *t, int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	ADIv5_AP_t *ap = t->priv;
	uint32_t status;

	/* Read status */
	status = adiv5_ap_read(ap, AAP_STATUS);
	DEBUG_INFO("EFM32: AAP STATUS=%08"PRIx32"\n", status);

	if (status & AAP_STATUS_ERASEBUSY) {
		DEBUG_WARN("EFM32: AAP Erase in progress\n");
		DEBUG_WARN("EFM32: -> ABORT\n");
		return false;
	}

	DEBUG_INFO("EFM32: Issuing DEVICEERASE...\n");
	adiv5_ap_write(ap, AAP_CMDKEY, CMDKEY);
	adiv5_ap_write(ap, AAP_CMD, 1);

	/* Read until 0, probably should have a timeout here... */
	do {
		status = adiv5_ap_read(ap, AAP_STATUS);
	} while (status & AAP_STATUS_ERASEBUSY);

	/* Read status */
	status = adiv5_ap_read(ap, AAP_STATUS);
	DEBUG_INFO("EFM32: AAP STATUS=%08"PRIx32"\n", status);

	return true;
}
