/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
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

#ifndef TARGET_ADIV5_H
#define TARGET_ADIV5_H

#include "jtag_scan.h"

#if PC_HOSTED == 1
#include "platform.h"
#endif

#define ADIV5_APnDP     0x100U
#define ADIV5_DP_REG(x) (x)
#define ADIV5_AP_REG(x) (ADIV5_APnDP | (x))

/* ADIv5 DP Register addresses */
#define ADIV5_DP_DPIDR     ADIV5_DP_REG(0x0U)
#define ADIV5_DP_ABORT     ADIV5_DP_REG(0x0U)
#define ADIV5_DP_CTRLSTAT  ADIV5_DP_REG(0x4U)
#define ADIV5_DP_TARGETID  ADIV5_DP_REG(0x4U) /* ADIV5_DP_BANK2 */
#define ADIV5_DP_SELECT    ADIV5_DP_REG(0x8U)
#define ADIV5_DP_RDBUFF    ADIV5_DP_REG(0xCU)
#define ADIV5_DP_TARGETSEL ADIV5_DP_REG(0xCU)

/* DP DPIDR */
#define ADIV5_DP_DPIDR_REVISION_OFFSET 28U
#define ADIV5_DP_DPIDR_REVISION_MASK   (0xfU << ADIV5_DP_DPIDR_VERSION_OFFSET)
#define ADIV5_DP_DPIDR_PARTNO_OFFSET   20U
#define ADIV5_DP_DPIDR_PARTNO_MASK     (0xffU << ADIV5_DP_DPIDR_PARTNO_OFFSET)
#define ADIV5_DP_DPIDR_MINDP_OFFSET    16U
#define ADIV5_DP_DPIDR_MINDP           (1U << ADIV5_DP_DPIDR_MINDP_OFFSET)
#define ADIV5_DP_DPIDR_VERSION_OFFSET  12U
#define ADIV5_DP_DPIDR_VERSION_MASK    (0xfU << ADIV5_DP_DPIDR_VERSION_OFFSET)
#define ADIV5_DP_DPIDR_VERSION_DPv1    (1U << ADIV5_DP_DPIDR_VERSION_OFFSET)
#define ADIV5_DP_DPIDR_VERSION_DPv2    (2U << ADIV5_DP_DPIDR_VERSION_OFFSET)
#define ADIV5_DP_DPIDR_DESIGNER_OFFSET 1U
#define ADIV5_DP_DPIDR_DESIGNER_MASK   (0x7ffU << ADIV5_DP_DPIDR_DESIGNER_OFFSET)

/* DP SELECT register DP bank numbers */
#define ADIV5_DP_BANK0 0U
#define ADIV5_DP_BANK1 1U
#define ADIV5_DP_BANK2 2U
#define ADIV5_DP_BANK3 3U
#define ADIV5_DP_BANK4 4U

/* DP TARGETID */
#define ADIV5_DP_TARGETID_TREVISION_OFFSET 28U
#define ADIV5_DP_TARGETID_TREVISION_MASK   (0xfU << ADIV5_DP_TARGETID_TREVISION_OFFSET)
#define ADIV5_DP_TARGETID_TPARTNO_OFFSET   12U
#define ADIV5_DP_TARGETID_TPARTNO_MASK     (0xffffU << ADIV5_DP_TARGETID_TPARTNO_OFFSET)
#define ADIV5_DP_TARGETID_TDESIGNER_OFFSET 1U
#define ADIV5_DP_TARGETID_TDESIGNER_MASK   (0x7ffU << ADIV5_DP_TARGETID_TDESIGNER_OFFSET)

/* DP TARGETSEL */
#define ADIV5_DP_TARGETSEL_TINSTANCE_OFFSET 28U
#define ADIV5_DP_TARGETSEL_TINSTANCE_MASK   (0xfU << ADIV5_DP_TARGETSEL_TINSTANCE_OFFSET)
#define ADIV5_DP_TARGETSEL_TPARTNO_OFFSET   12U
#define ADIV5_DP_TARGETSEL_TPARTNO_MASK     (0xffffU << ADIV5_DP_TARGETSEL_TPARTNO_OFFSET)
#define ADIV5_DP_TARGETSEL_TDESIGNER_OFFSET 1U
#define ADIV5_DP_TARGETSEL_TDESIGNER_MASK   (0x7ffU << ADIV5_DP_TARGETSEL_TDESIGNER_OFFSET)

