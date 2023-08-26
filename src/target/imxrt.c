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

#include <string.h>
#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"
#include "spi.h"
#include "sfdp.h"

/*
 * For detailed information on how this code works, see:
 * https://www.nxp.com/docs/en/nxp/data-sheets/IMXRT1060CEC.pdf
 * and (behind their login wall):
 * https://cache.nxp.com/secured/assets/documents/en/reference-manual/IMXRT1060RM.pdf?fileExt=.pdf
 */

#define IMXRT_SRC_BASE       UINT32_C(0x400f8000)
#define IMXRT_SRC_BOOT_MODE1 (IMXRT_SRC_BASE + 0x004U)
#define IMXRT_SRC_BOOT_MODE2 (IMXRT_SRC_BASE + 0x01cU)

#define IMXRT_OCRAM1_BASE  UINT32_C(0x20280000)
#define IMXRT_OCRAM1_SIZE  0x00080000U
#define IMXRT_OCRAM2_BASE  UINT32_C(0x20200000)
#define IMXRT_OCRAM2_SIZE  0x00080000U
#define IMXRT_FLEXSPI_BASE UINT32_C(0x60000000)

#define IMXRT_MPU_BASE UINT32_C(0xe000ed90)
#define IMXRT_MPU_CTRL (IMXRT_MPU_BASE + 0x04U)

#define IMXRT_CCM_ANALOG_BASE     UINT32_C(0x400d8000)
#define IMXRT_CCM_ANALOG_PLL3_PFD (IMXRT_CCM_ANALOG_BASE + 0x0f0U)

#define IMXRT_CCM_ANALOG_PLL_PFD0_FRAC_MASK 0xffffffc0

#define IMXRT_CCM_BASE  UINT32_C(0x400fc000)
#define IMXRT_CCM_CSCM1 (IMXRT_CCM_BASE + 0x01cU)
#define IMXRT_CCM_CCG6  (IMXRT_CCM_BASE + 0x080U)

#define IMXRT_CCM_CSCM1_FLEXSPI_CLK_SEL_MASK      0xfc7fffffU
#define IMXRT_CCM_CSCM1_FLEXSPI_CLK_SEL_PLL3_PFD0 0x03800000U
#define IMXRT_CCM_CCG6_FLEXSPI_CLK_MASK           0xfffff3ffU
#define IMXRT_CCM_CCG6_FLEXSPI_CLK_ENABLE         0x00000c00U

#define IMXRT_FLEXSPI1_BASE UINT32_C(0x402a8000)
/* We only carry definitions for FlexSPI1 Flash controller A1. */
#define IMXRT_FLEXSPI1_MOD_CTRL0             (IMXRT_FLEXSPI1_BASE + 0x000U)
#define IMXRT_FLEXSPI1_INT                   (IMXRT_FLEXSPI1_BASE + 0x014U)
#define IMXRT_FLEXSPI1_LUT_KEY               (IMXRT_FLEXSPI1_BASE + 0x018U)
#define IMXRT_FLEXSPI1_LUT_CTRL              (IMXRT_FLEXSPI1_BASE + 0x01cU)
#define IMXRT_FLEXSPI1_CTRL0                 (IMXRT_FLEXSPI1_BASE + 0x060U)
#define IMXRT_FLEXSPI1_CTRL1                 (IMXRT_FLEXSPI1_BASE + 0x070U)
#define IMXRT_FLEXSPI1_CTRL2                 (IMXRT_FLEXSPI1_BASE + 0x080U)
#define IMXRT_FLEXSPI1_PRG_CTRL0             (IMXRT_FLEXSPI1_BASE + 0x0a0U)
#define IMXRT_FLEXSPI1_PRG_CTRL1             (IMXRT_FLEXSPI1_BASE + 0x0a4U)
#define IMXRT_FLEXSPI1_PRG_CMD               (IMXRT_FLEXSPI1_BASE + 0x0b0U)
#define IMXRT_FLEXSPI1_PRG_READ_FIFO_CTRL    (IMXRT_FLEXSPI1_BASE + 0x0b8U)
#define IMXRT_FLEXSPI1_PRG_WRITE_FIFO_CTRL   (IMXRT_FLEXSPI1_BASE + 0x0bcU)
#define IMXRT_FLEXSPI1_STAT1                 (IMXRT_FLEXSPI1_BASE + 0x0e4U)
#define IMXRT_FLEXSPI1_PRG_WRITE_FIFO_STATUS (IMXRT_FLEXSPI1_BASE + 0x0f4U)
#define IMXRT_FLEXSPI1_PRG_READ_FIFO         (IMXRT_FLEXSPI1_BASE + 0x100U)
#define IMXRT_FLEXSPI1_PRG_WRITE_FIFO        (IMXRT_FLEXSPI1_BASE + 0x180U)
#define IMXRT_FLEXSPI1_LUT_BASE              (IMXRT_FLEXSPI1_BASE + 0x200U)

