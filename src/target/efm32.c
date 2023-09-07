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

/*
 * This file implements EFM32 target specific functions for
 * detecting the device, providing the memory map and Flash memory
 * programming.
 *
 * EFM32, EZR32 and EFR32 devices are all currently supported through
 * this driver.
 *
 * Tested with:
 * * EZR32LG230 (EZR Leopard Gecko M3)
 * * EFR32BG13P532F512GM32 (EFR Blue Gecko)
 */

/* Refer to the family reference manuals:
 *
 * Also refer to AN0062 "Programming Internal Flash Over the Serial Wire Debug Interface"
 * http://www.silabs.com/Support%20Documents/TechnicalDocs/an0062.pdf
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"
#include "adiv5.h"

#define SRAM_BASE        0x20000000U
#define STUB_BUFFER_BASE ALIGN(SRAM_BASE + sizeof(efm32_flash_write_stub), 4U)

static bool efm32_flash_erase(target_flash_s *f, target_addr_t addr, size_t len);
static bool efm32_flash_write(target_flash_s *f, target_addr_t dest, const void *src, size_t len);
static bool efm32_mass_erase(target_s *t);

static const uint16_t efm32_flash_write_stub[] = {
#include "flashstub/efm32.stub"
};

static bool efm32_cmd_serial(target_s *t, int argc, const char **argv);
static bool efm32_cmd_efm_info(target_s *t, int argc, const char **argv);
static bool efm32_cmd_bootloader(target_s *t, int argc, const char **argv);

const command_s efm32_cmd_list[] = {
	{"serial", efm32_cmd_serial, "Prints unique number"},
	{"efm_info", efm32_cmd_efm_info, "Prints information about the device"},
	{"bootloader", efm32_cmd_bootloader, "Bootloader status in CLW0"},
	{NULL, NULL, NULL},
};

/* -------------------------------------------------------------------------- */
/* Memory System Controller (MSC) Registers                                   */
/* -------------------------------------------------------------------------- */

#define EFM32_MSC_WRITECTRL(msc) (msc + 0x008U)
#define EFM32_MSC_WRITECMD(msc)  (msc + 0x00cU)
#define EFM32_MSC_ADDRB(msc)     (msc + 0x010U)
#define EFM32_MSC_WDATA(msc)     (msc + 0x018U)
#define EFM32_MSC_STATUS(msc)    (msc + 0x01cU)
#define EFM32_MSC_IF(msc)        (msc + 0x030U)
#define EFM32_MSC_LOCK(msc)      (msc + (msc == 0x400c0000U ? 0x3cU : 0x40U))
#define EFM32_MSC_MASSLOCK(msc)  (msc + 0x054U)

#define EFM32_MSC_LOCK_LOCKKEY     0x1b71U
#define EFM32_MSC_MASSLOCK_LOCKKEY 0x631aU

#define EFM32_MSC_WRITECMD_LADDRIM    (1U << 0U)
#define EFM32_MSC_WRITECMD_ERASEPAGE  (1U << 1U)
#define EFM32_MSC_WRITECMD_WRITEEND   (1U << 2U)
#define EFM32_MSC_WRITECMD_WRITEONCE  (1U << 3U)
#define EFM32_MSC_WRITECMD_WRITETRIG  (1U << 4U)
#define EFM32_MSC_WRITECMD_ERASEABORT (1U << 5U)
#define EFM32_MSC_WRITECMD_ERASEMAIN0 (1U << 8U)
#define EFM32_MSC_WRITECMD_ERASEMAIN1 (1U << 9U)

#define EFM32_MSC_STATUS_BUSY       (1U << 0U)
#define EFM32_MSC_STATUS_LOCKED     (1U << 1U)
#define EFM32_MSC_STATUS_INVADDR    (1U << 2U)
#define EFM32_MSC_STATUS_WDATAREADY (1U << 3U)

/* -------------------------------------------------------------------------- */
/* Flash Information Area                                                      */
/* -------------------------------------------------------------------------- */

#define EFM32_INFO      0x0fe00000U
#define EFM32_USER_DATA (EFM32_INFO + 0x0000U)
#define EFM32_LOCK_BITS (EFM32_INFO + 0x4000U)
#define EFM32_V1_DI     (EFM32_INFO + 0x8000U)
#define EFM32_V2_DI     (EFM32_INFO + 0x81b0U)

/* -------------------------------------------------------------------------- */
/* Lock Bits (LB)                                                             */
/* -------------------------------------------------------------------------- */

#define EFM32_LOCK_BITS_DLW  (EFM32_LOCK_BITS + (4U * 127U))
#define EFM32_LOCK_BITS_ULW  (EFM32_LOCK_BITS + (4U * 126U))
#define EFM32_LOCK_BITS_MLW  (EFM32_LOCK_BITS + (4U * 125U))
#define EFM32_LOCK_BITS_CLW0 (EFM32_LOCK_BITS + (4U * 122U))

#define EFM32_CLW0_BOOTLOADER_ENABLE (1U << 1U)
#define EFM32_CLW0_PINRESETSOFT      (1U << 2U)

/* -------------------------------------------------------------------------- */
/* Device Information (DI) Area - Version 1                                   */
/* -------------------------------------------------------------------------- */

