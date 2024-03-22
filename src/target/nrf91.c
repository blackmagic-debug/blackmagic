#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"
#include "adiv5.h"
#include "gdb_packet.h"

/* Non-Volatile Memory Controller (NVMC) Registers */
#define NRF91_NVMC           0x50039000U
#define NRF91_NVMC_READY     (NRF91_NVMC + 0x400U)
#define NRF91_NVMC_READYNEXT (NRF91_NVMC + 0x408U)
#define NRF91_NVMC_CONFIG    (NRF91_NVMC + 0x504U)
#define NRF91_NVMC_ERASEALL  (NRF91_NVMC + 0x50cU)

#define NVMC_TIMEOUT_MS 300U

#define NRF91_NVMC_CONFIG_REN  0x0U // Read only access
#define NRF91_NVMC_CONFIG_WEN  0x1U // Write enable
#define NRF91_NVMC_CONFIG_EEN  0x2U // Erase enable
#define NRF91_NVMC_CONFIG_PEEN 0x3U // Partial erase enable

/* https://infocenter.nordicsemi.com/topic/ps_nrf9160/dif.html */
#define NRF91_PARTNO 0x90U

#define NRF91_CTRL_AP_RESET          ADIV5_AP_REG(0x000)
#define NRF91_CTRL_AP_ERASEALL       ADIV5_AP_REG(0x004)
#define NRF91_CTRL_IDR_EXPECTED      0x12880000
#define NRF91_AHB_AP_IDR_EXPECTED    0x84770001
#define NRF91_CTRL_AP_ERASEALLSTATUS ADIV5_AP_REG(0x008)

/* https://infocenter.nordicsemi.com/topic/ps_nrf9161/uicr.html */
#define NRF91_UICR_APPROTECT               0x00FF8000U
#define NRF91_UICR_SECUREAPPROTECT         0x00FF802CU
#define NRF91_UICR_APPROTECT_UNPROTECT_VAL 0x50FA50FAU
#define NRF91_UICR_ERASED_VAL              0xFFFFFFFFU

unsigned char empty_app[] = {
  0x00, 0x10, 0x00, 0x20, 0x09, 0x00, 0x00, 0x00, 0x05, 0x4b, 0x4f, 0xf0,
  0x5a, 0x02, 0xc3, 0xf8, 0x10, 0x2e, 0x03, 0x4b, 0x4f, 0xf0, 0x5a, 0x02,
  0xc3, 0xf8, 0x00, 0x2e, 0xfe, 0xe7, 0x00, 0x00, 0x00, 0x90, 0x03, 0x50
};
unsigned int empty_app_len = 36;

static bool nrf91_ctrl_ap_mass_erase(adiv5_access_port_s *ap)
{
	adiv5_ap_write(ap, NRF91_CTRL_AP_ERASEALL, 1);
	platform_timeout_s timeout;
	platform_timeout_set(&timeout, NVMC_TIMEOUT_MS);

	bool ret = false;

	while (true) {
		uint32_t status = adiv5_ap_read(ap, NRF91_CTRL_AP_ERASEALLSTATUS);
		if (status == 0) {
			ret = true;
			DEBUG_INFO("nRF91 mass erase succeeded.\n");
			break;
		}
		if (platform_timeout_is_expired(&timeout)) {
			DEBUG_INFO("nRF91 mass erase failed.\n");
			break;
		}
	}

	platform_delay(10);

	adiv5_ap_write(ap, NRF91_CTRL_AP_RESET, 1);
	adiv5_ap_write(ap, NRF91_CTRL_AP_RESET, 0);

	platform_delay(200);

	return ret;
}

static bool nrf91_wait_ready(target_s *const target, platform_timeout_s *const timeout)
{
	/* Poll for NVMC_READY */
	while (target_mem_read32(target, NRF91_NVMC_READY) == 0) {
		if (target_check_error(target))
			return false;
		if (timeout)
			target_print_progress(timeout);
	}
	return true;
}

static bool nrf91_flash_erase(target_flash_s *flash, target_addr_t addr, size_t len)
{
	target_s *target = flash->t;

	/* Enable erase */
	target_mem_write32(target, NRF91_NVMC_CONFIG, NRF91_NVMC_CONFIG_EEN);
	if (!nrf91_wait_ready(target, NULL))
		return false;

	for (size_t offset = 0; offset < len; offset += flash->blocksize) {
		/* Write all ones to first word in page to erase it */
		target_mem_write32(target, addr + offset, 0xffffffffU);

		if (!nrf91_wait_ready(target, NULL))
			return false;
	}

	/* Return to read-only */
	target_mem_write32(target, NRF91_NVMC_CONFIG, NRF91_NVMC_CONFIG_REN);
	return nrf91_wait_ready(target, NULL);
}

