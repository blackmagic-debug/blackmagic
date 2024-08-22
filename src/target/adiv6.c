/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2024 1BitSquared <info@1bitsquared.com>
 * Written by Rachel Mant <git@dragonmux.network>
 * All rights reserved.
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

/*
 * This file implements transport generic ADIv6 functions.
 *
 * See the following ARM Reference Documents:
 * ARM Debug Interface v6 Architecture Specification, IHI0074 ver. e
 * - https://developer.arm.com/documentation/ihi0074/latest/
 */

#include "general.h"
#include "adiv6.h"
#include "adiv6_internal.h"

static bool adiv6_component_probe(adiv5_debug_port_s *dp, target_addr64_t base_address, uint32_t entry_number);
static uint32_t adiv6_ap_reg_read(adiv5_access_port_s *base_ap, uint16_t addr);
static void adiv6_ap_reg_write(adiv5_access_port_s *base_ap, uint16_t addr, uint32_t value);

static target_addr64_t adiv6_dp_read_base_address(adiv5_debug_port_s *const dp)
{
	/* BASEPTR0 is on bank 2 */
	adiv5_dp_write(dp, ADIV5_DP_SELECT, ADIV5_DP_BANK2);
	const uint32_t baseptr0 = adiv5_dp_read(dp, ADIV6_DP_BASEPTR0);
	/* BASEPTR1 is on bank 3 */
	adiv5_dp_write(dp, ADIV5_DP_SELECT, ADIV5_DP_BANK3);
	const uint32_t baseptr1 = adiv5_dp_read(dp, ADIV6_DP_BASEPTR1);
	/* Now re-combine the values and return */
	return baseptr0 | ((uint64_t)baseptr1 << 32U);
}

bool adiv6_dp_init(adiv5_debug_port_s *const dp)
{
	dp->ap_read = adiv6_ap_reg_read;
	dp->ap_write = adiv6_ap_reg_write;

	/* DPIDR1 is on bank 1 */
	adiv5_dp_write(dp, ADIV5_DP_SELECT, ADIV5_DP_BANK1);
	/* Read the other DPIDR and figure out the DP bus address width */
	const uint32_t dpidr1 = adiv5_dp_read(dp, ADIV6_DP_DPIDR1);
	dp->address_width = dpidr1 & ADIV6_DP_DPIDR1_ASIZE_MASK;

	DEBUG_INFO("DP DPIDR1 0x%08" PRIx32 " %u-bit addressing\n", dpidr1, dp->address_width);

	/* Now we know how wide the DP bus addresses are, read out the base pointers and validate them */
	target_addr64_t base_address = adiv6_dp_read_base_address(dp);
	if (!(base_address & ADIV6_DP_BASEPTR0_VALID)) {
		DEBUG_INFO("No valid base address on DP\n");
		return false;
	}
	if ((base_address & ((UINT64_C(1) << dp->address_width) - 1U)) != base_address) {
		DEBUG_INFO("Bad base address %" PRIx32 "%08" PRIx32 "on DP\n", (uint32_t)(base_address >> 32U),
			(uint32_t)base_address);
		return false;
	}
	base_address &= ADIV6_DP_BASE_ADDRESS_MASK;

	return adiv6_component_probe(dp, base_address, 0U);
}

static uint32_t adiv6_dp_read_id(adiv6_access_port_s *const ap, const uint16_t addr)
{
	/*
	 * Set up the DP resource bus to do the reads.
	 * Set SELECT1 in the DP up first
	 */
	adiv5_dp_write(ap->base.dp, ADIV5_DP_SELECT, ADIV5_DP_BANK5);
	adiv5_dp_write(ap->base.dp, ADIV6_DP_SELECT1, (uint32_t)(ap->ap_address >> 32U));
	/* Now set up SELECT in the DP */
	adiv5_dp_write(ap->base.dp, ADIV5_DP_SELECT, (uint32_t)ap->ap_address | (addr & 0x0ff0U));

	uint32_t result = 0;
	/* Loop through each CIDR register location and read it, pulling out only the relevant byte */
	for (size_t i = 0; i < 4U; ++i) {
		const uint32_t value = adiv5_dp_read(ap->base.dp, ADIV5_APnDP | (i << 2U));
		result |= (value & 0xffU) << (i * 8U);
	}
	return result;
}

static uint64_t adiv6_dp_read_pidr(adiv6_access_port_s *const ap)
{
	const uint32_t pidr_upper = adiv6_dp_read_id(ap, PIDR4_OFFSET);
	const uint32_t pidr_lower = adiv6_dp_read_id(ap, PIDR0_OFFSET);
	return ((uint64_t)pidr_upper << 32U) | (uint64_t)pidr_lower;
}