#define EFM32_V1_DI_CMU_LFRCOCTRL          (EFM32_V1_DI + 0x020U)
#define EFM32_V1_DI_CMU_HFRCOCTRL          (EFM32_V1_DI + 0x028U)
#define EFM32_V1_DI_CMU_AUXHFRCOCTRL       (EFM32_V1_DI + 0x030U)
#define EFM32_V1_DI_ADC0_CAL               (EFM32_V1_DI + 0x040U)
#define EFM32_V1_DI_ADC0_BIASPROG          (EFM32_V1_DI + 0x048U)
#define EFM32_V1_DI_DAC0_CAL               (EFM32_V1_DI + 0x050U)
#define EFM32_V1_DI_DAC0_BIASPROG          (EFM32_V1_DI + 0x058U)
#define EFM32_V1_DI_ACMP0_CTRL             (EFM32_V1_DI + 0x060U)
#define EFM32_V1_DI_ACMP1_CTRL             (EFM32_V1_DI + 0x068U)
#define EFM32_V1_DI_CMU_LCDCTRL            (EFM32_V1_DI + 0x078U)
#define EFM32_V1_DI_DAC0_OPACTRL           (EFM32_V1_DI + 0x0a0U)
#define EFM32_V1_DI_DAC0_OPAOFFSET         (EFM32_V1_DI + 0x0a8U)
#define EFM32_V1_DI_EMU_BUINACT            (EFM32_V1_DI + 0x0b0U)
#define EFM32_V1_DI_EMU_BUACT              (EFM32_V1_DI + 0x0b8U)
#define EFM32_V1_DI_EMU_BUBODBUVINCAL      (EFM32_V1_DI + 0x0c0U)
#define EFM32_V1_DI_EMU_BUBODUNREGCAL      (EFM32_V1_DI + 0x0c8U)
#define EFM32_V1_DI_MCM_REV_MIN            (EFM32_V1_DI + 0x1aaU)
#define EFM32_V1_DI_MCM_REV_MAJ            (EFM32_V1_DI + 0x1abU)
#define EFM32_V1_DI_RADIO_REV_MIN          (EFM32_V1_DI + 0x1acU)
#define EFM32_V1_DI_RADIO_REV_MAJ          (EFM32_V1_DI + 0x1adU)
#define EFM32_V1_DI_RADIO_OPN              (EFM32_V1_DI + 0x1aeU)
#define EFM32_V1_DI_V1_DI_CRC              (EFM32_V1_DI + 0x1b0U)
#define EFM32_V1_DI_CAL_TEMP_0             (EFM32_V1_DI + 0x1b2U)
#define EFM32_V1_DI_ADC0_CAL_1V25          (EFM32_V1_DI + 0x1b4U)
#define EFM32_V1_DI_ADC0_CAL_2V5           (EFM32_V1_DI + 0x1b6U)
#define EFM32_V1_DI_ADC0_CAL_VDD           (EFM32_V1_DI + 0x1b8U)
#define EFM32_V1_DI_ADC0_CAL_5VDIFF        (EFM32_V1_DI + 0x1baU)
#define EFM32_V1_DI_ADC0_CAL_2XVDD         (EFM32_V1_DI + 0x1bcU)
#define EFM32_V1_DI_ADC0_TEMP_0_READ_1V25  (EFM32_V1_DI + 0x1beU)
#define EFM32_V1_DI_DAC0_CAL_1V25          (EFM32_V1_DI + 0x1c8U)
#define EFM32_V1_DI_DAC0_CAL_2V5           (EFM32_V1_DI + 0x1ccU)
#define EFM32_V1_DI_DAC0_CAL_VDD           (EFM32_V1_DI + 0x1d0U)
#define EFM32_V1_DI_AUXHFRCO_CALIB_BAND_1  (EFM32_V1_DI + 0x1d4U)
#define EFM32_V1_DI_AUXHFRCO_CALIB_BAND_7  (EFM32_V1_DI + 0x1d5U)
#define EFM32_V1_DI_AUXHFRCO_CALIB_BAND_11 (EFM32_V1_DI + 0x1d6U)
#define EFM32_V1_DI_AUXHFRCO_CALIB_BAND_14 (EFM32_V1_DI + 0x1d7U)
#define EFM32_V1_DI_AUXHFRCO_CALIB_BAND_21 (EFM32_V1_DI + 0x1d8U)
#define EFM32_V1_DI_AUXHFRCO_CALIB_BAND_28 (EFM32_V1_DI + 0x1d9U)
#define EFM32_V1_DI_HFRCO_CALIB_BAND_1     (EFM32_V1_DI + 0x1dcU)
#define EFM32_V1_DI_HFRCO_CALIB_BAND_7     (EFM32_V1_DI + 0x1ddU)
#define EFM32_V1_DI_HFRCO_CALIB_BAND_11    (EFM32_V1_DI + 0x1deU)
#define EFM32_V1_DI_HFRCO_CALIB_BAND_14    (EFM32_V1_DI + 0x1dfU)
#define EFM32_V1_DI_HFRCO_CALIB_BAND_21    (EFM32_V1_DI + 0x1e0U)
#define EFM32_V1_DI_HFRCO_CALIB_BAND_28    (EFM32_V1_DI + 0x1e1U)
#define EFM32_V1_DI_MEM_INFO_PAGE_SIZE     (EFM32_V1_DI + 0x1e7U)
#define EFM32_V1_DI_RADIO_ID               (EFM32_V1_DI + 0x1eeU)
#define EFM32_V1_DI_EUI64_0                (EFM32_V1_DI + 0x1f0U)
#define EFM32_V1_DI_EUI64_1                (EFM32_V1_DI + 0x1f4U)
#define EFM32_V1_DI_MEM_INFO_FLASH         (EFM32_V1_DI + 0x1f8U)
#define EFM32_V1_DI_MEM_INFO_RAM           (EFM32_V1_DI + 0x1faU)
#define EFM32_V1_DI_PART_NUMBER            (EFM32_V1_DI + 0x1fcU)
#define EFM32_V1_DI_PART_FAMILY            (EFM32_V1_DI + 0x1feU)
#define EFM32_V1_DI_PROD_REV               (EFM32_V1_DI + 0x1ffU)

/* top 24 bits of eui */
#define EFM32_V1_DI_EUI_SILABS 0x000b57U

/* -------------------------------------------------------------------------- */
/* Device Information (DI) Area - Version 2                                   */
/* -------------------------------------------------------------------------- */

