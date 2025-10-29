/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011 Mike Smith <drziplok@me.com>
 * Copyright (C) 2016 Gareth McMullin <gareth@blacksphere.co.nz>
 * Copyright (C) 2022-2025 1BitSquared <info@1bitsquared.com>
 * Modified by Rachel Mant <git@dragonmux.network>
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
 * This file implements support for LPC11xx, LPC13xx and LPC8xx series devices, providing
 * memory maps and Flash programming routines.
 *
 * References and details about the IAP variant used here:
 * LPC802 32-bit ARM Cortex-M0+ microcontroller, Product data sheet, Rev. 1.9
 *   https://www.nxp.com/docs/en/data-sheet/LPC802.pdf
 * LPC804 32-bit ARM Cortex-M0+ microcontroller, Product data sheet, Rev. 2.1
 *   https://www.nxp.com/docs/en/nxp/data-sheets/LPC804_DS.pdf
 * LPC81xM 32-bit ARM Cortex-M0+ microcontroller, Product data sheet, Rev. 4.7
 *   https://www.nxp.com/docs/en/data-sheet/LPC81XM.pdf
 * LPC82x 32-bit ARM Cortex-M0+ microcontroller, Product data sheet, Rev. 1.5
 *   https://www.nxp.com/docs/en/data-sheet/LPC82X.pdf
 * LPC83x 32-bit ARM Cortex-M0+ microcontroller, Product data sheet, Rev. 1.2
 *   https://www.nxp.com/docs/en/data-sheet/LPC83X.pdf
 * LPC84x 32-bit ARM Cortex-M0+ microcontroller, Product data sheet, Rev. 2.1
 *   https://www.nxp.com/docs/en/data-sheet/LPC84x.pdf
 * LPC8N04 32-bit ARM Cortex-M0+ microcontroller, Product data sheet, Rev. 1.4
 *   https://www.nxp.com/docs/en/data-sheet/LPC8N04.pdf
 * LPC11Axx 32-bit ARM Cortex-M0 microcontroller, Product data sheet, Rev. 4
 *   https://www.nxp.com/docs/en/data-sheet/LPC11AXX.pdf
 * LPC111xLV/LPC11xxLVUK 32-bit ARM Cortex-M0 microcontroller, Product data sheet, Rev. 2
 *   https://www.nxp.com/docs/en/data-sheet/LPC111XLV_LPC11XXLVUK.pdf
 * LPC11U1x 32-bit ARM Cortex-M0 microcontroller, Product data sheet, Rev. 2.2
 *   https://www.nxp.com/docs/en/data-sheet/LPC11U1X.pdf
 * LPC11U2x 32-bit ARM Cortex-M0 microcontroller, Product data sheet, Rev. 2.3
 *   https://www.nxp.com/docs/en/data-sheet/LPC11U2X.pdf
 * LPC11U3x 32-bit ARM Cortex-M0 microcontroller, Product data sheet, Rev. 2.5
 *   https://www.nxp.com/docs/en/data-sheet/LPC11U3X.pdf
 * LPC11U6x 32-bit ARM Cortex-M0+ microcontroller, Product data sheet, Rev. 1.5
 *   https://www.nxp.com/docs/en/data-sheet/LPC11U6X.pdf
 * LPC13111/13/42/43 32-bit ARM Cortex-M3 microcontroller, Product data sheet, Rev. 5
 *   https://www.nxp.com/docs/en/data-sheet/LPC1311_13_42_43.pdf
 * and (behind their login wall):
 * UM11045 - LPC802 User manual, Rev. 1.5
 *   https://www.nxp.com/webapp/Download?colCode=UM11045&location=null
 * UM11065 - LPC804 User manual, Rev. 1.4
 *   https://www.nxp.com/webapp/Download?colCode=UM11065&location=null
 * UM10601 - LPC81x User manual, Rev. 1.7
 *   https://www.nxp.com/webapp/Download?colCode=UM10601&location=null
 * UM10800 - LPC82x User manual, Rev. 1.4
 *   https://www.nxp.com/webapp/Download?colCode=UM10800&location=null
 * UM11021 - LPC83x User manual, Rev. 1.1
 *   https://www.nxp.com/webapp/Download?colCode=UM11021&location=null
 * UM11029 - LPC84x User manual, Rev. 1.7
 *   https://www.nxp.com/webapp/Download?colCode=UM11029&location=null
 * UM11074 - LPC8N04 User manual, Rev. 1.3
 *   https://www.nxp.com/webapp/Download?colCode=UM11074&location=null
 * UM10429 - LPC1102/04 User manual, Rev. 6
 *   https://www.nxp.com/webapp/Download?colCode=UM10429&location=null
 * UM10398 - LPC111x/LPC11Cxx User manual, Rev. 12.5
 *   https://www.nxp.com/webapp/Download?colCode=UM10398&location=null
 * UM10578 - LPC11xxLV user manual, Rev. 1
 *   https://www.nxp.com/webapp/Download?colCode=UM10578&location=null
 * UM10462 - LPC11U3x/2x/1x User manual, Rev, 5.5
 *   https://www.nxp.com/webapp/Download?colCode=UM10462&location=null
 * UM10732 - LPC11U6x/Ex User manual, Rev, 1.9
 *   https://www.nxp.com/webapp/Download?colCode=UM10732&location=null
 * UM10839 - LPC112x User manual, Rev. 1.0
 *   https://www.nxp.com/webapp/Download?colCode=UM10839&location=null
 * UM10375 - LPC1311/13/42/43 User Manual, Rev. 5
 *   https://www.nxp.com/webapp/Download?colCode=UM10375&location=null
 */

