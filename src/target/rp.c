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
 * for detecting the device, providing the XML memory map but not yet
 * Flash memory programming.
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"

#define RP_ID "Raspberry RP2040"
#define BOOTROM_MAGIC ('M' | ('u' << 8) | (0x01 << 16))
#define BOOTROM_MAGIC_ADDR 0x00000010
#define XIP_FLASH_START    0x10000000
#define SRAM_START         0x20000000

struct rp_flash {
	struct target_flash f;
	uint16_t _debug_trampoline;
	uint16_t _debug_trampoline_end;
	uint16_t _connect_internal_flash;
	uint16_t _flash_exit_xip;
	uint16_t _flash_range_erase;
	uint16_t flash_range_program;
	uint16_t _flash_flush_cache;
	uint16_t _flash_enter_cmd_xip;
	uint8_t block_cmd;
};
static bool rp2040_fill_table(struct rp_flash *lf, uint16_t *table, int max)
{
	uint16_t tag = *table++;
	int check = 0;
	lf->block_cmd = 0xd8; /* Evt. et other command by monitor commands*/
	while ((tag != 0) && max) {
		uint16_t data = *table++;
		check++;
		max -= 2;
		switch (tag) {
		case ('D' | ('T' << 8)):
			lf->_debug_trampoline = data;
			break;
		case ('D' | ('E' << 8)):
			lf->_debug_trampoline_end = data;
			break;
		case ('I' | ('F' << 8)):
			lf->_connect_internal_flash = data;
			break;
		case ('E' | ('X' << 8)):
			lf->_flash_exit_xip = data;
			break;
		case ('R' | ('E' << 8)):
			lf->_flash_range_erase = data;
			break;
		case ('R' | ('P' << 8)):
			lf->flash_range_program = data;
			break;
		case ('F' | ('C' << 8)):
			lf->_flash_flush_cache = data;
			break;
		case ('C' | ('X' << 8)):
			lf->_flash_enter_cmd_xip = data;
			break;
		default:
			check--;
		}
		tag = *table++;
	}
	DEBUG_WARN("connect %04x debug_trampoline %04x end %04x\n",
			   lf->_connect_internal_flash, lf->_debug_trampoline, lf->_debug_trampoline_end);
	return (check != 8);
}

/* RP ROM functions for flash handling return void */
static bool rp_rom_call(struct rp_flash *f, uint32_t *regs, uint32_t cmd,
						uint32_t timeout)
{
	target *t = f->f.t;
	regs[7] = cmd;
	regs[REG_LR] = f->_debug_trampoline_end;
	regs[REG_PC] = f->_debug_trampoline;
	regs[REG_MSP] = 0x20042000;
	uint32_t dbg_regs[t->regs_size / sizeof(uint32_t)];
	target_regs_write(t, regs);
	/* start the target and wait for it to halt again */
	target_halt_resume(t, false);
	DEBUG_INFO("Call cmd %04x\n", cmd);
	platform_timeout to;
	platform_timeout_set(&to, timeout);
	do {
		if (platform_timeout_is_expired(&to)) {
			DEBUG_WARN("RP Run timout %d ms reached: ", timeout);
			break;
		}
	} while (!target_halt_poll(t, NULL));
	/* Debug */
	target_regs_read(t, dbg_regs);
	bool ret = ((dbg_regs[REG_PC] &~1) != (f->_debug_trampoline_end & ~1));
	if (ret) {
		DEBUG_WARN("rp_rom_call cmd %04x failed, PC %08" PRIx32 "\n",
				   cmd, dbg_regs[REG_PC]);
	}
	return ret;
}

/* FLASHCMD_SECTOR_ERASE  45/  400 ms
 * 32k block erase       120/ 1600 ms
 * 64k block erase       150/ 2000 ms
 * chip erase           5000/25000 ms
 * page programm           0.4/  3 ms
 */
static int rp_flash_erase(struct target_flash *tf, target_addr addr,
						  size_t len)
{
	if (addr & 0xfff) {
		DEBUG_WARN("Unaligned erase\n");
		return -1;
	}
	if (len & 0xfff) {
		DEBUG_WARN("Unaligned len\n");
		len = (len + 0xfff) & ~0xfff;
	}
	DEBUG_INFO("Erase addr %08" PRIx32 " len 0x%" PRIx32 "\n", addr, len);
	struct rp_flash *f = (struct rp_flash *)tf;
	target *t = f->f.t;
	/* Register playground*/
	uint32_t regs[t->regs_size / sizeof(uint32_t)];
	/* connect*/
	if (rp_rom_call(f, regs, f->_connect_internal_flash, 100))
		return -1;
	/* exit_xip */
	rp_rom_call(f, regs, f->_flash_exit_xip, 100);
	/* erase */

#define MAX_FLASH               (2 * 1024 * 1024)
#define FLASHCMD_SECTOR_ERASE   0x20
#define FLASHCMD_BLOCK32K_ERASE 0x52
#define FLASHCMD_BLOCK64K_ERASE 0xd8
#define FLASHCMD_CHIP_ERASE     0x72
	addr -= XIP_FLASH_START;
	if (len > MAX_FLASH)
		len = MAX_FLASH;
	while (len) {
		regs[0] = addr;
		if (len >= MAX_FLASH) { /* Bulk erase */
			regs[1] = MAX_FLASH;
			regs[2] = MAX_FLASH;
			regs[3] = FLASHCMD_CHIP_ERASE;
			rp_rom_call(f, regs, f->_flash_range_erase, 25100);
			len = 0;
		} else if (len >= (64 * 1024)) {
			uint32_t chunk = len & ~((1 << 16) - 1);
			regs[1] = chunk;
			regs[2] = 64 * 1024;
			regs[3] = FLASHCMD_BLOCK64K_ERASE;
			rp_rom_call(f, regs, f->_flash_range_erase, 2100);
			len -= chunk ;
			addr += chunk;
		} else if (len >= (32 * 1024)) {
			uint32_t chunk = len & ~((1 << 15) - 1);
			regs[1] = chunk;
			regs[2] = 32 * 1024;
			regs[3] = FLASHCMD_BLOCK32K_ERASE;
			rp_rom_call(f, regs, f->_flash_range_erase, 1700);
			len -= chunk;
			addr += chunk;
		} else {
			regs[1] = len;
			regs[2] = MAX_FLASH;
			regs[3] = FLASHCMD_SECTOR_ERASE;
			rp_rom_call(f, regs, f->_flash_range_erase, 410);
			len = 0;
		}
	}
	/* flush*/
	rp_rom_call(f, regs, f->_flash_flush_cache, 100);
	/* enter XIP */
	rp_rom_call(f, regs, f->_flash_enter_cmd_xip, 100);
	/* restore registers */
	DEBUG_INFO("Erase done!\n");
	return 0;
}