#define EFM32_V2_DI_CAL              (EFM32_V2_DI + 0x000U) /* CRC of DI-page and calibration temperature */
#define EFM32_V2_DI_EXTINFO          (EFM32_V2_DI + 0x020U) /* External Component description */
#define EFM32_V2_DI_EUI48L           (EFM32_V2_DI + 0x028U) /* EUI48 OUI and Unique identifier */
#define EFM32_V2_DI_EUI48H           (EFM32_V2_DI + 0x02cU) /* OUI */
#define EFM32_V2_DI_CUSTOMINFO       (EFM32_V2_DI + 0x030U) /* Custom information */
#define EFM32_V2_DI_MEMINFO          (EFM32_V2_DI + 0x034U) /* Flash page size and misc. chip information */
#define EFM32_V2_DI_UNIQUEL          (EFM32_V2_DI + 0x040U) /* Low 32 bits of device unique number */
#define EFM32_V2_DI_UNIQUEH          (EFM32_V2_DI + 0x044U) /* High 32 bits of device unique number */
#define EFM32_V2_DI_MSIZE            (EFM32_V2_DI + 0x048U) /* Flash and SRAM Memory size in kB */
#define EFM32_V2_DI_PART             (EFM32_V2_DI + 0x04cU) /* Part description */
#define EFM32_V2_DI_DEVINFOREV       (EFM32_V2_DI + 0x050U) /* Device information page revision */
#define EFM32_V2_DI_EMUTEMP          (EFM32_V2_DI + 0x054U) /* EMU Temperature Calibration Information */
#define EFM32_V2_DI_ADC0CAL0         (EFM32_V2_DI + 0x060U) /* ADC0 calibration register 0 */
#define EFM32_V2_DI_ADC0CAL1         (EFM32_V2_DI + 0x064U) /* ADC0 calibration register 1 */
#define EFM32_V2_DI_ADC0CAL2         (EFM32_V2_DI + 0x068U) /* ADC0 calibration register 2 */
#define EFM32_V2_DI_ADC0CAL3         (EFM32_V2_DI + 0x06cU) /* ADC0 calibration register 3 */
#define EFM32_V2_DI_HFRCOCAL0        (EFM32_V2_DI + 0x080U) /* HFRCO Calibration Register (4 MHz) */
#define EFM32_V2_DI_HFRCOCAL3        (EFM32_V2_DI + 0x08cU) /* HFRCO Calibration Register (7 MHz) */
#define EFM32_V2_DI_HFRCOCAL6        (EFM32_V2_DI + 0x098U) /* HFRCO Calibration Register (13 MHz) */
#define EFM32_V2_DI_HFRCOCAL7        (EFM32_V2_DI + 0x09cU) /* HFRCO Calibration Register (16 MHz)  */
#define EFM32_V2_DI_HFRCOCAL8        (EFM32_V2_DI + 0x0a0U)
#define EFM32_V2_DI_HFRCOCAL10       (EFM32_V2_DI + 0x0a8U)
#define EFM32_V2_DI_HFRCOCAL11       (EFM32_V2_DI + 0x0acU)
#define EFM32_V2_DI_HFRCOCAL12       (EFM32_V2_DI + 0x0b0U)
#define EFM32_V2_DI_AUXHFRCOCAL0     (EFM32_V2_DI + 0x0e0U)
#define EFM32_V2_DI_AUXHFRCOCAL3     (EFM32_V2_DI + 0x0ecU)
#define EFM32_V2_DI_AUXHFRCOCAL6     (EFM32_V2_DI + 0x0f8U)
#define EFM32_V2_DI_AUXHFRCOCAL7     (EFM32_V2_DI + 0x0fcU)
#define EFM32_V2_DI_AUXHFRCOCAL8     (EFM32_V2_DI + 0x100U)
#define EFM32_V2_DI_AUXHFRCOCAL10    (EFM32_V2_DI + 0x108U)
#define EFM32_V2_DI_AUXHFRCOCAL11    (EFM32_V2_DI + 0x10cU)
#define EFM32_V2_DI_AUXHFRCOCAL12    (EFM32_V2_DI + 0x110U)
#define EFM32_V2_DI_VMONCAL0         (EFM32_V2_DI + 0x140U)
#define EFM32_V2_DI_VMONCAL1         (EFM32_V2_DI + 0x144U) /* VMON Calibration Register 1 */
#define EFM32_V2_DI_VMONCAL2         (EFM32_V2_DI + 0x148U) /* VMON Calibration Register 2 */
#define EFM32_V2_DI_IDAC0CAL0        (EFM32_V2_DI + 0x158U) /* IDAC0 Calibration Register 0 */
#define EFM32_V2_DI_IDAC0CAL1        (EFM32_V2_DI + 0x15cU) /* IDAC0 Calibration Register 1 */
#define EFM32_V2_DI_DCDCLNVCTRL0     (EFM32_V2_DI + 0x168U) /* DCDC Low-noise VREF Trim Register 0 */
#define EFM32_V2_DI_DCDCLPVCTRL0     (EFM32_V2_DI + 0x16cU) /* DCDC Low-power VREF Trim Register 0 */
#define EFM32_V2_DI_DCDCLPVCTRL1     (EFM32_V2_DI + 0x170U) /* DCDC Low-power VREF Trim Register 1 */
#define EFM32_V2_DI_DCDCLPVCTRL2     (EFM32_V2_DI + 0x174U) /* DCDC Low-power VREF Trim Register 2 */
#define EFM32_V2_DI_DCDCLPVCTRL3     (EFM32_V2_DI + 0x178U) /* DCDC Low-power VREF Trim Register 3 */
#define EFM32_V2_DI_DCDCLPCMPHYSSEL0 (EFM32_V2_DI + 0x17cU) /* DCDC LPCMPHYSSEL Trim Register 0 */
#define EFM32_V2_DI_DCDCLPCMPHYSSEL1 (EFM32_V2_DI + 0x180U) /* DCDC LPCMPHYSSEL Trim Register 1 */
#define EFM32_V2_DI_VDAC0MAINCAL     (EFM32_V2_DI + 0x184U) /* VDAC0 Cals for Main Path */
#define EFM32_V2_DI_VDAC0ALTCAL      (EFM32_V2_DI + 0x188U) /* VDAC0 Cals for Alternate Path */
#define EFM32_V2_DI_VDAC0CH1CAL      (EFM32_V2_DI + 0x18cU) /* VDAC0 CH1 Error Cal */
#define EFM32_V2_DI_OPA0CAL0         (EFM32_V2_DI + 0x190U) /* OPA0 Calibration Register for DRIVESTRENGTH 0, INCBW=1 */
#define EFM32_V2_DI_OPA0CAL1         (EFM32_V2_DI + 0x194U) /* OPA0 Calibration Register for DRIVESTRENGTH 1, INCBW=1 */
#define EFM32_V2_DI_OPA0CAL2         (EFM32_V2_DI + 0x198U) /* OPA0 Calibration Register for DRIVESTRENGTH 2, INCBW=1 */
#define EFM32_V2_DI_OPA0CAL3         (EFM32_V2_DI + 0x19cU) /* OPA0 Calibration Register for DRIVESTRENGTH 3, INCBW=1 */
#define EFM32_V2_DI_OPA1CAL0         (EFM32_V2_DI + 0x1a0U) /* OPA1 Calibration Register for DRIVESTRENGTH 0, INCBW=1 */
#define EFM32_V2_DI_OPA1CAL1         (EFM32_V2_DI + 0x1a4U) /* OPA1 Calibration Register for DRIVESTRENGTH 1, INCBW=1 */
#define EFM32_V2_DI_OPA1CAL2         (EFM32_V2_DI + 0x1a8U)
#define EFM32_V2_DI_OPA1CAL3         (EFM32_V2_DI + 0x1acU)
#define EFM32_V2_DI_OPA2CAL0         (EFM32_V2_DI + 0x1b0U)
#define EFM32_V2_DI_OPA2CAL1         (EFM32_V2_DI + 0x1b4U)
#define EFM32_V2_DI_OPA2CAL2         (EFM32_V2_DI + 0x1b8U)
#define EFM32_V2_DI_OPA2CAL3         (EFM32_V2_DI + 0x1bcU)
#define EFM32_V2_DI_CSENGAINCAL      (EFM32_V2_DI + 0x1c0U)
#define EFM32_V2_DI_OPA0CAL4         (EFM32_V2_DI + 0x1d0U)
#define EFM32_V2_DI_OPA0CAL5         (EFM32_V2_DI + 0x1d4U)
#define EFM32_V2_DI_OPA0CAL6         (EFM32_V2_DI + 0x1d8U)
#define EFM32_V2_DI_OPA0CAL7         (EFM32_V2_DI + 0x1dcU)
#define EFM32_V2_DI_OPA1CAL4         (EFM32_V2_DI + 0x1e0U)
#define EFM32_V2_DI_OPA1CAL5         (EFM32_V2_DI + 0x1e4U)
#define EFM32_V2_DI_OPA1CAL6         (EFM32_V2_DI + 0x1e8U)
#define EFM32_V2_DI_OPA1CAL7         (EFM32_V2_DI + 0x1ecU)
#define EFM32_V2_DI_OPA2CAL4         (EFM32_V2_DI + 0x1f0U)
#define EFM32_V2_DI_OPA2CAL5         (EFM32_V2_DI + 0x1f4U)
#define EFM32_V2_DI_OPA2CAL6         (EFM32_V2_DI + 0x1f8U) /* OPA2 Calibration Register for DRIVESTRENGTH 2, INCBW=0 */
#define EFM32_V2_DI_OPA2CAL7         (EFM32_V2_DI + 0x1fcU) /* OPA2 Calibration Register for DRIVESTRENGTH 3, INCBW=0 */