#include <string.h>
#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "jep106.h"
#include "lpc_common.h"

/* Common memory map constants for LPC11xx and LPC8xx parts */
#define LPC11xx_FLASH_BASE       0x00000000U
#define LPC11xx_SRAM_BASE        0x10000000U
#define LPC8xx_FLASH_ERASE_SIZE  0x00000400U
#define LPC11xx_FLASH_ERASE_SIZE 0x00001000U

/* Memory map constants for LPC802 parts */
#define LPC802_FLASH_SIZE 0x00004000U
#define LPC802_SRAM_SIZE  0x00000800U
/* Memory map constants for LPC804 parts */
#define LPC804_FLASH_SIZE 0x00008000U
#define LPC804_SRAM_SIZE  0x00001000U
/* Memory map constants for LPC81x parts */
#define LPC810_FLASH_SIZE 0x00001000U
#define LPC811_FLASH_SIZE 0x00002000U
#define LPC81x_FLASH_SIZE 0x00004000U
#define LPC810_SRAM_SIZE  0x00000400U
#define LPC811_SRAM_SIZE  0x00000200U
#define LPC81x_SRAM_SIZE  0x00001000U
/* Memory map constants for LPC82x parts */
#define LPC822_FLASH_SIZE 0x00004000U
#define LPC824_FLASH_SIZE 0x00008000U
#define LPC822_SRAM_SIZE  0x00001000U
#define LPC824_SRAM_SIZE  0x00002000U
/* Memory map constants for LPC83x parts */
#define LPC832_FLASH_SIZE 0x00004000U
#define LPC834_FLASH_SIZE 0x00008000U
#define LPC83x_SRAM_SIZE  0x00001000U
/* Memory map constants for LPC84x parts */
#define LPC84x_FLASH_SIZE 0x00010000U
#define LPC844_SRAM_SIZE  0x00002000U
#define LPC845_SRAM_SIZE  0x00004000U
/* Memory map constants for LPC8N04 parts */
#define LPC8N04_FLASH_SIZE 0x00007800U
#define LPC8N04_SRAM_SIZE  0x00002000U
/* Memory map constants for LPC111x-XL parts */
#define LPC111x_SRAM_2KiB 0x00000800U
#define LPC111x_SRAM_4KiB 0x00001000U
#define LPC111x_SRAM_8KiB 0x00002000U
/* Memory map constants for LPC11U3x parts */
#define LPC11U34_311_FLASH_SIZE 0x0000a000U
#define LPC11U34_421_FLASH_SIZE 0x0000c000U
#define LPC11U35_FLASH_SIZE     0x00010000U
#define LPC11U36_FLASH_SIZE     0x00018000U
#define LPC11U37_FLASH_SIZE     0x00020000U
#define LPC11U3x_SRAM_SIZE      0x00002000U
/* Memory map constants for LPC11U6x parts */
#define LPC11x66_FLASH_SIZE 0x00010000U
#define LPC11x67_FLASH_SIZE 0x00020000U
#define LPC11x68_FLASH_SIZE 0x00040000U
#define LPC11x66_SRAM_SIZE  0x00003000U
#define LPC11x67_SRAM_SIZE  0x00005000U
#define LPC11x68_SRAM_SIZE  0x00009000U
/* Memory map constants for LPC112x parts */
#define LPC1124_FLASH_SIZE 0x00008000U
#define LPC1125_FLASH_SIZE 0x00010000U
#define LPC112x_SRAM_SIZE  0x00002000U
/* Memory map constants for LPC13xx parts */
#define LPC1311_FLASH_SIZE 0x00002000U
#define LPC1342_FLASH_SIZE 0x00004000U
#define LPC13x3_FLASH_SIZE 0x00008000U
#define LPC13xx_SRAM_SIZE  0x00001000U
#define LPC13x3_SRAM_SIZE  0x00002000U

/* IAP constants and locations */
#define LPC11xx_SRAM_SIZE_MIN 1024U
#define LPC11xx_SRAM_IAP_SIZE 32U /* IAP routines use 32 bytes at top of ram */

#define LPC11xx_IAP_ENTRYPOINT_LOCATION 0x1fff1ff1U /* All except LPC802, LPC804 & LPC84x */
#define LPC8xx_IAP_ENTRYPOINT_LOCATION  0x0f001ff1U /* LPC802, LPC804 & LPC84x */
#define LPC11xx_IAP_RAM_BASE            0x10000000U

#define LPC11xx_IAP_PGM_CHUNKSIZE 512U /* Should fit in RAM on any device */

