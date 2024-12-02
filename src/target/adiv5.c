/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2015 Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 * Copyright (C) 2018-2021 Uwe Bonnes <bon@elektron.ikp.physik.tu-darmstadt.de>
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
 * This file implements transport generic ADIv5 functions.
 *
 * See the following ARM Reference Documents:
 * ARM Debug Interface v5 Architecture Specification, IHI0031 ver. g
 * - https://developer.arm.com/documentation/ihi0031/latest/
 */

#include "general.h"
#include "target.h"
#include "target_probe.h"
#include "jep106.h"
#include "adi.h"
#include "adiv5.h"
#include "adiv6.h"
#include "cortexm.h"
#include "cortex_internal.h"
#include "exception.h"
#if CONFIG_BMDA == 1
#include "bmp_hosted.h"
#endif

/*
 * All this should probably be defined in a dedicated ADIV5 header, so that they
 * are consistently named and accessible when needed in the codebase.
 */

/*
 * This value is taken from the ADIv5 spec table C1-2
 * "AP Identification types for an AP designed by Arm"
 * §C1.3 pg146. This defines a AHB3 AP when the class value is 8
 */
#define ARM_AP_TYPE_AHB3 1U

#define S32K344_TARGET_PARTNO        0x995cU
#define S32K3xx_APB_AP               1U
#define S32K3xx_AHB_AP               4U
#define S32K3xx_MDM_AP               6U
#define S32K3xx_SDA_AP               7U
#define S32K3xx_SDA_AP_DBGENCTR      ADIV5_AP_REG(0x80U)
#define S32K3xx_SDA_AP_DBGENCTR_MASK 0x300000f0U

void adiv5_ap_ref(adiv5_access_port_s *ap)
{
	if (ap->refcnt == 0)
		ap->dp->refcnt++;
	ap->refcnt++;
}

static void adiv5_dp_unref(adiv5_debug_port_s *dp)
{
	if (--(dp->refcnt) == 0)
		free(dp);
}

void adiv5_ap_unref(adiv5_access_port_s *ap)
{
	if (--(ap->refcnt) == 0) {
		adiv5_dp_unref(ap->dp);
		free(ap);
	}
}

/*
 * This function tries to halt Cortex-M processors.
 * To handle WFI and other sleep states, it does this in as tight a loop as it can,
 * either using the TRNCNT bits, or, if on a minimal DP implementation by doing the
 * memory writes as fast as possible.
 */
static uint32_t cortexm_initial_halt(adiv5_access_port_s *ap)
{
	/* Read the current CTRL/STATUS register value to use in the non-minimal DP case */
	const uint32_t ctrlstat = adiv5_dp_read(ap->dp, ADIV5_DP_CTRLSTAT);

	platform_timeout_s halt_timeout;
	platform_timeout_set(&halt_timeout, cortexm_wait_timeout);

	/* Setup to read/write DHCSR */
	/* adi_ap_mem_access_setup() uses ADIV5_AP_CSW_ADDRINC_SINGLE which is undesirable for our use here */
	adiv5_ap_write(ap, ADIV5_AP_CSW, ap->csw | ADIV5_AP_CSW_SIZE_WORD);
	adiv5_dp_low_access(ap->dp, ADIV5_LOW_WRITE, ADIV5_AP_TAR_LOW, CORTEXM_DHCSR);
	/* Write (and do a dummy read of) DHCSR to ensure debug is enabled */
	adiv5_dp_low_access(ap->dp, ADIV5_LOW_WRITE, ADIV5_AP_DRW, CORTEXM_DHCSR_DBGKEY | CORTEXM_DHCSR_C_DEBUGEN);
	adiv5_dp_read(ap->dp, ADIV5_DP_RDBUFF);

	bool reset_seen = false;
	while (!platform_timeout_is_expired(&halt_timeout)) {
		uint32_t dhcsr;

		/* If we're not on a minimal DP implementation, use TRNCNT to help */
		if (!(ap->dp->quirks & ADIV5_DP_QUIRK_MINDP)) {
			/* Ask the AP to repeatedly retry the write to DHCSR */
			adiv5_dp_low_access(
				ap->dp, ADIV5_LOW_WRITE, ADIV5_DP_CTRLSTAT, ctrlstat | ADIV5_DP_CTRLSTAT_TRNCNT(0xfffU));
		}
		/* Repeatedly try to halt the processor */
		adiv5_dp_low_access(ap->dp, ADIV5_LOW_WRITE, ADIV5_AP_DRW,
			CORTEXM_DHCSR_DBGKEY | CORTEXM_DHCSR_C_DEBUGEN | CORTEXM_DHCSR_C_HALT);
		dhcsr = adiv5_dp_low_access(ap->dp, ADIV5_LOW_READ, ADIV5_AP_DRW, 0);

		/*
		 * If we are on a minimal DP implementation, then we have to do things a little differently
		 * so the reads behave consistently. If we use raw accesses as above, then on some parts the
		 * data we want to read will be returned in the first raw access, and on others the read
		 * will do nothing (return 0) and instead need RDBUFF read to get the data.
		 */
		if ((ap->dp->quirks & ADIV5_DP_QUIRK_MINDP)
#if CONFIG_BMDA == 1
			&& bmda_probe_info.type != PROBE_TYPE_CMSIS_DAP && bmda_probe_info.type != PROBE_TYPE_STLINK_V2
#endif
		)
			dhcsr = adiv5_dp_low_access(ap->dp, ADIV5_LOW_READ, ADIV5_DP_RDBUFF, 0);

		/*
		 * Check how we did, handling some errata along the way.
		 * On STM32F7 parts, invalid DHCSR reads of 0xffffffff and 0xa05f0000 may happen,
		 * so filter those out (we check for the latter by checking the reserved bits are 0)
		 */
		if (dhcsr == 0xffffffffU || (dhcsr & 0xf000fff0U) != 0)
			continue;
		/* Now we've got some confidence we've got a good read, check for resets */
		if ((dhcsr & CORTEXM_DHCSR_S_RESET_ST) && !reset_seen) {
			if (connect_assert_nrst)
				return dhcsr;
			reset_seen = true;
			continue;
		}
		/* And finally check if halt succeeded */
		if ((dhcsr & (CORTEXM_DHCSR_S_HALT | CORTEXM_DHCSR_C_DEBUGEN)) ==
			(CORTEXM_DHCSR_S_HALT | CORTEXM_DHCSR_C_DEBUGEN))
			return dhcsr;
	}

	return 0U;
}

