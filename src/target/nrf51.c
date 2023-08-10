/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2014  Mike Walters <mike@flomp.net>
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

/* This file implements nRF51 target specific functions for detecting
 * the device, providing the XML memory map and Flash memory programming.
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"
#include "adiv5.h"

static bool nrf51_flash_erase(target_flash_s *f, target_addr_t addr, size_t len);
static bool nrf51_flash_write(target_flash_s *f, target_addr_t dest, const void *src, size_t len);
static bool nrf51_flash_prepare(target_flash_s *f);
static bool nrf51_flash_done(target_flash_s *f);
static bool nrf51_mass_erase(target_s *t);

static bool nrf51_cmd_erase_uicr(target_s *t, int argc, const char **argv);
static bool nrf51_cmd_protect_flash(target_s *t, int argc, const char **argv);
static bool nrf51_cmd_read_hwid(target_s *t, int argc, const char **argv);
static bool nrf51_cmd_read_fwid(target_s *t, int argc, const char **argv);
static bool nrf51_cmd_read_deviceid(target_s *t, int argc, const char **argv);
static bool nrf51_cmd_read_deviceaddr(target_s *t, int argc, const char **argv);
static bool nrf51_cmd_read_deviceinfo(target_s *t, int argc, const char **argv);
static bool nrf51_cmd_read_help(target_s *t, int argc, const char **argv);
static bool nrf51_cmd_read(target_s *t, int argc, const char **argv);

const command_s nrf51_cmd_list[] = {
	{"erase_uicr", nrf51_cmd_erase_uicr, "Erase UICR registers"},
	{"protect_flash", nrf51_cmd_protect_flash, "Enable flash read/write protection"},
	{"read", nrf51_cmd_read, "Read device parameters"},
	{NULL, NULL, NULL},
};

const command_s nrf51_read_cmd_list[] = {
	{"help", nrf51_cmd_read_help, "Display help for read commands"},
	{"hwid", nrf51_cmd_read_hwid, "Read hardware identification number"},
	{"fwid", nrf51_cmd_read_fwid, "Read pre-loaded firmware ID"},
	{"deviceid", nrf51_cmd_read_deviceid, "Read unique device ID"},
	{"deviceaddr", nrf51_cmd_read_deviceaddr, "Read device address"},
	{"deviceinfo", nrf51_cmd_read_deviceinfo, "Read device information"},
	{NULL, NULL, NULL},
};

/* Non-Volatile Memory Controller (NVMC) Registers */
#define NRF51_NVMC           0x4001e000U
#define NRF51_NVMC_READY     (NRF51_NVMC + 0x400U)
#define NRF51_NVMC_CONFIG    (NRF51_NVMC + 0x504U)
#define NRF51_NVMC_ERASEPAGE (NRF51_NVMC + 0x508U)
#define NRF51_NVMC_ERASEALL  (NRF51_NVMC + 0x50cU)
#define NRF51_NVMC_ERASEUICR (NRF51_NVMC + 0x514U)

#define NRF51_NVMC_CONFIG_REN 0x0U // Read only access
#define NRF51_NVMC_CONFIG_WEN 0x1U // Write enable
#define NRF51_NVMC_CONFIG_EEN 0x2U // Erase enable

/* Factory Information Configuration Registers (FICR) */
#define NRF51_FICR                 0x10000000U
#define NRF51_FICR_CODEPAGESIZE    (NRF51_FICR + 0x010U)
#define NRF51_FICR_CODESIZE        (NRF51_FICR + 0x014U)
#define NRF51_FICR_CONFIGID        (NRF51_FICR + 0x05cU)
#define NRF51_FICR_DEVICEID_LOW    (NRF51_FICR + 0x060U)
#define NRF51_FICR_DEVICEID_HIGH   (NRF51_FICR + 0x064U)
#define NRF51_FICR_DEVICEADDRTYPE  (NRF51_FICR + 0x0a0U)
#define NRF51_FICR_DEVICEADDR_LOW  (NRF51_FICR + 0x0a4U)
#define NRF51_FICR_DEVICEADDR_HIGH (NRF51_FICR + 0x0a8U)
#define NRF52_PART_INFO            (NRF51_FICR + 0x100U)
#define NRF52_INFO_RAM             (NRF51_FICR + 0x10cU)
/* Device Info Registers */
#define NRF51_FICR_DEVICE_INFO_BASE    (NRF51_FICR + 0x100U)
#define NRF51_FICR_DEVICE_INFO_PART    NRF51_FICR_DEVICE_INFO_BASE
#define NRF51_FICR_DEVICE_INFO_VARIANT (NRF51_FICR_DEVICE_INFO_BASE + 4U)
#define NRF51_FICR_DEVICE_INFO_PACKAGE (NRF51_FICR_DEVICE_INFO_BASE + 8U)
#define NRF51_FICR_DEVICE_INFO_RAM     (NRF51_FICR_DEVICE_INFO_BASE + 12U)
#define NRF51_FICR_DEVICE_INFO_FLASH   (NRF51_FICR_DEVICE_INFO_BASE + 16U)