/* SYSCON and device ID register locations */
#define LPC11xx_SYSCON_BASE      0x40048000U
#define LPC8xx_SYSCON_DEVICE_ID  (LPC11xx_SYSCON_BASE + 0x3f8U)
#define LPC11xx_SYSCON_DEVICE_ID (LPC11xx_SYSCON_BASE + 0x3f4U)

/* Taken from UM11045 §6.6.29 Device ID register, pg72 */
#define ID_LPC802M001JDH20 0x00008021U /* LPC802M001JDH20/LPC802UK */
#define ID_LPC802M011JDH20 0x00008022U
#define ID_LPC802M001JDH16 0x00008023U
#define ID_LPC802M001JHI33 0x00008024U
/* Taken from UM11065 §6.6.31 Device ID register, pg76 */
#define ID_LPC804M101JBD64 0x00008040U
#define ID_LPC804M101JDH20 0x00008041U
#define ID_LPC804M101JDH24 0x00008042U
#define ID_LPC804M111JDH24 0x00008043U
#define ID_LPC804M101JHI33 0x00008044U
/* Taken from UM10601 §4.6.34 Device ID register, pg49 */
#define ID_LPC810M021FN8   0x00008100U
#define ID_LPC811M001JDH16 0x00008110U
#define ID_LPC812M101JDH16 0x00008120U
#define ID_LPC812M101JD20  0x00008121U
#define ID_LPC812M101Jxxxx 0x00008122U /* LPC812M101JDH20/LPC812M101JTB16 */
/* Taken from UM10800 §5.6.34 Device ID register, pg53 */
#define ID_LPC822M101JHI33 0x00008221U
#define ID_LPC822M101JDH20 0x00008222U
#define ID_LPC824M201JHI33 0x00008241U
#define ID_LPC824M201JDH20 0x00008242U
#define ID_LPC82x_MASK     0x000000f0U
#define ID_LPC822          0x00000020U
/* Taken from UM11021 §5.6.34 Device ID register, pg53 */
#define ID_LPC832M101FDH20 0x00008322U
#define ID_LPC8341201FHI33 0x00008341U
/* Taken from UM11029 §8.6.49 Device ID register, pg120 */
#define ID_LPC844M201JBD64 0x00008441U
#define ID_LPC844M201JBD48 0x00008442U
#define ID_LPC844M201JHI48 0x00008443U
#define ID_LPC844M201JHI33 0x00008444U
#define ID_LPC845M301JBD64 0x00008451U
#define ID_LPC845M301JBD48 0x00008452U
#define ID_LPC845M301JHI48 0x00008453U
#define ID_LPC845M301JHI33 0x00008454U
#define ID_LPC84x_MASK     0x000000f0U
#define ID_LPC844          0x00000040U
/* Taken from UM11074 §4.5.19 Device ID register, pg23 */
#define ID_LPC8N04 0x00008a04U
/* Taken from UM10389 §3.5.37 Device ID register, pg45 */
#define ID_LPC1110_0     0x0a07102bU
#define ID_LPC1110_1     0x1a07102bU
#define ID_LPC1111_002_0 0x0a16d02bU
#define ID_LPC1111_002_1 0x1a16d02bU
#define ID_LPC1111_101   0x041e502bU
#define ID_LPC1111_102   0x2516d02bU
#define ID_LPC1111_201   0x0416502bU
#define ID_LPC1111_202   0x2516902bU
#define ID_LPC1112_101_0 0x042d502bU
#define ID_LPC1112_101_1 0x2524d02bU
#define ID_LPC1112_102_0 0x0a23902bU
#define ID_LPC1112_102_1 0x1a23902bU
#define ID_LPC1112_102_2 0x0a24902bU
#define ID_LPC1112_102_3 0x1a24902bU
#define ID_LPC1112_201   0x0425502bU
#define ID_LPC1112_202   0x2524902bU
#define ID_LPC1113_201   0x0434502bU
#define ID_LPC1113_202   0x2532902bU
#define ID_LPC1113_301   0x0434102bU
#define ID_LPC1113_302   0x2532102bU
#define ID_LPC1114_102_0 0x0a40902bU
#define ID_LPC1114_102_1 0x1a40902bU
#define ID_LPC1114_201   0x0444502bU
#define ID_LPC1114_202   0x2540902bU
#define ID_LPC1114_301   0x0444102bU
#define ID_LPC1114_302   0x2540102bU
#define ID_LPC11C12_301  0x1421102bU
#define ID_LPC11C14_301  0x1440102bU
#define ID_LPC11C22_301  0x1431102bU
#define ID_LPC11C24_301  0x1430102bU
/* Taken from UM10398 §25.5.11 Read Part Identification number, pg431 */
#define ID_LPC1111_203         0x00010012U
#define ID_LPC1111_103         0x00010013U
#define ID_LPC1112_203         0x00020022U
#define ID_LPC1112_103         0x00020023U
#define ID_LPC1113_303         0x00030030U
#define ID_LPC1113_203         0x00030032U
#define ID_LPC1114_303         0x00040040U
#define ID_LPC1114_203         0x00040042U
#define ID_LPC1114_323         0x00040060U
#define ID_LPC1114_333         0x00040070U
#define ID_LPC1115_303         0x00050080U
#define ID_LPC111x_SRAM_MASK   (0xfU << 0U)
#define ID_LPC111x_SRAM_2KiB   (0x3U << 0U)
#define ID_LPC111x_SRAM_4KiB   (0x2U << 0U)
#define ID_LPC111x_SRAM_8KiB   (0x0U << 0U)
#define ID_LPC111x_FLASH_SHIFT 4U
#define ID_LPC111x_FLASH_MASK  (0xfU << ID_LPC111x_FLASH_SHIFT)
/* Taken from UM10462 §3.5.424 Device ID register, pg44 */
#define ID_LPC11U12_201_0 0x095c802bU
#define ID_LPC11U12_201_1 0x295c802bU
#define ID_LPC11U13_201_0 0x097a802bU
#define ID_LPC11U13_201_1 0x297a802bU
#define ID_LPC11U14_201_0 0x0998802bU
#define ID_LPC11U14_201_1 0x2998802bU
#define ID_LPC11U22_301   0x2954402bU
#define ID_LPC11U23_301   0x2972402bU
#define ID_LPC11U24_301   0x2988402bU
#define ID_LPC11U24_401   0x2980002bU
/* Taken from UM10462 §20.13.11 Read Part Identification number, pg407 */
#define ID_LPC11U34_311    0x0003d440U
#define ID_LPC11U34_421    0x0001cc40U
#define ID_LPC11U35_401    0x0001bc40U
#define ID_LPC11U35_501    0x0000bc40U
#define ID_LPC11U36_401    0x00019c40U
#define ID_LPC11U37x48_401 0x00017c40U
#define ID_LPC11U37x64_401 0x00007c44U
#define ID_LPC11U37x64_501 0x00007c40U
/* Taken from UM10732 §4.4.9 Device ID register, pg61 */
#define ID_LPC11E66           0x0000dcc1U
#define ID_LPC11E67           0x0000bc81U
#define ID_LPC11E68           0x00007c01U
#define ID_LPC11U66           0x0000dcc8U
#define ID_LPC11U67           0x0000bc88U
#define ID_LPC11U67_100       0x0000bc80U
#define ID_LPC11U68           0x00007c08U
#define ID_LPC11U68_100       0x00007c00U
#define ID_LPC11x6x_PART_MASK (0xfU << 12U)
#define ID_LPC11x6x_PART_xx6  (0xdU << 12U)
#define ID_LPC11x6x_PART_xx7  (0xbU << 12U)
#define ID_LPC11x6x_PART_xx8  (0x7U << 12U)
/* Taken from UM10839 §18.4.11 Read Part Identification number, pg271 */
#define ID_LPC1124 0x00140040U
#define ID_LPC1125 0x00150080U
/* Taken from UM10375 §3.5.48 Device ID register, pg43 */
#define ID_LPC1311             0x2c42502bU
#define ID_LPC1311_01          0x1816902bU
#define ID_LPC1313             0x2c40102bU
#define ID_LPC1313_01          0x1830102bU
#define ID_LPC1342             0x3d01402bU
#define ID_LPC1343             0x3d00002bU
#define ID_LPC13xx_FLASH_MASK  (0x3U << 16U)
#define ID_LPC13xx_FLASH_32KiB (0x0U << 16U)
#define ID_LPC13xx_FLASH_16KiB (0x1U << 16U)
#define ID_LPC13xx_FLASH_8KiB  (0x2U << 16U)

