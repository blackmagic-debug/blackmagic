/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2022 1BitSquared <info@1bitsquared.com>
 * Written by Rafael Silva <perigoso@riseup.net>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* Support for Renesas RA family of microcontrollers (Arm Core) */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"
#include "adiv5.h"

#define RENESAS_PARTID_RA2A1 0x01b0U
#define RENESAS_PARTID_RA4M2 0x0340U
#define RENESAS_PARTID_RA4M3 0x0310U
#define RENESAS_PARTID_RA6M2 0x0150U

/*
 * Part numbering scheme
 *
 *  R7   F   A   xx   x   x   x   x   x   xx
 * \__/ \_/ \_/ \__/ \_/ \_/ \_/ \_/ \_/ \__/
 *  |    |   |   |    |   |   |   |   |   |
 *  |    |   |   |    |   |   |   |   |   \_ Package type
 *  |    |   |   |    |   |   |   |   \_____ Quality Grade
 *  |    |   |   |    |   |   |   \_________ Operating temperature
 *  |    |   |   |    |   |   \_____________ Code flash memory size
 *  |    |   |   |    |   \_________________ Feature set
 *  |    |   |   |    \_____________________ Group number
 *  |    |   |   \__________________________ Series name
 *  |    |   \______________________________ family (A: RA)
 *  |    \__________________________________ Flash memory
 *  \_______________________________________ Renesas microcontroller (always 'R7')
 *
 * Renesas Flash MCUs have an internal 16 byte read only register that stores
 * the part number, the code is stored ascii encoded, starting from the lowest memory address
 * except for pnrs stored in 'FIXED_PNR1', where the code is stored in reverse order (but the last 3 bytes are still 0x20 aka ' ')
 */

/* family + series + group no */
#define PNR_FAMILY_INDEX                   3
#define PNR_SERIES(pnr3, pnr4, pnr5, pnr6) (((pnr3) << 24U) | ((pnr4) << 16U) | ((pnr5) << 8U) | (pnr6))

typedef enum {
	PNR_SERIES_RA2L1 = PNR_SERIES('A', '2', 'L', '1'),
	PNR_SERIES_RA2E1 = PNR_SERIES('A', '2', 'E', '1'),
	PNR_SERIES_RA2E2 = PNR_SERIES('A', '2', 'E', '2'),
	PNR_SERIES_RA2A1 = PNR_SERIES('A', '2', 'A', '1'),
	PNR_SERIES_RA4M1 = PNR_SERIES('A', '4', 'M', '1'),
	PNR_SERIES_RA4M2 = PNR_SERIES('A', '4', 'M', '2'),
	PNR_SERIES_RA4M3 = PNR_SERIES('A', '4', 'M', '3'),
	PNR_SERIES_RA4E1 = PNR_SERIES('A', '4', 'E', '1'),
	PNR_SERIES_RA4E2 = PNR_SERIES('A', '4', 'E', '2'),
	PNR_SERIES_RA4W1 = PNR_SERIES('A', '4', 'W', '1'),
	PNR_SERIES_RA6M1 = PNR_SERIES('A', '6', 'M', '1'),
	PNR_SERIES_RA6M2 = PNR_SERIES('A', '6', 'M', '2'),
	PNR_SERIES_RA6M3 = PNR_SERIES('A', '6', 'M', '3'),
	PNR_SERIES_RA6M4 = PNR_SERIES('A', '6', 'M', '4'),
	PNR_SERIES_RA6M5 = PNR_SERIES('A', '6', 'M', '5'),
	PNR_SERIES_RA6E1 = PNR_SERIES('A', '6', 'E', '1'),
	PNR_SERIES_RA6E2 = PNR_SERIES('A', '6', 'E', '2'),
	PNR_SERIES_RA6T1 = PNR_SERIES('A', '6', 'T', '1'),
	PNR_SERIES_RA6T2 = PNR_SERIES('A', '6', 'T', '2'),
} pnr_series_t;

#define NONE 0xffffffff

typedef enum {
	FLASH_MF3,
	FLASH_MF4,
	FLASH_RV40,
} renesas_ra_flash_type_t;

typedef enum {
	PNR_FL1,
	PNR_FL2,
	PNR_FRT,
} renesas_sa_pnr_location;

typedef struct renesas_ra_family_details {
	uint32_t code_end;
	uint32_t option_start;
	uint32_t option_size;
	uint32_t option_start_2;
	uint32_t option_size_2;
	uint32_t data_flash_start;
	uint32_t data_flash_end;
	uint32_t factory_flash_start;
	uint32_t factory_size;
	uint32_t sramhs_start;
	uint32_t sramhs_end;
	uint32_t sram0_start;
	uint32_t sram0_end;
	uint32_t sram1_start;
	uint32_t sram1_end;
	uint32_t stdby_sram_start;
	uint32_t stdby_sram_end;
	renesas_ra_flash_type_t flash_type;
	renesas_sa_pnr_location pnr_location;
	uint32_t frt_start;
	uint32_t frt_size;
} renesas_ra_family_details_s;

typedef struct renesas_ra_family {
	pnr_series_t series;
	renesas_ra_family_details_s details;
} renesas_ra_family_s;

typedef struct renesas_option_setting_range {
	uint32_t addr;
	uint8_t size;
} renesas_option_setting_range_s;

typedef struct {
	pnr_series_t series;
	renesas_option_setting_range_s rw_registers[9];  /* 9 is maximum registers across all RA MCU series */
	renesas_option_setting_range_s opt_registers[5]; /* 5 is maximum registers across all RA MCU series */
} renesas_option_setting_s;

