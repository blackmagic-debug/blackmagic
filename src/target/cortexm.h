/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2015 Gareth McMullin <gareth@blacksphere.co.nz>
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

#ifndef TARGET_CORTEXM_H
#define TARGET_CORTEXM_H

#include "target.h"
#include "adiv5.h"
#include "cortex.h"

extern unsigned cortexm_wait_timeout;
/* Private peripheral bus base address */
#define CORTEXM_PPB_BASE 0xe0000000U

#define CORTEXM_SCS_BASE (CORTEXM_PPB_BASE + 0xe000U)

#define CORTEXM_CPUID (CORTEXM_SCS_BASE + 0xd00U)
#define CORTEXM_AIRCR (CORTEXM_SCS_BASE + 0xd0cU)
#define CORTEXM_CFSR  (CORTEXM_SCS_BASE + 0xd28U)
#define CORTEXM_HFSR  (CORTEXM_SCS_BASE + 0xd2cU)
#define CORTEXM_DFSR  (CORTEXM_SCS_BASE + 0xd30U)
#define CORTEXM_CPACR (CORTEXM_SCS_BASE + 0xd88U)
#define CORTEXM_DHCSR (CORTEXM_SCS_BASE + 0xdf0U)
#define CORTEXM_DCRSR (CORTEXM_SCS_BASE + 0xdf4U)
#define CORTEXM_DCRDR (CORTEXM_SCS_BASE + 0xdf8U)
#define CORTEXM_DEMCR (CORTEXM_SCS_BASE + 0xdfcU)

/* Cache identification */
#define CORTEXM_CLIDR  (CORTEXM_SCS_BASE + 0xd78U)
#define CORTEXM_CTR    (CORTEXM_SCS_BASE + 0xd7cU)
#define CORTEXM_CCSIDR (CORTEXM_SCS_BASE + 0xd80U)
#define CORTEXM_CSSELR (CORTEXM_SCS_BASE + 0xd84U)

/* Cache maintenance operations */
#define CORTEXM_ICIALLU  (CORTEXM_SCS_BASE + 0xf50U)
#define CORTEXM_DCCMVAC  (CORTEXM_SCS_BASE + 0xf68U)
#define CORTEXM_DCCIMVAC (CORTEXM_SCS_BASE + 0xf70U)

#define CORTEXM_FPB_BASE (CORTEXM_PPB_BASE + 0x2000U)

/* ARM Literature uses FP_*, we use CORTEXM_FPB_* consistently */
#define CORTEXM_FPB_CTRL    (CORTEXM_FPB_BASE + 0x000U)
#define CORTEXM_FPB_REMAP   (CORTEXM_FPB_BASE + 0x004U)
#define CORTEXM_FPB_COMP(i) (CORTEXM_FPB_BASE + 0x008U + (4U * (i)))

#define CORTEXM_DWT_BASE (CORTEXM_PPB_BASE + 0x1000U)

#define CORTEXM_DWT_CTRL    (CORTEXM_DWT_BASE + 0x000U)
#define CORTEXM_DWT_COMP(i) (CORTEXM_DWT_BASE + 0x020U + (0x10U * (i)))
#define CORTEXM_DWT_MASK(i) (CORTEXM_DWT_BASE + 0x024U + (0x10U * (i)))
#define CORTEXM_DWT_FUNC(i) (CORTEXM_DWT_BASE + 0x028U + (0x10U * (i)))

/* Application Interrupt and Reset Control Register (AIRCR) */
#define CORTEXM_AIRCR_VECTKEY (0x05faU << 16U)
/* Bits 31:16 - Read as VECTKETSTAT, 0xfa05 */
#define CORTEXM_AIRCR_ENDIANESS (1U << 15U)
/* Bits 15:11 - Unused, reserved */
#define CORTEXM_AIRCR_PRIGROUP (7U << 8U)
/* Bits 7:3 - Unused, reserved */
#define CORTEXM_AIRCR_SYSRESETREQ   (1U << 2U)
#define CORTEXM_AIRCR_VECTCLRACTIVE (1U << 1U)
#define CORTEXM_AIRCR_VECTRESET     (1U << 0U)

/* HardFault Status Register (HFSR) */
#define CORTEXM_HFSR_DEBUGEVT (1U << 31U)
#define CORTEXM_HFSR_FORCED   (1U << 30U)
/* Bits 29:2 - Not specified */
#define CORTEXM_HFSR_VECTTBL (1U << 1U)
/* Bits 0 - Reserved */

/* Debug Fault Status Register (DFSR) */
/* Bits 31:5 - Reserved */
#define CORTEXM_DFSR_RESETALL 0x1fU
#define CORTEXM_DFSR_EXTERNAL (1U << 4U)
#define CORTEXM_DFSR_VCATCH   (1U << 3U)
#define CORTEXM_DFSR_DWTTRAP  (1U << 2U)
#define CORTEXM_DFSR_BKPT     (1U << 1U)
#define CORTEXM_DFSR_HALTED   (1U << 0U)

