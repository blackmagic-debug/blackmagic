/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2023 1BitSquared <info@1bitsquared.com>
 * Written by Rafael Silva <perigoso@riseup.net>
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

#ifndef TARGET_CH32_FLASH_H
#define TARGET_CH32_FLASH_H

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "stm32_flash.h"

/*
 * STM32 Flash program and erase controller (FPEC) extension for CH32
 * contains the CH32 specific extension registers and bits.
 * Macros named CH32FV2X_V3X are shared between CH32F2x, CH32V2x and CH32V3x
 * 
 * This is based on CH32xRM Chapter §24 Flash Memory and CH32FV2x_V3xRM Chapter §32 lash Memory and User Option Bytes
 * https://www.wch-ic.com/downloads/file/306.html
 * https://www.wch-ic.com/downloads/file/324.html
 */
#define CH32_FPEC_BASE 0x40022000U /* Shared between CH32F1x, CH32F2x, CH32V2x and CH32V3x, may not apply to others */

#define CH32F1X_FAST_MODE_PAGE_SIZE      128U /* CH32F1x Fast erase/programming works on 128 byte pages */
#define CH32FV2X_V3X_FAST_MODE_PAGE_SIZE 256U /* CH32FV2x/V3x Fast erase/programming works on 256 byte pages */

/* Flash control register (FLASH_CR) */
#define CH32_FLASH_CR_FTER  (1U << 17U) /* Fast page (128Byte) erase operation */
#define CH32_FLASH_CR_FTPG  (1U << 16U) /* Fast programming operation */
#define CH32_FLASH_CR_FLOCK (1U << 15U) /* Fast programming lock (locked: fast programming/erase mode not available) */
/* CH32F1x Only */
#define CH32F1X_FLASH_CR_BUFRST  (1U << 19U) /* Clear the internal buffer data */
#define CH32F1X_FLASH_CR_BUFLOAD (1U << 18U) /* Load data into the internal buffer */
/* CH32FV2x/V3x Only */
#define CH32FV2X_V3X_FLASH_CR_SCKMOD  (1U << 25U) /* Flash access clock (1: SYSCLK, 0: ½SYSCLK) (must be < 60 MHz) */
#define CH32FV2X_V3X_FLASH_CR_EHMOD   (1U << 24U) /* Flash enhanced read mode */
#define CH32FV2X_V3X_FLASH_CR_RSENACT (1U << 22U) /* Exit the enhanced read mode, first clear the ENHANCE_MOD */
#define CH32FV2X_V3X_FLASH_CR_PGSTRT  (1U << 21U) /* Start a page programming */
#define CH32FV2X_V3X_FLASH_CR_BER64   (1U << 19U) /* Perform a 64KB erase */
#define CH32FV2X_V3X_FLASH_CR_BER32   (1U << 18U) /* Perform a 32KB erase */

/* Flash status register (FLASH_SR), CH32FV2x/V3x Only */
#define CH32FV2X_V3X_FLASH_SR_EHMODS (1U << 7U) /* Flash enhanced read mode (1: enabled, 0: disabled) */
#define CH32FV2X_V3X_FLASH_SR_WRBSY  (1U << 0U) /* Fast page programming busy */

/* 
 * Extension key register (FLASH_MODEKEYR)
 * Shares the keys with the ST FPEC key register (FLASH_KEYR)
 */
#define CH32_FLASH_MODEKEYR_OFFSET     0x24U
#define CH32_FLASH_MODEKEYR(fpec_base) ((fpec_base) + CH32_FLASH_MODEKEYR_OFFSET)

/*
 * FIXME: What is this?, CH32F1x Only
 * It's unclear what the purpose of this register is, it's not referenced in CH32xRM documentation
 * but it is used on the standard peripherals library for CH32F103x
 */
#define CH32F1X_FLASH_MAGIC_OFFSET     0x34U
#define CH32F1X_FLASH_MAGIC(fpec_base) ((fpec_base) + CH32F1X_FLASH_MAGIC_OFFSET)

#define CH32F1X_FLASH_MAGIC_XOR 0x00000100U

/* Insternal API */
bool ch32_flash_fast_mode_locked(target_s *target, uint32_t fpec_base);
bool ch32_flash_fast_mode_unlock(target_s *target, uint32_t fpec_base);
void ch32_flash_lock(target_s *target, uint32_t fpec_base);

/* Generic flash routines */
void ch32f1x_add_flash(target_s *target, target_addr_t addr, size_t length);
void ch32fv2x_v3x_add_flash(target_s *target, target_addr_t addr, size_t length);

#endif /* TARGET_CH32_FLASH_H */
