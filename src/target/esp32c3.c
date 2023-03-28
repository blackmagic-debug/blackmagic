/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2023 1BitSquared <info@1bitsquared.com>
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

#include "general.h"
#include "target_internal.h"
#include "target_probe.h"
#include "riscv_debug.h"

#define ESP32_C3_ARCH_ID 0x80000001U
#define ESP32_C3_IMPL_ID 0x00000001U

#define ESP32_C3_DBUS_SRAM1_BASE 0x3fc80000U
#define ESP32_C3_DBUS_SRAM1_SIZE 0x00060000U
#define ESP32_C3_IBUS_SRAM0_BASE 0x4037c000U
#define ESP32_C3_IBUS_SRAM0_SIZE 0x00004000U
#define ESP32_C3_IBUS_SRAM1_BASE 0x40380000U
#define ESP32_C3_IBUS_SRAM1_SIZE 0x00060000U
#define ESP32_C3_RTC_SRAM_BASE   0x50000000U
#define ESP32_C3_RTC_SRAM_SIZE   0x00002000U

#define ESP32_C3_IBUS_FLASH_BASE 0x42000000U
#define ESP32_C3_IBUS_FLASH_SIZE 0x00800000U

static void esp32c3_add_flash(target_s *const target)
{
	target_flash_s *const flash = calloc(1, sizeof(*flash));
	if (!flash) { /* calloc failed: heap exhaustion */
		DEBUG_WARN("calloc: failed in %s\n", __func__);
		return;
	}

	flash->start = ESP32_C3_IBUS_FLASH_BASE;
	flash->length = ESP32_C3_IBUS_FLASH_SIZE;
	flash->blocksize = 256U;
	flash->writesize = 4096U;
	flash->erased = 0xffU;
	target_add_flash(target, flash);
}

bool esp32c3_probe(target_s *const target)
{
	const riscv_hart_s *const hart = riscv_hart_struct(target);
	/* Seems that the best we can do is check the marchid and mimplid register values */
	if (hart->archid != ESP32_C3_ARCH_ID || hart->implid != ESP32_C3_IMPL_ID)
		return false;

	target->driver = "ESP32-C3";
	/* Establish the target RAM mappings */
	target_add_ram(target, ESP32_C3_IBUS_SRAM0_BASE, ESP32_C3_IBUS_SRAM0_SIZE);
	target_add_ram(target, ESP32_C3_IBUS_SRAM1_BASE, ESP32_C3_IBUS_SRAM1_SIZE);
	target_add_ram(target, ESP32_C3_DBUS_SRAM1_BASE, ESP32_C3_DBUS_SRAM1_SIZE);
	target_add_ram(target, ESP32_C3_RTC_SRAM_BASE, ESP32_C3_RTC_SRAM_SIZE);

	/* Establish the target Flash mappings */
	esp32c3_add_flash(target);

	return true;
}