/*
 * Prepare the core to read the ROM tables, PIDR, etc
 *
 * Because of various errata, failing to halt the core is considered
 * a hard error. We also need to set the debug exception and monitor
 * control register (DEMCR) up but save its value to restore later,
 * and release the core from reset when connecting under reset.
 *
 * Example errata for STM32F7:
 * - fails reading romtable in WFI
 * - fails with some AP accesses when romtable is read under reset.
 * - fails reading some ROMTABLE entries w/o TRCENA
 * - fails reading outside SYSROM when halted from WFI and DBGMCU_CR not set.
 *
 * Example errata for STM32F0
 * - fails reading DBGMCU when under reset
 */
static bool cortexm_prepare(adiv5_access_port_s *ap)
{
#if CONFIG_BMDA == 1 || ENABLE_DEBUG == 1
	uint32_t start_time = platform_time_ms();
#endif
	uint32_t dhcsr = cortexm_initial_halt(ap);
	if (!dhcsr) {
		DEBUG_ERROR("Halt via DHCSR(%08" PRIx32 "): failure after %" PRIu32 "ms\nTry again with longer "
					"timeout or connect under reset\n",
			adi_mem_read32(ap, CORTEXM_DHCSR), platform_time_ms() - start_time);
		return false;
	}
	/* Clear any residual WAIT fault code to keep things in good state for the next steps */
	ap->dp->fault = 0;
	DEBUG_INFO("Halt via DHCSR(%08" PRIx32 "): success after %" PRIu32 "ms\n", dhcsr, platform_time_ms() - start_time);
	/* Save the old value of DEMCR and enable the DWT, and both vector table debug bits */
	ap->ap_cortexm_demcr = adi_mem_read32(ap, CORTEXM_DEMCR);
	const uint32_t demcr = CORTEXM_DEMCR_TRCENA | CORTEXM_DEMCR_VC_HARDERR | CORTEXM_DEMCR_VC_CORERESET;
	adiv5_mem_write(ap, CORTEXM_DEMCR, &demcr, sizeof(demcr));
	/* Having setup DEMCR, try to observe the core being released from reset */
	platform_timeout_s reset_timeout;
	platform_timeout_set(&reset_timeout, cortexm_wait_timeout);
	/* Deassert the physical reset line */
	platform_nrst_set_val(false);
	while (true) {
		/* Read back DHCSR and check if the reset status bit is still set */
		dhcsr = adi_mem_read32(ap, CORTEXM_DHCSR);
		if ((dhcsr & CORTEXM_DHCSR_S_RESET_ST) == 0)
			break;
		/* If it is and we timeout, turn that into an error */
		if (platform_timeout_is_expired(&reset_timeout)) {
			DEBUG_ERROR("Error releasing from reset\n");
			return false;
		}
	}
	/* Core is now in a good state */
	return true;
}