/*
 * Chip    RAM Flash page sector   Rsvd pages  EEPROM
 * LPX80x   2k   16k   64   1024            2
 * LPC804   4k   32k   64   1024            2
 * LPC8N04  8k   32k   64   1024           32
 * LPC810   1k    4k   64   1024            0
 * LPC811   2k    8k   64   1024            0
 * LPC812   4k   16k   64   1024
 * LPC822   4k   16k   64   1024
 * LPC822   8k   32k   64   1024
 * LPC832   4k   16k   64   1024
 * LPC834   4k   32k   64   1024
 * LPC844   8k   64k   64   1024
 * LPC845  16k   64k   64   1024
 */

static bool lpc8xx_flash_mode(target_s *target);

static void lpc11xx_add_flash(
	target_s *target, const uint32_t addr, const size_t len, const size_t erase_block_len, const size_t reserved_pages)
{
	lpc_flash_s *const flash = lpc_add_flash(target, addr, len, LPC11xx_IAP_PGM_CHUNKSIZE);
	flash->target_flash.blocksize = erase_block_len;
	flash->target_flash.write = lpc_flash_write_magic_vect;
	flash->reserved_pages = reserved_pages;
}

static bool lpc11xx_priv_init(target_s *const target, const target_addr32_t iap_entry)
{
	/* Allocate the private structure needed for lpc_iap_call() to work */
	lpc_priv_s *const priv = calloc(1, sizeof(*priv));
	if (!priv) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return false;
	}
	target->target_storage = priv;

	/* Set the structure up for this target */
	priv->iap_params = lpc_iap_params;
	priv->iap_entry = iap_entry;
	priv->iap_ram = LPC11xx_IAP_RAM_BASE;
	priv->iap_msp = LPC11xx_IAP_RAM_BASE + LPC11xx_SRAM_SIZE_MIN - LPC11xx_SRAM_IAP_SIZE;
	return true;
}

