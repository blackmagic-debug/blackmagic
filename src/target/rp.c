/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2021 Uwe Bonnes (bon@elektron.ikp.physik.tu-darmstadt.de)
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holders nor the names of
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* This file implements Raspberry Pico (RP2040) target specific functions
 * for detecting the device, providing the XML memory map and
 * Flash memory programming.
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"

#define RP_ID "Raspberry RP2040"
#define RP_MAX_TABLE_SIZE  0x80
#define BOOTROM_MAGIC ('M' | ('u' << 8) | (0x01 << 16))
#define BOOTROM_MAGIC_ADDR 0x00000010
#define XIP_FLASH_START    0x10000000
#define SRAM_START         0x20000000
#define SRAM_SIZE          0x42000
#define SSI_DR0_ADDR       0x18000060
#define QSPI_CTRL_ADDR     0x4001800c

#define FLASHSIZE_4K_SECTOR     (4 * 1024)
#define FLASHSIZE_32K_BLOCK     (32 * 1024)
#define FLASHSIZE_64K_BLOCK     (64 * 1024)
#define MAX_FLASH               (16 * 1024 * 1024)

/* Instruction codes taken from Winbond W25Q16JV datasheet, as used on the
 * original Pico board from Raspberry Pi.
 * https://www.winbond.com/resource-files/w25q16jv%20spi%20revd%2008122016.pdf
 * All dev boards supported by Pico SDK V1.3.1 use SPI flash chips which support
 * these commands. Other custom boards using different SPI flash chips might
 * not support these commands
 */

#define FLASHCMD_SECTOR_ERASE   0x20
#define FLASHCMD_BLOCK32K_ERASE 0x52
#define FLASHCMD_BLOCK64K_ERASE 0xd8
#define FLASHCMD_CHIP_ERASE     0x60
#define FLASHCMD_READ_JEDEC_ID  0x9F

struct rp_priv_s {
	uint16_t _debug_trampoline;
	uint16_t _debug_trampoline_end;
	uint16_t _connect_internal_flash;
	uint16_t _flash_exit_xip;
	uint16_t _flash_range_erase;
	uint16_t flash_range_program;
	uint16_t _flash_flush_cache;
	uint16_t _flash_enter_cmd_xip;
	uint16_t reset_usb_boot;
	bool     is_prepared;
	bool     is_monitor;
	uint32_t regs[0x20];/* Register playground*/
};

static bool rp2040_fill_table(struct rp_priv_s *priv, uint16_t *table, int max)
{
	uint16_t tag = *table++;
	int check = 0;
	while ((tag != 0) && max) {
		uint16_t data = *table++;
		check++;
		max -= 2;
		switch (tag) {
		case ('D' | ('T' << 8)):
			priv->_debug_trampoline = data;
			break;
		case ('D' | ('E' << 8)):
			priv->_debug_trampoline_end = data;
			break;
		case ('I' | ('F' << 8)):
			priv->_connect_internal_flash = data;
			break;
		case ('E' | ('X' << 8)):
			priv->_flash_exit_xip = data;
			break;
		case ('R' | ('E' << 8)):
			priv->_flash_range_erase = data;
			break;
		case ('R' | ('P' << 8)):
			priv->flash_range_program = data;
			break;
		case ('F' | ('C' << 8)):
			priv->_flash_flush_cache = data;
			break;
		case ('C' | ('X' << 8)):
			priv->_flash_enter_cmd_xip = data;
			break;
		case ('U' | ('B' << 8)):
			priv->reset_usb_boot = data;
			break;
		default:
			check--;
		}
		tag = *table++;
	}
	DEBUG_TARGET("connect %04x debug_trampoline %04x end %04x\n",
			   priv->_connect_internal_flash, priv->_debug_trampoline,
			   priv->_debug_trampoline_end);
	return (check != 9);
}

/* RP ROM functions calls
 *
 * timout == 0: Do not wait for poll, use for reset_usb_boot()
 * timeout > 500 (ms) : display spinner
 */