/* DP DPIDR/TARGETID/IDCODE DESIGNER */
/* Bits 10:7 - JEP-106 Continuation code */
/* Bits 6:0 - JEP-106 Identity code */
#define ADIV5_DP_DESIGNER_JEP106_CONT_OFFSET 7U
#define ADIV5_DP_DESIGNER_JEP106_CONT_MASK   (0xfU << ADIV5_DP_DESIGNER_JEP106_CONT_OFFSET)
#define ADIV5_DP_DESIGNER_JEP106_CODE_MASK   (0x7fU)

/* AP Abort Register (ABORT) */
/* Bits 31:5 - Reserved */
#define ADIV5_DP_ABORT_ORUNERRCLR (1U << 4U)
#define ADIV5_DP_ABORT_WDERRCLR   (1U << 3U)
#define ADIV5_DP_ABORT_STKERRCLR  (1U << 2U)
#define ADIV5_DP_ABORT_STKCMPCLR  (1U << 1U)
/* Bits 5:1 - SW-DP only, reserved in JTAG-DP */
#define ADIV5_DP_ABORT_DAPABORT (1U << 0U)

/* Control/Status Register (CTRLSTAT) */
#define ADIV5_DP_CTRLSTAT_CSYSPWRUPACK (1U << 31U)
#define ADIV5_DP_CTRLSTAT_CSYSPWRUPREQ (1U << 30U)
#define ADIV5_DP_CTRLSTAT_CDBGPWRUPACK (1U << 29U)
#define ADIV5_DP_CTRLSTAT_CDBGPWRUPREQ (1U << 28U)
#define ADIV5_DP_CTRLSTAT_CDBGRSTACK   (1U << 27U)
#define ADIV5_DP_CTRLSTAT_CDBGRSTREQ   (1U << 26U)
/* Bits 25:24 - Reserved */
/* Bits 23:12 - TRNCNT */
#define ADIV5_DP_CTRLSTAT_TRNCNT (1U << 12U)
/* Bits 11:8 - MASKLANE */
#define ADIV5_DP_CTRLSTAT_MASKLANE
/* Bits 7:6 - Reserved in JTAG-DP */
#define ADIV5_DP_CTRLSTAT_WDATAERR     (1U << 7U)
#define ADIV5_DP_CTRLSTAT_READOK       (1U << 6U)
#define ADIV5_DP_CTRLSTAT_STICKYERR    (1U << 5U)
#define ADIV5_DP_CTRLSTAT_STICKYCMP    (1U << 4U)
#define ADIV5_DP_CTRLSTAT_TRNMODE_MASK (3U << 2U)
#define ADIV5_DP_CTRLSTAT_STICKYORUN   (1U << 1U)
#define ADIV5_DP_CTRLSTAT_ORUNDETECT   (1U << 0U)

/* ADIv5 MEM-AP Registers */
#define ADIV5_AP_CSW ADIV5_AP_REG(0x00U)
#define ADIV5_AP_TAR ADIV5_AP_REG(0x04U)
/* 0x08 - Reserved */
#define ADIV5_AP_DRW   ADIV5_AP_REG(0x0CU)
#define ADIV5_AP_DB(x) ADIV5_AP_REG(0x10U + (4U * (x)))
/* 0x20:0xF0 - Reserved */
#define ADIV5_AP_CFG  ADIV5_AP_REG(0xF4U)
#define ADIV5_AP_BASE ADIV5_AP_REG(0xF8U)
#define ADIV5_AP_IDR  ADIV5_AP_REG(0xFCU)

/* AP Control and Status Word (CSW) */
#define ADIV5_AP_CSW_DBGSWENABLE (1U << 31U)
/* Bits 30:24 - Prot, Implementation defined, for Cortex-M3: */
#define ADIV5_AP_CSW_MASTERTYPE_DEBUG (1U << 29U)
#define ADIV5_AP_CSW_HPROT1           (1U << 25U)
#define ADIV5_AP_CSW_SPIDEN           (1U << 23U)
/* Bits 22:12 - Reserved */
/* Bits 11:8 - Mode, must be zero */
#define ADIV5_AP_CSW_TRINPROG       (1U << 7U)
#define ADIV5_AP_CSW_DEVICEEN       (1U << 6U)
#define ADIV5_AP_CSW_ADDRINC_NONE   (0U << 4U)
#define ADIV5_AP_CSW_ADDRINC_SINGLE (1U << 4U)
#define ADIV5_AP_CSW_ADDRINC_PACKED (2U << 4U)
#define ADIV5_AP_CSW_ADDRINC_MASK   (3U << 4U)
/* Bit 3 - Reserved */
#define ADIV5_AP_CSW_SIZE_BYTE     (0U << 0U)
#define ADIV5_AP_CSW_SIZE_HALFWORD (1U << 0U)
#define ADIV5_AP_CSW_SIZE_WORD     (2U << 0U)
#define ADIV5_AP_CSW_SIZE_MASK     (7U << 0U)