#define IMXRT_FLEXSPI1_MOD_CTRL0_SUSPEND          0x00000002U
#define IMXRT_FLEXSPI1_INT_PRG_CMD_DONE           0x00000001U
#define IMXRT_FLEXSPI1_INT_CMD_ERR                0x00000008U
#define IMXRT_FLEXSPI1_INT_READ_FIFO_FULL         0x00000020U
#define IMXRT_FLEXSPI1_INT_WRITE_FIFO_EMPTY       0x00000040U
#define IMXRT_FLEXSPI1_LUT_KEY_VALUE              0x5af05af0U
#define IMXRT_FLEXSPI1_LUT_CTRL_LOCK              0x00000001U
#define IMXRT_FLEXSPI1_LUT_CTRL_UNLOCK            0x00000002U
#define IMXRT_FLEXSPI1_CTRL1_CAS_MASK             0x00007800U
#define IMXRT_FLEXSPI1_CTRL1_CAS_SHIFT            11U
#define IMXRT_FLEXSPI1_PRG_LENGTH(x)              ((x)&0x0000ffffU)
#define IMXRT_FLEXSPI1_PRG_SEQ_INDEX(x)           (((x)&0xfU) << 16U)
#define IMXRT_FLEXSPI1_PRG_RUN                    0x00000001U
#define IMXRT_FLEXSPI1_PRG_FIFO_CTRL_CLR          0x00000001U
#define IMXRT_FLEXSPI1_PRG_FIFO_CTRL_WATERMARK(x) ((((((x) + 7U) >> 3U) - 1U) & 0xfU) << 2U)
#define IMXRT_FLEXSPI1_PRG_WRITE_FIFO_STATUS_FILL 0x000000ffU
#define IMXRT_FLEXSI_SLOT_OFFSET(x)               ((x)*16U)

#define IMXRT_FLEXSPI_LUT_OPCODE(x)   (((x)&0x3fU) << 2U)
#define IMXRT_FLEXSPI_LUT_MODE_SERIAL 0x0U
#define IMXRT_FLEXSPI_LUT_MODE_DUAL   0x1U
#define IMXRT_FLEXSPI_LUT_MODE_QUAD   0x2U
#define IMXRT_FLEXSPI_LUT_MODE_OCT    0x3U

#define IMXRT_FLEXSPI_LUT_OP_STOP         0x00U
#define IMXRT_FLEXSPI_LUT_OP_COMMAND      0x01U
#define IMXRT_FLEXSPI_LUT_OP_CADDR        0x03U
#define IMXRT_FLEXSPI_LUT_OP_RADDR        0x02U
#define IMXRT_FLEXSPI_LUT_OP_DUMMY_CYCLES 0x0cU
#define IMXRT_FLEXSPI_LUT_OP_READ         0x09U
#define IMXRT_FLEXSPI_LUT_OP_WRITE        0x08U