static bool adiv6_parse_coresight_v0_rom_table(
	adiv6_access_port_s *const base_ap, const target_addr64_t base_address, const uint64_t pidr)
{
#ifdef DEBUG_INFO_IS_NOOP
	(void)pidr;
#else
	/* Extract the designer code and part number from the part ID register */
	const uint16_t designer_code = adiv5_designer_from_pidr(pidr);
	const uint16_t part_number = pidr & PIDR_PN_MASK;
#endif

	/* Now we know we're in a CoreSight v0 ROM table, read out the device ID field and set up the memory flag on the AP */
	const uint32_t dev_id = adiv5_ap_read(&base_ap->base, CORESIGHT_ROM_DEVID);
	if (dev_id & CORESIGHT_ROM_DEVID_SYSMEM)
		base_ap->base.flags |= ADIV5_AP_FLAGS_HAS_MEM;
	const bool rom_format = dev_id & CORESIGHT_ROM_DEVID_FORMAT;

	DEBUG_INFO("ROM Table: BASE=0x%" PRIx32 "%08" PRIx32 " SYSMEM=%u, Manufacturer %03x Partno %03x (PIDR = "
			   "0x%02" PRIx32 "%08" PRIx32 ")\n",
		(uint32_t)(base_address >> 32U), (uint32_t)base_address, 0U, designer_code, part_number,
		(uint32_t)(pidr >> 32U), (uint32_t)pidr);

	DEBUG_INFO("ROM Table: END\n");
	return true;
}

static bool adiv6_component_probe(
	adiv5_debug_port_s *const dp, const target_addr64_t base_address, const uint32_t entry_number)
{
	/* Start out by making a fake AP to use for all the reads */
	adiv6_access_port_s base_ap = {
		.base.dp = dp,
		.ap_address = base_address,
	};

	const uint32_t cidr = adiv6_dp_read_id(&base_ap, CIDR0_OFFSET);
	/* CIDR preamble sanity check */
	if ((cidr & ~CID_CLASS_MASK) != CID_PREAMBLE) {
		DEBUG_WARN("%" PRIu32 " 0x%" PRIx32 "%08" PRIx32 ": 0x%08" PRIx32 " <- does not match preamble (0x%08" PRIx32
				   ")\n",
			entry_number, (uint32_t)(base_address >> 32U), (uint32_t)base_address, cidr, CID_PREAMBLE);
		return false;
	}
	/* Extract Component ID class nibble */
	const uint32_t cid_class = (cidr & CID_CLASS_MASK) >> CID_CLASS_SHIFT;

	/* Read out the peripheral ID register */
	const uint64_t pidr = adiv6_dp_read_pidr(&base_ap);

	/* Check if this is a legacy ROM table */
	if (cid_class == cidc_romtab) {
		/* Validate that the SIZE field is 0 per the spec */
		if (pidr & PIDR_SIZE_MASK) {
			DEBUG_ERROR("Fault reading ROM table\n");
			return false;
		}
	} else {
		/* Extract the designer code from the part ID register */
		const uint16_t designer_code = adiv5_designer_from_pidr(pidr);

		/* Check if this is a CoreSight component */
		uint16_t arch_id = 0U;
		uint8_t dev_type = 0U;
		if (cid_class == cidc_dc) {
			/* Read out the component's identification information */
			const uint32_t dev_arch = adiv5_ap_read(&base_ap.base, ADIV6_AP_REG(DEVARCH_OFFSET));
			dev_type = adiv5_ap_read(&base_ap.base, ADIV6_AP_REG(DEVTYPE_OFFSET)) & DEVTYPE_MASK;

			if (dev_arch & DEVARCH_PRESENT)
				arch_id = dev_arch & DEVARCH_ARCHID_MASK;
		}

		/* Check if this is a CoreSight component ROM table */
		if (cid_class == cidc_dc && arch_id == DEVARCH_ARCHID_ROMTABLE_V0) {
			if (pidr & PIDR_SIZE_MASK) {
				DEBUG_ERROR("Fault reading ROM table\n");
				return false;
			}
			return adiv6_parse_coresight_v0_rom_table(&base_ap, base_address, pidr);
		}
	}
	return false;
}

static uint32_t adiv6_ap_reg_read(adiv5_access_port_s *const base_ap, const uint16_t addr)
{
	adiv6_access_port_s *const ap = (adiv6_access_port_s *)base_ap;
	/* Set SELECT1 in the DP up first */
	adiv5_dp_write(base_ap->dp, ADIV5_DP_SELECT, ADIV5_DP_BANK5);
	adiv5_dp_write(base_ap->dp, ADIV6_DP_SELECT1, (uint32_t)(ap->ap_address >> 32U));
	/* Now set up SELECT in the DP */
	const uint16_t bank = addr & ADIV6_AP_BANK_MASK;
	adiv5_dp_write(base_ap->dp, ADIV5_DP_SELECT, (uint32_t)ap->ap_address | ((bank & 0xf000U) >> 4U) | (bank & 0xf0U));
	return adiv5_dp_read(base_ap->dp, addr);
}

static void adiv6_ap_reg_write(adiv5_access_port_s *const base_ap, const uint16_t addr, const uint32_t value)
{
	adiv6_access_port_s *const ap = (adiv6_access_port_s *)base_ap;
	/* Set SELECT1 in the DP up first */
	adiv5_dp_write(base_ap->dp, ADIV5_DP_SELECT, ADIV5_DP_BANK5);
	adiv5_dp_write(base_ap->dp, ADIV6_DP_SELECT1, (uint32_t)(ap->ap_address >> 32U));
	/* Now set up SELECT in the DP */
	const uint16_t bank = addr & ADIV6_AP_BANK_MASK;
	adiv5_dp_write(base_ap->dp, ADIV5_DP_SELECT, (uint32_t)ap->ap_address | ((bank & 0xf000U) >> 4U) | (bank & 0xf0U));
	adiv5_dp_write(base_ap->dp, addr, value);
}