renesas_ra_family_s renesas_ra_family[] = {
	{PNR_SERIES_RA2L1,
		{0x00040000, 0x01010010, 36, NONE, NONE, 0x40100000, 0x40102000, NONE, NONE, NONE, NONE, 0x20000000, 0x20008000,
			NONE, NONE, NONE, NONE, FLASH_MF4, PNR_FL1, NONE, NONE}},
	{PNR_SERIES_RA2E1,
		{0x00020000, 0x01010010, 36, NONE, NONE, 0x40100000, 0x40101000, NONE, NONE, NONE, NONE, 0x20004000, 0x20008000,
			NONE, NONE, NONE, NONE, FLASH_MF4, PNR_FL1, NONE, NONE}},
	{PNR_SERIES_RA2E2,
		{0x00010000, 0x01010010, 36, NONE, NONE, 0x40100000, 0x40100800, NONE, NONE, NONE, NONE, 0x20004000, 0x20006000,
			NONE, NONE, NONE, NONE, FLASH_MF4, PNR_FL1, NONE, NONE}},
	{PNR_SERIES_RA2A1,
		{0x00040000, 0x01010008, 44, NONE, NONE, 0x40100000, 0x40102000, NONE, NONE, NONE, NONE, 0x20000000, 0x20008000,
			NONE, NONE, NONE, NONE, FLASH_MF3, PNR_FRT, 0x407fb19c, 4}},
	{PNR_SERIES_RA4M1,
		{0x00040000, 0x01010008, 44, NONE, NONE, 0x40100000, 0x40102000, NONE, NONE, NONE, NONE, 0x20000000, 0x20008000,
			NONE, NONE, NONE, NONE, FLASH_MF3, PNR_FRT, 0x407fb19c, 4}},
	{PNR_SERIES_RA4M2,
		{0x00080000, 0x0100a100, 512, NONE, NONE, 0x08000000, 0x08002000, 0x010080f0, 196, NONE, NONE, 0x20000000,
			0x20020000, NONE, NONE, 0x28000000, 0x28000400, FLASH_RV40, PNR_FL2, NONE, NONE}},
	{PNR_SERIES_RA4M3,
		{0x00100000, 0x0100a100, 512, NONE, NONE, 0x08000000, 0x08002000, 0x010080f0, 196, NONE, NONE, 0x20000000,
			0x20020000, NONE, NONE, 0x28000000, 0x28000400, FLASH_RV40, PNR_FL2, NONE, NONE}},
	{PNR_SERIES_RA4E1,
		{0x00080000, 0x0100a100, 512, NONE, NONE, 0x08000000, 0x08002000, 0x010080f0, 196, NONE, NONE, 0x20000000,
			0x20020000, NONE, NONE, 0x28000000, 0x28000400, FLASH_RV40, PNR_FL2, NONE, NONE}},
	{PNR_SERIES_RA4E2,
		{0x00020000, 0x0100a100, 512, NONE, NONE, 0x08000000, 0x08001000, 0x010080f0, 196, NONE, NONE, 0x20000000,
			0x2000a000, NONE, NONE, 0x28000000, 0x28000400, FLASH_RV40, PNR_FL2, NONE, NONE}},
	{PNR_SERIES_RA4W1,
		{0x00080000, 0x01010008, 44, NONE, NONE, 0x40100000, 0x40102000, NONE, NONE, NONE, NONE, 0x20000000, 0x20018000,
			NONE, NONE, NONE, NONE, FLASH_MF3, PNR_FRT, 0x407fb19c, 4}},
	{PNR_SERIES_RA6M1,
		{0x00080000, 0x01007000, 4096, 0x0100a150, 24, 0x40100000, 0x40102000, NONE, NONE, 0x1ffe0000, 0x20000000,
			0x20000000, 0x20020000, NONE, NONE, 0x200fe000, 0x20100000, FLASH_RV40, PNR_FRT, 0x407fb17c, 36}},
	{PNR_SERIES_RA6M2,
		{0x00100000, 0x01007000, 4096, 0x0100a150, 24, 0x40100000, 0x40108000, NONE, NONE, 0x1ffe0000, 0x20000000,
			0x20000000, 0x20040000, NONE, NONE, 0x200fe000, 0x20100000, FLASH_RV40, PNR_FRT, 0x407fb17c, 36}},
	{PNR_SERIES_RA6M3,
		{0x00200000, 0x01007000, 4096, 0x0100a150, 24, 0x40100000, 0x40110000, NONE, NONE, 0x1ffe0000, 0x20000000,
			0x20000000, 0x20040000, 0x20040000, 0x20080000, 0x200fe000, 0x20100000, FLASH_RV40, PNR_FRT, 0x407fb17c,
			36}},
	{PNR_SERIES_RA6M4,
		{0x00280000, 0x0100a100, 512, NONE, NONE, 0x08000000, 0x08002000, 0x010080f0, 196, NONE, NONE, 0x20000000,
			0x20040000, NONE, NONE, 0x28000000, 0x28000400, FLASH_RV40, PNR_FL2, NONE, NONE}},
	{PNR_SERIES_RA6M5,
		{0x00300000, 0x0100a100, 512, NONE, NONE, 0x08000000, 0x08002000, 0x010080f0, 196, NONE, NONE, 0x20000000,
			0x20080000, NONE, NONE, 0x28000000, 0x28000400, FLASH_RV40, PNR_FL2, NONE, NONE}},
	{PNR_SERIES_RA6E1,
		{0x00280000, 0x0100a100, 512, NONE, NONE, 0x08000000, 0x08002000, 0x010080f0, 196, NONE, NONE, 0x20000000,
			0x20040000, NONE, NONE, 0x28000000, 0x28000400, FLASH_RV40, PNR_FL2, NONE, NONE}},
	{PNR_SERIES_RA6E2,
		{0x00040000, 0x0100a100, 512, NONE, NONE, 0x08000000, 0x08001000, 0x010080f0, 196, NONE, NONE, 0x20000000,
			0x2000a000, NONE, NONE, 0x28000000, 0x28000400, FLASH_RV40, PNR_FL2, NONE, NONE}},
	{PNR_SERIES_RA6T1,
		{0x00080000, 0x01007000, 4096, 0x0100a150, 24, 0x40100000, 0x40102000, NONE, NONE, 0x1ffe0000, 0x1fff0000, NONE,
			NONE, NONE, NONE, NONE, NONE, FLASH_RV40, PNR_FRT, 0x407fb17c, 36}},
	{PNR_SERIES_RA6T2,
		{0x00080000, 0x0100a100, 512, NONE, NONE, 0x08000000, 0x08004000, 0x010080f0, 196, NONE, NONE, 0x20000000,
			0x20010000, NONE, NONE, 0x28000000, 0x28000400, FLASH_RV40, PNR_FL2, NONE, NONE}},
};