#define NRF51_FIELD_UNSPECIFIED (0xffffffffU)

/* User Information Configuration Registers (UICR) */
#define NRF51_UICR 0x10001000U

/* Flash R/W Protection Register */
#define NRF51_APPROTECT 0x10001208U

#define NRF51_PAGE_SIZE 1024U
#define NRF52_PAGE_SIZE 4096U

static void nrf51_add_flash(target_s *t, uint32_t addr, size_t length, size_t erasesize)
{
	target_flash_s *f = calloc(1, sizeof(*f));
	if (!f) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return;
	}

	f->start = addr;
	f->length = length;
	f->blocksize = erasesize;
	/* Limit the write buffer size to 1k to help prevent probe memory exhaustion */
	f->writesize = MIN(erasesize, 1024U);
	f->erase = nrf51_flash_erase;
	f->write = nrf51_flash_write;
	f->prepare = nrf51_flash_prepare;
	f->done = nrf51_flash_done;
	f->erased = 0xff;
	target_add_flash(t, f);
}

bool nrf51_probe(target_s *t)
{
	uint32_t page_size = target_mem_read32(t, NRF51_FICR_CODEPAGESIZE);
	uint32_t code_size = target_mem_read32(t, NRF51_FICR_CODESIZE);
	/* Check that page_size and code_size makes sense */
	if (page_size == 0xffffffffU || code_size == 0xffffffffU || page_size == 0 || code_size == 0 ||
		page_size > 0x10000U || code_size > 0x10000U)
		return false;
	DEBUG_INFO("nRF51/52: code page size: %" PRIu32 ", code size: %" PRIu32 "\n", page_size, code_size);
	/* Check that device identifier makes sense */
	uint32_t uid0 = target_mem_read32(t, NRF51_FICR_DEVICEID_LOW);
	uint32_t uid1 = target_mem_read32(t, NRF51_FICR_DEVICEID_HIGH);
	if (uid0 == 0xffffffffU || uid1 == 0xffffffffU || uid0 == 0 || uid1 == 0)
		return false;
	/* Test for NRF52 device */
	uint32_t info_part = target_mem_read32(t, NRF52_PART_INFO);
	if (info_part != 0xffffffffU && info_part != 0 && (info_part & 0x00ff000U) == 0x52000U) {
		uint32_t ram_size = target_mem_read32(t, NRF52_INFO_RAM);
		t->driver = "Nordic nRF52";
		t->target_options |= CORTEXM_TOPT_INHIBIT_NRST;
		target_add_ram(t, 0x20000000U, ram_size * 1024U);
		nrf51_add_flash(t, 0, page_size * code_size, page_size);
		nrf51_add_flash(t, NRF51_UICR, page_size, page_size);
		target_add_commands(t, nrf51_cmd_list, "nRF52");
	} else {
		t->driver = "Nordic nRF51";
		/*
		 * Use the biggest RAM size seen in NRF51 fammily.
		 * IDCODE is kept as '0', as deciphering is hard and there is later no usage.
		 */
		target_add_ram(t, 0x20000000U, 0x8000U);
		t->target_options |= CORTEXM_TOPT_INHIBIT_NRST;
		nrf51_add_flash(t, 0, page_size * code_size, page_size);
		nrf51_add_flash(t, NRF51_UICR, page_size, page_size);
		target_add_commands(t, nrf51_cmd_list, "nRF51");
	}
	t->mass_erase = nrf51_mass_erase;
	return true;
}

static bool nrf51_wait_ready(target_s *const t, platform_timeout_s *const timeout)
{
	/* Poll for NVMC_READY */
	while (target_mem_read32(t, NRF51_NVMC_READY) == 0) {
		if (target_check_error(t))
			return false;
		if (timeout)
			target_print_progress(timeout);
	}
	return true;
}

static bool nrf51_flash_prepare(target_flash_s *f)
{
	target_s *t = f->t;
	/* If there is a buffer allocated, we're in the Flash write phase, otherwise it's erase */
	if (f->buf)
		/* Enable write */
		target_mem_write32(t, NRF51_NVMC_CONFIG, NRF51_NVMC_CONFIG_WEN);
	else
		/* Enable erase */
		target_mem_write32(t, NRF51_NVMC_CONFIG, NRF51_NVMC_CONFIG_EEN);
	return nrf51_wait_ready(t, NULL);
}