static bool rp_rom_call(target *t, uint32_t *regs, uint32_t cmd,
						uint32_t timeout)
{
	const char spinner[] = "|/-\\";
	int spinindex = 0;
	struct rp_priv_s *ps = (struct rp_priv_s*)t->target_storage;
	regs[7] = cmd;
	regs[REG_LR] = ps->_debug_trampoline_end;
	regs[REG_PC] = ps->_debug_trampoline;
	regs[REG_MSP] = 0x20042000;
	regs[REG_XPSR] = CORTEXM_XPSR_THUMB;
	uint32_t dbg_regs[t->regs_size / sizeof(uint32_t)];
	target_regs_write(t, regs);
	/* start the target and wait for it to halt again */
	target_halt_resume(t, false);
	if (!timeout)
		return false;
	DEBUG_INFO("Call cmd %04" PRIx32 "\n", cmd);
	platform_timeout to;
	platform_timeout_set(&to, timeout);
	platform_timeout to_spinner;
	if (timeout > 500)
		platform_timeout_set(&to_spinner, 500);
	else
		/* never trigger if timeout is short */
		platform_timeout_set(&to_spinner, timeout + 1);
	do {
		if (platform_timeout_is_expired(&to_spinner)) {
			if (ps->is_monitor)
				tc_printf(t, "\b%c", spinner[spinindex++ % 4]);
			platform_timeout_set(&to_spinner, 500);
		}
		if (platform_timeout_is_expired(&to)) {
			DEBUG_WARN("RP Run timout %d ms reached: ", (int)timeout);
			break;
		}
	} while (!target_halt_poll(t, NULL));
	/* Debug */
	target_regs_read(t, dbg_regs);
	bool ret = ((dbg_regs[REG_PC] &~1) != (ps->_debug_trampoline_end & ~1));
	if (ret) {
		DEBUG_WARN("rp_rom_call cmd %04" PRIx32 " failed, PC %08" PRIx32 "\n",
				   cmd, dbg_regs[REG_PC]);
	}
	return ret;
}

static void rp_flash_prepare(target *t)
{
	struct rp_priv_s *ps = (struct rp_priv_s*)t->target_storage;
	if (!ps->is_prepared) {
		DEBUG_INFO("rp_flash_prepare\n");
		/* connect*/
		rp_rom_call(t, ps->regs, ps->_connect_internal_flash,100);
		/* exit_xip */
		rp_rom_call(t, ps->regs, ps->_flash_exit_xip, 100);
		ps->is_prepared = true;
	}
}

static void rp_flash_resume(target *t)
{
	struct rp_priv_s *ps = (struct rp_priv_s*)t->target_storage;
	if (ps->is_prepared) {
		DEBUG_INFO("rp_flash_resume\n");
		/* flush */
		rp_rom_call(t, ps->regs, ps->_flash_flush_cache,100);
		/* enter_cmd_xip */
		rp_rom_call(t, ps->regs, ps->_flash_enter_cmd_xip, 100);
		ps->is_prepared = false;
	}
}

/* FLASHCMD_SECTOR_ERASE  45/  400 ms
 * 32k block erase       120/ 1600 ms
 * 64k block erase       150/ 2000 ms
 * chip erase           5000/25000 ms
 * page programm           0.4/  3 ms
 */