adiv5_access_port_s *adiv5_new_ap(adiv5_debug_port_s *const dp, const uint8_t apsel)
{
	adiv5_access_port_s ap = {
		.dp = dp,
		.apsel = apsel,
	};
	/* Try to configure the AP for use */
	if (!adi_configure_ap(&ap))
		return NULL;

	/* It's valid to so create a heap copy */
	adiv5_access_port_s *result = malloc(sizeof(*result));
	if (!result) { /* malloc failed: heap exhaustion */
		DEBUG_ERROR("malloc: failed in %s\n", __func__);
		return NULL;
	}
	/* Copy the new AP into place and ref it */
	memcpy(result, &ap, sizeof(*result));
	adiv5_ap_ref(result);
	return result;
}

/* No real AP on RP2040. Special setup.*/
static void rp2040_rescue_setup(adiv5_debug_port_s *dp)
{
	adiv5_access_port_s *ap = calloc(1, sizeof(*ap));
	if (!ap) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return;
	}
	ap->dp = dp;

	rp2040_rescue_probe(ap);
}

static void adiv5_dp_clear_sticky_errors(adiv5_debug_port_s *dp)
{
	/*
	 * For DPv1+ APs, this is done by writing through the ABORT register.
	 * For DPv0 APs, this must be done by writing a 1 back to the appropriate
	 * CTRL/STATUS register bit
	 */
	if (dp->version)
		adiv5_dp_abort(dp, ADIV5_DP_ABORT_STKERRCLR);
	else
		/* For JTAG-DPs (which all DPv0 DPs are), use the adiv5_jtag_clear_error routine */
		adiv5_dp_error(dp);
}

/* Keep the TRY/CATCH funkiness contained to avoid clobbering and reduce the need for volatiles */
uint32_t adiv5_dp_read_dpidr(adiv5_debug_port_s *const dp)
{
	if (dp->read_no_check)
		return adiv5_read_no_check(dp, ADIV5_DP_DPIDR);
	volatile uint32_t dpidr = 0U;
	TRY (EXCEPTION_ALL) {
		/* JTAG has a clean DP read routine, so use that as that handles the RDBUFF quirk of the physical protocol */
		if (dp->quirks & ADIV5_DP_JTAG)
			dpidr = adiv5_dp_read(dp, ADIV5_DP_DPIDR);
		/* Otherwise, if we're talking over SWD, issue a raw access for the register to avoid protocol recovery */
		else
			dpidr = adiv5_dp_low_access(dp, ADIV5_LOW_READ, ADIV5_DP_DPIDR, 0U);
	}
	CATCH () {
	default:
		return 0U;
	}
	return dpidr;
}

static bool s32k3xx_dp_prepare(adiv5_debug_port_s *const dp)
{
	/* Is this an S32K344? */
	if (dp->target_partno != S32K344_TARGET_PARTNO)
		return false;

	adiv5_dp_abort(dp, ADIV5_DP_ABORT_DAPABORT);

	/* SDA_AP has various flags we must enable before we can have debug access, so
	 * start with it and enable them */
	adiv5_access_port_s *sda_ap = adiv5_new_ap(dp, S32K3xx_SDA_AP);
	if (!sda_ap)
		return false;
	adiv5_ap_write(sda_ap, S32K3xx_SDA_AP_DBGENCTR, S32K3xx_SDA_AP_DBGENCTR_MASK);
	adiv5_ap_unref(sda_ap);

	/* If we try to access an invalid AP the S32K3 will hard fault, so we must
	 * statically enumerate the APs we expect */
	adiv5_access_port_s *apb_ap = adiv5_new_ap(dp, S32K3xx_APB_AP);
	if (!apb_ap)
		return false;
	adi_ap_component_probe(apb_ap, apb_ap->base, 0, 0);
	adiv5_ap_unref(apb_ap);

	adiv5_access_port_s *ahb_ap = adiv5_new_ap(dp, S32K3xx_AHB_AP);
	if (!ahb_ap)
		return false;
	adi_ap_component_probe(ahb_ap, ahb_ap->base, 0, 0);

	cortexm_prepare(ahb_ap);
	adi_ap_resume_cores(ahb_ap);

	adiv5_ap_unref(ahb_ap);

	adiv5_access_port_s *mdm_ap = adiv5_new_ap(dp, S32K3xx_MDM_AP);
	if (!mdm_ap)
		return false;
	adi_ap_component_probe(mdm_ap, mdm_ap->base, 0, 0);
	adiv5_ap_unref(mdm_ap);

	return true;
}

