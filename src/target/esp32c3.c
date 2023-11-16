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
#include "jep106.h"

#define ESP32_C3_ARCH_ID 0x80000001U
#define ESP32_C3_IMPL_ID 0x00000001U

#define ESP32_C3_RTC_BASE           0x60008000U
#define ESP32_C3_RTC_WDT_CONFIG0    (ESP32_C3_RTC_BASE + 0x090U)
#define ESP32_C3_RTC_WDT_FEED       (ESP32_C3_RTC_BASE + 0x0a4U)
#define ESP32_C3_RTC_WDT_WRITE_PROT (ESP32_C3_RTC_BASE + 0x0a8U)
#define ESP32_C3_RTC_SWD_CONFIG     (ESP32_C3_RTC_BASE + 0x0acU)
#define ESP32_C3_RTC_SWD_WRITE_PROT (ESP32_C3_RTC_BASE + 0x0b0U)

#define ESP32_C3_WDT_WRITE_PROT_KEY     0x50d83aa1U
#define ESP32_C3_RTC_SWD_WRITE_PROT_KEY 0x8f1d312aU
#define ESP32_C3_RTC_SWD_CONFIG_DISABLE 0x40000002U
#define ESP32_C3_RTC_SWD_CONFIG_FEED    0x60000002U

#define ESP32_C3_TIMG0_BASE           0x6001f000U
#define ESP32_C3_TIMG0_WDT_CONFIG0    (ESP32_C3_TIMG0_BASE + 0x048U)
#define ESP32_C3_TIMG0_WDT_FEED       (ESP32_C3_TIMG0_BASE + 0x060U)
#define ESP32_C3_TIMG0_WDT_WRITE_PROT (ESP32_C3_TIMG0_BASE + 0x064U)

#define ESP32_C3_TIMG1_BASE           0x60020000U
#define ESP32_C3_TIMG1_WDT_CONFIG0    (ESP32_C3_TIMG1_BASE + 0x048U)
#define ESP32_C3_TIMG1_WDT_FEED       (ESP32_C3_TIMG1_BASE + 0x060U)
#define ESP32_C3_TIMG1_WDT_WRITE_PROT (ESP32_C3_TIMG1_BASE + 0x064U)

typedef struct esp32c3_priv {
	uint32_t wdt_config[4];
} esp32c3_priv_s;

static void esp32c3_disable_wdts(target_s *target);

/* Make an ESP32-C3 ready for probe operations having identified one */
bool esp32c3_target_prepare(target_s *const target)
{
	const riscv_hart_s *const hart = riscv_hart_struct(target);
	/* Seems that the best we can do is check the marchid and mimplid register values */
	if (target->designer_code != JEP106_MANUFACTURER_ESPRESSIF || hart->archid != ESP32_C3_ARCH_ID ||
		hart->implid != ESP32_C3_IMPL_ID)
		return false;

	/* Allocate the private structure here so we can store the WDT states */
	esp32c3_priv_s *const priv = calloc(1, sizeof(esp32c3_priv_s));
	if (!priv) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return false;
	}
	target->target_storage = priv;
	/* Prepare the target for memory IO */
	target->mem_read = riscv32_mem_read;
	target->mem_write = riscv32_mem_write;
	/* Now disable the WDTs so the stop causing problems ready for discovering trigger slots, etc */
	esp32c3_disable_wdts(target);
	return true;
}

static void esp32c3_disable_wdts(target_s *const target)
{
	esp32c3_priv_s *const priv = (esp32c3_priv_s *)target->target_storage;
	/* Disable Timer Group 0's WDT */
	target_mem32_write32(target, ESP32_C3_TIMG0_WDT_WRITE_PROT, ESP32_C3_WDT_WRITE_PROT_KEY);
	priv->wdt_config[0] = target_mem32_read32(target, ESP32_C3_TIMG0_WDT_CONFIG0);
	target_mem32_write32(target, ESP32_C3_TIMG0_WDT_CONFIG0, 0U);
	/* Disable Timer Group 1's WDT */
	target_mem32_write32(target, ESP32_C3_TIMG1_WDT_WRITE_PROT, ESP32_C3_WDT_WRITE_PROT_KEY);
	priv->wdt_config[1] = target_mem32_read32(target, ESP32_C3_TIMG1_WDT_CONFIG0);
	target_mem32_write32(target, ESP32_C3_TIMG1_WDT_CONFIG0, 0U);
	/* Disable the RTC WDT */
	target_mem32_write32(target, ESP32_C3_RTC_WDT_WRITE_PROT, ESP32_C3_WDT_WRITE_PROT_KEY);
	priv->wdt_config[2] = target_mem32_read32(target, ESP32_C3_RTC_WDT_CONFIG0);
	target_mem32_write32(target, ESP32_C3_RTC_WDT_CONFIG0, 0U);
	/* Disable the "super" WDT */
	target_mem32_write32(target, ESP32_C3_RTC_SWD_WRITE_PROT, ESP32_C3_RTC_SWD_WRITE_PROT_KEY);
	priv->wdt_config[3] = target_mem32_read32(target, ESP32_C3_RTC_SWD_CONFIG);
	target_mem32_write32(target, ESP32_C3_RTC_SWD_CONFIG, ESP32_C3_RTC_SWD_CONFIG_DISABLE);
}