static int rp_flash_erase(struct target_flash *f, target_addr addr,
						  size_t len)
{
	DEBUG_INFO("Erase addr 0x%08" PRIx32 " len 0x%" PRIx32 "\n", addr, (uint32_t)len);
	target *t = f->t;
	if (addr & (FLASHSIZE_4K_SECTOR - 1)) {
		DEBUG_WARN("Unaligned erase\n");
		return -1;
	}
	if ((addr < t->flash->start) || (addr >= t->flash->start + t->flash->length)) {
		DEBUG_WARN("Address is invalid\n");
		return -1;
	}
	addr -= t->flash->start;
	len = ALIGN(len, FLASHSIZE_4K_SECTOR);
	len = MIN(len, t->flash->length - addr);
	struct rp_priv_s *ps = (struct rp_priv_s*)t->target_storage;
	/* erase */
	rp_flash_prepare(t);
	bool ret = 0;
	while (len) {
		if (len >= FLASHSIZE_64K_BLOCK) {
			uint32_t chunk = len & ~(FLASHSIZE_64K_BLOCK - 1);
			ps->regs[0] = addr;
			ps->regs[1] = chunk;
			ps->regs[2] = FLASHSIZE_64K_BLOCK;
			ps->regs[3] = FLASHCMD_BLOCK64K_ERASE;
			DEBUG_WARN("64k_ERASE addr 0x%08" PRIx32 " len 0x%" PRIx32 "\n", addr, chunk);
			ret = rp_rom_call(t, ps->regs, ps->_flash_range_erase, 25100);
			len -= chunk ;
			addr += chunk;
		} else if (len >= FLASHSIZE_32K_BLOCK) {
			uint32_t chunk = len & ~(FLASHSIZE_32K_BLOCK - 1);
			ps->regs[0] = addr;
			ps->regs[1] = chunk;
			ps->regs[2] = FLASHSIZE_32K_BLOCK;
			ps->regs[3] = FLASHCMD_BLOCK32K_ERASE;
			DEBUG_WARN("32k_ERASE addr 0x%08" PRIx32 " len 0x%" PRIx32 "\n", addr, chunk);
			ret = rp_rom_call(t, ps->regs, ps->_flash_range_erase, 1700);
			len -= chunk;
			addr += chunk;
		} else {
			ps->regs[0] = addr;
			ps->regs[1] = len;
			ps->regs[2] = FLASHSIZE_4K_SECTOR;
			ps->regs[3] = FLASHCMD_SECTOR_ERASE;
			DEBUG_WARN("Sector_ERASE addr 0x%08" PRIx32 " len 0x%" PRIx32 "\n", addr, (uint32_t)len);
			ret = rp_rom_call(t, ps->regs, ps->_flash_range_erase, 410);
			len = 0;
		}
		if (ret) {
			DEBUG_WARN("Erase failed!\n");
			break;
		}
	}
	rp_flash_resume(t);
	DEBUG_INFO("Erase done!\n");
	return ret;
}

static int rp_flash_write(struct target_flash *f,
                    target_addr dest, const void *src, size_t len)
{
	DEBUG_INFO("RP Write 0x%08" PRIx32 " len 0x%" PRIx32 "\n", dest, (uint32_t)len);
	target *t = f->t;
	if ((dest & 0xff) || (len & 0xff)) {
		DEBUG_WARN("Unaligned write\n");
		return -1;
	}
	dest -= t->flash->start;
	struct rp_priv_s *ps = (struct rp_priv_s*)t->target_storage;
	/* Write payload to target ram */
	rp_flash_prepare(t);
	bool ret = 0;
#define MAX_WRITE_CHUNK 0x1000
	while (len) {
		uint32_t chunksize = (len <= MAX_WRITE_CHUNK) ? len : MAX_WRITE_CHUNK;
		target_mem_write(t, SRAM_START, src, chunksize);
		/* Programm range */
		ps->regs[0] = dest;
		ps->regs[1] = SRAM_START;
		ps->regs[2] = chunksize;
		/* Loading takes 3 ms per 256 byte page
		 * however it takes much longer if the XOSC is not enabled
		 * so lets give ourselves a little bit more time (x10)
		 */
		ret |= rp_rom_call(t, ps->regs, ps->flash_range_program,
					(3 *  chunksize * 10) >> 8);
		if (ret) {
			DEBUG_WARN("Write failed!\n");
			break;
		}
		len -= chunksize;
		src += chunksize;
		dest += chunksize;
	}
	rp_flash_resume(t);
	DEBUG_INFO("Write done!\n");
	return ret;
}

static bool rp_cmd_reset_usb_boot(target *t, int argc, const char *argv[])
{
	struct rp_priv_s *ps = (struct rp_priv_s*)t->target_storage;
	if (argc > 2) {
		ps->regs[1] = atoi(argv[2]);
	} else if (argc < 3) {
		ps->regs[0] = atoi(argv[1]);
	} else {
		ps->regs[0] = 0;
		ps->regs[1] = 0;
	}
	rp_rom_call(t, ps->regs, ps->reset_usb_boot, 0);
	return true;
}