static bool adiv5_power_cycle_aps(adiv5_debug_port_s *const dp)
{
	platform_timeout_s timeout;
	platform_timeout_set(&timeout, 250);

	/* Start by resetting the DP control state so the debug domain powers down */
	adiv5_dp_write(dp, ADIV5_DP_CTRLSTAT, 0U);
	uint32_t status = ADIV5_DP_CTRLSTAT_CSYSPWRUPACK | ADIV5_DP_CTRLSTAT_CDBGPWRUPACK;
	/* Wait for the acknowledgements to go low */
	while (status & (ADIV5_DP_CTRLSTAT_CSYSPWRUPACK | ADIV5_DP_CTRLSTAT_CDBGPWRUPACK)) {
		status = adiv5_dp_read(dp, ADIV5_DP_CTRLSTAT);
		if (platform_timeout_is_expired(&timeout)) {
			DEBUG_WARN("adiv5: power-down failed\n");
			break;
		}
	}

	platform_timeout_set(&timeout, 201);
	/* Write request for system and debug power up */
	adiv5_dp_write(dp, ADIV5_DP_CTRLSTAT, ADIV5_DP_CTRLSTAT_CSYSPWRUPREQ | ADIV5_DP_CTRLSTAT_CDBGPWRUPREQ);
	/* Wait for acknowledge */
	status = 0U;
	while (status != (ADIV5_DP_CTRLSTAT_CSYSPWRUPACK | ADIV5_DP_CTRLSTAT_CDBGPWRUPACK)) {
		platform_delay(10);
		status =
			adiv5_dp_read(dp, ADIV5_DP_CTRLSTAT) & (ADIV5_DP_CTRLSTAT_CSYSPWRUPACK | ADIV5_DP_CTRLSTAT_CDBGPWRUPACK);
		if (status == (ADIV5_DP_CTRLSTAT_CSYSPWRUPACK | ADIV5_DP_CTRLSTAT_CDBGPWRUPACK))
			break;
		if (platform_timeout_is_expired(&timeout)) {
			DEBUG_WARN("adiv5: power-up failed\n");
			return false;
		}
	}
	/* At this point due to the guaranteed power domain restart, the APs are all up and in their reset state. */
	return true;
}

