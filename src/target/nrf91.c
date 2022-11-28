#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"
#include "adiv5.h"

/* Non-Volatile Memory Controller (NVMC) Registers */
#define NRF91_NVMC          0x50039000U
#define NRF91_NVMC_READY    (NRF91_NVMC + 0x400U)
#define NRF91_NVMC_CONFIG   (NRF91_NVMC + 0x504U)
#define NRF91_NVMC_ERASEALL (NRF91_NVMC + 0x50cU)

#define NRF91_NVMC_CONFIG_REN  0x0U // Read only access
#define NRF91_NVMC_CONFIG_WEN  0x1U // Write enable
#define NRF91_NVMC_CONFIG_EEN  0x2U // Erase enable
#define NRF91_NVMC_CONFIG_PEEN 0x3U // Partial erase enable

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
}

bool nrf91_probe(target_s *target)
{
	adiv5_access_port_s *ap = cortex_ap(target);

	if (ap->dp->version < 2U)
		return false;

	switch (ap->dp->target_partno) {
	case 0x90:
		target->driver = "Nordic nRF9160";
		target->target_options |= CORTEXM_TOPT_INHIBIT_NRST;
		target_add_ram(target, 0x20000000, 256U * 1024U);
		nrf91_add_flash(target, 0, 4096U * 256U, 4096U);
		break;
	default:
		return false;
	}

	return true;
}