static bool rp_cmd_erase_mass(target *t, int argc, const char *argv[])
{
	(void) argc;
	(void) argv;
	struct target_flash f;
	f.t = t;
	struct rp_priv_s *ps = (struct rp_priv_s*)t->target_storage;
	ps->is_monitor = true;
	bool res =  (rp_flash_erase(&f, t->flash->start, t->flash->length)) ? false: true;
	ps->is_monitor = false;
	return res;
}

static bool rp_cmd_erase_sector(target *t, int argc, const char *argv[])
{
	uint32_t length = t->flash->length;
	uint32_t start = t->flash->start;

	if (argc == 3) {
		start = strtoul(argv[1], NULL, 0);
		length = strtoul(argv[2], NULL, 0);
	}
	else if (argc == 2)
		length = strtoul(argv[1], NULL, 0);
	else
		return -1;

	struct target_flash f;
	f.t = t;
	struct rp_priv_s *ps = (struct rp_priv_s*)t->target_storage;
	ps->is_monitor = true;
	bool res =  (rp_flash_erase(&f, start, length)) ? false: true;
	ps->is_monitor = false;
	return res;
}

const struct command_s rp_cmd_list[] = {
	{"erase_mass", rp_cmd_erase_mass, "Erase entire flash memory"},
	{"erase_sector", rp_cmd_erase_sector, "Erase a sector: [start address] length" },
	{"reset_usb_boot", rp_cmd_reset_usb_boot, "Reboot the device into BOOTSEL mode"},
	{NULL, NULL, NULL}
};

static void rp_add_flash(target *t, uint32_t addr, size_t length)
{
        struct target_flash *f = calloc(1, sizeof(*f));
        if (!f) {                       /* calloc failed: heap exhaustion */
                DEBUG_WARN("calloc: failed in %s\n", __func__);
                return;
        }

        f->start = addr;
        f->length = length;
        f->blocksize = 0x1000;
        f->erase = rp_flash_erase;
        f->write = rp_flash_write;
        f->buf_size = 2048; /* Max buffer size used otherwise */
		f->erased = 0xFF;
        target_add_flash(t, f);
}

void rp_ssel_active(target *t, bool active)
{
	const uint32_t qspi_ctrl_outover_low  = 2UL << 8;
	const uint32_t qspi_ctrl_outover_high = 3UL << 8;
	uint32_t state = (active) ? qspi_ctrl_outover_low : qspi_ctrl_outover_high;
	uint32_t val = target_mem_read32(t, QSPI_CTRL_ADDR);
	val = (val & ~qspi_ctrl_outover_high) | state;
	target_mem_write32(t, QSPI_CTRL_ADDR, val);
}

uint32_t rp_read_flash_chip(target *t, uint32_t cmd)
{
	uint32_t value = 0;

	rp_ssel_active(t, true);

	/* write command into SPI peripheral's FIFO */
	for (size_t i = 0; i < 4; i++)
		target_mem_write32(t, SSI_DR0_ADDR, cmd);

	/* now we have an entry in the receive FIFO for each write */
	for (size_t i = 0; i < 4; i++) {
		uint32_t status = target_mem_read32(t, SSI_DR0_ADDR);
		value |= (status & 0xFF) << 24;
		value >>= 8;
	}

	rp_ssel_active(t, false);

	return value;
}