static bool nrf51_flash_done(target_flash_s *f)
{
	target_s *t = f->t;
	/* Return to read-only */
	target_mem_write32(t, NRF51_NVMC_CONFIG, NRF51_NVMC_CONFIG_REN);
	return nrf51_wait_ready(t, NULL);
}

static bool nrf51_flash_erase(target_flash_s *f, target_addr_t addr, size_t len)
{
	target_s *t = f->t;

	for (size_t offset = 0; offset < len; offset += f->blocksize) {
		/* If the address to erase is the UICR, we have to handle that separately */
		if (addr + offset == NRF51_UICR)
			/* Write to the ERASE_UICR register to erase */
			target_mem_write32(t, NRF51_NVMC_ERASEUICR, 0x1U);
		else
			/* Write address of first word in page to erase it */
			target_mem_write32(t, NRF51_NVMC_ERASEPAGE, addr + offset);

		if (!nrf51_wait_ready(t, NULL))
			return false;
	}

	return true;
}

static bool nrf51_flash_write(target_flash_s *f, target_addr_t dest, const void *src, size_t len)
{
	/* nrf51_flash_prepare() and nrf51_flash_done() top-and-tail this, just write the data to the target. */
	target_s *t = f->t;
	target_mem_write(t, dest, src, len);
	return nrf51_wait_ready(t, NULL);
}

static bool nrf51_mass_erase(target_s *t)
{
	target_reset(t);

	/* Enable erase */
	target_mem_write32(t, NRF51_NVMC_CONFIG, NRF51_NVMC_CONFIG_EEN);
	if (!nrf51_wait_ready(t, NULL))
		return false;

	platform_timeout_s timeout;
	platform_timeout_set(&timeout, 500U);
	/* Erase all */
	target_mem_write32(t, NRF51_NVMC_ERASEALL, 1U);
	return nrf51_wait_ready(t, &timeout);
}

static bool nrf51_cmd_erase_uicr(target_s *t, int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	tc_printf(t, "Erasing..\n");

	/* Enable erase */
	target_mem_write32(t, NRF51_NVMC_CONFIG, NRF51_NVMC_CONFIG_EEN);
	if (!nrf51_wait_ready(t, NULL))
		return false;

	/* Erase UICR */
	target_mem_write32(t, NRF51_NVMC_ERASEUICR, 1U);
	return nrf51_wait_ready(t, NULL);
}

static bool nrf51_cmd_protect_flash(target_s *t, int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	tc_printf(t, "Enabling Flash protection..\n");

	/* Enable write */
	target_mem_write32(t, NRF51_NVMC_CONFIG, NRF51_NVMC_CONFIG_WEN);
	if (!nrf51_wait_ready(t, NULL))
		return false;

	target_mem_write32(t, NRF51_APPROTECT, 0xffffff00U);
	return nrf51_wait_ready(t, NULL);
}

static bool nrf51_cmd_read_hwid(target_s *t, int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	uint32_t hwid = target_mem_read32(t, NRF51_FICR_CONFIGID) & 0xffffU;
	tc_printf(t, "Hardware ID: 0x%04X\n", hwid);

	return true;
}

static bool nrf51_cmd_read_fwid(target_s *t, int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	uint32_t fwid = (target_mem_read32(t, NRF51_FICR_CONFIGID) >> 16U) & 0xffffU;
	tc_printf(t, "Firmware ID: 0x%04X\n", fwid);

	return true;
}

static bool nrf51_cmd_read_deviceid(target_s *t, int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	uint32_t deviceid_low = target_mem_read32(t, NRF51_FICR_DEVICEID_LOW);
	uint32_t deviceid_high = target_mem_read32(t, NRF51_FICR_DEVICEID_HIGH);

	tc_printf(t, "Device ID: 0x%08X%08X\n", deviceid_high, deviceid_low);

	return true;
}

static bool nrf51_cmd_read_deviceinfo(target_s *t, int argc, const char **argv)
{
	(void)argc;
	(void)argv;

	struct deviceinfo {
		uint32_t part;

		union {
			char c[4];
			uint32_t f;
		} variant;

		uint32_t package;
		uint32_t ram;
		uint32_t flash;
	} di;

	di.package = target_mem_read32(t, NRF51_FICR_DEVICE_INFO_PACKAGE);
	di.part = target_mem_read32(t, NRF51_FICR_DEVICE_INFO_PART);
	di.ram = target_mem_read32(t, NRF51_FICR_DEVICE_INFO_RAM);
	di.flash = target_mem_read32(t, NRF51_FICR_DEVICE_INFO_FLASH);
	di.variant.f = target_mem_read32(t, NRF51_FICR_DEVICE_INFO_VARIANT);

	tc_printf(t, "Part:\t\tNRF%X\n", di.part);
	tc_printf(t, "Variant:\t%c%c%c%c\n", di.variant.c[3], di.variant.c[2], di.variant.c[1], di.variant.c[0]);
	tc_printf(t, "Package:\t");
	switch (di.package) {
	case NRF51_FIELD_UNSPECIFIED:
		tc_printf(t, "Unspecified\n");
		break;
	case 0x2000U:
		tc_printf(t, "QF\n");
		break;
	case 0x2001U:
		tc_printf(t, "CI\n");
		break;
	case 0x2004U:
		tc_printf(t, "QIxx\n");
		break;
	default:
		tc_printf(t, "Unknown (Code %X)\n", di.package);
		break;
	}

	tc_printf(t, "Ram:\t\t%ukiB\n", di.ram);
	tc_printf(t, "Flash:\t\t%ukiB\n", di.flash);
	return true;
}