/* top 24 bits of eui */
#define EFM32_V2_DI_EUI_ENERGYMICRO 0xd0cf5e

/* -------------------------------------------------------------------------- */
/* Constants                                                                  */
/* -------------------------------------------------------------------------- */

typedef struct efm32_device {
	uint8_t family_id;        /* Family for device matching */
	bool has_radio;           /* Indicates a device has attached radio */
	uint16_t flash_page_size; /* Flash page size */
	char *name;               /* Friendly device family name */
	uint32_t msc_addr;        /* MSC Address */
	uint16_t user_data_size;  /* User Data (UD) region size */
	uint16_t bootloader_size; /* Bootloader (BL) region size (may be 0 for no BL region) */
	char *description;        /* Human-readable description */
} efm32_device_s;

static const efm32_device_s efm32_devices[] = {
	/*  First gen micros */
	{71, false, 512, "EFM32G", 0x400c0000, 512, 0, "Gecko"},
	{72, false, 2048, "EFM32GG", 0x400c0000, 4096, 0, "Giant Gecko"},
	{73, false, 512, "EFM32TG", 0x400c0000, 512, 0, "Tiny Gecko"},
	{74, false, 2048, "EFM32LG", 0x400c0000, 2048, 0, "Leopard Gecko"},
	{75, false, 2048, "EFM32WG", 0x400c0000, 2048, 0, "Wonder Gecko"},
	{76, false, 1024, "EFM32ZG", 0x400c0000, 1024, 0, "Zero Gecko"},
	{77, false, 1024, "EFM32HG", 0x400c0000, 1024, 0, "Happy Gecko"},
	/*  First (1.5) gen micro + radio */
	{120, true, 2048, "EZR32WG", 0x400c0000, 2048, 0, "EZR Wonder Gecko"},
	{121, true, 2048, "EZR32LG", 0x400c0000, 2048, 0, "EZR Leopard Gecko"},
	{122, true, 1024, "EZR32HG", 0x400c0000, 1024, 0, "EZR Happy Gecko"},
	/*  Second gen micros */
	{81, false, 2048, "EFM32PG1B", 0x400e0000, 2048, 10240, "Pearl Gecko"},
	{83, false, 2048, "EFM32JG1B", 0x400e0000, 2048, 10240, "Jade Gecko"},
	{85, false, 2048, "EFM32PG12B", 0x400e0000, 2048, 32768, "Pearl Gecko 12"},
	{87, false, 2048, "EFM32JG12B", 0x400e0000, 2048, 32768, "Jade Gecko 12"},
	/*  Second (2.5) gen micros, with re-located MSC */
	{100, false, 4096, "EFM32GG11B", 0x40000000, 4096, 32768, "Giant Gecko 11"},
	{103, false, 2048, "EFM32TG11B", 0x40000000, 2048, 18432, "Tiny Gecko 11"},
	{106, false, 2048, "EFM32GG12B", 0x40000000, 2048, 32768, "Giant Gecko 12"},
	/*  Second gen devices micro + radio */
	{16, true, 2048, "EFR32MG1P", 0x400e0000, 2048, 10240, "Mighty Gecko"},
	{17, true, 2048, "EFR32MG1B", 0x400e0000, 2048, 10240, "Mighty Gecko"},
	{18, true, 2048, "EFR32MG1V", 0x400e0000, 2048, 10240, "Mighty Gecko"},
	{19, true, 2048, "EFR32BG1P", 0x400e0000, 2048, 10240, "Blue Gecko"},
	{20, true, 2048, "EFR32BG1B", 0x400e0000, 2048, 10240, "Blue Gecko"},
	{21, true, 2048, "EFR32BG1V", 0x400e0000, 2048, 10240, "Blue Gecko"},
	{25, true, 2048, "EFR32FG1P", 0x400e0000, 2048, 10240, "Flex Gecko"},
	{26, true, 2048, "EFR32FG1B", 0x400e0000, 2048, 10240, "Flex Gecko"},
	{27, true, 2048, "EFR32FG1V", 0x400e0000, 2048, 10240, "Flex Gecko"},
	{28, true, 2048, "EFR32MG12P", 0x400e0000, 2048, 32768, "Mighty Gecko"},
	{29, true, 2048, "EFR32MG12B", 0x400e0000, 2048, 32768, "Mighty Gecko"},
	{30, true, 2048, "EFR32MG12V", 0x400e0000, 2048, 32768, "Mighty Gecko"},
	{31, true, 2048, "EFR32BG12P", 0x400e0000, 2048, 32768, "Blue Gecko"},
	{32, true, 2048, "EFR32BG12B", 0x400e0000, 2048, 32768, "Blue Gecko"},
	{33, true, 2048, "EFR32BG12V", 0x400e0000, 2048, 32768, "Blue Gecko"},
	{37, true, 2048, "EFR32FG12P", 0x400e0000, 2048, 32768, "Flex Gecko"},
	{38, true, 2048, "EFR32FG12B", 0x400e0000, 2048, 32768, "Flex Gecko"},
	{39, true, 2048, "EFR32FG12V", 0x400e0000, 2048, 32768, "Flex Gecko"},
	{40, true, 2048, "EFR32MG13P", 0x400e0000, 2048, 16384, "Mighty Gecko"},
	{41, true, 2048, "EFR32MG13B", 0x400e0000, 2048, 16384, "Mighty Gecko"},
	{42, true, 2048, "EFR32MG13V", 0x400e0000, 2048, 16384, "Mighty Gecko"},
	{43, true, 2048, "EFR32BG13P", 0x400e0000, 2048, 16384, "Blue Gecko"},
	{44, true, 2048, "EFR32BG13B", 0x400e0000, 2048, 16384, "Blue Gecko"},
	{45, true, 2048, "EFR32BG13V", 0x400e0000, 2048, 16384, "Blue Gecko"},
	{49, true, 2048, "EFR32FG13P", 0x400e0000, 2048, 16384, "Flex Gecko"},
	{50, true, 2048, "EFR32FG13B", 0x400e0000, 2048, 16384, "Flex Gecko"},
	{51, true, 2048, "EFR32FG13V", 0x400e0000, 2048, 16384, "Flex Gecko"},
	{52, true, 2048, "EFR32MG14P", 0x400e0000, 2048, 16384, "Mighty Gecko"},
	{53, true, 2048, "EFR32MG14B", 0x400e0000, 2048, 16384, "Mighty Gecko"},
	{54, true, 2048, "EFR32MG14V", 0x400e0000, 2048, 16384, "Mighty Gecko"},
	{55, true, 2048, "EFR32BG14P", 0x400e0000, 2048, 16384, "Blue Gecko"},
	{56, true, 2048, "EFR32BG14B", 0x400e0000, 2048, 16384, "Blue Gecko"},
	{57, true, 2048, "EFR32BG14V", 0x400e0000, 2048, 16384, "Blue Gecko"},
	{61, true, 2048, "EFR32FG14P", 0x400e0000, 2048, 16384, "Flex Gecko"},
	{62, true, 2048, "EFR32FG14B", 0x400e0000, 2048, 16384, "Flex Gecko"},
	{63, true, 2048, "EFR32FG14V", 0x400e0000, 2048, 16384, "Flex Gecko"},
};