/* AP Debug Base Address Register (BASE) */
#define ADIV5_AP_BASE_BASEADDR (0xFFFFF000U)
#define ADIV5_AP_BASE_PRESENT  (1U << 0)

/* ADIv5 Class 0x1 ROM Table Registers */
#define ADIV5_ROM_MEMTYPE          0xFCCU
#define ADIV5_ROM_MEMTYPE_SYSMEM   (1U << 0U)
#define ADIV5_ROM_ROMENTRY_PRESENT (1U << 0U)
#define ADIV5_ROM_ROMENTRY_OFFSET  (0xFFFFF000U)

/* JTAG TAP IDCODE */
#define JTAG_IDCODE_VERSION_OFFSET  28U
#define JTAG_IDCODE_VERSION_MASK    (0xfU << JTAG_IDCODE_VERSION_OFFSET)
#define JTAG_IDCODE_PARTNO_OFFSET   12U
#define JTAG_IDCODE_PARTNO_MASK     (0xffffU << JTAG_IDCODE_PARTNO_OFFSET)
#define JTAG_IDCODE_DESIGNER_OFFSET 1U
#define JTAG_IDCODE_DESIGNER_MASK   (0x7ffU << JTAG_IDCODE_DESIGNER_OFFSET)

#define JTAG_IDCODE_ARM_DPv0 0x4ba00477U

/* Constants to make RnW parameters more clear in code */
#define ADIV5_LOW_WRITE 0
#define ADIV5_LOW_READ  1

#define SWDP_ACK_OK    0x01U
#define SWDP_ACK_WAIT  0x02U
#define SWDP_ACK_FAULT 0x04U

/* JEP-106 code list
 * JEP-106 is a JEDEC standard assigning IDs to different manufacturers
 * the codes in this list are encoded as 16 bit values,
 * with the first bit marking a legacy code (ASCII, not JEP106), the following 3 bits being NULL/unused
 * the following 4 bits the number of continuation codes (see JEP106 continuation scheme),
 * and the last 8 bits being the code itself (without parity, bit 7 is always 0).
 *
 * |15     |11     |7|6           0|
 * | | | | | | | | |0| | | | | | | |
 *  |\____/ \______/|\_____________/
 *  |  V        V   |       V
 *  | Unused   Cont	|      code
 *  |          Code |
 *  \_ Legacy flag  \_ Parity bit (always 0)
 */
#define ASCII_CODE_FLAG (1U << 15U) /* flag the code as legacy ASCII */

#define JEP106_MANUFACTURER_ARM          0x43BU /* ARM Ltd. */
#define JEP106_MANUFACTURER_FREESCALE    0x00eU /* Freescale */
#define JEP106_MANUFACTURER_TEXAS        0x017U /* Texas Instruments */
#define JEP106_MANUFACTURER_ATMEL        0x01fU /* Atmel */
#define JEP106_MANUFACTURER_STM          0x020U /* STMicroelectronics */
#define JEP106_MANUFACTURER_CYPRESS      0x034U /* Cypress Semiconductor */
#define JEP106_MANUFACTURER_INFINEON     0x041U /* Infineon Technologies */
#define JEP106_MANUFACTURER_NORDIC       0x244U /* Nordic Semiconductor */
#define JEP106_MANUFACTURER_SPECULAR     0x501U /* LPC845 with code 501. Strange!? Specular Networks */
#define JEP106_MANUFACTURER_ENERGY_MICRO 0x673U /* Energy Micro */
#define JEP106_MANUFACTURER_GIGADEVICE   0x751U /* GigaDevice */
#define JEP106_MANUFACTURER_RASPBERRY    0x913U /* Raspberry Pi */
#define JEP106_MANUFACTURER_RENESAS      0x423U /* Renesas */

/*
 * This code is not listed in the JEP106 standard, but is used by some stm32f1 clones
 * since we're not using this code elsewhere let's switch to the stm code.
 */
#define JEP106_MANUFACTURER_ERRATA_CS 0x555U

