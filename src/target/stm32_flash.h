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

#ifndef TARGET_STM32_FLASH_H
#define TARGET_STM32_FLASH_H

#include "general.h"
#include "target.h"
#include "target_internal.h"

/*
 * Flash program and erase controller (FPEC)
 *
 * This accounts for STM32F1x, STM32F3x and STM32F4x series
 * 
 * This is based on the PM0068 and PM0075 ST programing manuals 
 * https://www.st.com/resource/en/programming_manual/pm0068-stm32f10xxx-xldensity-flash-programming-stmicroelectronics.pdf
 * https://www.st.com/resource/en/programming_manual/pm0075-stm32f10xxx-flash-memory-microcontrollers-stmicroelectronics.pdf
 */
#define STM32F10X_FPEC_BASE     0x40022000U
#define STM32F4X_FPEC_BASE      0x40023c00U
#define STM32_FLASH_BANK_OFFSET 0x40U

/* Flash access control register (FLASH_ACR) */
#define STM32_FLASH_ACR_OFFSET     0x00U
#define STM32_FLASH_ACR(fpec_base) ((fpec_base) + STM32_FLASH_ACR_OFFSET)

/* STM32F4x Only */
#define STM32F4X_FLASH_ACR_DCRST  (1U << 12U) /* Data cache reset (1: reset, 0: not reset) */
#define STM32F4X_FLASH_ACR_ICRST  (1U << 11U) /* Instruction cache reset (1: reset, 0: not reset) */
#define STM32F4X_FLASH_ACR_DCEN   (1U << 10U) /* Data cache enable (1: enabled, 0: disabled) */
#define STM32F4X_FLASH_ACR_ICEN   (1U << 9U)  /* Instruction cache enable (1: enabled, 0: disabled) */
#define STM32F4X_FLASH_ACR_PRFTEN (1U << 8U)  /* Prefetch enable (1: enabled, 0: disabled) */
/* STM32F1x/F3x Only */
#define STM32F10X_FLASH_ACR_PRFTBS (1U << 5U) /* Prefetch buffer status (1: enabled, 0: disabled) */
#define STM32F10X_FLASH_ACR_PRFTBE (1U << 4U) /* Prefetch buffer enable (1: enable, 0: disable) */
#define STM32F10X_FLASH_ACR_HLFCYA (1U << 3U) /* Flash half cycle access enable (1: enabled, 0: disabled) */

#define STM32_FLASH_ACR_LATENCY_MASK 0x7U /* Latency */
#define STM32_FLASH_ACR_LATENCY_0WS  0U   /* 0b000 - Zero wait state (STM32F10X: 0 < SYSCLK≤ 24 MHz) */
#define STM32_FLASH_ACR_LATENCY_1WS  1U   /* 0b001 - One wait state (STM32F10X: 24 MHz < SYSCLK ≤ 48 MHz) */
#define STM32_FLASH_ACR_LATENCY_2WS  2U   /* 0b010 - Two wait states (STM32F10X: 48 MHz < SYSCLK ≤ 72 MHz) */
#define STM32_FLASH_ACR_LATENCY_3WS  3U   /* 0b011 - Three wait states (STM32F4x Only) */
#define STM32_FLASH_ACR_LATENCY_4WS  4U   /* 0b100 - Four wait states (STM32F4x Only) */
#define STM32_FLASH_ACR_LATENCY_5WS  5U   /* 0b101 - Five wait states (STM32F4x Only) */
#define STM32_FLASH_ACR_LATENCY_6WS  6U   /* 0b110 - Six wait states (STM32F4x Only) */
#define STM32_FLASH_ACR_LATENCY_7WS  7U   /* 0b111 - Seven wait states (STM32F4x Only) */

/* FPEC key register (FLASH_KEYR) */
#define STM32_FLASH_KEYR_OFFSET                0x04U
#define STM32_FLASH_KEYR(fpec_base)            ((fpec_base) + STM32_FLASH_KEYR_OFFSET)
#define STM32_FLASH_KEYR_BANK(fpec_base, bank) (STM32_FLASH_KEYR(fpec_base) + (bank)*STM32_FLASH_BANK_OFFSET)

#define STM32_FLASH_KEY1 0x45670123U
#define STM32_FLASH_KEY2 0xcdef89abU

/* Flash OPTKEY register (FLASH_OPTKEYR) */
#define STM32_FLASH_OPTKEYR_OFFSET     0x08U
#define STM32_FLASH_OPTKEYR(fpec_base) ((fpec_base) + STM32_FLASH_OPTKEYR_OFFSET)