/* miscchip */
typedef struct efm32_v2_di_miscchip {
	uint8_t pincount;
	uint8_t pkgtype;
	uint8_t tempgrade;
} efm32_v2_di_miscchip_s;

/* pkgtype */
typedef struct efm32_v2_di_pkgtype {
	uint8_t pkgtype;
	char *name;
} efm32_v2_di_pkgtype_s;

static const efm32_v2_di_pkgtype_s efm32_v2_di_pkgtypes[] = {
	{74, "WLCSP"}, /* WLCSP package */
	{76, "BGA"},   /* BGA package */
	{77, "QFN"},   /* QFN package */
	{81, "QFxP"},  /* QFP package */
};

/* tempgrade */
typedef struct efm32_v2_di_tempgrade {
	uint8_t tempgrade;
	char *name;
} efm32_v2_di_tempgrade_s;

static const efm32_v2_di_tempgrade_s efm32_v2_di_tempgrades[] = {
	{0, "-40 to 85degC"},
	{1, "-40 to 125degC"},
	{2, "-40 to 105degC"},
	{3, "0 to 70degC"},
};

/* -------------------------------------------------------------------------- */
/* Helper functions                                                           */
/* -------------------------------------------------------------------------- */

/* Reads the EFM32 Extended Unique Identifier EUI64 (V1) */
static uint64_t efm32_v1_read_eui64(target_s *t)
{
	return ((uint64_t)target_mem_read32(t, EFM32_V1_DI_EUI64_1) << 32U) | target_mem_read32(t, EFM32_V1_DI_EUI64_0);
}

/* Reads the Unique Number (DI V2 only) */
static uint64_t efm32_v2_read_unique(target_s *t, uint8_t di_version)
{
	if (di_version != 2)
		return 0;

	return ((uint64_t)target_mem_read32(t, EFM32_V2_DI_UNIQUEH) << 32U) | target_mem_read32(t, EFM32_V2_DI_UNIQUEL);
}

/* Reads the EFM32 flash size in kiB */
static uint16_t efm32_read_flash_size(target_s *t, uint8_t di_version)
{
	switch (di_version) {
	case 1:
		return target_mem_read16(t, EFM32_V1_DI_MEM_INFO_FLASH);
	case 2:
		return target_mem_read32(t, EFM32_V2_DI_MSIZE) & 0xffffU;
	default:
		return 0;
	}
}

/* Reads the EFM32 RAM size in kiB */
static uint16_t efm32_read_ram_size(target_s *t, uint8_t di_version)
{
	switch (di_version) {
	case 1:
		return target_mem_read16(t, EFM32_V1_DI_MEM_INFO_RAM);
	case 2:
		return (target_mem_read32(t, EFM32_V2_DI_MSIZE) >> 16U) & 0xffffU;
	default:
		return 0;
	}
}

/**
 * Reads the EFM32 reported flash page size in bytes.  Note: This
 * driver ignores this value and uses a conservative hard-coded
 * value. There are errata on the value reported by the EFM32
 * eg. DI_101
 */
static uint32_t efm32_flash_page_size(target_s *t, uint8_t di_version)
{
	uint8_t mem_info_page_size;

	switch (di_version) {
	case 1U:
		mem_info_page_size = target_mem_read8(t, EFM32_V1_DI_MEM_INFO_PAGE_SIZE);
		break;
	case 2U:
		mem_info_page_size = (target_mem_read32(t, EFM32_V2_DI_MEMINFO) >> 24U) & 0xffU;
		break;
	default:
		return 0;
	}

	return 1U << (mem_info_page_size + 10U); /* uint8_t ovf here */
}