static bool nrf91_uicr_flash_erase(target_flash_s *flash, target_addr_t addr, size_t len)
{
	target_s *target = flash->t;

	bool erase_needed = false;

	for (size_t offset = 0; offset < len; offset += 4) {
		if (target_mem_read32(target, addr + offset) != NRF91_UICR_ERASED_VAL) {
			erase_needed = true;
			break;
		}
	}

	if (erase_needed) {
		gdb_out("Skipping UICR erase, mass erase might be needed\n");
	}
	return true;
}

static bool nrf91_flash_write(target_flash_s *flash, target_addr_t dest, const void *src, size_t len)
{
	target_s *target = flash->t;

	/* Enable write */
	target_mem_write32(target, NRF91_NVMC_CONFIG, NRF91_NVMC_CONFIG_WEN);
	if (!nrf91_wait_ready(target, NULL))
		return false;
	/* Write the data */
	target_mem_write(target, dest, src, len);
	if (!nrf91_wait_ready(target, NULL))
		return false;
	/* Return to read-only */
	target_mem_write32(target, NRF91_NVMC_CONFIG, NRF91_NVMC_CONFIG_REN);
	return true;
}

static void nrf91_add_flash(target_s *target, uint32_t addr, size_t length, size_t erasesize)
{
	/* add main flash */
	target_flash_s *flash = calloc(1, sizeof(*flash));
	if (!flash) { /* calloc failed: heap exhaustion */
		DEBUG_WARN("calloc: failed in %s\n", __func__);
		return;
	}

	flash->start = addr;
	flash->length = length;
	flash->blocksize = erasesize;
	flash->erase = nrf91_flash_erase;
	flash->write = nrf91_flash_write;
	flash->erased = 0xff;
	target_add_flash(target, flash);

	/* add separate UICR flash */
	target_flash_s *flash_uicr = calloc(1, sizeof(*flash_uicr));
	if (!flash_uicr) { /* calloc failed: heap exhaustion */
		DEBUG_WARN("calloc: failed in %s\n", __func__);
		return;
	}

	flash_uicr->start = 0xff8000U;
	flash_uicr->length = 0x1000U;
	flash_uicr->blocksize = 0x4U;
	flash_uicr->erase = nrf91_uicr_flash_erase;
	flash_uicr->write = nrf91_flash_write;
	flash_uicr->erased = 0xff;
	target_add_flash(target, flash_uicr);
}

static bool nrf91_mass_erase(target_s *target)
{
	adiv5_access_port_s *ap = cortex_ap(target);
	adiv5_access_port_s ctrl_ap = {
		.dp = ap->dp,
		.apsel = 0x4U,
	};

	if (!nrf91_ctrl_ap_mass_erase(&ctrl_ap)) {
		return false;
	}

	if (ap->dp->target_revision > 2) {
		target_mem_write32(target, NRF91_NVMC_CONFIG, NRF91_NVMC_CONFIG_WEN);
		while (target_mem_read32(target, NRF91_NVMC_READY) == 0) {
			platform_delay(1);
			DEBUG_INFO("Waiting for NVMC to become ready\n");
		}

		target_mem_write(target, 0, empty_app, empty_app_len);
		target_mem_write32(target, NRF91_UICR_APPROTECT, NRF91_UICR_APPROTECT_UNPROTECT_VAL);
		target_mem_write32(target, NRF91_UICR_SECUREAPPROTECT, NRF91_UICR_APPROTECT_UNPROTECT_VAL);

		while (target_mem_read32(target, NRF91_NVMC_READY) == 0) {
			platform_delay(1);
			DEBUG_INFO("Waiting for NVMC to become ready\n");
		}

		target_mem_write32(target, NRF91_NVMC_CONFIG, NRF91_NVMC_CONFIG_REN);
	}

	return true;
}

static bool nrf91_exit_flash_mode(target_s *const target)
{
	adiv5_access_port_s *ap = cortex_ap(target);
	/* Persist AP access if uninitialized (only needed for devices with hardenend APPROTECT) */
	if (ap->dp->target_revision > 2) {
		bool approtect_erased = target_mem_read32(target, NRF91_UICR_APPROTECT) == NRF91_UICR_ERASED_VAL;
		bool secureapprotect_erased = target_mem_read32(target, NRF91_UICR_SECUREAPPROTECT) == NRF91_UICR_ERASED_VAL;

		target_mem_write32(target, NRF91_NVMC_CONFIG, NRF91_NVMC_CONFIG_WEN);

		while (target_mem_read32(target, NRF91_NVMC_READY) == 0) {
			platform_delay(1);
			DEBUG_INFO("Waiting for NVMC to become ready\n");
		}

		if (approtect_erased) {
			target_mem_write32(target, NRF91_UICR_APPROTECT, NRF91_UICR_APPROTECT_UNPROTECT_VAL);
		}
		if (secureapprotect_erased) {
			target_mem_write32(target, NRF91_UICR_SECUREAPPROTECT, NRF91_UICR_APPROTECT_UNPROTECT_VAL);
		}

		while (target_mem_read32(target, NRF91_NVMC_READY) == 0) {
			platform_delay(1);
			DEBUG_INFO("Waiting for NVMC to become ready\n");
		}

		target_mem_write32(target, NRF91_NVMC_CONFIG, NRF91_NVMC_CONFIG_REN);
	}
	return true;
}

