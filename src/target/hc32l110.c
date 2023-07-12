/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2022 Maciej Kulinski (vesim809@pm.me)
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

#include "general.h"
#include "target.h"
#include "target_internal.h"

#define HC32L110_FLASH_BASE 0x00000000U
#define HC32L110_BLOCKSIZE  512U

#define HC32L110_ADDR_FLASH_SIZE 0x00100c70U

static void hc32l110_add_flash(target_s *target, const uint32_t flash_size)
{
	target_flash_s *flash = calloc(1, sizeof(*flash));
	if (!flash) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return;
	}

	flash->start = HC32L110_FLASH_BASE;
	flash->length = flash_size;
	flash->blocksize = HC32L110_BLOCKSIZE;
	flash->writesize = HC32L110_BLOCKSIZE; // TODO
	flash->erased = 0xffU;
	target_add_flash(target, flash);
}

bool hc32l110_probe(target_s *t)
{
	const uint32_t flash_size = target_mem_read32(t, HC32L110_ADDR_FLASH_SIZE);

	switch (flash_size) {
	case 16384:
		t->driver = "HC32L110A";
		target_add_ram(t, 0x2000000, 2048);
		break;
	case 32768:
		t->driver = "HC32L110B";
		target_add_ram(t, 0x2000000, 4096);
		break;
	default:
		return false;
	}
	hc32l110_add_flash(t, flash_size);
	return true;
}