void adiv5_dp_init(adiv5_debug_port_s *const dp)
{
	/*
	 * We have to initialise the DP routines up front before any adiv5_* functions are called or
	 * bad things happen under BMDA (particularly CMSIS-DAP)
	 */
	dp->ap_write = adiv5_ap_reg_write;
	dp->ap_read = adiv5_ap_reg_read;
	dp->mem_read = adiv5_mem_read_bytes;
	dp->mem_write = adiv5_mem_write_bytes;
#if CONFIG_BMDA == 1
	bmda_adiv5_dp_init(dp);
#endif

	/*
	 * Unless we've got an ARM SoC-400 JTAG-DP, which must be ADIv5 and so DPv0, we can safely assume
	 * that the DPIDR exists to read and find out what DP version we're working with here.
	 *
	 * If the part ID code indicates it is a SoC-400 JTAG-DP, however, it is DPv0. In this case, DPIDR
	 * is not implemented and attempting to read is is UNPREDICTABLE so we want to avoid doing that.
	 */
	if (dp->designer_code != JEP106_MANUFACTURER_ARM || dp->partno != JTAG_IDCODE_PARTNO_SOC400_4BIT) {
		/* Ensure that DPIDR is definitely selected */
		adiv5_dp_write(dp, ADIV5_DP_SELECT, ADIV5_DP_BANK0);
		const uint32_t dpidr = adiv5_dp_read_dpidr(dp);
		if (!dpidr) {
			DEBUG_ERROR("Failed to read DPIDR\n");
			free(dp);
			return;
		}

		dp->version = (dpidr & ADIV5_DP_DPIDR_VERSION_MASK) >> ADIV5_DP_DPIDR_VERSION_OFFSET;

		/*
		 * The code in the DPIDR is in the form
		 * Bits 10:7 - JEP-106 Continuation code
		 * Bits 6:0 - JEP-106 Identity code
		 * here we convert it to our internal representation, See JEP-106 code list
		 *
		 * Note: this is the code of the designer not the implementer, we expect it to be ARM
		 */
		dp->designer_code =
			adi_decode_designer((dpidr & ADIV5_DP_DPIDR_DESIGNER_MASK) >> ADIV5_DP_DPIDR_DESIGNER_OFFSET);
		dp->partno = (dpidr & ADIV5_DP_DPIDR_PARTNO_MASK) >> ADIV5_DP_DPIDR_PARTNO_OFFSET;

		/* Minimal Debug Port (MINDP) functions implemented */
		dp->quirks = (dpidr >> ADIV5_DP_DPIDR_MINDP_OFFSET) & ADIV5_DP_QUIRK_MINDP;

		/*
		 * Check DPIDR validity
		 * Designer code 0 is not a valid JEP-106 code
		 * Version 0 is reserved for DPv0 which does not implement DPIDR
		 * Bit 0 of DPIDR is read as 1
		 */
		if (dp->designer_code != 0U && dp->version > 0U && (dpidr & 1U)) {
			DEBUG_INFO("DP DPIDR 0x%08" PRIx32 " (v%x %srev%" PRIu32 ") designer 0x%x partno 0x%x\n", dpidr,
				dp->version, (dp->quirks & ADIV5_DP_QUIRK_MINDP) ? "MINDP " : "",
				(dpidr & ADIV5_DP_DPIDR_REVISION_MASK) >> ADIV5_DP_DPIDR_REVISION_OFFSET, dp->designer_code,
				dp->partno);
		} else {
			DEBUG_WARN("Invalid DPIDR %08" PRIx32 " assuming DPv0\n", dpidr);
			dp->version = 0U;
			dp->designer_code = 0U;
			dp->partno = 0U;
			dp->quirks = 0U;
		}
	} else if (dp->version == 0)
		/* DP v0 */
		DEBUG_WARN("DPv0 detected based on JTAG IDCode\n");

	/*
	 * Ensure that whatever previous accesses happened to this DP before we
	 * scanned the chain and found it, the sticky error bit is cleared
	 */
	adiv5_dp_clear_sticky_errors(dp);

	if (dp->version >= 2) {
		/* TARGETID is on bank 2 */
		adiv5_dp_write(dp, ADIV5_DP_SELECT, ADIV5_DP_BANK2);
		const uint32_t targetid = adiv5_dp_read(dp, ADIV5_DP_TARGETID);
		adiv5_dp_write(dp, ADIV5_DP_SELECT, ADIV5_DP_BANK0);

		/*
		 * Use TARGETID register to identify target and convert it
		 * to our internal representation, See JEP-106 code list.
		 */
		dp->target_designer_code =
			adi_decode_designer((targetid & ADIV5_DP_TARGETID_TDESIGNER_MASK) >> ADIV5_DP_TARGETID_TDESIGNER_OFFSET);

		dp->target_partno = (targetid & ADIV5_DP_TARGETID_TPARTNO_MASK) >> ADIV5_DP_TARGETID_TPARTNO_OFFSET;

		DEBUG_INFO("TARGETID 0x%08" PRIx32 " designer 0x%x partno 0x%x\n", targetid, dp->target_designer_code,
			dp->target_partno);

		dp->targetsel = ((uint32_t)dp->dev_index << ADIV5_DP_TARGETSEL_TINSTANCE_OFFSET) |
			(targetid & (ADIV5_DP_TARGETID_TDESIGNER_MASK | ADIV5_DP_TARGETID_TPARTNO_MASK)) | 1U;
	}

	if (dp->designer_code == JEP106_MANUFACTURER_RASPBERRY && dp->partno == 0x2U) {
		rp2040_rescue_setup(dp);
		return;
	}

	/* Try to power cycle the APs, affecting a reset on them */
	if (!adiv5_power_cycle_aps(dp)) {
		/* Clean up by freeing the DP - no APs have been constructed at this point, so this is safe */
		free(dp);
		return;
	}

	/* If this is a DPv3+ device, switch to ADIv6 DP initialisation */
	if (dp->version >= 3U) {
		++dp->refcnt;
		if (!adiv6_dp_init(dp))
			DEBUG_ERROR("Error while discovering ADIv6 DP\n");
		adiv5_dp_unref(dp);
		return;
	}

	if (dp->target_designer_code == JEP106_MANUFACTURER_NXP)
		lpc55_dp_prepare(dp);

	/* Probe for APs on this DP */
	size_t invalid_aps = 0U;
	dp->refcnt++;

	if (dp->target_designer_code == JEP106_MANUFACTURER_FREESCALE) {
		/* S32K3XX will requires special handling, do so and skip the AP enumeration */
		if (s32k3xx_dp_prepare(dp)) {
			adiv5_dp_unref(dp);
			return;
		}
	}

	for (size_t i = 0; i < 256U && invalid_aps < 8U; ++i) {
		adiv5_access_port_s *ap = adiv5_new_ap(dp, i);
		if (ap == NULL) {
			/* Clear sticky errors in case scanning for this AP triggered any */
			adiv5_dp_clear_sticky_errors(dp);
			/*
			 * We have probably found all APs on this DP so no need to keep looking.
			 * Continue with rest of init function down below.
			 */
			if (++invalid_aps == 8U)
				break;

			continue;
		}

		kinetis_mdm_probe(ap);
		nrf51_ctrl_ap_probe(ap);
		nrf54l_ctrl_ap_probe(ap);
		efm32_aap_probe(ap);
		lpc55_dmap_probe(ap);

		if (ADIV5_AP_IDR_CLASS(ap->idr) == ADIV5_AP_IDR_CLASS_MEM) {
			/* Try to prepare the AP if it seems to be a AHB3 MEM-AP */
			if (!ap->apsel && ADIV5_AP_IDR_TYPE(ap->idr) == ARM_AP_TYPE_AHB3) {
				if (!cortexm_prepare(ap))
					DEBUG_WARN("adiv5: Failed to prepare AP, results may be unpredictable\n");
			}

			/* The rest should only be added after checking ROM table */
			adi_ap_component_probe(ap, ap->base, 0, 0);
			/* Having completed discovery on this AP, try to resume any halted cores */
			adi_ap_resume_cores(ap);

			/*
			* Due to the Tiva TM4C1294KCDT (among others) repeating the single AP ad-nauseum,
			* this check is needed so that we bail rather than repeating the same AP ~256 times.
			*/
			if (ap->dp->quirks & ADIV5_DP_QUIRK_DUPED_AP) {
				adiv5_ap_unref(ap);
				adiv5_dp_unref(dp);
				return;
			}
		}

		adiv5_ap_unref(ap);
	}
	adiv5_dp_unref(dp);
}