/* Reads the EFM32 Part Number */
static uint16_t efm32_read_part_number(target_s *t, uint8_t di_version)
{
	switch (di_version) {
	case 1:
		return target_mem_read8(t, EFM32_V1_DI_PART_NUMBER);
	case 2:
		return target_mem_read32(t, EFM32_V2_DI_PART) & 0xffffU;
	default:
		return 0;
	}
}

/* Reads the EFM32 Part Family */
static uint8_t efm32_read_part_family(target_s *t, uint8_t di_version)
{
	switch (di_version) {
	case 1:
		return target_mem_read8(t, EFM32_V1_DI_PART_FAMILY);
	case 2:
		return (target_mem_read32(t, EFM32_V2_DI_PART) >> 16U) & 0xffU;
	default:
		return 0;
	}
}

/* Reads the EFM32 Radio part number (EZR parts with V1 DI only) */
static uint16_t efm32_read_radio_part_number(target_s *t, uint8_t di_version)
{
	switch (di_version) {
	case 1:
		return target_mem_read16(t, EFM32_V1_DI_RADIO_OPN);
	default:
		return 0;
	}
}

/* Reads the EFM32 Misc. Chip definitions */
static efm32_v2_di_miscchip_s efm32_v2_read_miscchip(target_s *t, uint8_t di_version)
{
	efm32_v2_di_miscchip_s miscchip = {0};

	switch (di_version) {
	case 2: {
		const uint32_t meminfo = target_mem_read32(t, EFM32_V2_DI_MEMINFO);
		miscchip.pincount = (meminfo >> 16U) & 0xffU;
		miscchip.pkgtype = (meminfo >> 8U) & 0xffU;
		miscchip.tempgrade = (meminfo >> 0U) & 0xffU;
	}
	}

	return miscchip;
}

/* -------------------------------------------------------------------------- */
/* Shared Functions                                                           */
/* -------------------------------------------------------------------------- */

static void efm32_add_flash(target_s *t, target_addr_t addr, size_t length, size_t page_size)
{
	target_flash_s *f = calloc(1, sizeof(*f));
	if (!f) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return;
	}

	f->start = addr;
	f->length = length;
	f->blocksize = page_size;
	f->erase = efm32_flash_erase;
	f->write = efm32_flash_write;
	f->writesize = page_size;
	target_add_flash(t, f);
}

/* Lookup device */
static efm32_device_s const *efm32_get_device(target_s *t, uint8_t di_version)
{
	uint8_t part_family = efm32_read_part_family(t, di_version);

	/* Search for family */
	for (size_t i = 0; i < (sizeof(efm32_devices) / sizeof(efm32_device_s)); i++) {
		if (efm32_devices[i].family_id == part_family) {
			return &efm32_devices[i];
		}
	}

	/* Unknown family */
	return NULL;
}

/* Probe */
typedef struct efm32_priv {
	char efm32_variant_string[60];
	uint8_t di_version;
	const efm32_device_s *device;
} efm32_priv_s;

bool efm32_probe(target_s *t)
{
	/* Check if the OUI in the EUI is silabs or energymicro.
	 * Use this to identify the Device Identification (DI) version */
	uint8_t di_version = 1;
	uint64_t oui24 = ((efm32_v1_read_eui64(t) >> 40U) & 0xffffffU);
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
	efm32_device_s const *device = efm32_get_device(t, di_version);
	if (!device)
		return false;

	t->mass_erase = efm32_mass_erase;
	uint16_t part_number = efm32_read_part_number(t, di_version);

	/* Read memory sizes, convert to bytes */
	uint16_t flash_kib = efm32_read_flash_size(t, di_version);
	uint32_t flash_size = flash_kib * 0x400U;
	uint16_t ram_kib = efm32_read_ram_size(t, di_version);
	uint32_t ram_size = ram_kib * 0x400U;
	uint32_t flash_page_size = device->flash_page_size;

	efm32_priv_s *priv_storage = calloc(1, sizeof(*priv_storage));
	if (!priv_storage) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return false;
	}
	t->target_storage = (void *)priv_storage;

	priv_storage->di_version = di_version;
	priv_storage->device = device;

	snprintf(priv_storage->efm32_variant_string, sizeof(priv_storage->efm32_variant_string), "%s%huF%hu %s",
		device->name, part_number, flash_kib, device->description);

	/* Setup Target */
	t->target_options |= CORTEXM_TOPT_INHIBIT_NRST;
	t->driver = priv_storage->efm32_variant_string;
	tc_printf(t, "flash size %u page size %u\n", flash_size, flash_page_size);

	target_add_ram(t, SRAM_BASE, ram_size);
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

/* Erase flash row by row */
static bool efm32_flash_erase(target_flash_s *f, target_addr_t addr, size_t len)
{
	target_s *t = f->t;

	efm32_priv_s *priv_storage = (efm32_priv_s *)t->target_storage;
	if (!priv_storage || !priv_storage->device)
		return false;

	uint32_t msc = priv_storage->device->msc_addr;

	/* Unlock */
	target_mem_write32(t, EFM32_MSC_LOCK(msc), EFM32_MSC_LOCK_LOCKKEY);

	/* Set WREN bit to enable MSC write and erase functionality */
	target_mem_write32(t, EFM32_MSC_WRITECTRL(msc), 1);

	while (len) {
		/* Write address of first word in row to erase it */
		target_mem_write32(t, EFM32_MSC_ADDRB(msc), addr);
		target_mem_write32(t, EFM32_MSC_WRITECMD(msc), EFM32_MSC_WRITECMD_LADDRIM);

		/* Issue the erase command */
		target_mem_write32(t, EFM32_MSC_WRITECMD(msc), EFM32_MSC_WRITECMD_ERASEPAGE);

		/* Poll MSC Busy */
		while ((target_mem_read32(t, EFM32_MSC_STATUS(msc)) & EFM32_MSC_STATUS_BUSY)) {
			if (target_check_error(t))
				return false;
		}

		addr += f->blocksize;
		if (len > f->blocksize)
			len -= f->blocksize;
		else
			len = 0;
	}

	return true;
}