typedef enum imxrt_boot_src {
	BOOT_FLEX_SPI,
	boot_sd_card,
	boot_emmc,
	boot_slc_nand,
	boot_parallel_nor,
} imxrt_boot_src_e;

typedef struct imxrt_flexspi_lut_insn {
	uint8_t value;
	uint8_t opcode_mode;
} imxrt_flexspi_lut_insn_s;

typedef struct imxrt_priv {
	imxrt_boot_src_e boot_source;
	uint32_t mpu_state;
	uint32_t flexspi_lut_state;
	uint16_t flexspi_cached_commands[4];
	imxrt_flexspi_lut_insn_s flexspi_prg_seq_state[4][8];
} imxrt_priv_s;

static imxrt_boot_src_e imxrt_boot_source(uint32_t boot_cfg);
static bool imxrt_enter_flash_mode(target_s *target);
static bool imxrt_exit_flash_mode(target_s *target);
static uint8_t imxrt_spi_build_insn_sequence(target_s *target, uint16_t command, uint16_t length);
static void imxrt_spi_read(target_s *target, uint16_t command, target_addr_t address, void *buffer, size_t length);
static void imxrt_spi_write(
	target_s *target, uint16_t command, target_addr_t address, const void *buffer, size_t length);
static void imxrt_spi_run_command(target_s *target, uint16_t command, target_addr_t address);

bool imxrt_probe(target_s *const target)
{
	/* If the part number fails to match, instantly return. */
	if (target->part_id != 0x88cU)
		return false;

	/* XXX: Would really like to find some way to have a more positive identification on the part */

	imxrt_priv_s *priv = calloc(1, sizeof(imxrt_priv_s));
	if (!priv) { /* calloc faled: heap exhaustion */
		DEBUG_WARN("calloc: failed in %s\n", __func__);
		return false;
	}
	target->target_storage = priv;
	target->target_options |= CORTEXM_TOPT_INHIBIT_NRST;
	target->driver = "i.MXRT10xx";

#if defined(ENABLE_DEBUG) && (PC_HOSTED == 1 || defined(ESP_LOGD))
	const uint8_t boot_mode = (target_mem_read32(target, IMXRT_SRC_BOOT_MODE2) >> 24U) & 3U;
#endif
	DEBUG_TARGET("i.MXRT boot mode is %x\n", boot_mode);
	const uint32_t boot_cfg = target_mem_read32(target, IMXRT_SRC_BOOT_MODE1);
	DEBUG_TARGET("i.MXRT boot config is %08" PRIx32 "\n", boot_cfg);
	priv->boot_source = imxrt_boot_source(boot_cfg);
	switch (priv->boot_source) {
	case BOOT_FLEX_SPI:
		DEBUG_TARGET("-> booting from SPI Flash (FlexSPI)\n");
		break;
	case boot_sd_card:
		DEBUG_TARGET("-> booting from SD Card\n");
		break;
	case boot_emmc:
		DEBUG_TARGET("-> booting from eMMC via uSDHC\n");
		break;
	case boot_slc_nand:
		DEBUG_TARGET("-> booting from SLC NAND via SEMC\n");
		break;
	case boot_parallel_nor:
		DEBUG_TARGET("-> booting from parallel Flash (NOR) via SEMC\n");
		break;
	}

	/* Build the RAM map for the part */
	target_add_ram(target, IMXRT_OCRAM1_BASE, IMXRT_OCRAM1_SIZE);
	target_add_ram(target, IMXRT_OCRAM2_BASE, IMXRT_OCRAM2_SIZE);

	if (priv->boot_source == BOOT_FLEX_SPI) {
		/* Try to detect the Flash that should be attached */
		imxrt_enter_flash_mode(target);
		spi_flash_id_s flash_id;
		imxrt_spi_read(target, SPI_FLASH_CMD_READ_JEDEC_ID, 0, &flash_id, sizeof(flash_id));

		target->mass_erase = bmp_spi_mass_erase;
		target->enter_flash_mode = imxrt_enter_flash_mode;
		target->exit_flash_mode = imxrt_exit_flash_mode;

		/* If we read out valid Flash information, set up a region for it */
		if (flash_id.manufacturer != 0xffU && flash_id.type != 0xffU && flash_id.capacity != 0xffU) {
			const uint32_t capacity = 1U << flash_id.capacity;
			DEBUG_INFO("SPI Flash: mfr = %02x, type = %02x, capacity = %08" PRIx32 "\n", flash_id.manufacturer,
				flash_id.type, capacity);
			bmp_spi_add_flash(
				target, IMXRT_FLEXSPI_BASE, capacity, imxrt_spi_read, imxrt_spi_write, imxrt_spi_run_command);
		} else
			DEBUG_INFO("Flash identification failed\n");

		imxrt_exit_flash_mode(target);
	}

	return true;
}