#define STM32F4X_FLASH_OPTKEY1 0x08192a3bU
#define STM32F4X_FLASH_OPTKEY2 0x4c5d6e7fU

/* Flash status register (FLASH_SR) */
#define STM32_FLASH_SR_OFFSET                0x0cU
#define STM32_FLASH_SR(fpec_base)            ((fpec_base) + STM32_FLASH_SR_OFFSET)
#define STM32_FLASH_SR_BANK(fpec_base, bank) (STM32_FLASH_SR(fpec_base) + (bank)*STM32_FLASH_BANK_OFFSET)

#define STM32_FLASH_SR_WRPRTERR (1U << 4U) /* Write protection error */
// /* STM32F4x Only */
// #define STM32F4X_FLASH_SR_BSY    (1U << 16U) /* Busy */
// #define STM32F4X_FLASH_SR_RDERR  (1U << 8U)  /* Proprietary readout protection error (STM32F42/3x Only) */
// #define STM32F4X_FLASH_SR_PGSERR (1U << 7U)  /* Programming sequence error */
// #define STM32F4X_FLASH_SR_PGPERR (1U << 6U)  /* Programming parallelism error */
// #define STM32F4X_FLASH_SR_PGAERR (1U << 5U)  /* Programming alignment error */
// #define STM32F4X_FLASH_SR_OPERR  (1U << 1U)  /* Operation error */
// #define STM32F4X_FLASH_SR_EOP    (1U << 0U)  /* End of operation */
/* STM32F1x/F3x Only */
// #define STM32F10X_FLASH_SR_EOP   (1U << 5U) /* End of operation */
// #define STM32F10X_FLASH_SR_PGERR (1U << 2U) /* Programming error */
// #define STM32F10X_FLASH_SR_BSY   (1U << 0U) /* Busy operation */
#define STM32_FLASH_SR_EOP   (1U << 5U) /* End of operation */
#define STM32_FLASH_SR_PGERR (1U << 2U) /* Programming error */
#define STM32_FLASH_SR_BSY   (1U << 0U) /* Busy operation */

/* Flash control register (FLASH_CR) */
#define STM32_FLASH_CR_OFFSET                0x10U
#define STM32_FLASH_CR(fpec_base)            ((fpec_base) + STM32_FLASH_CR_OFFSET)
#define STM32_FLASH_CR_BANK(fpec_base, bank) (STM32_FLASH_CR(fpec_base) + (bank)*STM32_FLASH_BANK_OFFSET)

#define STM32_FLASH_CR_MER     (1U << 2U) /* Mass erase bank */
#define STM32_FLASH_CR_PER_SER (1U << 1U) /* Page / Sector erase */
#define STM32_FLASH_CR_PG      (1U << 0U) /* Programming */
// /* STM32F4x Only */
// #define STM32F4X_FLASH_CR_LOCK         (1U << 31)  /* Lock */
// #define STM32F4X_FLASH_CR_ERRIE        (1U << 25U) /* Error interrupt enable */
// #define STM32F4X_FLASH_CR_EOPIE        (1U << 24U) /* End of operation interrupt enable */
// #define STM32F4X_FLASH_CR_STRT         (1U << 16U) /* Start */
// #define STM32F4X_FLASH_CR_MER2         (1U << 15U) /* Mass Erase of bank 2 (STM32F42/3x Only) */
// #define STM32F4X_FLASH_CR_PSIZE_OFFSET 8U          /* Program parallelism */
// #define STM32F4X_FLASH_CR_PSIZE_MASK   (0x3U << STM32F4X_FLASH_CR_PSIZE_OFFSET) /* Program parallelism */
// #define STM32F4X_FLASH_CR_PSIZE_8BIT   (0x0U << STM32F4X_FLASH_CR_PSIZE_OFFSET) /* 0b00 program x8 */
// #define STM32F4X_FLASH_CR_PSIZE_16BIT  (0x1U << STM32F4X_FLASH_CR_PSIZE_OFFSET) /* 0b01 program x16 */
// #define STM32F4X_FLASH_CR_PSIZE_32BIT  (0x2U << STM32F4X_FLASH_CR_PSIZE_OFFSET) /* 0b10 program x32 */
// #define STM32F4X_FLASH_CR_PSIZE_64BIT  (0x3U << STM32F4X_FLASH_CR_PSIZE_OFFSET) /* 0b11 program x64 */
// #define STM32F4X_FLASH_CR_SNB_OFFSET   3U                                       /* Sector number */
// #define STM32F4X_FLASH_CR_SNB_MASK     (0x1fU << STM32F4X_FLASH_CR_SNB_OFFSET)  /* Sector number */
// /* STM32F1x/F3x Only */
// #define STM32F10X_FLASH_CR_EOPIE  (1U << 12U) /* End of operation interrupt enable */
// #define STM32F10X_FLASH_CR_ERRIE  (1U << 10U) /* Error interrupt enable */
// #define STM32F10X_FLASH_CR_OPTWRE (1U << 9U)  /* Option bytes write enable */
// #define STM32F10X_FLASH_CR_LOCK   (1U << 7U)  /* Lock */
// #define STM32F10X_FLASH_CR_STRT   (1U << 6U)  /* Start */
// #define STM32F10X_FLASH_CR_OPTER  (1U << 5U)  /* Option byte erase */
// #define STM32F10X_FLASH_CR_OPTPG  (1U << 4U)  /* Option byte programming */
#define STM32_FLASH_CR_EOPIE  (1U << 12U) /* End of operation interrupt enable */
#define STM32_FLASH_CR_ERRIE  (1U << 10U) /* Error interrupt enable */
#define STM32_FLASH_CR_OPTWRE (1U << 9U)  /* Option bytes write enable */
#define STM32_FLASH_CR_LOCK   (1U << 7U)  /* Lock */
#define STM32_FLASH_CR_STRT   (1U << 6U)  /* Start */
#define STM32_FLASH_CR_OPTER  (1U << 5U)  /* Option byte erase */
#define STM32_FLASH_CR_OPTPG  (1U << 4U)  /* Option byte programming */