/* Debug Halting Control and Status Register (DHCSR) */
/* This key must be written to bits 31:16 for write to take effect */
#define CORTEXM_DHCSR_DBGKEY 0xa05f0000U
/* Bits 31:26 - Reserved */
#define CORTEXM_DHCSR_S_RESET_ST  (1U << 25U)
#define CORTEXM_DHCSR_S_RETIRE_ST (1U << 24U)
/* Bits 23:20 - Reserved */
#define CORTEXM_DHCSR_S_LOCKUP (1U << 19U)
#define CORTEXM_DHCSR_S_SLEEP  (1U << 18U)
#define CORTEXM_DHCSR_S_HALT   (1U << 17U)
#define CORTEXM_DHCSR_S_REGRDY (1U << 16U)
/* Bits 15:6 - Reserved */
#define CORTEXM_DHCSR_C_SNAPSTALL (1U << 5U) /* v7m only */
/* Bit 4 - Reserved */
#define CORTEXM_DHCSR_C_MASKINTS (1U << 3U)
#define CORTEXM_DHCSR_C_STEP     (1U << 2U)
#define CORTEXM_DHCSR_C_HALT     (1U << 1U)
#define CORTEXM_DHCSR_C_DEBUGEN  (1U << 0U)

/* Debug Core Register Selector Register (DCRSR) */
#define CORTEXM_DCRSR_REGWnR      0x00010000U
#define CORTEXM_DCRSR_REGSEL_MASK 0x0000001fU
#define CORTEXM_DCRSR_REGSEL_XPSR 0x00000010U
#define CORTEXM_DCRSR_REGSEL_MSP  0x00000011U
#define CORTEXM_DCRSR_REGSEL_PSP  0x00000012U

/* Debug Exception and Monitor Control Register (DEMCR) */
/* Bits 31:25 - Reserved */
#define CORTEXM_DEMCR_TRCENA (1U << 24U)
/* Bits 23:20 - Reserved */
#define CORTEXM_DEMCR_MON_REQ     (1U << 19U) /* v7m only */
#define CORTEXM_DEMCR_MON_STEP    (1U << 18U) /* v7m only */
#define CORTEXM_DEMCR_VC_MON_PEND (1U << 17U) /* v7m only */
#define CORTEXM_DEMCR_VC_MON_EN   (1U << 16U) /* v7m only */
/* Bits 15:11 - Reserved */
#define CORTEXM_DEMCR_VC_HARDERR (1U << 10U)
#define CORTEXM_DEMCR_VC_INTERR  (1U << 9U) /* v7m only */
#define CORTEXM_DEMCR_VC_BUSERR  (1U << 8U) /* v7m only */
#define CORTEXM_DEMCR_VC_STATERR (1U << 7U) /* v7m only */
#define CORTEXM_DEMCR_VC_CHKERR  (1U << 6U) /* v7m only */
#define CORTEXM_DEMCR_VC_NOCPERR (1U << 5U) /* v7m only */
#define CORTEXM_DEMCR_VC_MMERR   (1U << 4U) /* v7m only */
/* Bits 3:1 - Reserved */
#define CORTEXM_DEMCR_VC_CORERESET (1U << 0U)

/* Flash Patch and Breakpoint Control Register (FP_CTRL) */
/* Bits 32:15 - Reserved */
/* Bits 14:12 - NUM_CODE2 */ /* v7m only */
/* Bits 11:8 - NUM_LIT */    /* v7m only */
/* Bits 7:4 - NUM_CODE1 */
/* Bits 3:2 - Unspecified */
#define CORTEXM_FPB_CTRL_KEY    (1U << 1U)
#define CORTEXM_FPB_CTRL_ENABLE (1U << 0U)

/* Data Watchpoint and Trace Mask Register (DWT_MASKx)
*  The value here is the number of address bits we mask out */
#define CORTEXM_DWT_MASK_BYTE     (0U)
#define CORTEXM_DWT_MASK_HALFWORD (1U)
#define CORTEXM_DWT_MASK_WORD     (2U)
#define CORTEXM_DWT_MASK_DWORD    (3U)

/* Data Watchpoint and Trace Function Register (DWT_FUNCTIONx) */
#define CORTEXM_DWT_FUNC_MATCHED        (1U << 24U)
#define CORTEXM_DWT_FUNC_DATAVSIZE_WORD (2U << 10U) /* v7m only */
#define CORTEXM_DWT_FUNC_FUNC_READ      (5U << 0U)
#define CORTEXM_DWT_FUNC_FUNC_WRITE     (6U << 0U)
#define CORTEXM_DWT_FUNC_FUNC_ACCESS    (7U << 0U)

#define CORTEXM_XPSR_THUMB          (1U << 24U)
#define CORTEXM_XPSR_EXCEPTION_MASK 0x0000001fU

#define CORTEXM_TOPT_INHIBIT_NRST (1U << 2U)

bool cortexm_attach(target_s *t);
void cortexm_detach(target_s *t);
void cortexm_halt_resume(target_s *t, bool step);
bool cortexm_run_stub(target_s *t, uint32_t loadaddr, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3);
int cortexm_mem_write_sized(target_s *t, target_addr_t dest, const void *src, size_t len, align_e align);

#endif /* TARGET_CORTEXM_H */