static bool nrf51_cmd_read_deviceaddr(target_s *t, int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	uint32_t addr_type = target_mem_read32(t, NRF51_FICR_DEVICEADDRTYPE);
	uint32_t addr_low = target_mem_read32(t, NRF51_FICR_DEVICEADDR_LOW);
	uint32_t addr_high = target_mem_read32(t, NRF51_FICR_DEVICEADDR_HIGH) & 0xffffU;

	if (!(addr_type & 1U))
		tc_printf(t, "Publicly Listed Address: 0x%04X%08X\n", addr_high, addr_low);
	else
		tc_printf(t, "Randomly Assigned Address: 0x%04X%08X\n", addr_high, addr_low);

	return true;
}

static bool nrf51_cmd_read_help(target_s *t, int argc, const char **argv)
{
	(void)argc;
	(void)argv;

	tc_printf(t, "Read commands:\n");
	for (const command_s *c = nrf51_read_cmd_list; c->cmd; c++)
		tc_printf(t, "\t%s -- %s\n", c->cmd, c->help);

	return true;
}

static bool nrf51_cmd_read(target_s *t, int argc, const char **argv)
{
	if (argc > 1) {
		for (const command_s *c = nrf51_read_cmd_list; c->cmd; c++) {
			/*
			 * Accept a partial match as GDB does.
			 * So 'mon ver' will match 'monitor version'
			 */
			if (!strncmp(argv[1], c->cmd, strlen(argv[1])))
				return c->handler(t, argc - 1, &argv[1]);
		}
	}
	return nrf51_cmd_read_help(t, 0, NULL);
}

#define NRF52_MDM_IDR 0x02880000U

static bool nrf51_mdm_mass_erase(target_s *t);

#define MDM_POWER_EN  ADIV5_DP_REG(0x01U)
#define MDM_SELECT_AP ADIV5_DP_REG(0x02U)
#define MDM_STATUS    ADIV5_AP_REG(0x08U)
#define MDM_CONTROL   ADIV5_AP_REG(0x04U)
#define MDM_PROT_EN   ADIV5_AP_REG(0x0cU)

bool nrf51_mdm_probe(adiv5_access_port_s *ap)
{
	switch (ap->idr) {
	case NRF52_MDM_IDR:
		break;
	default:
		return false;
	}

	target_s *t = target_new();
	if (!t)
		return false;

	t->mass_erase = nrf51_mdm_mass_erase;
	adiv5_ap_ref(ap);
	t->priv = ap;
	t->priv_free = (void *)adiv5_ap_unref;

	uint32_t status = adiv5_ap_read(ap, MDM_PROT_EN);
	status = adiv5_ap_read(ap, MDM_PROT_EN);
	if (status)
		t->driver = "Nordic nRF52 Access Port";
	else
		t->driver = "Nordic nRF52 Access Port (protected)";
	t->regs_size = 0;

	return true;
}

static bool nrf51_mdm_mass_erase(target_s *t)
{
	adiv5_access_port_s *ap = t->priv;

	uint32_t status = adiv5_ap_read(ap, MDM_STATUS);
	adiv5_dp_write(ap->dp, MDM_POWER_EN, 0x50000000U);
	adiv5_dp_write(ap->dp, MDM_SELECT_AP, 0x01000000U);
	adiv5_ap_write(ap, MDM_CONTROL, 0x00000001U);

	platform_timeout_s timeout;
	platform_timeout_set(&timeout, 500U);
	// Read until 0, probably should have a timeout here...
	do {
		status = adiv5_ap_read(ap, MDM_STATUS);
		target_print_progress(&timeout);
	} while (status);

	// The second read will provide true prot status
	status = adiv5_ap_read(ap, MDM_PROT_EN);
	status = adiv5_ap_read(ap, MDM_PROT_EN);

	// Should we return the prot status here?
	return true;
}