/* Unpack data from the source uint32_t value based on data alignment and source address */
void *adiv5_unpack_data(void *const dest, const target_addr32_t src, const uint32_t data, const align_e align)
{
	switch (align) {
	case ALIGN_8BIT: {
		/*
		 * Mask off the bottom 2 bits of the address to figure out which byte of data to use
		 * then multiply that by 8 and shift the data down by the result to pick one of the 4 possible bytes
		 */
		uint8_t value = (data >> (8U * (src & 3U))) & 0xffU;
		/* Then memcpy() the result to the destination buffer (this avoids doing a possibly UB cast) */
		memcpy(dest, &value, sizeof(value));
		break;
	}
	case ALIGN_16BIT: {
		/*
		 * Mask off the 2nd bit of the address to figure out which 16 bits of data to use
		 * then multiply that by 8 and shift the data down by the result to pick one of the 2 possible 16-bit blocks
		 */
		uint16_t value = (data >> (8U * (src & 2U))) & 0xffffU;
		/* Then memcpy() the result to the destination buffer (this avoids unaligned write issues) */
		memcpy(dest, &value, sizeof(value));
		break;
	}
	case ALIGN_64BIT:
	case ALIGN_32BIT:
		/*
		 * When using 32- or 64-bit alignment, we don't have to do anything special, just memcpy() the data to the
		 * destination buffer (this avoids issues with unaligned writes and UB casts)
		 */
		memcpy(dest, &data, sizeof(data));
		break;
	}
	return (uint8_t *)dest + (1U << align);
}