/* Write flash page by page */
static bool efm32_flash_write(target_flash_s *f, target_addr_t dest, const void *src, size_t len)
{
	(void)len;

	target_s *t = f->t;

	efm32_priv_s *priv_storage = (efm32_priv_s *)t->target_storage;
	if (!priv_storage || !priv_storage->device)
		return false;

	/* Write flashloader */
	target_mem_write(t, SRAM_BASE, efm32_flash_write_stub, sizeof(efm32_flash_write_stub));
	/* Write Buffer */
	target_mem_write(t, STUB_BUFFER_BASE, src, len);
	/* Run flashloader */
	const bool ret = cortexm_run_stub(t, SRAM_BASE, dest, STUB_BUFFER_BASE, len, priv_storage->device->msc_addr) == 0;

#ifdef ENABLE_DEBUG
	/* Check the MSC_IF */
	uint32_t msc = priv_storage->device->msc_addr;
	uint32_t msc_if = target_mem_read32(t, EFM32_MSC_IF(msc));
	DEBUG_INFO("EFM32: Flash write done MSC_IF=%08" PRIx32 "\n", msc_if);
#endif
	return ret;
}

/* Uses the MSC ERASEMAIN0/1 command to erase the entire flash */
static bool efm32_mass_erase(target_s *t)
{
	efm32_priv_s *priv_storage = (efm32_priv_s *)t->target_storage;
	if (!priv_storage || !priv_storage->device)
		return false;

	if (priv_storage->device->family_id == 71 || priv_storage->device->family_id == 73) {
		/* original Gecko and Tiny Gecko families don't support mass erase */
		tc_printf(t, "This device does not support mass erase through MSC.\n");
		return false;
	}

	uint32_t msc = priv_storage->device->msc_addr;

	uint16_t flash_kib = efm32_read_flash_size(t, priv_storage->di_version);

	/* Set WREN bit to enable MSC write and erase functionality */
	target_mem_write32(t, EFM32_MSC_WRITECTRL(msc), 1);

	/* Unlock mass erase */
	target_mem_write32(t, EFM32_MSC_MASSLOCK(msc), EFM32_MSC_MASSLOCK_LOCKKEY);

	/* Erase operation */
	target_mem_write32(t, EFM32_MSC_WRITECMD(msc), EFM32_MSC_WRITECMD_ERASEMAIN0);

	platform_timeout_s timeout;
	platform_timeout_set(&timeout, 500);
	/* Poll MSC Busy */
	while ((target_mem_read32(t, EFM32_MSC_STATUS(msc)) & EFM32_MSC_STATUS_BUSY)) {
		if (target_check_error(t))
			return false;
		target_print_progress(&timeout);
	}

	/* Parts with >= 512 kiB flash have 2 mass erase regions */
	if (flash_kib >= 512) {
		/* Erase operation */
		target_mem_write32(t, EFM32_MSC_WRITECMD(msc), EFM32_MSC_WRITECMD_ERASEMAIN1);

		/* Poll MSC Busy */
		while ((target_mem_read32(t, EFM32_MSC_STATUS(msc)) & EFM32_MSC_STATUS_BUSY)) {
			if (target_check_error(t))
				return false;
			target_print_progress(&timeout);
		}
	}

	/* Relock mass erase */
	target_mem_write32(t, EFM32_MSC_MASSLOCK(msc), 0);

	return true;
}

/* Reads the 40-bit unique number */
static bool efm32_cmd_serial(target_s *t, int argc, const char **argv)
{
	(void)argc;
	(void)argv;

	efm32_priv_s *priv_storage = (efm32_priv_s *)t->target_storage;
	if (!priv_storage)
		return false;

	uint64_t unique = 0;
	uint8_t di_version = priv_storage->di_version;

	switch (di_version) {
	case 1:
		/* Read the eui */
		unique = efm32_v1_read_eui64(t);
		break;

	case 2:
		/* Read unique number */
		unique = efm32_v2_read_unique(t, di_version);
		break;

	default:
		tc_printf(t, "Bad DI version %hhu! This driver doesn't know about this DI version\n", di_version);
		return false;
	}

	tc_printf(t, "Unique Number: 0x%016llx\n", unique);

	return true;
}

/* Prints various information we know about the device */
static bool efm32_cmd_efm_info(target_s *t, int argc, const char **argv)
{
	(void)argc;
	(void)argv;

	efm32_priv_s *priv_storage = (efm32_priv_s *)t->target_storage;
	if (!priv_storage || !priv_storage->device)
		return false;

	efm32_device_s const *device = priv_storage->device;
	uint8_t di_version = priv_storage->di_version; /* hidden in driver str */

	switch (di_version) {
	case 1:
		tc_printf(t, "DI version 1 (silabs remix?) base 0x%08" PRIx32 "\n\n", EFM32_V1_DI);
		break;

	case 2:
		tc_printf(t, "DI version 2 (energy micro remix?) base 0x%08" PRIx32 "\n\n", EFM32_V2_DI);
		break;

	default:
		tc_printf(t, "Bad DI version %hhu! This driver doesn't know about this DI version\n", di_version);
		return false;
	}

	/* lookup device and part number */
	uint16_t part_number = efm32_read_part_number(t, di_version);

	/* Read memory sizes, convert to bytes */
	uint16_t flash_kib = efm32_read_flash_size(t, di_version);
	uint16_t ram_kib = efm32_read_ram_size(t, di_version);
	uint32_t flash_page_size_reported = efm32_flash_page_size(t, di_version);
	uint32_t flash_page_size = device->flash_page_size;

	tc_printf(t, "%s %hu F%hu = %s %hukiB flash, %hukiB ram\n", device->name, part_number, flash_kib,
		device->description, flash_kib, ram_kib);
	tc_printf(t, "Device says flash page size is %u bytes, we're using %u bytes\n", flash_page_size_reported,
		flash_page_size);
	if (flash_page_size_reported < flash_page_size) {
		tc_printf(t, "This is bad, flash writes may be corrupted\n");
	}
	tc_printf(t, "\n");

	if (di_version == 2) {
		efm32_v2_di_miscchip_s miscchip = efm32_v2_read_miscchip(t, di_version);
		efm32_v2_di_pkgtype_s const *pkgtype = NULL;
		efm32_v2_di_tempgrade_s const *tempgrade;

		for (size_t i = 0; i < (sizeof(efm32_v2_di_pkgtypes) / sizeof(efm32_v2_di_pkgtype_s)); i++) {
			if (efm32_v2_di_pkgtypes[i].pkgtype == miscchip.pkgtype) {
				pkgtype = &efm32_v2_di_pkgtypes[i];
			}
		}
		for (size_t i = 0; i < (sizeof(efm32_v2_di_tempgrades) / sizeof(efm32_v2_di_tempgrade_s)); i++) {
			if (efm32_v2_di_tempgrades[i].tempgrade == miscchip.tempgrade) {
				tempgrade = &efm32_v2_di_tempgrades[i];
			}
		}

		tc_printf(t, "Package %s %hhu pins\n", pkgtype->name, miscchip.pincount);
		tc_printf(t, "Temperature grade %s\n", tempgrade->name);
		tc_printf(t, "\n");
	}

	if (di_version == 1 && device->has_radio) {
		uint16_t radio_number = efm32_read_radio_part_number(t, di_version); /* on-chip radio */
		tc_printf(t, "Radio si%hu\n", radio_number);
		tc_printf(t, "\n");
	}

	return true;
}