static imxrt_boot_src_e imxrt_boot_source(const uint32_t boot_cfg)
{
	/*
	 * See table 9-9 in §9.6, pg210 of the reference manual for how all these constants and masks were derived.
	 * The bottom 8 bits of boot_cfg must be the value of register BOOT_CFG1.
	 * The boot source is the upper 4 bits of this register (BOOT_CFG1[7:4])
	 */
	const uint8_t boot_src = boot_cfg & 0xf0U;
	if (boot_src == 0x00U)
		return BOOT_FLEX_SPI;
	if ((boot_src & 0xc0U) == 0x40U)
		return boot_sd_card;
	if ((boot_src & 0xc0U) == 0x80U)
		return boot_emmc;
	if ((boot_src & 0xe0U) == 0x20U)
		return boot_slc_nand;
	if (boot_src == 0x10U)
		return boot_parallel_nor;
	/* The only upper bits combination not tested by this point is 0b11xx. */
	return BOOT_FLEX_SPI;
}

static bool imxrt_enter_flash_mode(target_s *const target)
{
	imxrt_priv_s *const priv = (imxrt_priv_s *)target->target_storage;
	/* Store MPU state and disable it to guarantee Flash control works */
	priv->mpu_state = target_mem_read32(target, IMXRT_MPU_CTRL);
	target_mem_write32(target, IMXRT_MPU_CTRL, 0);
	/* Start by stepping the clocks to ~50MHz and putting the controller in a known state */
	target_mem_write32(target, IMXRT_FLEXSPI1_MOD_CTRL0,
		target_mem_read32(target, IMXRT_FLEXSPI1_MOD_CTRL0) | IMXRT_FLEXSPI1_MOD_CTRL0_SUSPEND);
	target_mem_write32(
		target, IMXRT_CCM_CCG6, target_mem_read32(target, IMXRT_CCM_CCG6) & IMXRT_CCM_CCG6_FLEXSPI_CLK_MASK);
	target_mem_write32(target, IMXRT_CCM_CSCM1,
		(target_mem_read32(target, IMXRT_CCM_CSCM1) & IMXRT_CCM_CSCM1_FLEXSPI_CLK_SEL_MASK) |
			IMXRT_CCM_CSCM1_FLEXSPI_CLK_SEL_PLL3_PFD0);
	target_mem_write32(target, IMXRT_CCM_ANALOG_PLL3_PFD,
		(target_mem_read32(target, IMXRT_CCM_ANALOG_PLL3_PFD) & IMXRT_CCM_ANALOG_PLL_PFD0_FRAC_MASK) | 0x16U);
	target_mem_write32(
		target, IMXRT_CCM_CCG6, target_mem_read32(target, IMXRT_CCM_CCG6) | IMXRT_CCM_CCG6_FLEXSPI_CLK_ENABLE);
	target_mem_write32(target, IMXRT_FLEXSPI1_MOD_CTRL0,
		target_mem_read32(target, IMXRT_FLEXSPI1_MOD_CTRL0) & ~IMXRT_FLEXSPI1_MOD_CTRL0_SUSPEND);
	/* Clear all outstanding interrupts so we can consume their status cleanly */
	target_mem_write32(target, IMXRT_FLEXSPI1_INT, target_mem_read32(target, IMXRT_FLEXSPI1_INT));
	/* Tell the controller we want to use the entire read FIFO */
	target_mem_write32(target, IMXRT_FLEXSPI1_PRG_READ_FIFO_CTRL,
		IMXRT_FLEXSPI1_PRG_FIFO_CTRL_WATERMARK(128) | IMXRT_FLEXSPI1_PRG_FIFO_CTRL_CLR);
	/* Tell the controller we want to use the entire write FIFO */
	target_mem_write32(target, IMXRT_FLEXSPI1_PRG_WRITE_FIFO_CTRL,
		IMXRT_FLEXSPI1_PRG_FIFO_CTRL_WATERMARK(128) | IMXRT_FLEXSPI1_PRG_FIFO_CTRL_CLR);
	/* Then unlock the sequence LUT so we can use it to to run Flash commands */
	priv->flexspi_lut_state = target_mem_read32(target, IMXRT_FLEXSPI1_LUT_CTRL);
	if (priv->flexspi_lut_state != IMXRT_FLEXSPI1_LUT_CTRL_UNLOCK) {
		target_mem_write32(target, IMXRT_FLEXSPI1_LUT_KEY, IMXRT_FLEXSPI1_LUT_KEY_VALUE);
		target_mem_write32(target, IMXRT_FLEXSPI1_LUT_CTRL, IMXRT_FLEXSPI1_LUT_CTRL_UNLOCK);
	}
	/* Save the current state of the LUT the SPI Flash routines will use */
	target_mem_read(target, priv->flexspi_prg_seq_state, IMXRT_FLEXSPI1_LUT_BASE, sizeof(priv->flexspi_prg_seq_state));
	/* Clear the sequence microcode cache state */
	memset(priv->flexspi_cached_commands, 0, sizeof(priv->flexspi_cached_commands));
	return true;
}