renesas_ra_family_details_s renesas_ra_family_lookup(pnr_series_t series)
{
	renesas_ra_family_details_s details = {};
	for (uint32_t i = 0U; i < sizeof(renesas_ra_family) / 19; i++) {
		if (renesas_ra_family[i].series == series) {
			return renesas_ra_family[i].details;
		}
	}
	return details;
}

/* Code flash memory size */
#define PNR_MEMSIZE_INDEX 8

typedef enum {
	PNR_MEMSIZE_16KB = '3',
	PNR_MEMSIZE_32KB = '5',
	PNR_MEMSIZE_64KB = '7',
	PNR_MEMSIZE_128KB = '9',
	PNR_MEMSIZE_256KB = 'B',
	PNR_MEMSIZE_384KB = 'C',
	PNR_MEMSIZE_512KB = 'D',
	PNR_MEMSIZE_768KB = 'E',
	PNR_MEMSIZE_1MB = 'F',
	PNR_MEMSIZE_1_5MB = 'G',
	PNR_MEMSIZE_2MB = 'H',
} pnr_memsize_t;

/* For future reference, if we want to add an info command
 *
 * Package type
 * FP: LQFP 100 pins 0.5 mm pitch
 * FN: LQFP 80 pins 0.5 mm pitch
 * FM: LQFP 64 pins 0.5 mm pitch
 * FL: LQFP 48 pins 0.5 mm pitch
 * NE: HWQFN 48 pins 0.5 mm pitch
 * FK: LQFP 64 pins 0.8 mm pitch
 * BU: BGA 64 pins 0.4 mm pitch
 * LM: LGA 36 pins 0.5 mm pitch
 * FJ: LQFP 32 pins 0.8 mm pitch
 * NH: HWQFN 32 pins 0.5 mm pitch
 * BV: WLCSP 25 pins 0.4 mm pitch
 * BT: BGA 36 pins
 * NK: HWQFN 24 pins 0.5 mm pitch
 * NJ: HWQFN 20 pins 0.5 mm pitch
 * BY: WLCSP 16 pins 0.4 mm pitch
 * NF: QFN 40 pins
 * LJ: LGA 100 pins
 * NB: QFN 64 pins
 * FB: LQFP 144 pins
 * NG: QFN 56 pins
 * LK: LGA 145 pins
 * BG: BGA 176 pins
 * FC: LQFP 176 pins
 *
 * Quality ID
 * C: Industrial applications
 * D: Consumer applications
 *
 * Operating temperature
 * 2: -40°C to +85°C
 * 3: -40°C to +105°C
 * 4: -40°C to +125°C
 */

/* PNR/UID location by series
 * newer series have a 'Flash Root Table'
 * older series have a fixed location in the flash memory
 *
 * ra2l1 - Fixed location 1
 * ra2e1 - Fixed location 1
 * ra2e2 - Fixed location 1
 * ra2a1 - Flash Root Table *undocumented
 * ra4m1 - *undocumented
 * ra4m2 - Fixed location 2 *undocumented
 * ra4m3 - Fixed location 2 *undocumented
 * ra4e1 - Fixed location 2
 * ra4w1 - *undocumented
 * ra6m1 - Flash Root Table
 * ra6m2 - Flash Root Table
 * ra6m3 - Flash Root Table
 * ra6m4 - Fixed location 2
 * ra6m5 - Fixed location 2
 * ra6e1 - Fixed location 2
 * ra6t1 - Flash Root Table
 * ra6t2 - Fixed location 2
 */
#define RENESAS_FIXED1_UID    UINT32_C(0x01001c00) /* Unique ID Register */
#define RENESAS_FIXED1_PNR    UINT32_C(0x01001c10) /* Part Numbering Register */
#define RENESAS_FIXED1_MCUVER UINT32_C(0x01001c20) /* MCU Version Register */

#define RENESAS_FIXED2_UID    UINT32_C(0x01008190) /* Unique ID Register */
#define RENESAS_FIXED2_PNR    UINT32_C(0x010080f0) /* Part Numbering Register */
#define RENESAS_FIXED2_MCUVER UINT32_C(0x010081b0) /* MCU Version Register */

/* The FMIFRT is a read-only register that stores the Flash Root Table address */
#define RENESAS_FMIFRT             UINT32_C(0x407fb19c)
#define RENESAS_FMIFRT_UID(frt)    ((frt) + 0x14U) /* UID Register offset from Flash Root Table */
#define RENESAS_FMIFRT_PNR(frt)    ((frt) + 0x24U) /* PNR Register offset from Flash Root Table */
#define RENESAS_FMIFRT_MCUVER(frt) ((frt) + 0x44U) /* MCUVER Register offset from Flash Root Table */