int rp_flash_write(struct target_flash *tf,
                    target_addr dest, const void *src, size_t len)
{
	DEBUG_INFO("RP Write %08" PRIx32 " len 0x%" PRIx32 "\n", dest, len);
	if ((dest & 0xff) || (len & 0xff)) {
		DEBUG_WARN("Unaligned erase\n");
		return -1;
	}
	struct rp_flash *f = (struct rp_flash *)tf;
	target *t = f->f.t;
	/* save registers to restore after ROM call */
	uint32_t backup_regs[t->regs_size / sizeof(uint32_t)];
	target_regs_read(t, backup_regs);
	/* Register playground*/
	uint32_t regs[t->regs_size / sizeof(uint32_t)];
	/* connect*/
	rp_rom_call(f, regs, f->_connect_internal_flash,100);
	/* exit_xip */
	rp_rom_call(f, regs, f->_flash_exit_xip, 100);
	/* Write payload to target ram */
	dest -= XIP_FLASH_START;
#define MAX_WRITE_CHUNK 0x1000
	while (len) {
		uint32_t chunksize = (len <= MAX_WRITE_CHUNK) ? len : MAX_WRITE_CHUNK;
		target_mem_write(f->f.t, SRAM_START, src, chunksize);
		/* Programm range */
		regs[0] = dest;
		regs[1] = SRAM_START;
		regs[2] = chunksize;
		rp_rom_call(f, regs, f->flash_range_program, 3 *  chunksize);
		len -= chunksize;
		src += chunksize;
		dest += chunksize;
	}
	/* flush */
	rp_rom_call(f, regs, f->_flash_flush_cache, 100);
	/* enter XIP */
	rp_rom_call(f, regs, f->_flash_enter_cmd_xip, 100);
	/* restore registers */
	target_regs_write(t, backup_regs);
	return 0;
}

static bool rp_cmd_erase_mass(target *t, int argc, const char *argv[])
{
	(void) argc;
	(void) argv;
	struct target_flash *f = t->flash;
	return (rp_flash_erase(f, XIP_FLASH_START, MAX_FLASH)) ? false: true;
}

const struct command_s rp_cmd_list[] = {
	{"erase_mass", rp_cmd_erase_mass, "Erase entire flash memory"},
	{NULL, NULL, NULL}
};
static void rp_add_flash(target *t, struct rp_flash *lf, uint32_t addr,
						 size_t length)
{
	struct target_flash *f = &lf->f;
	f->start = addr;
	f->length = length;
	f->erase = rp_flash_erase;
	f->write = rp_flash_write;
	f->blocksize = 0x1000;
	f->buf_size = 0x1000;
	f->erased = 0xff;
	target_add_flash(t, f);
}

bool rp_probe(target *t)
{
	/* Check bootrom magic*/
	uint32_t boot_magic = target_mem_read32(t, BOOTROM_MAGIC_ADDR);
	if ((boot_magic & 0x00ffffff) != BOOTROM_MAGIC) {
		DEBUG_WARN("Wrong Bootmagic %08" PRIx32 " found\n!", boot_magic);
		return false;
	}
#if defined(ENABLE_DEBUG)
	if ((boot_magic >> 24) == 1)
		DEBUG_WARN("Old Bootrom Version 1!\n");
#endif
#define RP_MAX_TABLE_SIZE  0x80
	uint16_t *table =  alloca(RP_MAX_TABLE_SIZE);
	uint16_t table_offset = target_mem_read32( t, BOOTROM_MAGIC_ADDR + 4);
	if (!table || target_mem_read(t, table, table_offset, RP_MAX_TABLE_SIZE))
		return false;
	struct rp_flash *lf = calloc(1, sizeof(*lf));
	if (!lf) {                       /* calloc failed: heap exhaustion */
		DEBUG_WARN("calloc: failed in %s\n", __func__);
		return false;
	}
	if (rp2040_fill_table(lf, table, RP_MAX_TABLE_SIZE)) {
		free(lf);
		return false;
	}
	t->driver = RP_ID;
	target_add_ram(t, SRAM_START, 0x40000);
	target_add_ram(t, 0x51000000,  0x1000);
	rp_add_flash(t, lf, XIP_FLASH_START, 16 * 1024 * 1024);
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