static bool imxrt_exit_flash_mode(target_s *const target)
{
	const imxrt_priv_s *const priv = (imxrt_priv_s *)target->target_storage;
	/* To leave Flash mode, we do things in the opposite order to entering. */
	target_mem_write(target, IMXRT_FLEXSPI1_LUT_BASE, priv->flexspi_prg_seq_state, sizeof(priv->flexspi_prg_seq_state));
	if (priv->flexspi_lut_state != IMXRT_FLEXSPI1_LUT_CTRL_UNLOCK) {
		target_mem_write32(target, IMXRT_FLEXSPI1_LUT_KEY, IMXRT_FLEXSPI1_LUT_KEY_VALUE);
		target_mem_write32(target, IMXRT_FLEXSPI1_LUT_CTRL, priv->flexspi_lut_state);
	}
	/* But we don't bother restoring the clocks as the boot ROM'll do that if needed */
	target_mem_write32(target, IMXRT_MPU_CTRL, priv->mpu_state);
	return true;
}

static uint8_t imxrt_spi_build_insn_sequence(target_s *const target, const uint16_t command, const uint16_t length)
{
	imxrt_priv_s *const priv = (imxrt_priv_s *)target->target_storage;
	/* Check if the command is already cached */
	uint8_t slot = 0;
	for (; slot < 4; ++slot) {
		/* If we find a matching slot, fast return that slot */
		if (priv->flexspi_cached_commands[slot] == command)
			return slot;
		/* If it's an empty slot, use it */
		if (priv->flexspi_cached_commands[slot] == 0)
			break;
	}
	/* If all slots are filled, re-use the first */
	if (slot == 4)
		slot = 0;

	/* Build a new microcode sequence to run */
	imxrt_flexspi_lut_insn_s sequence[8] = {{0}};
	/* Start by writing the command opcode to the Flash */
	sequence[0].opcode_mode = IMXRT_FLEXSPI_LUT_OPCODE(IMXRT_FLEXSPI_LUT_OP_COMMAND) | IMXRT_FLEXSPI_LUT_MODE_SERIAL;
	sequence[0].value = command & SPI_FLASH_OPCODE_MASK;
	uint8_t offset = 1;
	/* Then, if the command has an address, perform the necessary addressing */
	if ((command & SPI_FLASH_OPCODE_MODE_MASK) == SPI_FLASH_OPCODE_3B_ADDR) {
		sequence[offset].opcode_mode =
			IMXRT_FLEXSPI_LUT_OPCODE(IMXRT_FLEXSPI_LUT_OP_RADDR) | IMXRT_FLEXSPI_LUT_MODE_SERIAL;
		sequence[offset++].value = 24U;
	}
	/* If the command uses dummy cycles, include the command for those */
	if (command & SPI_FLASH_DUMMY_MASK) {
		sequence[offset].opcode_mode =
			IMXRT_FLEXSPI_LUT_OPCODE(IMXRT_FLEXSPI_LUT_OP_DUMMY_CYCLES) | IMXRT_FLEXSPI_LUT_MODE_SERIAL;
		/* Convert bytes to bits in the process of building this */
		sequence[offset++].value = ((command & SPI_FLASH_DUMMY_MASK) >> SPI_FLASH_DUMMY_SHIFT) * 8U;
	}
	/* Now run the data phase based on the operation's data direction */
	if (length) {
		if (command & SPI_FLASH_DATA_OUT)
			sequence[offset].opcode_mode =
				IMXRT_FLEXSPI_LUT_OPCODE(IMXRT_FLEXSPI_LUT_OP_WRITE) | IMXRT_FLEXSPI_LUT_MODE_SERIAL;
		else
			sequence[offset].opcode_mode =
				IMXRT_FLEXSPI_LUT_OPCODE(IMXRT_FLEXSPI_LUT_OP_READ) | IMXRT_FLEXSPI_LUT_MODE_SERIAL;
		sequence[offset++].value = 0;
	}
	/* Because sequence gets 0 initalised above when it's declared, the STOP entry is already present */
	DEBUG_TARGET("Writing new instruction seqeunce to slot %u\n", slot);
	for (size_t idx = 0; idx < 8U; ++idx)
		DEBUG_TARGET("%zu: %02x %02x\n", idx, sequence[idx].opcode_mode, sequence[idx].value);

	/* Write the new sequence to the programmable sequence LUT */
	target_mem_write(target, IMXRT_FLEXSPI1_LUT_BASE + IMXRT_FLEXSI_SLOT_OFFSET(slot), sequence, sizeof(sequence));
	/* Update the cache information */
	priv->flexspi_cached_commands[slot] = command;
	return slot;
}