/* System Control OCD Control */
#define SYSC_BASE UINT32_C(0x4001e000)

#define SYSC_SYOCDCR  (SYSC_BASE + 0x40eU) /* System Control OCD Control Register */
#define SYOCDCR_DBGEN (1U << 7U)           /* Debug Enable */

#define SYSC_FWEPROR          (SYSC_BASE + 0x416U) /* Flash P/E Protect Register */
#define SYSC_FWEPROR_PERMIT   (0x01U)
#define SYSC_FWEPROR_PROHIBIT (0x10U)

/* Flash Memory Control */
#define FENTRYR_KEY_OFFSET 8U
#define FENTRYR_KEY        (0xaaU << FENTRYR_KEY_OFFSET)
#define FENTRYR_PE_CF      (1U)
#define FENTRYR_PE_DF      (1U << 7U)

/* Renesas RA MCUs can have one of two kinds of flash memory, MF3/4 and RV40 */

#define RENESAS_CF_END UINT32_C(0x00200000) /* End of Flash (maximum possible across families) */

/* MF3/4 Flash */
/*
 * MF3/4 Flash Memory Specifications
 * Block Size: Code area: 2 KB (except RA2A1 is 1KB), Data area: 1 KB
 * Program/Erase unit Program: Code area: 64 bits, Data area: 8 bits
 *					  Erase:  1 block
 */
#define MF3_CF_BLOCK_SIZE       (0x800U)
#define MF3_RA2A1_CF_BLOCK_SIZE (0x400U)
#define MF3_DF_BLOCK_SIZE       (0x400U)
#define MF3_CF_WRITE_SIZE       (0x40U)
#define MF3_DF_WRITE_SIZE       (0x1U)

/* RV40 Flash */
/*
 * RV40F Flash Memory Specifications
 * Block Size: Code area: 8 KB/32KB  Data area: 64 Bytes
 * Program/Erase unit Program: Code area: 128 Bytes, Data area: 4/8/16 Bytes
 *					  Erase: 1 block
 */
#define RV40_CF_REGION0_SIZE       (0x10000U)
#define RV40_CF_REGION0_BLOCK_SIZE (0x2000U)
#define RV40_CF_REGION1_BLOCK_SIZE (0x8000U)
#define RV40_DF_BLOCK_SIZE         (0x40U)
#define RV40_CF_WRITE_SIZE         (0x80U)
#define RV40_DF_WRITE_SIZE         (0x4U)
#define RV40_OF_WRITE_SIZE         (0x10U)

/* RV40 Flash Commands */
#define RV40_CMD               UINT32_C(0x407e0000)
#define RV40_CMD_PROGRAM       0xe8U
#define RV40_CMD_PROGRAM_CF    0x80U
#define RV40_CMD_PROGRAM_DF    0x02U
#define RV40_CMD_BLOCK_ERASE   0x20U
#define RV40_CMD_PE_SUSPEND    0xb0U
#define RV40_CMD_PE_RESUME     0xd0U
#define RV40_CMD_STATUS_CLEAR  0x50U
#define RV40_CMD_FORCED_STOP   0xb3U
#define RV40_CMD_BLANK_CHECK   0x71U
#define RV40_CMD_CONFIG_SET_1  0x40U
#define RV40_CMD_CONFIG_SET_2  0x08U
#define RV40_CMD_LOCK_BIT_PGM  0x77U
#define RV40_CMD_LOCK_BIT_READ 0x71U
#define RV40_CMD_FINAL         0xd0U

#define RV40_BASE UINT32_C(0x407fe000)

#define RV40_FASTAT       (RV40_BASE + 0x10U) /* Flash Access Status */
#define RV40_FASTAT_CMDLK (1U << 4U)          /* Command Lock */

#define RV40_FSTATR (RV40_BASE + 0x80U) /* Flash Status */

#define RV40_FSTATR_DBFULL (1U << 10U) /* Data Buffer Full */
#define RV40_FSTATR_RDY    (1U << 15U) /* Flash Ready */

#define RV40_FSTATR_PRGERR    (1U << 12U) /* Programming Error */
#define RV40_FSTATR_ERSERR    (1U << 13U) /* Erasure Error */
#define RV40_FSTATR_ILGLERR   (1U << 14U) /* Illegal Command Error */
#define RV40_FSTATR_OTERR     (1U << 20U) /* Other Error */
#define RV40_FSTATR_SECERR    (1U << 21U) /* Security Error */
#define RV40_FSTATR_FESETERR  (1U << 22U) /* FENTRY Setting Error */
#define RV40_FSTATR_ILGCOMERR (1U << 23U) /* Illegal Command Error */

#define RV40_FSADDR (RV40_BASE + 0x30U)

#define RV40_FMEPROT        (RV40_BASE + 0x44U)
#define RV40_FMEPROT_LOCK   (0xd901U)
#define RV40_FMEPROT_UNLOCK (0xd900U)

#define RV40_FENTRYR            (RV40_BASE + 0x84U)
#define RV40_FENTRYR_KEY_OFFSET 8U
#define RV40_FENTRYR_KEY        (0xaaU << RV40_FENTRYR_KEY_OFFSET)
#define RV40_FENTRYR_PE_CF      (1U)
#define RV40_FENTRYR_PE_DF      (1U << 7U)

#define RV40_FCPSR         (RV40_BASE + 0xe0U)
#define RV40_FCPSR_ESUSPMD 1U

