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
#ifndef __CORTEXM_H
#define __CORTEXM_H

#include "target.h"
#include "adiv5.h"

extern long cortexm_wait_timeout;
/* Private peripheral bus base address */
#define CORTEXM_PPB_BASE	0xE0000000

#define CORTEXM_SCS_BASE	(CORTEXM_PPB_BASE + 0xE000)

#define CORTEXM_AIRCR		(CORTEXM_SCS_BASE + 0xD0C)
#define CORTEXM_CFSR		(CORTEXM_SCS_BASE + 0xD28)
#define CORTEXM_HFSR		(CORTEXM_SCS_BASE + 0xD2C)
#define CORTEXM_DFSR		(CORTEXM_SCS_BASE + 0xD30)
#define CORTEXM_CPACR		(CORTEXM_SCS_BASE + 0xD88)
#define CORTEXM_DHCSR		(CORTEXM_SCS_BASE + 0xDF0)
#define CORTEXM_DCRSR		(CORTEXM_SCS_BASE + 0xDF4)
#define CORTEXM_DCRDR		(CORTEXM_SCS_BASE + 0xDF8)
#define CORTEXM_DEMCR		(CORTEXM_SCS_BASE + 0xDFC)

/* Cache identification */
#define CORTEXM_CLIDR		(CORTEXM_SCS_BASE + 0xD78)
#define CORTEXM_CTR		(CORTEXM_SCS_BASE + 0xD7C)
#define CORTEXM_CCSIDR		(CORTEXM_SCS_BASE + 0xD80)
#define CORTEXM_CSSELR		(CORTEXM_SCS_BASE + 0xD84)

/* Cache maintenance operations */
#define CORTEXM_ICIALLU		(CORTEXM_SCS_BASE + 0xF50)
#define CORTEXM_DCCMVAC		(CORTEXM_SCS_BASE + 0xF68)
#define CORTEXM_DCCIMVAC	(CORTEXM_SCS_BASE + 0xF70)

#define CORTEXM_FPB_BASE	(CORTEXM_PPB_BASE + 0x2000)

/* ARM Literature uses FP_*, we use CORTEXM_FPB_* consistently */
#define CORTEXM_FPB_CTRL	(CORTEXM_FPB_BASE + 0x000)
#define CORTEXM_FPB_REMAP	(CORTEXM_FPB_BASE + 0x004)
#define CORTEXM_FPB_COMP(i)	(CORTEXM_FPB_BASE + 0x008 + (4*(i)))

#define CORTEXM_DWT_BASE	(CORTEXM_PPB_BASE + 0x1000)

#define CORTEXM_DWT_CTRL	(CORTEXM_DWT_BASE + 0x000)
#define CORTEXM_DWT_COMP(i)	(CORTEXM_DWT_BASE + 0x020 + (0x10*(i)))
#define CORTEXM_DWT_MASK(i)	(CORTEXM_DWT_BASE + 0x024 + (0x10*(i)))
#define CORTEXM_DWT_FUNC(i)	(CORTEXM_DWT_BASE + 0x028 + (0x10*(i)))

/* Application Interrupt and Reset Control Register (AIRCR) */
#define CORTEXM_AIRCR_VECTKEY		(0x05FA << 16)
/* Bits 31:16 - Read as VECTKETSTAT, 0xFA05 */
#define CORTEXM_AIRCR_ENDIANESS		(1 << 15)
/* Bits 15:11 - Unused, reserved */
#define CORTEXM_AIRCR_PRIGROUP		(7 << 8)
/* Bits 7:3 - Unused, reserved */
#define CORTEXM_AIRCR_SYSRESETREQ	(1 << 2)
#define CORTEXM_AIRCR_VECTCLRACTIVE	(1 << 1)
#define CORTEXM_AIRCR_VECTRESET		(1 << 0)

/* HardFault Status Register (HFSR) */
#define CORTEXM_HFSR_DEBUGEVT		(1 << 31)
#define CORTEXM_HFSR_FORCED		(1 << 30)
/* Bits 29:2 - Not specified */
#define CORTEXM_HFSR_VECTTBL		(1 << 1)
/* Bits 0 - Reserved */

/* Debug Fault Status Register (DFSR) */
/* Bits 31:5 - Reserved */
#define CORTEXM_DFSR_RESETALL		0x1F
#define CORTEXM_DFSR_EXTERNAL		(1 << 4)
#define CORTEXM_DFSR_VCATCH		(1 << 3)
#define CORTEXM_DFSR_DWTTRAP		(1 << 2)
#define CORTEXM_DFSR_BKPT		(1 << 1)
#define CORTEXM_DFSR_HALTED		(1 << 0)

