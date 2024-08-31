/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011 Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 * Copyright (C) 2022-2024 1BitSquared <info@1bitsquared.com>
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

#ifndef TARGET_ADI_H
#define TARGET_ADI_H

#include <stdint.h>
#include <stddef.h>
#include "adiv5.h"

/* Helper for disassembling PIDRs */
uint16_t adi_designer_from_pidr(uint64_t pidr);
/* Helper for looking up components in the component LUT */
const arm_coresight_component_s *adi_lookup_component(target_addr64_t base_address, uint32_t entry_number,
	const char *indent, uint8_t cid_class, uint64_t pidr, uint8_t dev_type, uint16_t arch_id);
/* Helper for figuring out what an AP is and configuring it for use */
bool adi_configure_ap(adiv5_access_port_s *ap);
/* Helper for reading 32-bit registers from an AP's MMIO space */
uint32_t adi_mem_read32(adiv5_access_port_s *ap, target_addr32_t addr);
/* Helper for probing a CoreSight debug component */
void adi_ap_component_probe(
	adiv5_access_port_s *ap, target_addr64_t base_address, size_t recursion, uint32_t entry_number);
/* Helper for resuming all cores halted on an AP during probe */
void adi_ap_resume_cores(adiv5_access_port_s *ap);

/* Helpers for setting up memory accesses and banked accesses */
void adi_ap_mem_access_setup(adiv5_access_port_s *ap, target_addr64_t addr, align_e align);
void adi_ap_banked_access_setup(adiv5_access_port_s *base_ap);

/*
 * Decode a designer code that's in the following form into BMD's internal designer code representation
 * Bits 10:7 - JEP-106 Continuation code
 * Bits 6:0 - JEP-106 Identity code
 */
static inline uint16_t adi_decode_designer(const uint16_t designer)
{
	return (designer & ADIV5_DP_DESIGNER_JEP106_CONT_MASK) << 1U | (designer & ADIV5_DP_DESIGNER_JEP106_CODE_MASK);
}

#endif /* TARGET_ADI_H */