static bool renesas_uid(target_s *t, int argc, const char **argv);

const command_s renesas_cmd_list[] = {
	{"uid", renesas_uid, "Prints unique number"},
	{NULL, NULL, NULL},
};

typedef struct renesas_priv {
	uint8_t pnr[17]; /* 16-byte PNR + 1-byte null termination */
	pnr_series_t series;
	uint32_t flash_root_table; /* if applicable */
	renesas_ra_family_details_s details;
} renesas_priv_s;

static uint32_t renesas_fmifrt_read(target_s *t)
{
	return target_mem_read32(t, RENESAS_FMIFRT);
}

static void renesas_uid_read(target_s *t, const uint32_t base, uint8_t *uid)
{
	uint32_t uidr[4];
	for (size_t i = 0U; i < 4U; i++)
		uidr[i] = target_mem_read32(t, base + i * 4U);

	for (size_t i = 0U; i < 16U; i++)
		uid[i] = uidr[i / 4U] >> (i & 3U) * 8U; /* & 3U == % 4U */
}

static bool renesas_pnr_read(target_s *t, const uint32_t base, uint8_t *pnr)
{
	uint32_t pnrr[4];
	for (size_t i = 0U; i < 4U; i++)
		pnrr[i] = target_mem_read32(t, base + i * 4U);

	if (base == RENESAS_FIXED1_PNR) {
		/* Renesas... look what you made me do...  */
		/* reverse order, see 'Part numbering scheme' note for context */
		for (size_t i = 0U; i < 13U; i++)
			pnr[i] = pnrr[3U - (i + 3U) / 4U] >> (24U - ((i + 3U) & 3U) * 8U); /* & 3U == % 4U */
		memset(pnr + 13U, 0x20, 3);
	} else {
		for (size_t i = 0; i < 16U; i++)
			pnr[i] = pnrr[i / 4U] >> (i & 3U) * 8U; /* & 3U == % 4U */
	}

	/* all Renesas mcus start with 'R7', sanity check */
	return pnr[0] == 'R' && pnr[1] == '7';
}

static pnr_series_t renesas_series(const uint8_t *pnr)
{
	uint32_t series = 0;
	for (size_t i = 0; i < 4U; i++)
		series = (series << 8U) | pnr[PNR_FAMILY_INDEX + i];

	return (pnr_series_t)series;
}

static uint32_t renesas_flash_size(const uint8_t *pnr)
{
	switch (pnr[PNR_MEMSIZE_INDEX]) {
	case PNR_MEMSIZE_16KB:
		return UINT32_C(16 * 1024);
	case PNR_MEMSIZE_32KB:
		return UINT32_C(32 * 1024);
	case PNR_MEMSIZE_64KB:
		return UINT32_C(64 * 1024);
	case PNR_MEMSIZE_128KB:
		return UINT32_C(128 * 1024);
	case PNR_MEMSIZE_256KB:
		return UINT32_C(256 * 1024);
	case PNR_MEMSIZE_384KB:
		return UINT32_C(384 * 1024);
	case PNR_MEMSIZE_512KB:
		return UINT32_C(512 * 1024);
	case PNR_MEMSIZE_768KB:
		return UINT32_C(768 * 1024);
	case PNR_MEMSIZE_1MB:
		return UINT32_C(1024 * 1024);
	case PNR_MEMSIZE_1_5MB:
		return UINT32_C(1536 * 1024);
	case PNR_MEMSIZE_2MB:
		return UINT32_C(2048 * 1024);
	default:
		return 0;
	}
}

static bool renesas_enter_flash_mode(target_s *t)
{
	target_reset(t);

	/* permit flash operations */
	target_mem_write8(t, SYSC_FWEPROR, SYSC_FWEPROR_PERMIT);

	return true;
}

typedef enum pe_mode {
	PE_MODE_READ,
	PE_MODE_CF,
	PE_MODE_DF,
} pe_mode_e;

static bool renesas_rv40_pe_mode(target_s *t, pe_mode_e pe_mode)
{
	/* See "Transition to Code Flash P/E Mode": Section 47.9.3.3 of the RA6M4 manual R01UH0890EJ0100. */

	renesas_priv_s *priv_storage = (renesas_priv_s *)t->target_storage;
	if (!priv_storage)
		return false;

	bool has_fmeprot = false; /* Code Flash P/E Mode Entry Protection */
	switch (priv_storage->series) {
	case PNR_SERIES_RA4E1:
	case PNR_SERIES_RA4M2:
	case PNR_SERIES_RA4M3:
	case PNR_SERIES_RA6M4:
	case PNR_SERIES_RA6M5:
	case PNR_SERIES_RA6E1:
	case PNR_SERIES_RA6T2:
		has_fmeprot = true;
	default:
		break;
	}

	if (has_fmeprot)
		target_mem_write16(t, RV40_FMEPROT, RV40_FMEPROT_UNLOCK);

	/* Set PE/READ mode */
	uint16_t fentryr = 0;
	switch (pe_mode) {
	case PE_MODE_CF:
		fentryr |= FENTRYR_PE_CF;
		break;
	case PE_MODE_DF:
		fentryr |= FENTRYR_PE_DF;
		break;
	default:
		break;
	}
	target_mem_write16(t, RV40_FENTRYR, FENTRYR_KEY | fentryr);

	platform_timeout_s timeout;
	platform_timeout_set(&timeout, 10);

	/* Wait for the operation to complete or timeout, Read until FENTRYR and FRDY is set */
	while (target_mem_read16(t, RV40_FENTRYR) != fentryr || !(target_mem_read32(t, RV40_FSTATR) & RV40_FSTATR_RDY)) {
		if (target_check_error(t) || platform_timeout_is_expired(&timeout))
			return false;
	}

	if (has_fmeprot && pe_mode == PE_MODE_READ)
		target_mem_write16(t, RV40_FMEPROT, RV40_FMEPROT_LOCK);

	return true;
}

