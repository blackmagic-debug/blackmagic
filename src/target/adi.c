/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2015  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 * Copyright (C) 2018-2021 Uwe Bonnes (bon@elektron.ikp.physik.tu-darmstadt.de)
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

/*
 * This file implements version-independant ADI functions.
 *
 * See the following ARM Reference Documents:
 * ARM Debug Interface v5 Architecture Specification, IHI0031 ver. g
 * - https://developer.arm.com/documentation/ihi0031/latest/
 * ARM Debug Interface v6 Architecture Specification, IHI0074 ver. e
 * - https://developer.arm.com/documentation/ihi0074/latest/
 */

#include "general.h"
#include "jep106.h"
#include "adi.h"

#if ENABLE_DEBUG == 1
static const char *adi_arm_ap_type_string(const uint8_t ap_type, const uint8_t ap_class)
{
	/*
	 * Values taken from ADIv5 spec §C1.3 pg146.
	 * table C1-2 "AP Identification types for an AP designed by Arm"
	 */

	/* All types except 0 are only valid for ap_class == 0x8 */
	if (ap_class == ADIV5_AP_IDR_CLASS_MEM || ap_type == 0U) {
		switch (ap_type) {
		case 0U:
			/* Type 0 APs are determined by the class code */
			if (ap_class == ADIV5_AP_IDR_CLASS_JTAG)
				return "JTAG-AP";
			if (ap_class == ADIV5_AP_IDR_CLASS_COM)
				return "COM-AP";
			break;
		case 0x1U:
			return "AHB3-AP";
		case 0x2U:
			return "APB2/3-AP";
		/* 0x3 is not defined */
		case 0x4U:
			return "AXI3/4-AP";
		case 0x5U:
			return "AHB5-AP";
		case 0x6U:
			return "APB4/5-AP";
		case 0x7U:
			return "AXI5-AP";
		case 0x8U:
			return "AHB5-AP";
		default:
			break;
		}
	}
	return "Unknown";
}
#endif

uint16_t adi_designer_from_pidr(const uint64_t pidr)
{
	uint16_t designer_code;
	if (pidr & PIDR_JEP106_USED) {
		/* (OFFSET - 8) because we want it on bits 11:8 of new code, see "JEP-106 code list" */
		designer_code = ((pidr & PIDR_JEP106_CONT_MASK) >> (PIDR_JEP106_CONT_OFFSET - 8U)) |
			((pidr & PIDR_JEP106_CODE_MASK) >> PIDR_JEP106_CODE_OFFSET);

	} else {
		/* legacy ascii code */
		designer_code = ((pidr & PIDR_JEP106_CODE_MASK) >> PIDR_JEP106_CODE_OFFSET) | ASCII_CODE_FLAG;
	}

	if (designer_code == JEP106_MANUFACTURER_ERRATA_STM32WX || designer_code == JEP106_MANUFACTURER_ERRATA_CS ||
		designer_code == JEP106_MANUFACTURER_ERRATA_CS_ASCII) {
		/**
         * see 'JEP-106 code list' for context, here we are aliasing codes that are non compliant with the
         * JEP-106 standard to their expected codes, this is later used to determine the correct probe function.
         */
		DEBUG_WARN("Patching Designer code %03x -> %03x\n", designer_code, JEP106_MANUFACTURER_STM);
		designer_code = JEP106_MANUFACTURER_STM;
	}
	return designer_code;
}

static void adi_display_ap(const adiv5_access_port_s *const ap)
{
#if ENABLE_DEBUG == 1
	const uint8_t ap_type = ADIV5_AP_IDR_TYPE(ap->idr);
	const uint8_t ap_class = ADIV5_AP_IDR_CLASS(ap->idr);
	const uint16_t designer = adi_decode_designer(ADIV5_AP_IDR_DESIGNER(ap->idr));
	/* If this is an ARM-designed AP, map the AP type. Otherwise display "Unknown" */
	const char *const ap_type_name =
		designer == JEP106_MANUFACTURER_ARM ? adi_arm_ap_type_string(ap_type, ap_class) : "Unknown";
	/* Display the AP's type, variant and revision information */
	DEBUG_INFO(" (%s var%" PRIx32 " rev%" PRIx32 ")\n", ap_type_name, ADIV5_AP_IDR_VARIANT(ap->idr),
		ADIV5_AP_IDR_REVISION(ap->idr));
#else
	(void)ap;
#endif
}

