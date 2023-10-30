/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2023 1BitSquared <info@1bitsquared.com>
 * Written by Rachel Mant <git@dragonmux.network>
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

/* Support for the Renesas RZ family of microprocessors */

#include "general.h"
#include "target.h"
#include "target_internal.h"

/*
 * Part numbering scheme
 *
 *  R7   S   xx   x   x   xx   x   x   xx
 * \__/ \_/ \__/ \_/ \_/ \__/ \_/ \_/ \__/
 *  |    |   |    |   |   |    |   |   |
 *  |    |   |    |   |   |    |   |   \_ Package type
 *  |    |   |    |   |   |    |   \_____ Quality Grade
 *  |    |   |    |   |   |    \_________ Operating temperature
 *  |    |   |    |   |   \______________ Group/Tier number?
 *  |    |   |    |   \__________________ Feature set
 *  |    |   |    \______________________ Group number
 *  |    |   \___________________________ Series name
 *  |    \_______________________________ Family (S: RZ)
 *  \____________________________________ Renesas microprocessor (always 'R7')
 *
 *  R9   A   xx   x   x   xx   x   x   xx
 * \__/ \_/ \__/ \_/ \_/ \__/ \_/ \_/ \__/
 *  |    |   |    |   |   |    |   |   |
 *  |    |   |    |   |   |    |   |   \_ Package type
 *  |    |   |    |   |   |    |   \_____ Quality Grade
 *  |    |   |    |   |   |    \_________ Operating temperature
 *  |    |   |    |   |   \______________ Group/Tier number?
 *  |    |   |    |   \__________________ Feature set
 *  |    |   |    \______________________ Group number
 *  |    |   \___________________________ Series name
 *  |    \_______________________________ Family (A: RZ)
 *  \____________________________________ Renesas microprocessor (always 'R9')
 *
 * Renesas Flash MCUs have an internal 16 byte read only register that stores
 * the part number, the code is stored ascii encoded, starting from the lowest memory address
 * except for pnrs stored in 'FIXED_PNR1', where the code is stored in reverse order (but the last 3 bytes are still 0x20 aka ' ')
 */

/* Base address and size for the 4 OCRAM regions + their mirrors (includes RETRAM) */
#define RENESAS_OCRAM_BASE        0x20000000U
#define RENESAS_OCRAM_MIRROR_BASE 0x60000000U
#define RENESAS_OCRAM_SIZE        0x00200000U

/* Base address for the boundary scan controller and boot mode register */
/*
 * NB: These addresses are only documented by rev 1 of the manual,
 * all further versions deleted these addresses and their documentation
 * wholesale. This has also been deduced in part from the ROM.
 */
#define RENESAS_BSCAN_BASE      0xfcfe1800U
#define RENESAS_BSCAN_BOOT_MODE (RENESAS_BSCAN_BASE + 0x000U)
#define RENESAS_BSCAN_BSID      (RENESAS_BSCAN_BASE + 0x004U)

#define RENESAS_BSCAN_BSID_RZ_A1L  0x081a6447U
#define RENESAS_BSCAN_BSID_RZ_A1LU 0x08178447U
#define RENESAS_BSCAN_BSID_RZ_A1LC 0x082f4447U

/* This is the part number from the ROM table of a R7S721030 and is a guess */
#define ID_RZ_A1LU 0x012U

static const char *renesas_rz_part_name(uint32_t part_id);

bool renesas_rz_probe(target_s *const target)
{
	/* Determine that it's *probably* a RZ part */
	if (target->part_id != ID_RZ_A1LU)
		return false;

	/* Read out the BSID register to confirm that */
	const uint32_t part_id = target_mem_read32(target, RENESAS_BSCAN_BSID);
	/* If the read failed, it's not a RZ/A1L* part */
	if (!part_id)
		return false;

	target->driver = renesas_rz_part_name(part_id);

	target_add_ram(target, RENESAS_OCRAM_BASE, RENESAS_OCRAM_SIZE);
	target_add_ram(target, RENESAS_OCRAM_MIRROR_BASE, RENESAS_OCRAM_SIZE);
	return true;
}

static const char *renesas_rz_part_name(const uint32_t part_id)
{
	switch (part_id) {
	case RENESAS_BSCAN_BSID_RZ_A1L:
		return "RZ/A1L";
	case RENESAS_BSCAN_BSID_RZ_A1LC:
		return "RZ/A1LC";
	case RENESAS_BSCAN_BSID_RZ_A1LU:
		return "RZ/A1LU";
	}
	return "Unknown";
}