static bool renesas_rv40_error_check(target_s *t, uint32_t error_bits)
{
	bool error = false;

	uint8_t fstatr = target_mem_read32(t, RV40_FSTATR);

	/* see "Recovery from the Command-Locked State": Section 47.9.3.6 of the RA6M4 manual R01UH0890EJ0100.*/
	if (target_mem_read8(t, RV40_FASTAT) & RV40_FASTAT_CMDLK) {
		/* if an illegal error occurred read and clear CFAE and DFAE in FASTAT. */
		if (fstatr & RV40_FSTATR_ILGLERR) {
			target_mem_read8(t, RV40_FASTAT);
			target_mem_write8(t, RV40_FASTAT, 0);
		}
		error = true;
	}

	/* check if status is indicating a programming error */
	if (fstatr & error_bits)
		error = true;

	if (error) {
		/* Stop the flash */
		target_mem_write8(t, RV40_CMD, RV40_CMD_FORCED_STOP);

		platform_timeout_s timeout;
		platform_timeout_set(&timeout, 10);

		/* Wait until the operation has completed or timeout */
		/* Read FRDY bit until it has been set to 1 indicating that the current  operation is complete.*/
		while (!(target_mem_read32(t, RV40_FSTATR) & RV40_FSTATR_RDY)) {
			if (target_check_error(t) || platform_timeout_is_expired(&timeout))
				return error;
		}

		if (target_mem_read8(t, RV40_FASTAT) & RV40_FASTAT_CMDLK)
			return error;
	}

	return error;
}

static bool renesas_rv40_prepare(target_flash_s *f)
{
	target_s *t = f->t;

	renesas_priv_s *priv_storage = (renesas_priv_s *)t->target_storage;
	if (!priv_storage)
		return false;

	if (!(target_mem_read32(t, RV40_FSTATR) & RV40_FSTATR_RDY) || target_mem_read16(t, RV40_FENTRYR) != 0) {
		DEBUG_WARN("flash is not ready, may be hanging mid unfinished command due to something going wrong, "
				   "please power on reset the device\n");

		return false;
	}

	/* code flash or data flash operation */
	/* Option-Setting flash is CF type as per Table 44.1 of RA4M2 User's Manual */
	const bool code_flash = f->start < (priv_storage->details.option_start + priv_storage->details.option_size);

	/* Transition to PE mode */
	const pe_mode_e pe_mode = code_flash ? PE_MODE_CF : PE_MODE_DF;

	return renesas_rv40_pe_mode(t, pe_mode) && !renesas_rv40_error_check(t, RV40_FSTATR_ILGLERR);
}

static bool renesas_rv40_done(target_flash_s *f)
{
	target_s *t = f->t;

	/* return to read mode */
	return renesas_rv40_pe_mode(t, PE_MODE_READ);
}

static bool renesas_check_option_setting(target_s *t, target_addr_t addr)
{
	renesas_priv_s *priv_storage = (renesas_priv_s *)t->target_storage;
	if (!priv_storage)
		return false;

	if ((priv_storage->details.option_start != NONE && addr >= priv_storage->details.option_start &&
			addr <= (priv_storage->details.option_start + priv_storage->details.option_size)) ||
		(priv_storage->details.option_start_2 != NONE && addr >= priv_storage->details.option_start_2 &&
			addr <= (priv_storage->details.option_start_2 + priv_storage->details.option_size_2)))
		return true;
	else
		return false;
}

/* !TODO: implement blank check */
static bool renesas_rv40_flash_erase(target_flash_s *f, target_addr_t addr, size_t len)
{
	target_s *t = f->t;

	renesas_priv_s *priv_storage = (renesas_priv_s *)t->target_storage;
	if (!priv_storage)
		return false;

	/* code flash or data flash operation */
	const bool code_flash = addr < RENESAS_CF_END;

	/* Set Erasure Priority Mode */
	target_mem_write16(t, RV40_FCPSR, RV40_FCPSR_ESUSPMD);

	while (len) {
		/* Set block start address*/
		target_mem_write32(t, RV40_FSADDR, addr);

		/* increment block address */
		uint16_t block_size;
		if (code_flash)
			block_size = addr < RV40_CF_REGION0_SIZE ? RV40_CF_REGION0_BLOCK_SIZE : RV40_CF_REGION1_BLOCK_SIZE;
		else
			block_size = RV40_DF_BLOCK_SIZE;

		addr += block_size;
		len -= block_size;

		/* Issue two part Block Erase commands */
		target_mem_write8(t, RV40_CMD, RV40_CMD_BLOCK_ERASE);
		target_mem_write8(t, RV40_CMD, RV40_CMD_FINAL);

		/* according to reference manual the max erase time for a 32K block is around 1040ms
		 * this is with a FCLK of 4MHz
		 */
		platform_timeout_s timeout;
		platform_timeout_set(&timeout, 1100);

		/* Wait until the operation has completed or timeout */
		/* Read FRDY bit until it has been set to 1 indicating that the current  operation is complete.*/
		while (!(target_mem_read32(t, RV40_FSTATR) & RV40_FSTATR_RDY)) {
			if (target_check_error(t) || platform_timeout_is_expired(&timeout))
				return false;
		}

		if (renesas_rv40_error_check(t, RV40_FSTATR_ERSERR | RV40_FSTATR_ILGLERR))
			return false;
	}

	return true;
}