/* CPU2 for STM32W(L|B) uses ARM's JEP-106 continuation code (4) instead of
 * STM's JEP-106 continuation code (0) like expected, CPU1 behaves as expected.
 *
 * See RM0453
 * https://www.st.com/resource/en/reference_manual/rm0453-stm32wl5x-advanced-armbased-32bit-mcus-with-subghz-radio-solution-stmicroelectronics.pdf :
 * 38.8.2 CPU1 ROM CoreSight peripheral identity register 4 (ROM_PIDR4)
 * vs
 * 38.13.2 CPU2 ROM1 CoreSight peripheral identity register 4 (C2ROM1_PIDR4)
 *
 * let's call this an errata and switch to the "correct" continuation scheme.
 *
 * note: the JEP code 0x420 would belong to "Legend Silicon Corp." so in
 * the unlikely event we need to support chips by them, here be dragons.
 */
#define JEP106_MANUFACTURER_ERRATA_STM32WX 0x420U

enum align {
	ALIGN_BYTE = 0,
	ALIGN_HALFWORD = 1,
	ALIGN_WORD = 2,
	ALIGN_DWORD = 3
};

typedef struct ADIv5_AP_s ADIv5_AP_t;

/* Try to keep this somewhat absract for later adding SW-DP */
typedef struct ADIv5_DP_s {
	int refcnt;

	void (*seq_out)(uint32_t tms_states, size_t clock_cycles);
	void (*seq_out_parity)(uint32_t tms_states, size_t clock_cycles);
	uint32_t (*seq_in)(size_t clock_cycles);
	bool (*seq_in_parity)(uint32_t *ret, size_t clock_cycles);
	/* dp_low_write returns true if no OK resonse, but ignores errors */
	bool (*dp_low_write)(struct ADIv5_DP_s *dp, uint16_t addr, const uint32_t data);
	uint32_t (*dp_read)(struct ADIv5_DP_s *dp, uint16_t addr);
	uint32_t (*error)(struct ADIv5_DP_s *dp);
	uint32_t (*low_access)(struct ADIv5_DP_s *dp, uint8_t RnW, uint16_t addr, uint32_t value);
	void (*abort)(struct ADIv5_DP_s *dp, uint32_t abort);

#if PC_HOSTED == 1
	bmp_type_t dp_bmp_type;
	bool (*ap_setup)(int i);
	void (*ap_cleanup)(int i);
	void (*ap_regs_read)(ADIv5_AP_t *ap, void *data);
	uint32_t (*ap_reg_read)(ADIv5_AP_t *ap, int num);
	void (*ap_reg_write)(ADIv5_AP_t *ap, int num, uint32_t value);
	void (*read_block)(uint32_t addr, uint8_t *data, int size);
	void (*dap_write_block_sized)(uint32_t addr, uint8_t *data, int size, enum align align);
#endif
	uint32_t (*ap_read)(ADIv5_AP_t *ap, uint16_t addr);
	void (*ap_write)(ADIv5_AP_t *ap, uint16_t addr, uint32_t value);

	void (*mem_read)(ADIv5_AP_t *ap, void *dest, uint32_t src, size_t len);
	void (*mem_write_sized)(ADIv5_AP_t *ap, uint32_t dest, const void *src, size_t len, enum align align);
	uint8_t dp_jd_index;
	uint8_t fault;

	/* targetsel DPv2 */
	uint8_t instance;
	uint32_t targetsel;

	uint8_t version;

	bool mindp;

	/* DP designer (not implementer!) and partno */
	uint16_t designer_code;
	uint16_t partno;

	/* TARGETID designer and partno, present on DPv2 */
	uint16_t target_designer_code;
	uint16_t target_partno;
} ADIv5_DP_t;

struct ADIv5_AP_s {
	int refcnt;

	ADIv5_DP_t *dp;
	uint8_t apsel;

	uint32_t idr;
	uint32_t base;
	uint32_t csw;
	uint32_t ap_cortexm_demcr; /* Copy of demcr when starting */
	uint32_t ap_storage;       /* E.g to hold STM32F7 initial DBGMCU_CR value.*/

	/* AP designer and partno */
	uint16_t designer_code;
	uint16_t partno;
};

uint8_t make_packet_request(uint8_t RnW, uint16_t addr);

#if PC_HOSTED == 0
static inline uint32_t adiv5_dp_read(ADIv5_DP_t *dp, uint16_t addr)
{
	return dp->dp_read(dp, addr);
}

static inline uint32_t adiv5_dp_error(ADIv5_DP_t *dp)
{
	return dp->error(dp);
}