bool nrf91_probe(target_s *target)
{
	adiv5_access_port_s *ap = cortex_ap(target);

	if (ap->dp->version < 2U || ap->dp->target_partno != NRF91_PARTNO)
		return false;

#ifndef ENABLE_DEBUG
	uint32_t partno = target_mem_read32(target, 0x00FF0140);
	uint32_t hwrevision = target_mem_read32(target, 0x00FF0144);
	uint32_t variant = target_mem_read32(target, 0x00FF0148);
	DEBUG_INFO("nRF%04" PRIx32 " %4s%4s detected!\n", partno, (const char *)&variant, (const char *)&hwrevision);
#endif
	switch (ap->dp->target_revision) {
	case 0:
	case 1:
	case 2:
		target->driver = "Nordic nRF9160";
		break;
	case 3:
		target->driver = "Nordic nRF91x1";
		break;
	default:
		target->driver = "Nordic nRF91";
	}

	target->target_options |= TOPT_INHIBIT_NRST;
	target_add_ram(target, 0x20000000, 256U * 1024U);
	nrf91_add_flash(target, 0, 4096U * 256U, 4096U);

	target->mass_erase = nrf91_mass_erase;
	target->exit_flash_mode = nrf91_exit_flash_mode;

	return true;
}

static bool nrf91_rescue_do_recover(target_s *target)
{
	adiv5_access_port_s *ap = (adiv5_access_port_s *)target->priv;

	const bool hardened_approtect = ap->dp->target_revision > 2;

	/* on some revisions, this needs to be repeated */
	for (size_t i = 0; i < 3; ++i) {
		if (!nrf91_ctrl_ap_mass_erase(ap))
			continue;
		if (!hardened_approtect) {
			/* pin reset is needed on older devices */
			platform_nrst_set_val(true);
			platform_delay(100);
			platform_nrst_set_val(false);

			/* repetition not needed and debug port inactive at this point */
			return false;
		}

		//check if CSW DEVICEEN is set
		struct adiv5_access_port ahb_ap = *ap;
		ahb_ap.apsel = 0x0U;
		const uint32_t csw = ap->dp->ap_read(&ahb_ap, ADIV5_AP_CSW);
		if (csw & ADIV5_AP_CSW_DEVICEEN) {
			DEBUG_INFO("nRF91 Rescue succeeded.\n");
			break;
		}
	}

	return false;
}

bool nrf91_rescue_probe(adiv5_access_port_s *ap)
{
	target_s *target = target_new();
	if (!target) {
		return false;
	}
	adiv5_ap_ref(ap);
	target->attach = (void *)nrf91_rescue_do_recover;
	target->priv = ap;
	target->priv_free = (void *)adiv5_ap_unref;
	target->driver = "nRF91 Rescue (Attach, then scan again!)";

	return true;
}

/* check if nRF91 target is in secure state, return false if device is protected */
bool nrf91_dp_prepare(adiv5_debug_port_s *const dp)
{
	adiv5_access_port_s ahb_ap = {
		.dp = dp,
		.apsel = 0x0U,
	};
	adiv5_access_port_s ctrl_ap = {
		.dp = dp,
		.apsel = 0x4U,
	};
	ahb_ap.idr = adiv5_ap_read(&ahb_ap, ADIV5_AP_IDR);
	ahb_ap.csw = adiv5_ap_read(&ahb_ap, ADIV5_AP_CSW);
	ctrl_ap.idr = adiv5_ap_read(&ctrl_ap, ADIV5_AP_IDR);

	if (ahb_ap.idr != NRF91_AHB_AP_IDR_EXPECTED) {
		DEBUG_ERROR(
			"nRF91: AHB-AP IDR is 0x%08" PRIx32 ", expected 0x%08" PRIx32 "\n", ahb_ap.idr, NRF91_AHB_AP_IDR_EXPECTED);
	}

	if (ctrl_ap.idr != NRF91_CTRL_IDR_EXPECTED) {
		DEBUG_ERROR(
			"nRF91: CTRL-AP IDR is 0x%08" PRIx32 ", expected 0x%08" PRIx32 "\n", ctrl_ap.idr, NRF91_CTRL_IDR_EXPECTED);
	}

	if (!(ahb_ap.csw & ADIV5_AP_CSW_DEVICEEN)) {
		DEBUG_INFO("nRF91 is in secure state, creating rescue target\n");
		adiv5_access_port_s *ap = calloc(1, sizeof(*ap));
		if (!ap) { /* calloc failed: heap exhaustion */
			DEBUG_ERROR("calloc: failed in %s\n", __func__);
			return false;
		}
		memcpy(ap, &ctrl_ap, sizeof(*ap));
		nrf91_rescue_probe(ap);
		return false;
	}
	return true;
}