static bool renesas_rv40_flash_write(target_flash_s *f, target_addr_t dest, const void *src, size_t len)
{
	target_s *t = f->t;

	/* code flash or data flash operation */
	const bool code_flash = dest < RENESAS_CF_END;
	bool option_flash = false;

	if (renesas_check_option_setting(t, dest)) {
		option_flash = true;
	}

	/* write size for code flash / data flash */
	uint8_t write_size = code_flash ? RV40_CF_WRITE_SIZE : RV40_DF_WRITE_SIZE;

	/* Option-Setting write size is different to CF and DF memory */
	if (option_flash)
		write_size = RV40_OF_WRITE_SIZE;

	while (len) {
		/* set block start address */
		target_mem_write32(t, RV40_FSADDR, dest);

		/* increment destination address */
		dest += write_size;
		len -= write_size;

		/* issue two part Write commands */
		if (option_flash) {
			target_mem_write8(t, RV40_CMD, RV40_CMD_CONFIG_SET_1);
			target_mem_write8(t, RV40_CMD, RV40_CMD_CONFIG_SET_2);
		} else {
			target_mem_write8(t, RV40_CMD, RV40_CMD_PROGRAM);
			target_mem_write8(t, RV40_CMD, (uint8_t)(write_size / 2U));
		}

		/* according to reference manual the data buffer full time for 2 bytes is 2 usec.
		 * this is with a FCLK of 4MHz
		 * a complete should take less than 1 msec.
		 */
		platform_timeout_s timeout;
		/* 200ms is arbitrary number i made up */
		platform_timeout_set(&timeout, option_flash ? 200 : 20);

		/* write one chunk */
		for (size_t i = 0U; i < (write_size / 2U); i++) {
			/* copy data from source address to destination */
			target_mem_write16(t, RV40_CMD, *(uint16_t *)src);

			/* 2 bytes of data */
			src += 2U;
		}

		/* issue write end command */
		target_mem_write8(t, RV40_CMD, RV40_CMD_FINAL);

		/* wait until the operation has completed or timeout */
		/* read FRDY bit until it has been set to 1 indicating that the current operation is complete.*/
		while (!(target_mem_read32(t, RV40_FSTATR) & RV40_FSTATR_RDY)) {
			if (target_check_error(t) || platform_timeout_is_expired(&timeout))
				return false;
		}
	}

	return !renesas_rv40_error_check(t, RV40_FSTATR_PRGERR | RV40_FSTATR_ILGLERR);
}

static void renesas_add_rv40_flash(target_s *t, target_addr_t addr, size_t length)
{
	target_flash_s *f = calloc(1, sizeof(*f));
	if (!f) /* calloc failed: heap exhaustion */
		return;

	const bool code_flash = addr < RENESAS_CF_END;

	f->start = addr;
	f->length = length;
	f->erased = 0xffU;
	f->write = renesas_rv40_flash_write;
	f->prepare = renesas_rv40_prepare;
	f->done = renesas_rv40_done;

	if (renesas_check_option_setting(t, addr)) {
		f->blocksize = RV40_OF_WRITE_SIZE;
		f->writesize = RV40_OF_WRITE_SIZE;
	} else {
		f->erase = renesas_rv40_flash_erase;
		if (code_flash) {
			f->blocksize = RV40_CF_REGION1_BLOCK_SIZE;
			f->writesize = RV40_CF_WRITE_SIZE;
		} else {
			f->blocksize = RV40_DF_BLOCK_SIZE;
			f->writesize = RV40_DF_WRITE_SIZE;
		}
	}

	target_add_flash(t, f);
}

static void renesas_add_flash(target_s *t, target_addr_t addr, size_t length)
{
	renesas_priv_s *priv_storage = (renesas_priv_s *)t->target_storage;
	if (!priv_storage)
		return;

	switch (priv_storage->details.flash_type) {
	case FLASH_MF3:
		DEBUG_WARN("Found renesas chip with Flash type MF3 type, not implemented\n");
		return;

	case FLASH_MF4:
		DEBUG_WARN("Found renesas chip with Flash type MF4 type, not implemented\n");
		return;

	case FLASH_RV40:
		t->enter_flash_mode = renesas_enter_flash_mode;
		return renesas_add_rv40_flash(t, addr, length);

	default:
		return;
	}
}