uint32_t rp_get_flash_length(target *t)
{
	uint32_t size = MAX_FLASH;
	uint32_t bootsec[16];
	size_t i;

	target_mem_read(t, bootsec, XIP_FLASH_START, sizeof(bootsec));
	for (i = 0; i < 16; i++) {
		if ((bootsec[i] != 0x00) && (bootsec[i] != 0xff))
			break;
	}

	if (i < 16) {
		// We have some data (hopefully a valid program) stored in the start
		// of the flash memory. We can check if the start of this data is
		// mirrored anywhere else in the flash as the flash region will repeat
		// when we try to read out of bounds.
		uint32_t mirrorsec[16];
		while (size > FLASHSIZE_4K_SECTOR) {
			target_mem_read(t, mirrorsec, XIP_FLASH_START + size, sizeof(bootsec));
			if (memcmp(bootsec, mirrorsec, sizeof(bootsec)))
				return size << 1;
			size >>= 1;
		}
	}

	// That approach didn't work. Most likely because there was no data found in
	// at the start of the flash memory. If we have no valid program it's ok to
	// interrupt the flash execution to check the JEDEC ID of the flash chip.
	size = MAX_FLASH;

	rp_flash_prepare(t);
	uint32_t flash_id = rp_read_flash_chip(t, FLASHCMD_READ_JEDEC_ID);
	rp_flash_resume(t);

	DEBUG_INFO("Flash device ID: %08" PRIx32 "\n", flash_id);

	uint8_t size_log2 = (flash_id & 0xff0000) >> 16;
	if (size_log2 >= 8 && size_log2 <= 34)
		size = 1 << size_log2;

	return size;
}

static bool rp_attach(target *t)
{
	if (!cortexm_attach(t))
		return false;

	struct rp_priv_s *ps = (struct rp_priv_s*)t->target_storage;
	uint16_t *table =  alloca(RP_MAX_TABLE_SIZE);
	uint16_t table_offset = target_mem_read32( t, BOOTROM_MAGIC_ADDR + 4);
	if (!table || target_mem_read(t, table, table_offset, RP_MAX_TABLE_SIZE))
		return false;
	if (rp2040_fill_table(ps, table, RP_MAX_TABLE_SIZE))
		return false;

	/* Free previously loaded memory map */
	target_mem_map_free(t);

	size_t size = rp_get_flash_length(t);
	DEBUG_INFO("Flash size: %zu MB\n", size / (1024U * 1024U));

	rp_add_flash(t, XIP_FLASH_START, size);
	target_add_ram(t, SRAM_START, SRAM_SIZE);

	return true;
}

static void rp_detach(target *t)
{
	cortexm_detach(t);
}

bool rp_probe(target *t)
{
	/* Check bootrom magic*/
	uint32_t boot_magic = target_mem_read32(t, BOOTROM_MAGIC_ADDR);
	if ((boot_magic & 0x00ffffff) != BOOTROM_MAGIC) {
		DEBUG_WARN("Wrong Bootmagic %08" PRIx32 " found!\n", boot_magic);
		return false;
	}
#if defined(ENABLE_DEBUG)
	if ((boot_magic >> 24) == 1)
		DEBUG_WARN("Old Bootrom Version 1!\n");
#endif
	struct rp_priv_s *priv_storage = calloc(1, sizeof(struct rp_priv_s));
	if (!priv_storage) {               /* calloc failed: heap exhaustion */
		DEBUG_WARN("calloc: failed in %s\n", __func__);
		return false;
	}
 	t->target_storage = (void*)priv_storage;

	t->driver = RP_ID;
	t->target_options |= CORTEXM_TOPT_INHIBIT_NRST;
	t->attach = rp_attach;
	t->detach = rp_detach;
	target_add_commands(t, rp_cmd_list, RP_ID);
	return true;
}

static bool rp_rescue_do_reset(target *t)
{
	ADIv5_AP_t *ap = (ADIv5_AP_t *)t->priv;
	ap->dp->low_access(ap->dp, ADIV5_LOW_WRITE, ADIV5_DP_CTRLSTAT,
						  ADIV5_DP_CTRLSTAT_CDBGPWRUPREQ);
	ap->dp->low_access(ap->dp, ADIV5_LOW_WRITE, ADIV5_DP_CTRLSTAT, 0);
	return false;
}

/* The RP Pico rescue DP provides no AP, so we need special handling
 *
 * Attach to this DP will do the reset, but will fail to attach!
 */
void rp_rescue_probe(ADIv5_AP_t *ap)
{
	target *t = target_new();
	if (!t) {
		return;
	}

	adiv5_ap_ref(ap);
	t->attach = (void*)rp_rescue_do_reset;
	t->priv = ap;
	t->priv_free = (void*)adiv5_ap_unref;
	t->driver = "Raspberry RP2040 Rescue(Attach to reset!)";
}