static void imxrt_spi_exec_sequence(
	target_s *const target, const uint8_t slot, const target_addr_t address, const uint16_t length)
{
	const imxrt_priv_s *const priv = (imxrt_priv_s *)target->target_storage;
	const uint32_t command = priv->flexspi_cached_commands[slot];
	/* Write the address, if any, to the sequence address register */
	if ((command & SPI_FLASH_OPCODE_MODE_MASK) == SPI_FLASH_OPCODE_3B_ADDR)
		target_mem_write32(target, IMXRT_FLEXSPI1_PRG_CTRL0, address);
	/* Write the command data length and instruction sequence index */
	target_mem_write32(
		target, IMXRT_FLEXSPI1_PRG_CTRL1, IMXRT_FLEXSPI1_PRG_SEQ_INDEX(slot) | IMXRT_FLEXSPI1_PRG_LENGTH(length));
	/* Execute the sequence */
	target_mem_write32(target, IMXRT_FLEXSPI1_PRG_CMD, IMXRT_FLEXSPI1_PRG_RUN);
}

static void imxrt_spi_wait_complete(target_s *const target)
{
	/* Wait till it finishes */
	while (!(target_mem_read32(target, IMXRT_FLEXSPI1_INT) & IMXRT_FLEXSPI1_INT_PRG_CMD_DONE))
		continue;
	/* Then clear the interrupt bit it sets. */
	target_mem_write32(target, IMXRT_FLEXSPI1_INT, IMXRT_FLEXSPI1_INT_PRG_CMD_DONE);
	/* Check if any errors occured */
	if (target_mem_read32(target, IMXRT_FLEXSPI1_INT) & IMXRT_FLEXSPI1_INT_CMD_ERR) {
#if defined(ENABLE_DEBUG) && PC_HOSTED == 1
		/* Read out the status code and display it */
		const uint32_t status = target_mem_read32(target, IMXRT_FLEXSPI1_STAT1);
		DEBUG_TARGET("Error executing sequence, offset %u, error code %u\n", (uint8_t)(status >> 16U) & 0xfU,
			(uint8_t)(status >> 24U) & 0xfU);
#endif
		/* Now clear the error (this clears the status field bits too) */
		target_mem_write32(target, IMXRT_FLEXSPI1_INT, IMXRT_FLEXSPI1_INT_CMD_ERR);
	}
}