bool renesas_probe(target_s *t)
{
	uint8_t pnr[16]; /* 16-byte PNR */
	uint32_t flash_root_table = 0;

	/* Enable debug */
	/* a read back doesn't seem to show the change, tried 32-bit write too */
	/* See "DBGEN": Section 2.13.1 of the RA6M4 manual R01UH0890EJ0100. */
	target_mem_write8(t, SYSC_SYOCDCR, SYOCDCR_DBGEN);

	/* Read the PNR */
	switch (t->part_id) {
		// case :
		/* mcus with PNR located at 0x01001c10
		 * ra2l1 (part_id wanted)
		 * ra2e1 (part_id wanted)
		 * ra2e2 (part_id wanted)
		 */
		// if (!renesas_pnr_read(t, RENESAS_FIXED1_PNR, pnr))
		//	return false;
		// break;

	case RENESAS_PARTID_RA4M2:
	case RENESAS_PARTID_RA4M3:
		/* mcus with PNR located at 0x010080f0
		 * ra4e1 (part_id wanted)
		 * ra6m4 (part_id wanted)
		 * ra6m5 (part_id wanted)
		 * ra6e1 (part_id wanted)
		 * ra6t2 (part_id wanted)
		 */
		if (!renesas_pnr_read(t, RENESAS_FIXED2_PNR, pnr))
			return false;
		break;

	case RENESAS_PARTID_RA2A1:
	case RENESAS_PARTID_RA6M2:
		/* mcus with Flash Root Table
		 * ra6m1 (part_id wanted)
		 * ra6m3 (part_id wanted)
		 * ra6t1 (part_id wanted)
		 */
		flash_root_table = renesas_fmifrt_read(t);
		if (!renesas_pnr_read(t, RENESAS_FMIFRT_PNR(flash_root_table), pnr))
			return false;

		break;

	default:
		/*
		 * unknown part_id, we know this AP is from renesas, so Let's try brute forcing
		 * unfortunately, this is will lead to illegal memory accesses,
		 * but experimentally there doesn't seem to be an issue with these in particular
		 *
		 * try the fixed address RENESAS_FIXED2_PNR first, as it should lead to less illegal/erroneous
		 * memory accesses in case of failure, and is the most common case
		 */
		/*
		 * ra4m1 *undocumented (part_id + pnr loc wanted)
		 * ra4w1 *undocumented (part_id + pnr loc wanted)
		 */

		if (renesas_pnr_read(t, RENESAS_FIXED2_PNR, pnr)) {
			DEBUG_WARN("Found renesas chip (%.*s) with pnr location RENESAS_FIXED2_PNR and unsupported Part ID %" PRIx16
					   " please report it\n",
				sizeof(pnr), pnr, t->part_id);
			break;
		}

		if (renesas_pnr_read(t, RENESAS_FIXED1_PNR, pnr)) {
			DEBUG_WARN("Found renesas chip (%.*s) with pnr location RENESAS_FIXED1_PNR and unsupported Part ID "
					   "0x%" PRIx16 " please report it\n",
				sizeof(pnr), pnr, t->part_id);
			break;
		}

		flash_root_table = renesas_fmifrt_read(t);
		if (renesas_pnr_read(t, RENESAS_FMIFRT_PNR(flash_root_table), pnr)) {
			DEBUG_WARN("Found renesas chip (%.*s) with Flash Root Table and unsupported Part ID 0x%" PRIx16 " "
					   "please report it\n",
				sizeof(pnr), pnr, t->part_id);
			break;
		}

		return false;
	}

	renesas_priv_s *priv_storage = calloc(1, sizeof(renesas_priv_s));
	if (!priv_storage) /* calloc failed: heap exhaustion */
		return false;
	memcpy(priv_storage->pnr, pnr, sizeof(pnr));

	priv_storage->series = renesas_series(pnr);
	priv_storage->flash_root_table = flash_root_table;

	t->target_storage = (void *)priv_storage;
	t->driver = (char *)priv_storage->pnr;

	priv_storage->details = renesas_ra_family_lookup(priv_storage->series);

	/* Data flash */
	if (priv_storage->details.data_flash_start != NONE)
		renesas_add_rv40_flash(t, priv_storage->details.data_flash_start,
			priv_storage->details.data_flash_end - priv_storage->details.data_flash_start);

	/* SRAM 0 */
	if (priv_storage->details.sram0_start != NONE)
		target_add_ram(
			t, priv_storage->details.sram0_start, priv_storage->details.sram0_end - priv_storage->details.sram0_start);

	/* Standby SRAM */
	if (priv_storage->details.stdby_sram_start != NONE)
		target_add_ram(t, priv_storage->details.stdby_sram_start,
			priv_storage->details.stdby_sram_end - priv_storage->details.stdby_sram_start);

	/* SRAM HS */
	if (priv_storage->details.sramhs_start != NONE)
		target_add_ram(t, priv_storage->details.sramhs_start,
			priv_storage->details.sramhs_end - priv_storage->details.sramhs_start);

	/* SRAM 1 */
	if (priv_storage->details.sram1_start != NONE)
		target_add_ram(
			t, priv_storage->details.sram1_start, priv_storage->details.sram1_end - priv_storage->details.sram1_start);

	/* Option-Setting flash */
	if (priv_storage->details.option_start != NONE)
		renesas_add_rv40_flash(t, priv_storage->details.option_start, priv_storage->details.option_size);

	/* Option-Setting 2 flash */
	if (priv_storage->details.option_start_2 != NONE)
		renesas_add_rv40_flash(t, priv_storage->details.option_start_2, priv_storage->details.option_size_2);

	/* Code flash */
	renesas_add_flash(t, 0x00000000, renesas_flash_size(pnr)); /* Code flash memory 0x00000000 */

	target_add_commands(t, renesas_cmd_list, t->driver);

	return true;
}

/* Reads the 16-byte unique number */
static bool renesas_uid(target_s *t, int argc, const char **argv)
{
	(void)argc;
	(void)argv;

	renesas_priv_s *priv_storage = (renesas_priv_s *)t->target_storage;
	if (!priv_storage)
		return false;

	uint8_t uid[16];
	uint32_t uid_addr;

	switch (priv_storage->details.pnr_location) {
	case PNR_FL1:
		uid_addr = RENESAS_FIXED1_UID;
		break;

	case PNR_FL2:
		uid_addr = RENESAS_FIXED2_UID;
		break;

	case PNR_FRT:
		uid_addr = RENESAS_FMIFRT_UID(priv_storage->flash_root_table);
		break;

	default:
		return false;
	}

	renesas_uid_read(t, uid_addr, uid);

	tc_printf(t, "Unique Number: 0x");
	for (size_t i = 0U; i < 16U; i++)
		tc_printf(t, "%02" PRIx8, uid[i]);
	tc_printf(t, "\n");

	return true;
}