/* OBL_LAUNCH is not available on all families */
#define STM32_FLASH_CR_OBL_LAUNCH (1U << 13U) /* Force the option byte loading (generates system reset) */

/* Flash address register (FLASH_AR) (STM32F1x/F3x Only) */
// #define STM32F10X_FLASH_AR_OFFSET                0x14U
// #define STM32F10X_FLASH_AR(fpec_base)            ((fpec_base) + STM32_FLASH_AR_OFFSET)
// #define STM32F10X_FLASH_AR_BANK(fpec_base, bank) (STM32_FLASH_AR(fpec_base) + (bank) * STM32_FLASH_BANK_OFFSET)
#define STM32_FLASH_AR_OFFSET                0x14U
#define STM32_FLASH_AR(fpec_base)            ((fpec_base) + STM32_FLASH_AR_OFFSET)
#define STM32_FLASH_AR_BANK(fpec_base, bank) (STM32_FLASH_AR(fpec_base) + (bank)*STM32_FLASH_BANK_OFFSET)

/* Flash option control register (FLASH_OPTCR) (STM32F4x Only) */
#define STM32F4X_FLASH_OPTCR_OFFSET     0x14U
#define STM32F4X_FLASH_OPTCR(fpec_base) ((fpec_base) + STM32F4X_FLASH_OPTCR_OFFSET)

/* TODO */

/* Flash option control register (FLASH_OPTCR1) (STM32F4x Only) */
#define STM32F4X_FLASH_OPTCR1_OFFSET     0x18U
#define STM32F4X_FLASH_OPTCR1(fpec_base) ((fpec_base) + STM32F4X_FLASH_OPTCR1_OFFSET)

/* TODO */

/* Option byte register (FLASH_OBR) (STM32F1x/F3x Only) */
#define STM32_FLASH_OBR_OFFSET     0x1cU
#define STM32_FLASH_OBR(fpec_base) ((fpec_base) + STM32_FLASH_OBR_OFFSET)

#define STM32_FLASH_OBR_OPTERR (1U << 0U) /* OPTERR Option byte error */

/* Write protection register (FLASH_WRPR) (STM32F1x/F3x Only) */
#define STM32_FLASH_WRPR_OFFSET     0x20U
#define STM32_FLASH_WRPR(fpec_base) ((fpec_base) + STM32_FLASH_WRPR_OFFSET)