static bool lpc11xx_detect(target_s *const target)
{
	/*
	 * Read the device ID register
	 *
	 * For LPC11xx & LPC11Cxx see UM10398 Rev. 12.4 §26.5.11 Table 387
	 * For LPC11Uxx see UM10462 Rev. 5.5 §20.13.11 Table 377
	 * NB: the DEVICE_ID register at address 0x400483f4 is not valid for:
	 *   1) the LPC11xx & LPC11Cxx "XL" series, see UM10398 Rev.12.4 §3.1
	 *   2) the LPC11U3x series, see UM10462 Rev.5.5 §3.1
	 * But see the comment for the LPC8xx series below.
	 */
	const uint32_t device_id = target_mem32_read32(target, LPC11xx_SYSCON_DEVICE_ID);

	switch (device_id) {
	case ID_LPC1110_0: /* 4KiB Flash, 1KiB SRAM */
	case ID_LPC1110_1:
	case ID_LPC1111_002_0: /* 8KiB Flash, 2KiB SRAM */
	case ID_LPC1111_002_1:
	case ID_LPC1111_101:
	case ID_LPC1111_102:
	case ID_LPC1111_201: /* 8KiB Flash, 4KiB SRAM */
	case ID_LPC1111_202:
	case ID_LPC1112_101_0: /* 16KiB Flash 2KiB SRAM */
	case ID_LPC1112_101_1:
	case ID_LPC1112_102_0: /* 16KiB Flash, 4KiB SRAM */
	case ID_LPC1112_102_1:
	case ID_LPC1112_102_2:
	case ID_LPC1112_102_3:
	case ID_LPC1112_201:
	case ID_LPC1112_202:
	case ID_LPC1113_201: /* 24KiB Flash 4KiB SRAM */
	case ID_LPC1113_202:
	case ID_LPC1113_301: /* 24KiB Flash, 8KiB SRAM */
	case ID_LPC1113_302:
	case ID_LPC1114_102_0: /* 32KiB Flash, 4KiB SRAM */
	case ID_LPC1114_102_1:
	case ID_LPC1114_201:
	case ID_LPC1114_202:
	case ID_LPC1114_301: /* 32KiB Flash, 8KiB SRAM */
	case ID_LPC1114_302:
	case ID_LPC11C12_301: /* 16KiB Flash, 8KiB SRAM */
	case ID_LPC11C22_301:
	case ID_LPC11C14_301: /* 32KiB Flash, 8KiB SRAM */
	case ID_LPC11C24_301:
	case ID_LPC11U12_201_0: /* 16KiB Flash, 4KiB SRAM */
	case ID_LPC11U12_201_1:
	case ID_LPC11U13_201_0: /* 24KiB Flash, 4KiB SRAM */
	case ID_LPC11U13_201_1:
	case ID_LPC11U14_201_0: /* 32KiB Flash, 4KiB SRAM */
	case ID_LPC11U14_201_1:
	case ID_LPC11U22_301: /* 16KiB Flash, 6KiB SRAM */
	case ID_LPC11U23_301: /* 24KiB Flash, 6KiB SRAM */
	case ID_LPC11U24_301: /* 32KiB Flash, 6KiB SRAM */
	case ID_LPC11U24_401: /* 32KiB Flash, 8KiB SRAM */
		target->driver = "LPC11xx";
		target_add_ram32(target, LPC11xx_SRAM_BASE, 0x2000);
		lpc11xx_add_flash(target, LPC11xx_FLASH_BASE, 0x8000, 0x1000, 0);
		break;
	case ID_LPC1311: /* 8KiB Flash, 4KiB SRAM */
	case ID_LPC1311_01:
	case ID_LPC1313: /* 32KiB Flash, 8KiB SRAM */
	case ID_LPC1313_01:
	case ID_LPC1342: /* 16KiB Flash, 4KiB SRAM */
	case ID_LPC1343: /* 32KiB Flash, 8KiB SRAM */
	case 0x3000002bU:
		target->driver = "LPC13xx";
		if ((device_id & ID_LPC13xx_FLASH_MASK) == ID_LPC13xx_FLASH_32KiB)
			target_add_ram32(target, LPC11xx_SRAM_BASE, LPC13x3_SRAM_SIZE);
		else
			target_add_ram32(target, LPC11xx_SRAM_BASE, LPC13xx_SRAM_SIZE);
		if ((device_id & ID_LPC13xx_FLASH_MASK) == ID_LPC13xx_FLASH_32KiB)
			lpc11xx_add_flash(target, LPC11xx_FLASH_BASE, LPC13x3_FLASH_SIZE, LPC11xx_FLASH_ERASE_SIZE, 0U);
		else if ((device_id & ID_LPC13xx_FLASH_MASK) == ID_LPC13xx_FLASH_16KiB)
			lpc11xx_add_flash(target, LPC11xx_FLASH_BASE, LPC1342_FLASH_SIZE, LPC11xx_FLASH_ERASE_SIZE, 0U);
		else if ((device_id & ID_LPC13xx_FLASH_MASK) == ID_LPC13xx_FLASH_8KiB)
			lpc11xx_add_flash(target, LPC11xx_FLASH_BASE, LPC1311_FLASH_SIZE, LPC11xx_FLASH_ERASE_SIZE, 0U);
		break;
	case ID_LPC8N04:
		target->driver = "LPC8N04";
		target_add_ram32(target, LPC11xx_SRAM_BASE, LPC8N04_SRAM_SIZE);
		/*
		 * UM11074 §15.2 Flash controller, pg97: The two topmost sectors
		 * contain the initialization code and IAP firmware.
		 * Do not touch them!
		 */
		lpc11xx_add_flash(target, LPC11xx_FLASH_BASE, LPC8N04_FLASH_SIZE, LPC8xx_FLASH_ERASE_SIZE, 0U);
		break;
	default:
		if (device_id && target->designer_code != JEP106_MANUFACTURER_SPECULAR)
			DEBUG_INFO("%s: Unknown Device ID 0x%08" PRIx32 "\n", "LPC11xx", device_id);
		return false;
	}

	return lpc11xx_priv_init(target, LPC11xx_IAP_ENTRYPOINT_LOCATION);
}