bool adi_configure_ap(adiv5_access_port_s *const ap)
{
	/* Grab the ID register and make sure the value is sane (non-zero) */
	ap->idr = adiv5_ap_read(ap, ADIV5_AP_IDR);
	if (!ap->idr)
		return false;
	const uint8_t ap_type = ADIV5_AP_IDR_TYPE(ap->idr);
	const uint8_t ap_class = ADIV5_AP_IDR_CLASS(ap->idr);
	DEBUG_INFO("AP %3u: IDR=%08" PRIx32, ap->apsel, ap->idr);
	/* If this is a MEM-AP */
	if (ap_class == ADIV5_AP_IDR_CLASS_MEM && ap_type >= 1U && ap_type <= 8U) {
		/* Grab the config, base and CSW registers */
		const uint32_t cfg = adiv5_ap_read(ap, ADIV5_AP_CFG);
		ap->csw = adiv5_ap_read(ap, ADIV5_AP_CSW);
		/* This reads the lower half of BASE */
		ap->base = adiv5_ap_read(ap, ADIV5_AP_BASE_LOW);
		const uint8_t base_flags = (uint8_t)ap->base & (ADIV5_AP_BASE_FORMAT | ADIV5_AP_BASE_PRESENT);
		/* Make sure we only pay attention to the base address, not the presence and format bits */
		ap->base &= ADIV5_AP_BASE_BASEADDR;
		/* Check if this is a 64-bit AP */
		if (cfg & ADIV5_AP_CFG_LARGE_ADDRESS) {
			/* If this base value is invalid for a LPAE MEM-AP, bomb out here */
			if (base_flags == (ADIV5_AP_BASE_FORMAT_LEGACY | ADIV5_AP_BASE_PRESENT_NO_ENTRY)) {
				DEBUG_INFO(" -> Invalid\n");
				return false;
			}
			/* Otherwise note this is a 64-bit AP and read the high part */
			ap->flags |= ADIV5_AP_FLAGS_64BIT;
			ap->base |= (uint64_t)adiv5_ap_read(ap, ADIV5_AP_BASE_HIGH) << 32U;
		}
		/* Check the Debug Base Address register for not-present. See ADIv5 Specification C2.6.1 */
		if (base_flags == (ADIV5_AP_BASE_FORMAT_ADIV5 | ADIV5_AP_BASE_PRESENT_NO_ENTRY) ||
			(!(ap->flags & ADIV5_AP_FLAGS_64BIT) && (uint32_t)ap->base == ADIV5_AP_BASE_NOT_PRESENT)) {
			/*
			 * Debug Base Address not present in this MEM-AP
			 * No debug entries... useless AP
			 * AP0 on STM32MP157C reads 0x00000002
			 */
			DEBUG_INFO(" -> Not Present\n");
			return false;
		}
		/* Check if the AP is disabled, skipping it if that is the case */
		if ((ap->csw & ADIV5_AP_CSW_AP_ENABLED) == 0U) {
			DEBUG_INFO(" -> Disabled\n");
			return false;
		}

		/* Apply bus-common fixups to the CSW value */
		ap->csw &= ~(ADIV5_AP_CSW_SIZE_MASK | ADIV5_AP_CSW_ADDRINC_MASK);
		ap->csw |= ADIV5_AP_CSW_DBGSWENABLE;

		switch (ap_type) {
		case ADIV5_AP_IDR_TYPE_AXI3_4:
			/* XXX: Handle AXI4 w/ ACE-Lite which makes Mode and Type do ~things~™ (§E1.3.1, pg237) */
			/* Clear any existing prot modes and disable memory tagging */
			ap->csw &= ~(ADIV5_AP_CSW_AXI3_4_PROT_MASK | ADIV5_AP_CSW_AXI_MTE);
			/* Check if secure access is allowed and enable it if so */
			if (ap->csw & ADIV5_AP_CSW_SPIDEN)
				ap->csw &= ~ADIV5_AP_CSW_AXI_PROT_NS;
			else
				ap->csw |= ADIV5_AP_CSW_AXI_PROT_NS;
			/* Always privileged accesses */
			ap->csw |= ADIV5_AP_CSW_AXI_PROT_PRIV;
			break;
		case ADIV5_AP_IDR_TYPE_AXI5:
			/* Clear any existing prot modes and disable memory tagging */
			ap->csw &= ~(ADIV5_AP_CSW_AXI5_PROT_MASK | ADIV5_AP_CSW_AXI_MTE);
			/* Check if secure access is allowed and enable it if so */
			if (ap->csw & ADIV5_AP_CSW_SPIDEN)
				ap->csw &= ~ADIV5_AP_CSW_AXI_PROT_NS;
			else
				ap->csw |= ADIV5_AP_CSW_AXI_PROT_NS;
			/* Always privileged accesses */
			ap->csw |= ADIV5_AP_CSW_AXI_PROT_PRIV;
			break;
		case ADIV5_AP_IDR_TYPE_AHB3:
		case ADIV5_AP_IDR_TYPE_AHB5:
		case ADIV5_AP_IDR_TYPE_AHB5_HPROT:
			/* Clear any existing HPROT modes */
			ap->csw &= ~ADIV5_AP_CSW_AHB_HPROT_MASK;
			/*
			 * Ensure that MasterType is set to generate transactions as requested from the AHB-AP,
			 * and that we generate privileged data requests via the HPROT bits
			 */
			ap->csw |= ADIV5_AP_CSW_AHB_MASTERTYPE | ADIV5_AP_CSW_AHB_HPROT_DATA | ADIV5_AP_CSW_AHB_HPROT_PRIV;
			/* Check to see if secure access is supported and allowed */
			if (ap->csw & ADIV5_AP_CSW_SPIDEN)
				ap->csw &= ~ADIV5_AP_CSW_AHB_HNONSEC;
			else
				ap->csw |= ADIV5_AP_CSW_AHB_HNONSEC;
			break;
		case ADIV5_AP_IDR_TYPE_APB4_5:
			/* Clear any existing prot modes and disable memory tagging */
			ap->csw &= ~ADIV5_AP_CSW_APB_PPROT_MASK;
			/* Check if secure access is allowed and enable it if so */
			if (ap->csw & ADIV5_AP_CSW_SPIDEN)
				ap->csw &= ~ADIV5_AP_CSW_APB_PPROT_NS;
			else
				ap->csw |= ADIV5_AP_CSW_APB_PPROT_NS;
			ap->csw |= ADIV5_AP_CSW_APB_PPROT_PRIV;
			break;
		default:
			DEBUG_ERROR("Unhandled AP type %u\n", ap_type);
		}

		if (cfg & ADIV5_AP_CFG_LARGE_ADDRESS)
			DEBUG_INFO(" CFG=%08" PRIx32 " BASE=%08" PRIx32 "%08" PRIx32 " CSW=%08" PRIx32, cfg,
				(uint32_t)(ap->base >> 32U), (uint32_t)ap->base, ap->csw);
		else
			DEBUG_INFO(" CFG=%08" PRIx32 " BASE=%08" PRIx32 " CSW=%08" PRIx32, cfg, (uint32_t)ap->base, ap->csw);

		if (ap->csw & ADIV5_AP_CSW_TRINPROG) {
			DEBUG_ERROR("AP %3u: Transaction in progress. AP is not usable!\n", ap->apsel);
			return false;
		}
	}

	adi_display_ap(ap);
	return true;
}
