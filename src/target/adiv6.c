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
#include "jep106.h"
#include "adi.h"
#include "adiv6.h"
#include "adiv6_internal.h"
#ifdef CONFIG_RISCV
#include "riscv_debug.h"
#endif

#define ID_RP2350 0x0004U

static bool adiv6_component_probe(adiv5_debug_port_s *dp, target_addr64_t base_address, uint32_t entry_number,
	uint16_t rom_designer_code, uint16_t rom_part_number);

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
#if CONFIG_BMDA == 1
	bmda_adiv6_dp_init(dp);
#endif

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
		DEBUG_INFO("Bad base address %0" PRIx32 "%08" PRIx32 "on DP\n", (uint32_t)(base_address >> 32U),
			(uint32_t)base_address);
		return false;
	}
	base_address &= ADIV6_DP_BASE_ADDRESS_MASK;

	return adiv6_component_probe(dp, base_address, 0U, dp->designer_code, dp->partno);
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
	adiv5_dp_write(ap->base.dp, ADIV5_DP_SELECT, (uint32_t)ap->ap_address | (addr & ADIV6_AP_BANK_MASK));
	const uint16_t ap_reg_base = ADIV5_APnDP | (addr & ADIV6_AP_BANK_MASK);

	uint32_t result = 0;
	/* Loop through each register location and read it, pulling out only the relevant byte */
	for (size_t i = 0; i < 4U; ++i) {
		const uint32_t value = adiv5_dp_read(ap->base.dp, ap_reg_base | (i << 2U));
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

static bool adiv6_reset_resources(adiv6_access_port_s *const rom_table)
{
	/* Read out power request ID register 0 and check if power control is actually implemented */
	const uint8_t pridr0 = adiv5_ap_read(&rom_table->base, ADIV5_AP_REG(CORESIGHT_ROM_PRIDR0)) & 0x3fU;
	if ((pridr0 & CORESIGHT_ROM_PRIDR0_VERSION_MASK) != CORESIGHT_ROM_PRIDR0_VERSION_NOT_IMPL)
		rom_table->base.flags |= ADIV6_DP_FLAGS_HAS_PWRCTRL;
	/* Now try and perform a debug reset request */
	if (pridr0 & CORESIGHT_ROM_PRIDR0_HAS_DBG_RESET_REQ) {
		platform_timeout_s timeout;
		platform_timeout_set(&timeout, 250);

		adiv5_ap_write(&rom_table->base, ADIV5_AP_REG(CORESIGHT_ROM_DBGRSTRR), CORESIGHT_ROM_DBGRST_REQ);
		/* While the reset request is in progress */
		while (adiv5_ap_read(&rom_table->base, ADIV5_AP_REG(CORESIGHT_ROM_DBGRSTRR)) & CORESIGHT_ROM_DBGRST_REQ) {
			/* Check if it's been acknowledge, and if it has, deassert the request */
			if (adiv5_ap_read(&rom_table->base, ADIV5_AP_REG(CORESIGHT_ROM_DBGRSTAR)) & CORESIGHT_ROM_DBGRST_REQ)
				adiv5_ap_write(&rom_table->base, ADIV5_AP_REG(CORESIGHT_ROM_DBGRSTRR), 0U);
			/* Check if the reset has timed out */
			if (platform_timeout_is_expired(&timeout)) {
				DEBUG_WARN("adiv6: debug reset failed\n");
				adiv5_ap_write(&rom_table->base, ADIV5_AP_REG(CORESIGHT_ROM_DBGRSTRR), 0U);
				break;
			}
		}
	}
	/* Regardless of what happened, extract whether system reset is supported this way */
	if (pridr0 & CORESIGHT_ROM_PRIDR0_HAS_SYS_RESET_REQ)
		rom_table->base.flags |= ADIV6_DP_FLAGS_HAS_SYSRESETREQ;
	return true;
}

static uint64_t adiv6_read_coresight_rom_entry(
	adiv6_access_port_s *const rom_table, const uint8_t rom_format, const uint16_t entry_offset)
{
	const uint32_t entry_lower = adiv5_ap_read(&rom_table->base, ADIV5_AP_REG(entry_offset));
	if (rom_format == CORESIGHT_ROM_DEVID_FORMAT_32BIT)
		return entry_lower;
	const uint32_t entry_upper = adiv5_ap_read(&rom_table->base, ADIV5_AP_REG(entry_offset + 4U));
	return ((uint64_t)entry_upper << 32U) | (uint64_t)entry_lower;
}

static bool adiv6_parse_coresight_v0_rom_table(
	adiv6_access_port_s *const rom_table, const target_addr64_t base_address, const uint64_t pidr)
{
	/* Extract the designer code and part number from the part ID register */
	const uint16_t designer_code = adi_designer_from_pidr(pidr);
	const uint16_t part_number = pidr & PIDR_PN_MASK;

	/* Now we know we're in a CoreSight v0 ROM table, read out the device ID field and set up the memory flag on the AP */
	const uint8_t dev_id = adiv5_ap_read(&rom_table->base, ADIV5_AP_REG(CORESIGHT_ROM_DEVID)) & 0x7fU;
	if (dev_id & CORESIGHT_ROM_DEVID_SYSMEM)
		rom_table->base.flags |= ADIV5_AP_FLAGS_HAS_MEM;
	const uint8_t rom_format = dev_id & CORESIGHT_ROM_DEVID_FORMAT;

	/* Check if the power control registers are available, and if they are try to reset all debug resources */
	if ((dev_id & CORESIGHT_ROM_DEVID_HAS_POWERREQ) && !adiv6_reset_resources(rom_table))
		return false;

	DEBUG_INFO("ROM Table: BASE=0x%0" PRIx32 "%08" PRIx32 " SYSMEM=%u, Manufacturer %03x Partno %03x (PIDR = "
			   "0x%02" PRIx32 "%08" PRIx32 ")\n",
		(uint32_t)(base_address >> 32U), (uint32_t)base_address, 0U, designer_code, part_number,
		(uint32_t)(pidr >> 32U), (uint32_t)pidr);

	bool result = true;

	/* ROM table has at most 512 entries when 32-bit and 256 entries when 64-bit */
	const uint32_t max_entries = rom_format == CORESIGHT_ROM_DEVID_FORMAT_32BIT ? 512U : 256U;
	const size_t entry_shift = rom_format == CORESIGHT_ROM_DEVID_FORMAT_32BIT ? 2U : 3U;
	for (uint32_t index = 0; index < max_entries; ++index) {
		/* Start by reading out the entry */
		const uint64_t entry = adiv6_read_coresight_rom_entry(rom_table, rom_format, (uint16_t)(index << entry_shift));
		const uint8_t presence = entry & CORESIGHT_ROM_ROMENTRY_ENTRY_MASK;
		/* Check if the entry is valid */
		if (presence == CORESIGHT_ROM_ROMENTRY_ENTRY_FINAL)
			break;
		/* Check for an entry to skip */
		if (presence == CORESIGHT_ROM_ROMENTRY_ENTRY_NOT_PRESENT) {
			DEBUG_INFO("%" PRIu32 " Entry 0x%0" PRIx32 "%08" PRIx32 " -> Not present\n", index,
				(uint32_t)(entry >> 32U), (uint32_t)entry);
			continue;
		}
		/* Check that the entry isn't invalid */
		if (presence == CORESIGHT_ROM_ROMENTRY_ENTRY_INVALID) {
			DEBUG_INFO("%" PRIu32 " Entry invalid\n", index);
			continue;
		}
		/* Got a good entry? great! Figure out if it has a power domain to cycle and what the address offset is */
		const target_addr64_t offset = entry & CORESIGHT_ROM_ROMENTRY_OFFSET_MASK;
		if ((rom_table->base.flags & ADIV6_DP_FLAGS_HAS_PWRCTRL) && (entry & CORESIGHT_ROM_ROMENTRY_POWERID_VALID)) {
			const uint8_t power_domain_offset =
				((entry & CORESIGHT_ROM_ROMENTRY_POWERID_MASK) >> CORESIGHT_ROM_ROMENTRY_POWERID_SHIFT) << 2U;
			/* Check if the power control register for this domain is present */
			if (adiv5_ap_read(&rom_table->base, ADIV5_AP_REG(CORESIGHT_ROM_DBGPCR_BASE + power_domain_offset)) &
				CORESIGHT_ROM_DBGPCR_PRESENT) {
				/* And if it is, ask the domain to power up */
				adiv5_ap_write(&rom_table->base, ADIV5_AP_REG(CORESIGHT_ROM_DBGPCR_BASE + power_domain_offset),
					CORESIGHT_ROM_DBGPCR_PWRREQ);
				/* Then spin for a little waiting for the domain to become powered usefully */
				platform_timeout_s timeout;
				platform_timeout_set(&timeout, 250);
				while (
					!(adiv5_ap_read(&rom_table->base, ADIV5_AP_REG(CORESIGHT_ROM_DBGPSR_BASE + power_domain_offset)) &
						CORESIGHT_ROM_DBGPSR_STATUS_ON)) {
					if (platform_timeout_is_expired(&timeout)) {
						DEBUG_WARN("adiv6: power-up failed\n");
						return false;
					}
				}
			}
		}

		/* Now recursively probe the component */
		result &= adiv6_component_probe(rom_table->base.dp, base_address + offset, index, designer_code, part_number);
	}

	DEBUG_INFO("ROM Table: END\n");
	return result;
}

static adiv6_access_port_s *adiv6_new_ap(
	adiv5_debug_port_s *const dp, const target_addr64_t base_address, const uint32_t entry_number)
{
	adiv6_access_port_s ap = {
		.base.dp = dp,
		.base.apsel = entry_number & 0xffU,
		.ap_address = base_address,
	};
	/* Try to configure the AP for use */
	if (!adi_configure_ap(&ap.base))
		return NULL;

	/* It's valid to so create a heap copy */
	adiv6_access_port_s *result = malloc(sizeof(*result));
	if (!result) { /* malloc failed: heap exhaustion */
		DEBUG_ERROR("malloc: failed in %s\n", __func__);
		return NULL;
	}
	/* Copy the new AP into place and ref it */
	memcpy(result, &ap, sizeof(*result));
	adiv5_ap_ref(&result->base);
	return result;
}

static bool adiv6_component_probe(adiv5_debug_port_s *const dp, const target_addr64_t base_address,
	const uint32_t entry_number, const uint16_t rom_designer_code, const uint16_t rom_part_number)
{
	/* Start out by making a fake AP to use for all the reads */
	adiv6_access_port_s base_ap = {
		.base.dp = dp,
		.ap_address = base_address,
	};

	const uint32_t cidr = adiv6_dp_read_id(&base_ap, CIDR0_OFFSET);
	/* CIDR preamble sanity check */
	if ((cidr & ~CID_CLASS_MASK) != CID_PREAMBLE) {
		DEBUG_WARN("%" PRIu32 " 0x%0" PRIx32 "%08" PRIx32 ": 0x%08" PRIx32 " <- does not match preamble (0x%08" PRIx32
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
		const uint16_t designer_code = adi_designer_from_pidr(pidr);

		/* Check if this is a CoreSight component */
		uint16_t arch_id = 0U;
		uint8_t dev_type = 0U;
		if (cid_class == cidc_dc) {
			/* Read out the component's identification information */
			const uint32_t dev_arch = adiv5_ap_read(&base_ap.base, ADIV5_AP_REG(CORESIGHT_ROM_DEVARCH));
			dev_type = adiv5_ap_read(&base_ap.base, ADIV5_AP_REG(CORESIGHT_ROM_DEVTYPE)) & DEVTYPE_MASK;

			if (dev_arch & DEVARCH_PRESENT)
				arch_id = dev_arch & DEVARCH_ARCHID_MASK;
		}

		/* If it's an ARM component of some kind, look it up in the ARM component table */
		if (designer_code == JEP106_MANUFACTURER_ARM || arch_id == DEVARCH_ARCHID_ROMTABLE_V0) {
			const arm_coresight_component_s *const component =
				adi_lookup_component(base_address, entry_number, " ", cid_class, pidr, dev_type, arch_id);
			if (component == NULL)
				return true;

			switch (component->arch) {
			/* Handle when the component is a CoreSight component ROM table */
			case aa_rom_table:
				if (pidr & PIDR_SIZE_MASK)
					DEBUG_ERROR("Fault reading ROM table\n");
				else
					return adiv6_parse_coresight_v0_rom_table(&base_ap, base_address, pidr);
				break;
			/* Handle when the component is an AP */
			case aa_access_port: {
				/* We've got an ADIv6 APv2, so try and set up to use it */
				adiv6_access_port_s *ap = adiv6_new_ap(dp, base_address, entry_number);
				if (ap == NULL)
					break;

				/* Copy the AP's designer and part code information in from this layer */
				ap->base.designer_code = rom_designer_code;
				ap->base.partno = rom_part_number;

#ifdef CONFIG_RISCV
				/* Special-cases for RISC-V parts using ADI as a DTM */
				if (rom_designer_code == JEP106_MANUFACTURER_RASPBERRY && rom_part_number == ID_RP2350 &&
					ap->base.base == 0U) {
					/* Dispatch to the RISC-V ADI DTM handler */
					riscv_adi_dtm_handler(&ap->base);
				} else
#endif
					/* Now we can use it, see what's on it and try to create debug targets */
					adi_ap_component_probe(&ap->base, ap->base.base, 1U, 0U);
				/* Having completed discovery on this AP, try to resume any halted cores */
				adi_ap_resume_cores(&ap->base);
				/* Then clean up so we don't leave an AP floating about if no (usable) cores were found */
				adiv5_ap_unref(&ap->base);
				break;
			}
			default:
				break;
			}
		}
	}
	return true;
}

uint32_t adiv6_ap_reg_read(adiv5_access_port_s *const base_ap, const uint16_t addr)
{
	adiv6_access_port_s *const ap = (adiv6_access_port_s *)base_ap;
	/* Set SELECT1 in the DP up first */
	adiv5_dp_write(base_ap->dp, ADIV5_DP_SELECT, ADIV5_DP_BANK5);
	adiv5_dp_write(base_ap->dp, ADIV6_DP_SELECT1, (uint32_t)(ap->ap_address >> 32U));
	/* Now set up SELECT in the DP */
	adiv5_dp_write(base_ap->dp, ADIV5_DP_SELECT, (uint32_t)ap->ap_address | (addr & ADIV6_AP_BANK_MASK));
	return base_ap->dp->dp_read(base_ap->dp, addr);
}

void adiv6_ap_reg_write(adiv5_access_port_s *const base_ap, const uint16_t addr, const uint32_t value)
{
	adiv6_access_port_s *const ap = (adiv6_access_port_s *)base_ap;
	/* Set SELECT1 in the DP up first */
	adiv5_dp_write(base_ap->dp, ADIV5_DP_SELECT, ADIV5_DP_BANK5);
	adiv5_dp_write(base_ap->dp, ADIV6_DP_SELECT1, (uint32_t)(ap->ap_address >> 32U));
	/* Now set up SELECT in the DP */
	adiv5_dp_write(base_ap->dp, ADIV5_DP_SELECT, (uint32_t)ap->ap_address | (addr & ADIV6_AP_BANK_MASK));
	base_ap->dp->low_access(base_ap->dp, ADIV5_LOW_WRITE, addr, value);
}