static bool lpc8xx_detect(target_s *const target)
{
	/*
	 * For LPC802, see UM11045 Rev. 1.4 §6.6.29 Table 84
	 * For LPC804, see UM11065 Rev. 1.0 §6.6.31 Table 87
	 * For LPC81x, see UM10601 Rev. 1.6 §4.6.33 Table 50
	 * For LPC82x, see UM10800 Rev. 1.2 §5.6.34 Table 55
	 * For LPC83x, see UM11021 Rev. 1.1 §5.6.34 Table 53
	 * For LPC84x, see UM11029 Rev. 1.4 §8.6.49 Table 174
	 *
	 * Not documented, but the DEVICE_ID register at address 0x400483f8
	 * for the LPC8xx series is also valid for the LPC11xx "XL" and the
	 * LPC11U3x variants.
	 */
	const uint32_t device_id = target_mem32_read32(target, LPC8xx_SYSCON_DEVICE_ID);
	target_addr32_t iap_entry = 0U;

	switch (device_id) {
	case ID_LPC802M001JDH20: /* 16KiB Flash, 2KiB SRAM */
	case ID_LPC802M011JDH20:
	case ID_LPC802M001JDH16:
	case ID_LPC802M001JHI33:
		target->driver = "LPC802";
		target_add_ram32(target, LPC11xx_SRAM_BASE, LPC802_SRAM_SIZE);
		lpc11xx_add_flash(target, LPC11xx_FLASH_BASE, LPC802_FLASH_SIZE, LPC8xx_FLASH_ERASE_SIZE, 2U);
		iap_entry = LPC8xx_IAP_ENTRYPOINT_LOCATION;
		break;
	case ID_LPC804M101JBD64: /* 32KiB Flash, 4KiB SRAM */
	case ID_LPC804M101JDH20:
	case ID_LPC804M101JDH24:
	case ID_LPC804M111JDH24:
	case ID_LPC804M101JHI33:
		target->driver = "LPC804";
		target_add_ram32(target, LPC11xx_SRAM_BASE, LPC804_SRAM_SIZE);
		lpc11xx_add_flash(target, LPC11xx_FLASH_BASE, LPC804_FLASH_SIZE, LPC8xx_FLASH_ERASE_SIZE, 2U);
		iap_entry = LPC8xx_IAP_ENTRYPOINT_LOCATION;
		break;
	case ID_LPC810M021FN8:   /* 4KiB Flash, 1KiB SRAM */
	case ID_LPC811M001JDH16: /* 8KiB Flash, 2KiB SRAM */
	case ID_LPC812M101JDH16: /* 16KiB Flash, 4KiB SRAM */
	case ID_LPC812M101JD20:
	case ID_LPC812M101Jxxxx:
		target->driver = "LPC81x";
		if (device_id == ID_LPC810M021FN8) {
			target_add_ram32(target, LPC11xx_SRAM_BASE, LPC810_SRAM_SIZE);
			lpc11xx_add_flash(target, LPC11xx_FLASH_BASE, LPC810_FLASH_SIZE, LPC8xx_FLASH_ERASE_SIZE, 0U);
		} else if (device_id == ID_LPC811M001JDH16) {
			target_add_ram32(target, LPC11xx_SRAM_BASE, LPC811_SRAM_SIZE);
			lpc11xx_add_flash(target, LPC11xx_FLASH_BASE, LPC811_FLASH_SIZE, LPC8xx_FLASH_ERASE_SIZE, 0U);
		} else {
			target_add_ram32(target, LPC11xx_SRAM_BASE, LPC81x_SRAM_SIZE);
			lpc11xx_add_flash(target, LPC11xx_FLASH_BASE, LPC81x_FLASH_SIZE, LPC8xx_FLASH_ERASE_SIZE, 0U);
		}
		iap_entry = LPC11xx_IAP_ENTRYPOINT_LOCATION;
		break;
	case ID_LPC822M101JHI33: /* 16KiB Flash, 4KiB SRAM */
	case ID_LPC822M101JDH20:
	case ID_LPC824M201JHI33: /* 32KiB Flash, 8KiB SRAM */
	case ID_LPC824M201JDH20:
		target->driver = "LPC82x";
		if ((device_id & ID_LPC82x_MASK) == ID_LPC822) {
			target_add_ram32(target, LPC11xx_SRAM_BASE, LPC822_SRAM_SIZE);
			lpc11xx_add_flash(target, LPC11xx_FLASH_BASE, LPC822_FLASH_SIZE, LPC8xx_FLASH_ERASE_SIZE, 0U);
		} else {
			target_add_ram32(target, LPC11xx_SRAM_BASE, LPC824_SRAM_SIZE);
			lpc11xx_add_flash(target, LPC11xx_FLASH_BASE, LPC824_FLASH_SIZE, LPC8xx_FLASH_ERASE_SIZE, 0U);
		}
		iap_entry = LPC11xx_IAP_ENTRYPOINT_LOCATION;
		break;
	case ID_LPC832M101FDH20: /* 16KiB Flash, 4KiB SRAM */
	case ID_LPC8341201FHI33: /* 32KiB Flash, 4KiB SRAM */
		target->driver = "LPC83x";
		target_add_ram32(target, LPC11xx_SRAM_BASE, LPC83x_SRAM_SIZE);
		if (device_id == ID_LPC832M101FDH20)
			lpc11xx_add_flash(target, LPC11xx_FLASH_BASE, LPC832_FLASH_SIZE, LPC8xx_FLASH_ERASE_SIZE, 0U);
		else
			lpc11xx_add_flash(target, LPC11xx_FLASH_BASE, LPC834_FLASH_SIZE, LPC8xx_FLASH_ERASE_SIZE, 0U);
		break;
	case ID_LPC844M201JBD64: /* 64KiB Flash, 8KiB SRAM */
	case ID_LPC844M201JBD48:
	case ID_LPC844M201JHI48:
	case ID_LPC844M201JHI33:
	case ID_LPC845M301JBD64: /* 64KiB Flash, 16KiB SRAM */
	case ID_LPC845M301JBD48:
	case ID_LPC845M301JHI48:
	case ID_LPC845M301JHI33:
		target->driver = "LPC84x";
		if ((device_id & ID_LPC84x_MASK) == ID_LPC844)
			target_add_ram32(target, LPC11xx_SRAM_BASE, LPC844_SRAM_SIZE);
		else
			target_add_ram32(target, LPC11xx_SRAM_BASE, LPC845_SRAM_SIZE);
		lpc11xx_add_flash(target, LPC11xx_FLASH_BASE, LPC84x_FLASH_SIZE, LPC8xx_FLASH_ERASE_SIZE, 0U);
		iap_entry = LPC8xx_IAP_ENTRYPOINT_LOCATION;
		break;
	case ID_LPC11U34_311: /* 40KiB Flash, 8KiB SRAM */
	case ID_LPC11U34_421: /* 48KiB Flash, 8KiB SRAM */
	case ID_LPC11U35_401: /* 64KiB Flash, 8KiB SRAM */
	case ID_LPC11U35_501:
	case ID_LPC11U36_401:    /* 96KiB Flash, 8KiB SRAM */
	case ID_LPC11U37x48_401: /* 128KiB Flash, 8KiB SRAM */
	case ID_LPC11U37x64_401:
	case ID_LPC11U37x64_501:
		target->driver = "LPC11U3x";
		target_add_ram32(target, LPC11xx_SRAM_BASE, LPC11U3x_SRAM_SIZE);
		if (device_id == ID_LPC11U34_311)
			lpc11xx_add_flash(target, LPC11xx_FLASH_BASE, LPC11U34_311_FLASH_SIZE, LPC11xx_FLASH_ERASE_SIZE, 0U);
		else if (device_id == ID_LPC11U34_421)
			lpc11xx_add_flash(target, LPC11xx_FLASH_BASE, LPC11U34_421_FLASH_SIZE, LPC11xx_FLASH_ERASE_SIZE, 0U);
		else if (device_id == ID_LPC11U35_401 || device_id == ID_LPC11U35_501)
			lpc11xx_add_flash(target, LPC11xx_FLASH_BASE, LPC11U35_FLASH_SIZE, LPC11xx_FLASH_ERASE_SIZE, 0U);
		else if (device_id == ID_LPC11U36_401)
			lpc11xx_add_flash(target, LPC11xx_FLASH_BASE, LPC11U36_FLASH_SIZE, LPC11xx_FLASH_ERASE_SIZE, 0U);
		else
			lpc11xx_add_flash(target, LPC11xx_FLASH_BASE, LPC11U37_FLASH_SIZE, LPC11xx_FLASH_ERASE_SIZE, 0U);
		iap_entry = LPC11xx_IAP_ENTRYPOINT_LOCATION;
		break;
	case ID_LPC11E66: /* 64KiB Flash, 12KiB SRAM */
	case ID_LPC11U66:
	case ID_LPC11E67: /* 128KiB Flash, 20KiB SRAM */
	case ID_LPC11U67:
	case ID_LPC11U67_100:
	case ID_LPC11E68: /* 256KiB Flash, 36KiB SRAM */
	case ID_LPC11U68:
	case ID_LPC11U68_100:
		target->driver = "LPC11U6x";
		if ((device_id & ID_LPC11x6x_PART_MASK) == ID_LPC11x6x_PART_xx6) {
			target_add_ram32(target, LPC11xx_SRAM_BASE, LPC11x66_SRAM_SIZE);
			lpc11xx_add_flash(target, LPC11xx_FLASH_BASE, LPC11x66_FLASH_SIZE, LPC11xx_FLASH_ERASE_SIZE, 0U);
		} else if ((device_id & ID_LPC11x6x_PART_MASK) == ID_LPC11x6x_PART_xx7) {
			target_add_ram32(target, LPC11xx_SRAM_BASE, LPC11x67_SRAM_SIZE);
			lpc11xx_add_flash(target, LPC11xx_FLASH_BASE, LPC11x67_FLASH_SIZE, LPC11xx_FLASH_ERASE_SIZE, 0U);
		} else if ((device_id & ID_LPC11x6x_PART_MASK) == ID_LPC11x6x_PART_xx8) {
			target_add_ram32(target, LPC11xx_SRAM_BASE, LPC11x68_SRAM_SIZE);
			lpc11xx_add_flash(target, LPC11xx_FLASH_BASE, LPC11x68_FLASH_SIZE, LPC11xx_FLASH_ERASE_SIZE, 0U);
		}
		break;
	case ID_LPC1111_103:   /* 8KiB Flash, 2KiB SRAM */
	case ID_LPC1111_203:   /* 8KiB Flash, 4KiB SRAM */
	case ID_LPC1112_103:   /* 16KiB Flash, 2KiB SRAM */
	case ID_LPC1112_203:   /* 16KiB Flash, 4KiB SRAM */
	case ID_LPC1113_203:   /* 24KiB Flash, 4KiB SRAM */
	case ID_LPC1113_303:   /* 24KiB Flash, 8KiB SRAM */
	case ID_LPC1114_203:   /* 32KiB Flash, 4KiB SRAM */
	case ID_LPC1114_303:   /* 32KiB Flash, 8KiB SRAM */
	case ID_LPC1114_323:   /* 48KiB Flash, 8KiB SRAM */
	case ID_LPC1114_333:   /* 56KiB Flash, 8KiB SRAM */
	case ID_LPC1115_303: { /* 64KiB Flash, 8KiB SRAM */
		target->driver = "LPC111x-XL";
		/* Available Flash on these parts is encoded as the second nibble of the part number as a multiple of 8KiB */
		const uint32_t flash_size = ((device_id & ID_LPC111x_FLASH_MASK) >> ID_LPC111x_FLASH_SHIFT) * 8192U;
		const uint8_t sram_enc = (uint8_t)(device_id & ID_LPC111x_SRAM_MASK);
		if (sram_enc == ID_LPC111x_SRAM_2KiB)
			target_add_ram32(target, LPC11xx_SRAM_BASE, LPC111x_SRAM_2KiB);
		else if (sram_enc == ID_LPC111x_SRAM_4KiB)
			target_add_ram32(target, LPC11xx_SRAM_BASE, LPC111x_SRAM_4KiB);
		else
			target_add_ram32(target, LPC11xx_SRAM_BASE, LPC111x_SRAM_8KiB);
		lpc11xx_add_flash(target, LPC11xx_FLASH_BASE, flash_size, LPC11xx_FLASH_ERASE_SIZE, 0U);
		iap_entry = LPC11xx_IAP_ENTRYPOINT_LOCATION;
		break;
	}
	case ID_LPC1124: /* 32KiB Flash, 8KiB SRAM */
	case ID_LPC1125: /* 64KiB Flash, 8KiB SRAM */
		target->driver = "LPC112x";
		target_add_ram32(target, LPC11xx_SRAM_BASE, LPC112x_SRAM_SIZE);
		lpc11xx_add_flash(target, LPC11xx_FLASH_BASE, device_id == ID_LPC1124 ? LPC1124_FLASH_SIZE : LPC1125_FLASH_SIZE,
			LPC11xx_FLASH_ERASE_SIZE, 0U);
		iap_entry = LPC11xx_IAP_ENTRYPOINT_LOCATION;
		break;
	default:
		if (device_id)
			DEBUG_INFO("%s: Unknown Device ID 0x%08" PRIx32 "\n", "LPC8xx", device_id);
		return false;
	}

	/* Set up the target structure to work for Flash programming */
	target->enter_flash_mode = lpc8xx_flash_mode;
	target->exit_flash_mode = lpc8xx_flash_mode;
	return lpc11xx_priv_init(target, iap_entry);
}

bool lpc11xx_probe(target_s *const target)
{
	const bool result = lpc11xx_detect(target) || lpc8xx_detect(target);
	if (result)
		lpc_add_commands(target);
	return result;
}

static bool lpc8xx_flash_mode(target_s *const target)
{
	(void)target;
	return true;
}