/*
 * §2.5 - Option byte description
 *
 * ┌───────────────────────────────────────────────────────────────────────────────────────┐
 * │                               Table 3. Option byte format                             │
 * ├─────────────────────────────┬───────────────┬─────────────────────────┬───────────────┤
 * │            31:24            │     23:16     │         15:8            │      7:0      │
 * ├─────────────────────────────┼───────────────┼─────────────────────────┼───────────────┤
 * │ Complement option byte1     │ Option byte 1 │ Complement option byte0 │ Option byte 0 │
 * └─────────────────────────────┴───────────────┴─────────────────────────┴───────────────┘
 * 
 * Table 4. Option byte organization
 * ┌────────────┬────────┬───────┬────────┬───────┐
 * │  Address   │ 31:24  │ 23:16 │  15:8  │  7:0  │
 * ├────────────┼────────┼───────┼────────┼───────┤
 * │ 0x1ffff800 │ nUSER  │ USER  │ nRDP   │ RDP   │
 * │ 0x1ffff804 │ nData1 │ Data1 │ nData0 │ Data0 │
 * │ 0x1ffff808 │ nWRP1  │ WRP1  │ nWRP0  │ WRP0  │
 * │ 0x1ffff80c │ nWRP3  │ WRP3  │ nWRP2  │ WRP2  │
 * └────────────┴────────┴───────┴────────┴───────┘
 */

#define STM32_FLASH_OPT_ADDR 0x1ffff800U

/* Option byte register STM32F10x */
#define STM32F10X_FLASH_OBR_DATA1_OFFSET    18U        /* 8 bits */
#define STM32F10X_FLASH_OBR_DATA0_OFFSET    10U        /* 8 bits */
#define STM32F10X_FLASH_OBR_USER_9          (1U << 9U) /* Not used */
#define STM32F10X_FLASH_OBR_USER_8          (1U << 8U) /* Not used */
#define STM32F10X_FLASH_OBR_USER_7          (1U << 7U) /* Not used */
#define STM32F10X_FLASH_OBR_USER_6          (1U << 6U) /* Not used */
#define STM32F10X_FLASH_OBR_USER_BFB2       (1U << 5U)
#define STM32F10X_FLASH_OBR_USER_NRST_STDBY (1U << 4U)
#define STM32F10X_FLASH_OBR_USER_NRST_STOP  (1U << 3U)
#define STM32F10X_FLASH_OBR_USER_WDG_SW     (1U << 2U)
#define STM32F10X_FLASH_OBR_RDPRT           (1U << 1U) /* Read protection */

#define STM32F10X_FLASH_RDPRT 0xa5U

/* Option byte register STM32F3xx */
//#define STM32F3XX_FLASH_RDPRT 0x55U
// Bits 31:24 Data1
// Bits 23:16 Data0
// Bits 15:8 OBR: User Option Byte
// Bit 15: Reserved, must be kept at reset value.
// Bit 14: Reserved, must be kept at reset value.
// Bit 13: VDDA_MONITOR
// Bit 12: nBOOT1
// Bit 11: Reserved, must be kept at reset value.
// Bit 10: nRST_STDBY
// Bit 9: nRST_STOP
// Bit 8: WDG_SW
// Bits 7:3 Reserved, must be kept at reset value.
// Bit 2:1 RDPRT[1:0]: Read protection Level status
// 00: Read protection Level 0 is enabled (ST production set up)
// 01: Read protection Level 1 is enabled
// 10: Reserved
// 11: Read protection Level 2 is enabled
// Note: These bits are read-only.

#define STM32F3X_FLASH_RDPRT 0xaaU

/* Insternal API */
typedef struct stm32_flash {
	target_flash_s flash;
	uint32_t fpec_base;
	uint8_t bank;
} stm32_flash_s;

bool stm32_flash_locked(target_s *target, uint32_t fpec_base, uint8_t bank);
bool stm32_flash_unlock(target_s *target, uint32_t fpec_base, uint8_t bank);
void stm32_flash_lock(target_s *target, uint32_t fpec_base, uint8_t bank);
void stm32_flash_clear_status(target_s *target, uint32_t fpec_base, uint8_t bank);
bool stm32_flash_busy_wait(target_s *target, uint32_t fpec_base, uint8_t bank, platform_timeout_s *print_progess);
bool stm32_flash_mass_erase(target_flash_s *flash, platform_timeout_s *print_progess);

/* Generic flash routines */
void stm32_add_flash(target_s *target, target_addr_t addr, size_t length, uint32_t fpec_base, size_t block_size);
void stm32_add_banked_flash(target_s *target, target_addr_t addr, size_t length, target_addr_t bank_split_addr,
	uint32_t fpec_base, size_t block_size);

/* Option bytes command */
bool stm32_option_bytes_cmd(target_s *target, int argc, const char **argv);

#endif /* TARGET_STM32_FLASH_H */