/*
 * XXX: This routine cannot handle reads larger than 128 bytes.
 * This doesn't currently matter but may need fixing in the future
 */
static void imxrt_spi_read(target_s *const target, const uint16_t command, const target_addr_t address,
	void *const buffer, const size_t length)
{
	/* Configure the programmable sequence LUT and execute the read */
	const uint8_t slot = imxrt_spi_build_insn_sequence(target, command, length);
	imxrt_spi_exec_sequence(target, slot, address, length);
	imxrt_spi_wait_complete(target);
	/* Transfer the resulting data into the target buffer */
	uint32_t data[32];
	target_mem_read(target, data, IMXRT_FLEXSPI1_PRG_READ_FIFO, 128);
	memcpy(buffer, data, length);
	target_mem_write32(target, IMXRT_FLEXSPI1_INT, IMXRT_FLEXSPI1_INT_READ_FIFO_FULL);
}

static void imxrt_spi_write(target_s *const target, const uint16_t command, const target_addr_t address,
	const void *const buffer, const size_t length)
{
	/* Configure the programmable sequence LUT */
	const uint8_t slot = imxrt_spi_build_insn_sequence(target, command, length);
	imxrt_spi_exec_sequence(target, slot, address, length);
	/* Transfer the data into the transmit FIFO in blocks */
	for (uint16_t offset = 0; offset < length; offset += 128U) {
		while (
			target_mem_read32(target, IMXRT_FLEXSPI1_PRG_WRITE_FIFO_STATUS) & IMXRT_FLEXSPI1_PRG_WRITE_FIFO_STATUS_FILL)
			continue;
		const uint16_t amount = MIN(128U, (uint16_t)(length - offset));
		uint32_t data[32] = {0};
		memcpy(data, (const char *)buffer + offset, amount);
		target_mem_write(target, IMXRT_FLEXSPI1_PRG_WRITE_FIFO, data, (amount + 3U) & ~3U);
		/* Tell the controller we've filled the write FIFO */
		target_mem_write32(target, IMXRT_FLEXSPI1_INT, IMXRT_FLEXSPI1_INT_WRITE_FIFO_EMPTY);
	}
	/* Now wait for the FlexSPI controller to indicate the command completed we're done */
	imxrt_spi_wait_complete(target);
}

static void imxrt_spi_run_command(target_s *const target, const uint16_t command, const target_addr_t address)
{
	/* Configure the programmable sequence LUT */
	const uint8_t slot = imxrt_spi_build_insn_sequence(target, command, 0U);
	imxrt_spi_exec_sequence(target, slot, address, 0U);
	/* Now wait for the FlexSPI controller to indicate the command completed we're done */
	imxrt_spi_wait_complete(target);
}
