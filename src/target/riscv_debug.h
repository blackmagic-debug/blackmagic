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
} riscv_hart_s;

#define RV_STATUS_VERSION_MASK 0x0000000fU

void riscv_jtag_dtm_handler(uint8_t dev_index);
void riscv_dmi_init(riscv_dmi_s *dmi);
bool riscv_csr_read(riscv_hart_s *hart, uint16_t reg, void *data);
bool riscv_csr_write(riscv_hart_s *hart, uint16_t reg, const void *data);

#endif /*TARGET_RISCV_DEBUG_H*/