static inline uint32_t adiv5_dp_low_access(struct ADIv5_DP_s *dp, uint8_t RnW, uint16_t addr, uint32_t value)
{
	return dp->low_access(dp, RnW, addr, value);
}

static inline void adiv5_dp_abort(struct ADIv5_DP_s *dp, uint32_t abort)
{
	return dp->abort(dp, abort);
}

static inline uint32_t adiv5_ap_read(ADIv5_AP_t *ap, uint16_t addr)
{
	return ap->dp->ap_read(ap, addr);
}

static inline void adiv5_ap_write(ADIv5_AP_t *ap, uint16_t addr, uint32_t value)
{
	return ap->dp->ap_write(ap, addr, value);
}

static inline void adiv5_mem_read(ADIv5_AP_t *ap, void *dest, uint32_t src, size_t len)
{
	return ap->dp->mem_read(ap, dest, src, len);
}

static inline void adiv5_mem_write_sized(ADIv5_AP_t *ap, uint32_t dest, const void *src, size_t len, enum align align)
{
	return ap->dp->mem_write_sized(ap, dest, src, len, align);
}

static inline void adiv5_dp_write(ADIv5_DP_t *dp, uint16_t addr, uint32_t value)
{
	dp->low_access(dp, ADIV5_LOW_WRITE, addr, value);
}

#else
uint32_t adiv5_dp_read(ADIv5_DP_t *dp, uint16_t addr);
uint32_t adiv5_dp_error(ADIv5_DP_t *dp);
uint32_t adiv5_dp_low_access(struct ADIv5_DP_s *dp, uint8_t RnW, uint16_t addr, uint32_t value);
void adiv5_dp_abort(struct ADIv5_DP_s *dp, uint32_t abort);
uint32_t adiv5_ap_read(ADIv5_AP_t *ap, uint16_t addr);
void adiv5_ap_write(ADIv5_AP_t *ap, uint16_t addr, uint32_t value);
void adiv5_mem_read(ADIv5_AP_t *ap, void *dest, uint32_t src, size_t len);
void adiv5_mem_write_sized(ADIv5_AP_t *ap, uint32_t dest, const void *src, size_t len, enum align align);
void adiv5_dp_write(ADIv5_DP_t *dp, uint16_t addr, uint32_t value);
#endif

void adiv5_dp_init(ADIv5_DP_t *dp, uint32_t idcode);
void platform_adiv5_dp_defaults(ADIv5_DP_t *dp);
ADIv5_AP_t *adiv5_new_ap(ADIv5_DP_t *dp, uint8_t apsel);
void remote_jtag_dev(const jtag_dev_t *jtag_dev);
void adiv5_ap_ref(ADIv5_AP_t *ap);
void adiv5_ap_unref(ADIv5_AP_t *ap);
void platform_add_jtag_dev(uint32_t dev_index, const jtag_dev_t *jtag_dev);

void adiv5_jtag_dp_handler(uint8_t jd_index);
int platform_jtag_dp_init(ADIv5_DP_t *dp);
int swdptap_init(ADIv5_DP_t *dp);

void adiv5_mem_write(ADIv5_AP_t *ap, uint32_t dest, const void *src, size_t len);
uint64_t adiv5_ap_read_pidr(ADIv5_AP_t *ap, uint32_t addr);
void *extract(void *dest, uint32_t src, uint32_t val, enum align align);

void firmware_mem_write_sized(ADIv5_AP_t *ap, uint32_t dest, const void *src, size_t len, enum align align);
void firmware_mem_read(ADIv5_AP_t *ap, void *dest, uint32_t src, size_t len);
void firmware_ap_write(ADIv5_AP_t *ap, uint16_t addr, uint32_t value);
uint32_t firmware_ap_read(ADIv5_AP_t *ap, uint16_t addr);
uint32_t firmware_swdp_low_access(ADIv5_DP_t *dp, uint8_t RnW, uint16_t addr, uint32_t value);
uint32_t fw_adiv5_jtagdp_low_access(ADIv5_DP_t *dp, uint8_t RnW, uint16_t addr, uint32_t value);
uint32_t firmware_swdp_read(ADIv5_DP_t *dp, uint16_t addr);
uint32_t fw_adiv5_jtagdp_read(ADIv5_DP_t *dp, uint16_t addr);

uint32_t firmware_swdp_error(ADIv5_DP_t *dp);

void firmware_swdp_abort(ADIv5_DP_t *dp, uint32_t abort);
void adiv5_jtagdp_abort(ADIv5_DP_t *dp, uint32_t abort);

#endif /* TARGET_ADIV5_H */