/* Pack data from the source value into a uint32_t based on data alignment and source address */
const void *adiv5_pack_data(
	const target_addr32_t dest, const void *const src, uint32_t *const data, const align_e align)
{
	switch (align) {
	case ALIGN_8BIT: {
		uint8_t value;
		/* Copy the data to pack in from the source buffer */
		memcpy(&value, src, sizeof(value));
		/* Then shift it up to the appropriate byte in data based on the bottom 2 bits of the destination address */
		*data = (uint32_t)value << (8U * (dest & 3U));
		break;
	}
	case ALIGN_16BIT: {
		uint16_t value;
		/* Copy the data to pack in from the source buffer (avoids unaligned read issues) */
		memcpy(&value, src, sizeof(value));
		/* Then shift it up to the appropriate 16-bit block in data based on the 2nd bit of the destination address */
		*data = (uint32_t)value << (8U * (dest & 2U));
		break;
	}
	default:
		/*
		 * 32- and 64-bit aligned reads don't need to do anything special beyond using memcpy()
		 * to avoid doing  an unaligned read of src, or any UB casts.
		 */
		memcpy(data, src, sizeof(*data));
		break;
	}
	return (const uint8_t *)src + (1U << align);
}

void adiv5_mem_read_bytes(adiv5_access_port_s *const ap, void *dest, const target_addr64_t src, const size_t len)
{
	/* Do nothing and return if there's nothing to read */
	if (len == 0U)
		return;
	/* Calculate the extent of the transfer */
	target_addr64_t begin = src;
	const target_addr64_t end = begin + len;
	/* Calculate the alignment of the transfer */
	const align_e align = MIN_ALIGN(src, len);
	/* Calculate how much each loop will increment the destination address by */
	const uint8_t stride = 1U << align;
	/* Set up the transfer */
	adi_ap_mem_access_setup(ap, src, align);
	/* Now loop through the data and move it 1 stride at a time to the target */
	for (; begin < end; begin += stride) {
		/*
		 * Check if the address doesn't overflow the 10-bit auto increment bound for TAR,
		 * if it's not the first transfer (offset == 0)
		 */
		if (begin != src && (begin & 0x000003ffU) == 0U) {
			/* Update TAR to adjust the upper bits */
			if (ap->flags & ADIV5_AP_FLAGS_64BIT)
				adiv5_dp_write(ap->dp, ADIV5_AP_TAR_HIGH, (uint32_t)(begin >> 32));
			adiv5_dp_write(ap->dp, ADIV5_AP_TAR_LOW, (uint32_t)begin);
		}
		/* Grab the next chunk of data from the target */
		const uint32_t value = adiv5_dp_read(ap->dp, ADIV5_AP_DRW);
		/* Unpack the data from the chunk */
		dest = adiv5_unpack_data(dest, begin, value, align);
	}
}

void adiv5_mem_write_bytes(
	adiv5_access_port_s *const ap, const target_addr64_t dest, const void *src, const size_t len, const align_e align)
{
	/* Do nothing and return if there's nothing to write */
	if (len == 0U)
		return;
	/* Calculate the extent of the transfer */
	target_addr64_t begin = dest;
	const target_addr64_t end = begin + len;
	/* Calculate how much each loop will increment the destination address by */
	const uint8_t stride = 1U << align;
	/* Set up the transfer */
	adi_ap_mem_access_setup(ap, dest, align);
	/* Now loop through the data and move it 1 stride at a time to the target */
	for (; begin < end; begin += stride) {
		/*
		 * Check if the address doesn't overflow the 10-bit auto increment bound for TAR,
		 * if it's not the first transfer (offset == 0)
		 */
		if (begin != dest && (begin & 0x000003ffU) == 0U) {
			/* Update TAR to adjust the upper bits */
			if (ap->flags & ADIV5_AP_FLAGS_64BIT)
				adiv5_dp_write(ap->dp, ADIV5_AP_TAR_HIGH, (uint32_t)(begin >> 32));
			adiv5_dp_write(ap->dp, ADIV5_AP_TAR_LOW, (uint32_t)begin);
		}
		/* Pack the data for transfer */
		uint32_t value = 0;
		src = adiv5_pack_data(begin, src, &value, align);
		/* And copy the result to the target */
		adiv5_dp_write(ap->dp, ADIV5_AP_DRW, value);
	}
	/* Make sure this write is complete by doing a dummy read */
	adiv5_dp_read(ap->dp, ADIV5_DP_RDBUFF);
}

void adiv5_ap_reg_write(adiv5_access_port_s *ap, uint16_t addr, uint32_t value)
{
	adiv5_dp_recoverable_access(
		ap->dp, ADIV5_LOW_WRITE, ADIV5_DP_SELECT, ((uint32_t)ap->apsel << 24U) | (addr & 0xf0U));
	adiv5_dp_write(ap->dp, addr, value);
}

uint32_t adiv5_ap_reg_read(adiv5_access_port_s *ap, uint16_t addr)
{
	adiv5_dp_recoverable_access(
		ap->dp, ADIV5_LOW_WRITE, ADIV5_DP_SELECT, ((uint32_t)ap->apsel << 24U) | (addr & 0xf0U));
	return adiv5_dp_read(ap->dp, addr);
}