/**
 * Bootloader status in CLW0, if applicable.
 *
 * This is a bit in flash, so it is possible to clear it only once.
 */
static bool efm32_cmd_bootloader(target_s *t, int argc, const char **argv)
{
	/* lookup device and part number */
	efm32_priv_s *priv_storage = (efm32_priv_s *)t->target_storage;
	if (!priv_storage || !priv_storage->device)
		return false;

	uint32_t msc = priv_storage->device->msc_addr;

	if (priv_storage->device->bootloader_size == 0) {
		tc_printf(t, "This device has no bootloader.\n");
		return false;
	}

	uint32_t clw0 = target_mem_read32(t, EFM32_LOCK_BITS_CLW0);

	if (argc == 1) {
		const bool bootloader_status = clw0 & EFM32_CLW0_BOOTLOADER_ENABLE;
		tc_printf(t, "Bootloader %s\n", bootloader_status ? "enabled" : "disabled");
		return true;
	}

	/* Modify bootloader enable bit */
	if (argv[1][0] == 'e')
		clw0 |= EFM32_CLW0_BOOTLOADER_ENABLE;
	else
		clw0 &= ~EFM32_CLW0_BOOTLOADER_ENABLE;

	/* Unlock */
	target_mem_write32(t, EFM32_MSC_LOCK(msc), EFM32_MSC_LOCK_LOCKKEY);

	/* Set WREN bit to enable MSC write and erase functionality */
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

/* -------------------------------------------------------------------------- */
/* Authentication Access Port (AAP)                                           */
/* -------------------------------------------------------------------------- */

/* There's an additional AP on the SW-DP that is accessible when the part
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
 * accessible again. If the device has a bootloader, it will attempt
 * to boot from this. If you have just unlocked the device the
 * bootloader could be anything (even garbage, if the bootloader
 * wasn't used before the DEVICEERASE). Therefore you may want to
 * connect under nrst and use the bootloader command to disable it.
 *
 * It is possible to lock the AAP itself by clearing the AAP Lock Word
 * (ALW). In this case the part is unrecoverable (unless you glitch
 * it, please try glitching it).
 */

/* IDR revision [31:28] jes106 [27:17] class [16:13] res [12:8]
 * variant [7:4] type [3:0] */
#define EFM32_AAP_IDR      0x06e60001U
#define EFM32_APP_IDR_MASK 0x0fffff0fU

#define AAP_CMD    ADIV5_AP_REG(0x00U)
#define AAP_CMDKEY ADIV5_AP_REG(0x04U)
#define AAP_STATUS ADIV5_AP_REG(0x08U)

#define AAP_STATUS_LOCKED    (1U << 1U)
#define AAP_STATUS_ERASEBUSY (1U << 0U)

#define CMDKEY 0xcfacc118U

static bool efm32_aap_mass_erase(target_s *t);

/* AAP Probe */
typedef struct efm32_aap_priv {
	char aap_driver_string[42];
} efm32_aap_priv_s;

bool efm32_aap_probe(adiv5_access_port_s *ap)
{
	if ((ap->idr & EFM32_APP_IDR_MASK) == EFM32_AAP_IDR) {
		/* It's an EFM32 AAP! */
		DEBUG_INFO("EFM32: Found EFM32 AAP\n");
	} else
		return false;

	/* Both revision 1 and revision 2 devices seen in the wild */
	uint16_t aap_revision = (uint16_t)((ap->idr & 0xf0000000U) >> 28U);

	/* New target */
	target_s *t = target_new();
	if (!t) {
		return false;
	}

	t->mass_erase = efm32_aap_mass_erase;

	adiv5_ap_ref(ap);
	t->priv = ap;
	t->priv_free = (void *)adiv5_ap_unref;

	/* Read status */
	DEBUG_INFO("EFM32: AAP STATUS=%08" PRIx32 "\n", adiv5_ap_read(ap, AAP_STATUS));

	efm32_aap_priv_s *priv_storage = calloc(1, sizeof(*priv_storage));
	sprintf(priv_storage->aap_driver_string, "EFM32 Authentication Access Port rev.%hu", aap_revision);
	t->driver = priv_storage->aap_driver_string;
	t->regs_size = 0;

	return true;
}

static bool efm32_aap_mass_erase(target_s *t)
{
	adiv5_access_port_s *ap = t->priv;
	uint32_t status;

	/* Read status */
	status = adiv5_ap_read(ap, AAP_STATUS);
	DEBUG_INFO("EFM32: AAP STATUS=%08" PRIx32 "\n", status);

	if (status & AAP_STATUS_ERASEBUSY) {
		DEBUG_WARN("EFM32: AAP Erase in progress\n");
		DEBUG_WARN("EFM32: -> ABORT\n");
		return false;
	}

	DEBUG_INFO("EFM32: Issuing DEVICEERASE...\n");
	adiv5_ap_write(ap, AAP_CMDKEY, CMDKEY);
	adiv5_ap_write(ap, AAP_CMD, 1);

	platform_timeout_s timeout;
	platform_timeout_set(&timeout, 500);
	/* Read until 0, probably should have a timeout here... */
	do {
		status = adiv5_ap_read(ap, AAP_STATUS);
		target_print_progress(&timeout);
	} while (status & AAP_STATUS_ERASEBUSY);

	/* Read status */
	status = adiv5_ap_read(ap, AAP_STATUS);
	DEBUG_INFO("EFM32: AAP STATUS=%08" PRIx32 "\n", status);

	return true;
}