/* Debug Halting Control and Status Register (DHCSR) */
/* This key must be written to bits 31:16 for write to take effect */
#define CORTEXM_DHCSR_DBGKEY		0xA05F0000
/* Bits 31:26 - Reserved */
#define CORTEXM_DHCSR_S_RESET_ST	(1 << 25)
#define CORTEXM_DHCSR_S_RETIRE_ST	(1 << 24)
/* Bits 23:20 - Reserved */
#define CORTEXM_DHCSR_S_LOCKUP		(1 << 19)
#define CORTEXM_DHCSR_S_SLEEP		(1 << 18)
#define CORTEXM_DHCSR_S_HALT		(1 << 17)
#define CORTEXM_DHCSR_S_REGRDY		(1 << 16)
/* Bits 15:6 - Reserved */
#define CORTEXM_DHCSR_C_SNAPSTALL	(1 << 5)	/* v7m only */
/* Bit 4 - Reserved */
#define CORTEXM_DHCSR_C_MASKINTS	(1 << 3)
#define CORTEXM_DHCSR_C_STEP		(1 << 2)
#define CORTEXM_DHCSR_C_HALT		(1 << 1)
#define CORTEXM_DHCSR_C_DEBUGEN		(1 << 0)

/* Debug Core Register Selector Register (DCRSR) */
#define CORTEXM_DCRSR_REGWnR		0x00010000
#define CORTEXM_DCRSR_REGSEL_MASK	0x0000001F
#define CORTEXM_DCRSR_REGSEL_XPSR	0x00000010
#define CORTEXM_DCRSR_REGSEL_MSP	0x00000011
#define CORTEXM_DCRSR_REGSEL_PSP	0x00000012

/* Debug Exception and Monitor Control Register (DEMCR) */
/* Bits 31:25 - Reserved */
#define CORTEXM_DEMCR_TRCENA		(1 << 24)
/* Bits 23:20 - Reserved */
#define CORTEXM_DEMCR_MON_REQ		(1 << 19)	/* v7m only */
#define CORTEXM_DEMCR_MON_STEP		(1 << 18)	/* v7m only */
#define CORTEXM_DEMCR_VC_MON_PEND	(1 << 17)	/* v7m only */
#define CORTEXM_DEMCR_VC_MON_EN		(1 << 16)	/* v7m only */
/* Bits 15:11 - Reserved */
#define CORTEXM_DEMCR_VC_HARDERR	(1 << 10)
#define CORTEXM_DEMCR_VC_INTERR		(1 << 9)	/* v7m only */
#define CORTEXM_DEMCR_VC_BUSERR		(1 << 8)	/* v7m only */
#define CORTEXM_DEMCR_VC_STATERR	(1 << 7)	/* v7m only */
#define CORTEXM_DEMCR_VC_CHKERR		(1 << 6)	/* v7m only */
#define CORTEXM_DEMCR_VC_NOCPERR	(1 << 5)	/* v7m only */
#define CORTEXM_DEMCR_VC_MMERR		(1 << 4)	/* v7m only */
/* Bits 3:1 - Reserved */
#define CORTEXM_DEMCR_VC_CORERESET	(1 << 0)

/* Flash Patch and Breakpoint Control Register (FP_CTRL) */
/* Bits 32:15 - Reserved */
/* Bits 14:12 - NUM_CODE2 */	/* v7m only */
/* Bits 11:8 - NUM_LIT */	/* v7m only */
/* Bits 7:4 - NUM_CODE1 */
/* Bits 3:2 - Unspecified */
#define CORTEXM_FPB_CTRL_KEY		(1 << 1)
#define CORTEXM_FPB_CTRL_ENABLE		(1 << 0)

/* Data Watchpoint and Trace Mask Register (DWT_MASKx) */
#define CORTEXM_DWT_MASK_BYTE		(0 << 0)
#define CORTEXM_DWT_MASK_HALFWORD	(1 << 0)
#define CORTEXM_DWT_MASK_WORD		(3 << 0)

/* Data Watchpoint and Trace Function Register (DWT_FUNCTIONx) */
#define CORTEXM_DWT_FUNC_MATCHED	(1 << 24)
#define CORTEXM_DWT_FUNC_DATAVSIZE_WORD	(2 << 10)	/* v7m only */
#define CORTEXM_DWT_FUNC_FUNC_READ	(5 << 0)
#define CORTEXM_DWT_FUNC_FUNC_WRITE	(6 << 0)
#define CORTEXM_DWT_FUNC_FUNC_ACCESS	(7 << 0)

#define REG_SP		13
#define REG_LR		14
#define REG_PC		15
#define REG_XPSR	16
#define REG_MSP		17
#define REG_PSP		18
#define REG_SPECIAL	19

#define ARM_THUMB_BREAKPOINT 0xBE00

#define	CORTEXM_TOPT_INHIBIT_SRST (1 << 2)

bool cortexm_probe(ADIv5_AP_t *ap, bool forced);
ADIv5_AP_t *cortexm_ap(target *t);

bool cortexm_attach(target *t);
void cortexm_detach(target *t);
void cortexm_halt_resume(target *t, bool step);
int cortexm_run_stub(target *t, uint32_t loadaddr,
                     uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3);
int cortexm_mem_write_sized(
	target *t, target_addr dest, const void *src, size_t len, enum align align);

#endif