void adiv5_mem_write(adiv5_access_port_s *const ap, const target_addr64_t dest, const void *const src, const size_t len)
{
	const align_e align = MIN_ALIGN(dest, len);
	adiv5_mem_write_aligned(ap, dest, src, len, align);
}

#ifndef DEBUG_PROTO_IS_NOOP
static void decode_dp_access(const uint8_t addr, const uint8_t rnw, const uint32_t value)
{
	/* How a DP address should be decoded depends on the bank that's presently selected, so make a note of that */
	static uint8_t dp_bank = 0;
	const char *reg = NULL;

	/* Try to decode the requested address */
	switch (addr) {
	case 0x00U:
		/* If it's a read, it depends on the bank */
		if (rnw) {
			switch (dp_bank) {
			case 0:
				reg = "DPIDR";
				break;
			case 1:
				reg = "DPIDR1";
				break;
			case 2:
				reg = "BASEPTR0";
				break;
			case 3:
				reg = "BASEPTR1";
				break;
			}
		} else
			/* Otherwise it must be a write to ABORT */
			reg = "ABORT";
		break;
	case 0x04U:
		switch (dp_bank) {
		case 0:
			reg = rnw ? "STATUS" : "CTRL";
			break;
		case 1:
			reg = "DLCR";
			break;
		case 2:
			reg = "TARGETID";
			break;
		case 3:
			reg = "DLPIDR";
			break;
		case 4:
			reg = "EVENTSTAT";
			break;
		case 5:
			if (!rnw)
				reg = "SELECT1";
			break;
		}
		break;
	case 0x08U:
		if (!rnw)
			dp_bank = value & 15U;
		reg = rnw ? "RESEND" : "SELECT";
		break;
	case 0x0cU:
		reg = rnw ? "RDBUFF" : "TARGETSEL";
		break;
	}

	if (reg)
		DEBUG_PROTO("%s: ", reg);
	else
		DEBUG_PROTO("Unknown DP register %02x: ", addr);
}

static void decode_ap_access(const uint8_t ap, const uint16_t addr)
{
	DEBUG_PROTO("AP %u ", ap);

	const char *reg = NULL;
	switch (addr) {
	case 0xd00U:
		reg = "CSW";
		break;
	case 0xd04U:
		reg = "TAR";
		break;
	case 0xd0cU:
		reg = "DRW";
		break;
	case 0xd10U:
		reg = "DB0";
		break;
	case 0xd14U:
		reg = "DB1";
		break;
	case 0xd18U:
		reg = "DB2";
		break;
	case 0xd1cU:
		reg = "DB3";
		break;
	case 0xdf8U:
		reg = "BASE";
		break;
	case 0xdf4U:
		reg = "CFG";
		break;
	case 0xdfcU:
		reg = "IDR";
		break;
	case 0xfbc:
		reg = "DEVARCH";
		break;
	case 0xfc8:
		reg = "DEVID";
		break;
	case 0xfcc:
		reg = "DEVTYPE";
		break;
	case 0xfd0:
		reg = "PIDR4";
		break;
	case 0xfd4:
		reg = "PIDR5";
		break;
	case 0xfd8:
		reg = "PIDR6";
		break;
	case 0xfdc:
		reg = "PIDR7";
		break;
	case 0xfe0:
		reg = "PIDR0";
		break;
	case 0xfe4:
		reg = "PIDR1";
		break;
	case 0xfe8:
		reg = "PIDR2";
		break;
	case 0xfec:
		reg = "PIDR3";
		break;
	case 0xff0:
		reg = "CIDR0";
		break;
	case 0xff4:
		reg = "CIDR1";
		break;
	case 0xff8:
		reg = "CIDR2";
		break;
	case 0xffc:
		reg = "CIDR3";
		break;
	}

	if (reg)
		DEBUG_PROTO("%s: ", reg);
	else
		DEBUG_PROTO("Reserved(%03x): ", addr);
}

void decode_access(const uint16_t addr, const uint8_t rnw, const uint8_t apsel, const uint32_t value)
{
	if (rnw)
		DEBUG_PROTO("Read ");
	else
		DEBUG_PROTO("Write ");

	if (addr & ADIV5_APnDP)
		decode_ap_access(apsel, addr & 0x0fffU);
	else
		decode_dp_access(addr & 0xffU, rnw, value);
}
#endif
