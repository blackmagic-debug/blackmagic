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

#ifndef TARGET_RISCV_DEBUG_H
#define TARGET_RISCV_DEBUG_H

#include <stdint.h>
#include <stdbool.h>
#include "target.h"

typedef enum riscv_debug_version {
	RISCV_DEBUG_UNKNOWN,
	RISCV_DEBUG_UNIMPL,
	RISCV_DEBUG_0_11,
	RISCV_DEBUG_0_13,
	RISCV_DEBUG_1_0,
} riscv_debug_version_e;

/* This enum describes the Hart status (eg after a CSR read/write) */
typedef enum riscv_hart_status {
	/* The Hart is in a good state */
	RISCV_HART_NO_ERROR = 0,
	/* The Hart was bust when the status was read */
	RISCV_HART_BUSY = 1,
	/* The operation requested of the Hart was not supported */
	RISCV_HART_NOT_SUPP = 2,
	/* An exception occured on the Hart while running the operation */
	RISCV_HART_EXCEPTION = 3,
	/* The Hart is in the wrong state for the requested operation */
	RISCV_HART_WRONG_STATE = 4,
	/* The operation triggered a Hart bus error (bad alignment, access size, or timeout) */
	RISCV_HART_BUS_ERROR = 5,
	/* The operation failed for other (unknown) reasons */
	RISCV_HART_OTHER = 7,
} riscv_hart_status_e;

/* This enum describes the current state of a trigger in the TM */
typedef enum riscv_trigger_state {
	RISCV_TRIGGER_MODE_UNUSED = 0x00000000U,
	RISCV_TRIGGER_MODE_BREAKPOINT = 0x00010000U,
	RISCV_TRIGGER_MODE_WATCHPOINT = 0x00020000U,
} riscv_trigger_state_e;

/* The size bits are 22:21 + 17:16, but the upper 2 are only valid on rv64 */
typedef enum riscv_match_size {
	RV_MATCH_SIZE_8_BIT = 0x00010000U,
	RV_MATCH_SIZE_16_BIT = 0x00020000U,
	RV_MATCH_SIZE_32_BIT = 0x00030000U,
	RV_MATCH_SIZE_48_BIT = 0x00200000U,
	RV_MATCH_SIZE_64_BIT = 0x00210000U,
	RV_MATCH_SIZE_80_BIT = 0x00220000U,
	RV_MATCH_SIZE_96_BIT = 0x00230000U,
	RV_MATCH_SIZE_112_BIT = 0x00400000U,
	RV_MATCH_SIZE_128_BIT = 0x00410000U,
} riscv_match_size_e;

typedef struct riscv_dmi riscv_dmi_s;

/* This structure represents a version-agnostic Debug Module Interface on a RISC-V device */
struct riscv_dmi {
	uint32_t ref_count;

	uint32_t idcode;
	riscv_debug_version_e version;

	uint8_t dev_index;
	uint8_t idle_cycles;
	uint8_t address_width;
	uint8_t fault;

	void (*prepare)(target_s *target);
	void (*quiesce)(target_s *target);
	bool (*read)(riscv_dmi_s *dmi, uint32_t address, uint32_t *value);
	bool (*write)(riscv_dmi_s *dmi, uint32_t address, uint32_t value);
};

/* This represents a specific Debug Module on the DMI bus */
typedef struct riscv_dm {
	uint32_t ref_count;

	riscv_dmi_s *dmi_bus;
	uint32_t base;
	riscv_debug_version_e version;
} riscv_dm_s;

#define RV_TRIGGERS_MAX 8U

/* This represents a specifc Hart on a DM */
typedef struct riscv_hart {
	riscv_dm_s *dbg_module;
	uint32_t hart_idx;
	uint32_t hartsel;
	uint8_t access_width;
	uint8_t address_width;
	riscv_hart_status_e status;

	uint32_t extensions;
	uint32_t vendorid;
	uint32_t archid;
	uint32_t implid;
	uint32_t hartid;

	uint32_t triggers;
	uint32_t trigger_uses[RV_TRIGGERS_MAX];
} riscv_hart_s;

#define RV_STATUS_VERSION_MASK 0x0000000fU

#define RV_DM_DATA0           0x04U
#define RV_DM_DATA1           0x05U
#define RV_DM_DATA2           0x06U
#define RV_DM_DATA3           0x07U
#define RV_DM_ABST_CTRLSTATUS 0x16U
#define RV_DM_ABST_COMMAND    0x17U

#define RV_DM_ABST_CMD_ACCESS_REG 0x00000000U
#define RV_DM_ABST_CMD_ACCESS_MEM 0x02000000U

#define RV_ABST_READ          0x00000000U
#define RV_ABST_WRITE         0x00010000U
#define RV_REG_XFER           0x00020000U
#define RV_REG_ACCESS_32_BIT  0x00200000U
#define RV_REG_ACCESS_64_BIT  0x00300000U
#define RV_REG_ACCESS_128_BIT 0x00400000U
#define RV_MEM_ADDR_POST_INC  0x00080000U

#define RV_MEM_ACCESS_8_BIT   0x0U
#define RV_MEM_ACCESS_16_BIT  0x1U
#define RV_MEM_ACCESS_32_BIT  0x2U
#define RV_MEM_ACCESS_64_BIT  0x3U
#define RV_MEM_ACCESS_128_BIT 0x4U
#define RV_MEM_ACCESS_SHIFT   20U

/* dpc -> Debug Program Counter */
#define RV_DPC 0x7b1U
/* The GPR base defines the starting register space address for the CPU state registers */
#define RV_GPR_BASE 0x1000U
/* The FP base defines the starting register space address for the floating point registers */
#define RV_FP_BASE 0x1020U

#define RV_ISA_EXT_EMBEDDED     0x00000010U
#define RV_ISA_EXT_ANY_FLOAT    0x00010028U
#define RV_ISA_EXT_SINGLE_FLOAT 0x00000020U
#define RV_ISA_EXT_DOUBLE_FLOAT 0x00000008U
#define RV_ISA_EXT_QUAD_FLOAT   0x00010000U

#define RV_TRIGGER_SUPPORT_MASK 0x0000fffeU
#define RV_TRIGGER_MODE_MASK    0xffff0000U

#define RV_TOPT_INHIBIT_NRST 0x00000001U

void riscv_jtag_dtm_handler(uint8_t dev_index);
void riscv_dmi_init(riscv_dmi_s *dmi);
riscv_hart_s *riscv_hart_struct(target_s *target);

bool riscv_dm_read(riscv_dm_s *dbg_module, uint8_t address, uint32_t *value);
bool riscv_dm_write(riscv_dm_s *dbg_module, uint8_t address, uint32_t value);
bool riscv_command_wait_complete(riscv_hart_s *hart);
bool riscv_csr_read(riscv_hart_s *hart, uint16_t reg, void *data);
bool riscv_csr_write(riscv_hart_s *hart, uint16_t reg, const void *data);
riscv_match_size_e riscv_breakwatch_match_size(size_t size);
bool riscv_config_trigger(
	riscv_hart_s *hart, uint32_t trigger, riscv_trigger_state_e mode, const void *config, const void *address);

uint8_t riscv_mem_access_width(const riscv_hart_s *hart, target_addr_t address, size_t length);
void riscv32_unpack_data(void *dest, uint32_t data, uint8_t access_width);

#endif /*TARGET_RISCV_DEBUG_H*/
