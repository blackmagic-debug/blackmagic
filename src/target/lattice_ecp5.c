/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2026 1BitSquared <info@1bitsquared.com>
 * Written by Aki Van Ness <aki@lethalbit.net>
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
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLEs
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "general.h"
#include "buffer_utils.h"
#include "jtag_scan.h"
#include "jtagtap.h"
#include "spi.h"
#include "sfdp.h"
#include "gdb_packet.h"
#include "lattice_common.h"
#include "lattice_ecp5.h"

#define REGISTER_FIELD(reg, mask, shift) (((reg) >> (shift)) & (mask))

#define ECP5_STATUS_TRANSPARENT_MASK  1U
#define ECP5_STATUS_TRANSPARENT_SHIFT 0U
#define ECP5_STATUS_TRANSPARENT(reg)  REGISTER_FIELD(reg, ECP5_STATUS_TRANSPARENT_MASK, ECP5_STATUS_TRANSPARENT_SHIFT)
#define ECP5_STATUS_TARGET_MASK       0x7U
#define ECP5_STATUS_TARGET_SHIFT      1U
#define ECP5_STATUS_TARGET(reg)       REGISTER_FIELD(reg, ECP5_STATUS_TARGET_MASK, ECP5_STATUS_TARGET_SHIFT)
#define ECP5_STATUS_JTAG_ACTIVE_MASK  1U
#define ECP5_STATUS_JTAG_ACTIVE_SHIFT 4U
#define ECP5_STATUS_JTAG_ACTIVE(reg)  REGISTER_FIELD(reg, ECP5_STATUS_JTAG_ACTIVE_MASK, ECP5_STATUS_JTAG_ACTIVE_SHIFT)
#define ECP5_STATUS_PASSWORD_PROTECTED_MASK  1U
#define ECP5_STATUS_PASSWORD_PROTECTED_SHIFT 5U
#define ECP5_STATUS_PASSWORD_PROTECTED(reg) \
	REGISTER_FIELD(reg, ECP5_STATUS_PASSWORD_PROTECTED_MASK, ECP5_STATUS_PASSWORD_PROTECTED_SHIFT)
#define ECP5_STATUS_INTERNAL0_MASK        1U
#define ECP5_STATUS_INTERNAL0_SHIFT       6U
#define ECP5_STATUS_INTERNAL0(reg)        REGISTER_FIELD(reg, ECP5_STATUS_INTERNAL0_MASK, ECP5_STATUS_INTERNAL0_SHIFT)
#define ECP5_STATUS_DECRYPT_ENABLED_MASK  1U
#define ECP5_STATUS_DECRYPT_ENABLED_SHIFT 7U
#define ECP5_STATUS_DECRYPT_ENABLED(reg) \
	REGISTER_FIELD(reg, ECP5_STATUS_DECRYPT_ENABLED_MASK, ECP5_STATUS_DECRYPT_ENABLED_SHIFT)
#define ECP5_STATUS_DONE_MASK           1U
#define ECP5_STATUS_DONE_SHIFT          8U
#define ECP5_STATUS_DONE(reg)           REGISTER_FIELD(reg, ECP5_STATUS_DONE_MASK, ECP5_STATUS_DONE_SHIFT)
#define ECP5_STATUS_ISC_ENABLED_MASK    1U
#define ECP5_STATUS_ISC_ENABLED_SHIFT   9U
#define ECP5_STATUS_ISC_ENABLED(reg)    REGISTER_FIELD(reg, ECP5_STATUS_ISC_ENABLED_MASK, ECP5_STATUS_ISC_ENABLED_SHIFT)
#define ECP5_STATUS_WRITE_ENABLED_MASK  1U
#define ECP5_STATUS_WRITE_ENABLED_SHIFT 10U
#define ECP5_STATUS_WRITE_ENABLED(reg) \
	REGISTER_FIELD(reg, ECP5_STATUS_WRITE_ENABLED_MASK, ECP5_STATUS_WRITE_ENABLED_SHIFT)
#define ECP5_STATUS_READ_ENABLED_MASK  1U
#define ECP5_STATUS_READ_ENABLED_SHIFT 11U
#define ECP5_STATUS_READ_ENABLED(reg) REGISTER_FIELD(reg, ECP5_STATUS_READ_ENABLED_MASK, ECP5_STATUS_READ_ENABLED_SHIFT)
#define ECP5_STATUS_BUSY_MASK         1U
#define ECP5_STATUS_BUSY_SHIFT        12U
#define ECP5_STATUS_BUSY(reg)         REGISTER_FIELD(reg, ECP5_STATUS_BUSY_MASK, ECP5_STATUS_BUSY_SHIFT)
#define ECP5_STATUS_FAILURE_MASK      1U
#define ECP5_STATUS_FAILURE_SHIFT     13U
#define ECP5_STATUS_FAILURE(reg)      REGISTER_FIELD(reg, ECP5_STATUS_FAILURE_MASK, ECP5_STATUS_FAILURE_SHIFT)
#define ECP5_STATUS_FEATURES_OTP_MASK 1U
#define ECP5_STATUS_FEATURES_OTP_SHIFT 14U
#define ECP5_STATUS_FEATURES_OTP(reg) REGISTER_FIELD(reg, ECP5_STATUS_FEATURES_OTP_MASK, ECP5_STATUS_FEATURES_OTP_SHIFT)
#define ECP5_STATUS_ENCRYPTED_ONLY_MASK  1U
#define ECP5_STATUS_ENCRYPTED_ONLY_SHIFT 15U
#define ECP5_STATUS_ENCRYPTED_ONLY(reg) \
	REGISTER_FIELD(reg, ECP5_STATUS_ENCRYPTED_ONLY_MASK, ECP5_STATUS_ENCRYPTED_ONLY_SHIFT)
#define ECP5_STATUS_PASSWORD_ENABLED_MASK  1U
#define ECP5_STATUS_PASSWORD_ENABLED_SHIFT 16U
#define ECP5_STATUS_PASSWORD_ENABLED(reg) \
	REGISTER_FIELD(reg, ECP5_STATUS_PASSWORD_ENABLED_MASK, ECP5_STATUS_PASSWORD_ENABLED_SHIFT)
#define ECP5_STATUS_INTERNAL1_MASK         0x7U
#define ECP5_STATUS_INTERNAL1_SHIFT        17U
#define ECP5_STATUS_INTERNAL1(reg)         REGISTER_FIELD(reg, ECP5_STATUS_INTERNAL1_MASK, ECP5_STATUS_INTERNAL1_SHIFT)
#define ECP5_STATUS_ENCRYPT_PREAMBLE_MASK  1U
#define ECP5_STATUS_ENCRYPT_PREAMBLE_SHIFT 20U
#define ECP5_STATUS_ENCRYPT_PREAMBLE(reg) \
	REGISTER_FIELD(reg, ECP5_STATUS_ENCRYPT_PREAMBLE_MASK, ECP5_STATUS_ENCRYPT_PREAMBLE_SHIFT)
#define ECP5_STATUS_STANDARD_PREAMBLE_MASK  1U
#define ECP5_STATUS_STANDARD_PREAMBLE_SHIFT 21U
#define ECP5_STATUS_STANDARD_PREAMBLE(reg) \
	REGISTER_FIELD(reg, ECP5_STATUS_STANDARD_PREAMBLE_MASK, ECP5_STATUS_STANDARD_PREAMBLE_SHIFT)
#define ECP5_STATUS_PRIMARY_CFG_FAIL_MASK  1U
#define ECP5_STATUS_PRIMARY_CFG_FAIL_SHIFT 22U
#define ECP5_STATUS_PRIMARY_CFG_FAIL(reg) \
	REGISTER_FIELD(reg, ECP5_STATUS_PRIMARY_CFG_FAIL_MASK, ECP5_STATUS_PRIMARY_CFG_FAIL_SHIFT)
#define ECP5_STATUS_BSE_ERROR_MASK        0x7U
#define ECP5_STATUS_BSE_ERROR_SHIFT       23U
#define ECP5_STATUS_BSE_ERROR(reg)        REGISTER_FIELD(reg, ECP5_STATUS_BSE_ERROR_MASK, ECP5_STATUS_BSE_ERROR_SHIFT)
#define ECP5_STATUS_EXEC_ERROR_MASK       1U
#define ECP5_STATUS_EXEC_ERROR_SHIFT      26U
#define ECP5_STATUS_EXEC_ERROR(reg)       REGISTER_FIELD(reg, ECP5_STATUS_EXEC_ERROR_MASK, ECP5_STATUS_EXEC_ERROR_SHIFT)
#define ECP5_STATUS_ID_ERROR_MASK         1U
#define ECP5_STATUS_ID_ERROR_SHIFT        27U
#define ECP5_STATUS_ID_ERROR(reg)         REGISTER_FIELD(reg, ECP5_STATUS_ID_ERROR_MASK, ECP5_STATUS_ID_ERROR_SHIFT)
#define ECP5_STATUS_INVALID_COMMAND_MASK  1U
#define ECP5_STATUS_INVALID_COMMAND_SHIFT 28U
#define ECP5_STATUS_INVALID_COMMAND(reg) \
	REGISTER_FIELD(reg, ECP5_STATUS_INVALID_COMMAND_MASK, ECP5_STATUS_INVALID_COMMAND_SHIFT)
#define ECP5_STATUS_SED_ERROR_MASK    1U
#define ECP5_STATUS_SED_ERROR_SHIFT   29U
#define ECP5_STATUS_SED_ERROR(reg)    REGISTER_FIELD(reg, ECP5_STATUS_SED_ERROR_MASK, ECP5_STATUS_SED_ERROR_SHIFT)
#define ECP5_STATUS_BYPASS_MODE_MASK  1U
#define ECP5_STATUS_BYPASS_MODE_SHIFT 30U
#define ECP5_STATUS_BYPASS_MODE(reg)  REGISTER_FIELD(reg, ECP5_STATUS_BYPASS_MODE_MASK, ECP5_STATUS_BYPASS_MODE_SHIFT)
#define ECP5_STATUS_FLOW_MODE_MASK    1U
#define ECP5_STATUS_FLOW_MODE_SHIFT   31U
#define ECP5_STATUS_FLOW_MODE(reg)    REGISTER_FIELD(reg, ECP5_STATUS_FLOW_MODE_MASK, ECP5_STATUS_FLOW_MODE_SHIFT)

#define ECP5_CTRL0_MSPI_CLK_MASK      0x1FU
#define ECP5_CTRL0_MSPI_CLK_SHIFT     0U
#define ECP5_CTRL0_MSPI_CLK(reg)      REGISTER_FIELD(reg, ECP5_CTRL0_MSPI_CLK_MASK, ECP5_CTRL0_MSPI_CLK_SHIFT)
#define ECP5_CTRL0_SLEW_MASK          0x3U
#define ECP5_CTRL0_SLEW_SHIFT         6U
#define ECP5_CTRL0_SLEW(reg)          REGISTER_FIELD(reg, ECP5_CTRL0_SLEW_MASK, ECP5_CTRL0_SLEW_SHIFT)
#define ECP5_CTRL0_RSVD0_MASK         0x1FFU
#define ECP5_CTRL0_RSVD0_SHIFT        8U
#define ECP5_CTRL0_RSVD0(reg)         REGISTER_FIELD(reg, ECP5_CTRL0_RSVD0_MASK, ECP5_CTRL0_RSVD0_SHIFT)
#define ECP5_CTRL0_PDONE_MASK         0x3U
#define ECP5_CTRL0_PDONE_SHIFT        18U
#define ECP5_CTRL0_PDONE(reg)         REGISTER_FIELD(reg, ECP5_CTRL0_PDONE_MASK, ECP5_CTRL0_PDONE_SHIFT)
#define ECP5_CTRL0_RSVD1_MASK         0x80U
#define ECP5_CTRL0_RSVD1_SHIFT        20U
#define ECP5_CTRL0_RSVD1(reg)         REGISTER_FIELD(reg, ECP5_CTRL0_RSVD1_MASK, ECP5_CTRL0_RSVD1_SHIFT)
#define ECP5_CTRL0_NDR_MASK           1U
#define ECP5_CTRL0_NDR_SHIFT          28U
#define ECP5_CTRL0_NDR(reg)           REGISTER_FIELD(reg, ECP5_CTRL0_NDR_MASK, ECP5_CTRL0_NDR_SHIFT)
#define ECP5_CTRL0_WAKEUP_TRANS_MASK  1U
#define ECP5_CTRL0_WAKEUP_TRANS_SHIFT 29U
#define ECP5_CTRL0_WAKEUP_TRANS(reg)  REGISTER_FIELD(reg, ECP5_CTRL0_WAKEUP_TRANS_MASK, ECP5_CTRL0_WAKEUP_TRANS_SHIFT)
#define ECP5_CTRL0_RSVD2_MASK         0x3U
#define ECP5_CTRL0_RSVD2_SHIFT        30U
#define ECP5_CTRL0_RSVD2(reg)         REGISTER_FIELD(reg, ECP5_CTRL0_RSVD2_MASK, ECP5_CTRL0_RSVD2_SHIFT)

#define ECP5_SRAM_BASE  0x00000000U
#define ECP5_FLASH_BASE 0x04000000U

static const uint8_t ecp5_spi_unlock[2U] = {0xfeU, 0x68U};

typedef struct ecp5_ctx {
	uint8_t device_index;
	uint8_t *cmd_buffer;
	uint8_t *data_buffer;
	uint16_t buffer_len;
} ecp5_ctx_s;

typedef struct ecp5_device {
	uint32_t idcode;
	uint32_t bitstream_len;
	uint8_t frame_len;
} ecp5_device_s;

static const ecp5_device_s devices[] = {
	// LEF5-12
	{.idcode = 0x21111043U, .bitstream_len = 677500U, .frame_len = 74U},
	// LEF5-25
	{.idcode = 0x41111043U, .bitstream_len = 677500U, .frame_len = 74U},
	// LEF5UM-25
	{.idcode = 0x01111043U, .bitstream_len = 677500U, .frame_len = 74U},
	// LEF5UM5G-25
	{.idcode = 0x81111043U, .bitstream_len = 677500U, .frame_len = 74U},
	// LEF5-45
	{.idcode = 0x41112043U, .bitstream_len = 1217500U, .frame_len = 106U},
	// LEF5UM-45
	{.idcode = 0x01112043U, .bitstream_len = 1217500U, .frame_len = 106U},
	// LEF5UM5G-45
	{.idcode = 0x81112043U, .bitstream_len = 1217500U, .frame_len = 106U},
	// LEF5-85
	{.idcode = 0x41113043U, .bitstream_len = 2293750U, .frame_len = 142U},
	// LEF5UM-85
	{.idcode = 0x01113043U, .bitstream_len = 2293750U, .frame_len = 142U},
	// LEF5UM5G-85
	{.idcode = 0x81113043U, .bitstream_len = 2293750U, .frame_len = 142U},
};

static bool ecp5_read_reg_status(target_s *target, int argc, const char **argv);
static bool ecp5_read_reg_control(target_s *target, int argc, const char **argv);
static bool ecp5_read_reg_usercode(target_s *target, int argc, const char **argv);

static const command_s ecp5_cmd_list[] = {
	{"status", ecp5_read_reg_status, "Read FPGA status register"},
	{"control", ecp5_read_reg_control, "Read FPGA control register"},
	{"usercode", ecp5_read_reg_usercode, "Read FPGA USERCODE register"},
	{NULL, NULL, NULL},
};

static void ecp5_free_ctx(void *priv);
static uint32_t ecp5_read32(uint8_t dev_index, uint8_t cmd);

static bool ecp5_attach(target_s *target);
static bool ecp5_check_error(target_s *target);
static void ecp5_reset(target_s *target);
static bool ecp5_enter_flash(target_s *target);
static bool ecp5_exit_flash(target_s *target);

static bool ecp5_spi_flash_prepare(target_flash_s *flash);
static bool ecp5_spi_flash_done(target_flash_s *flash);
static void ecp5_spi_read(target_s *target, uint16_t command, target_addr_t address, void *buffer, size_t length);
static void ecp5_spi_write(
	target_s *target, uint16_t command, target_addr_t address, const void *buffer, size_t length);
static void ecp5_spi_run_command(target_s *target, uint16_t command, target_addr_t address);
static void ecp5_spi_xfr_jtag(target_s *target, uint8_t *data_out, const uint8_t *data_in, size_t length);

static bool ecp5_sram_done(target_flash_s *flash);
static bool ecp5_sram_erase(target_flash_s *flash, target_addr_t addr, size_t length);
static bool ecp5_sram_write(target_flash_s *flash, target_addr_t dest, const void *buffer, size_t length);

void lattice_ecp5_handler(const uint8_t dev_index)
{
	target_s *target = target_new();
	target->driver = "Lattice";
	target->core = "ECP5";
	target->priv = calloc(1U, sizeof(ecp5_ctx_s));

	if (!target->priv) {
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return;
	}

	target->priv_free = ecp5_free_ctx;
	target->attach = ecp5_attach;
	target->check_error = ecp5_check_error;
	target->reset = ecp5_reset;
	target->enter_flash_mode = ecp5_enter_flash;
	target->exit_flash_mode = ecp5_exit_flash;
	target_add_commands(target, ecp5_cmd_list, target->driver);

	for (size_t dev = 0U; dev < ARRAY_LENGTH(devices); ++dev) {
		if (devices[dev].idcode == jtag_devs[dev_index].jd_idcode) {
			target_flash_s *flash = calloc(1U, sizeof(*flash));

			if (!flash) {
				DEBUG_ERROR("calloc: %s: failed to allocate flash\n", __func__);
				return;
			}

			flash->length = devices[dev].bitstream_len;
			flash->start = ECP5_SRAM_BASE;
			flash->blocksize = flash->length;
			flash->writesize = flash->length; // devices[dev].frame_len;
			flash->done = ecp5_sram_done;
			flash->erase = ecp5_sram_erase;
			flash->write = ecp5_sram_write;

			target_add_flash(target, flash);
		}
	}

	ecp5_ctx_s *ctx = target->priv;
	ctx->device_index = dev_index;
	// Setup the command/data buffers
	ctx->buffer_len = 4100U;
	ctx->data_buffer = calloc(1, ctx->buffer_len);
	ctx->cmd_buffer = calloc(1, ctx->buffer_len);

	if (!ctx->data_buffer || ctx->cmd_buffer)
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
}

static void ecp5_free_ctx(void *const priv)
{
	const ecp5_ctx_s *const ctx = (ecp5_ctx_s *)priv;

	free(ctx->cmd_buffer);
	free(ctx->data_buffer);
	free(priv);
}

static uint32_t ecp5_read32(const uint8_t dev_index, const uint8_t cmd)
{
	uint8_t data[4U];
	jtag_dev_write_ir(dev_index, cmd);
	jtag_dev_shift_dr(dev_index, data, NULL, 32U);
	return read_le4(data, 0U);
}

static bool ecp5_attach(target_s *const target)
{
	const ecp5_ctx_s *const ctx = (ecp5_ctx_s *)target->priv;
	const uint32_t status = ecp5_read32(ctx->device_index, CMD_LSC_READ_STATUS);

	if (ECP5_STATUS_ENCRYPTED_ONLY(status))
		DEBUG_WARN("This FPGA only accepts encrypted bitstreams!\n");

	if (ECP5_STATUS_DONE(status))
		DEBUG_INFO("FPGA is configured\n");

	ecp5_enter_flash(target);

	// Create a synthetic flash object so we can shell out to `spi_flash_prepare` to enter transparent SPI mode
	target_flash_s flash;
	flash.t = target;
	ecp5_spi_flash_prepare(&flash);

	spi_flash_id_s flash_id;
	ecp5_spi_read(target, SPI_FLASH_CMD_READ_JEDEC_ID, 0U, &flash_id, sizeof(flash_id));

	/* If we read out valid Flash information, set up a region for it */
	if (flash_id.manufacturer != 0xffU && flash_id.type != 0xffU && flash_id.capacity != 0xffU) {
		const uint32_t capacity = 1U << flash_id.capacity;
		DEBUG_INFO("SPI Flash: mfr = %02" PRIx8 ", type = %02" PRIx8 ", capacity = %08" PRIx32 "\n",
			flash_id.manufacturer, flash_id.type, capacity);
		spi_flash_s *const spi_flash =
			bmp_spi_add_flash(target, ECP5_FLASH_BASE, capacity, ecp5_spi_read, ecp5_spi_write, ecp5_spi_run_command);
		target_flash_s *const target_flash = &spi_flash->flash;
		target_flash->prepare = ecp5_spi_flash_prepare;
		target_flash->done = ecp5_spi_flash_done;
	} else
		DEBUG_INFO("Flash identification failed\n");

	ecp5_exit_flash(target);

	return true;
}

static bool ecp5_check_error(target_s *const target)
{
	const ecp5_ctx_s *const ctx = (ecp5_ctx_s *)target->priv;
	const uint32_t status = ecp5_read32(ctx->device_index, CMD_LSC_READ_STATUS);

	return !(ECP5_STATUS_BSE_ERROR(status) || ECP5_STATUS_ID_ERROR(status) || ECP5_STATUS_EXEC_ERROR(status) ||
		ECP5_STATUS_PRIMARY_CFG_FAIL(status) || ECP5_STATUS_FAILURE(status) || ECP5_STATUS_INVALID_COMMAND(status));
}

static void ecp5_reset(target_s *const target)
{
	// NOTE: BMDA doesn't handle flash finalization properly when in CLI mode, so we need to do so manually
	for (target_flash_s *flash = target->flash; flash != NULL; flash = flash->next) {
		if (flash->operation != FLASH_OPERATION_NONE)
			flash->done(flash);
	}
}

static bool ecp5_enter_flash(target_s *const target)
{
	const ecp5_ctx_s *const ctx = (ecp5_ctx_s *)target->priv;
	const uint8_t dev_index = ctx->device_index;

	// Enter Offline configuration mode
	jtag_dev_write_ir(dev_index, CMD_ISC_ENABLE);
	jtag_proc.jtagtap_cycle(false, false, 50U);
	// Erase configuration SRAM
	jtag_dev_write_ir(dev_index, CMD_ISC_ERASE);
	jtag_proc.jtagtap_cycle(false, false, 50U);
	// Reset the CRC
	jtag_dev_write_ir(dev_index, CMD_LSC_READ_CRC);
	jtag_proc.jtagtap_cycle(false, false, 50U);

	// Wait for the configuration to be erased
	while (ECP5_STATUS_BUSY(ecp5_read32(dev_index, CMD_LSC_READ_STATUS)))
		platform_delay(100U);

	return true;
}

static bool ecp5_exit_flash(target_s *const target)
{
	const ecp5_ctx_s *const ctx = (ecp5_ctx_s *)target->priv;
	const uint8_t dev_index = ctx->device_index;

	const uint32_t status = ecp5_read32(dev_index, CMD_LSC_READ_STATUS);

	const uint32_t result =
		(ECP5_STATUS_BSE_ERROR(status) || ECP5_STATUS_ID_ERROR(status) || ECP5_STATUS_EXEC_ERROR(status) ||
			ECP5_STATUS_PRIMARY_CFG_FAIL(status) || ECP5_STATUS_FAILURE(status) || ECP5_STATUS_INVALID_COMMAND(status));

	if (result != 0) {
		DEBUG_ERROR("Bitstream programming failed: %" PRIu32 "\n", result);
	}

	return result != 0;
}

static bool ecp5_spi_flash_prepare(target_flash_s *flash)
{
	const ecp5_ctx_s *const ctx = (ecp5_ctx_s *)flash->t->priv;
	const uint8_t dev_index = ctx->device_index;

	// Exit Offline configuration mode
	jtag_dev_write_ir(dev_index, CMD_ISC_DISABLE);
	jtag_proc.jtagtap_cycle(false, false, 50U);

	// Enter background SPI programming mode
	jtag_dev_write_ir(dev_index, CMD_LSC_BACKGROUND_SPI);
	jtag_dev_shift_dr(dev_index, NULL, ecp5_spi_unlock, 16U);
	jtag_proc.jtagtap_cycle(false, false, 50U);

	return true;
}

static bool ecp5_spi_flash_done(target_flash_s *flash)
{
	target_s *const target = flash->t;
	ecp5_ctx_s *const ctx = (ecp5_ctx_s *)target->priv;
	const uint8_t dev_index = ctx->device_index;

	/*
	 * The ECP5 doesn't have any way to exit SPI background mode, so we need to reset the whole
	 * device.
	 */
	jtag_dev_write_ir(dev_index, CMD_LSC_REFRESH);
	jtag_proc.jtagtap_cycle(false, false, 50U);

	return ecp5_check_error(target);
}

static void ecp5_spi_read(target_s *const target, const uint16_t command, const target_addr_t address,
	void *const buffer, const size_t length)
{
	const ecp5_ctx_s *ctx = (ecp5_ctx_s *)target->priv;
	size_t offset = 0U;
	ctx->cmd_buffer[offset++] = SPI_FLASH_OPCODE(command);
	if ((command & SPI_FLASH_OPCODE_MODE_MASK) == SPI_FLASH_OPCODE_3B_ADDR) {
		ctx->cmd_buffer[offset++] = (address & 0xff0000U) >> 16U;
		ctx->cmd_buffer[offset++] = (address & 0x00ff00U) >> 8U;
		ctx->cmd_buffer[offset++] = address & 0x0000ffU;
	}

	const size_t dummy_len = (command & SPI_FLASH_DUMMY_MASK) >> SPI_FLASH_DUMMY_SHIFT;
	for (size_t dummy = 0U; dummy < dummy_len; ++dummy)
		ctx->cmd_buffer[offset++] = 0U;

	memset(ctx->cmd_buffer + offset, 0, length);

	ecp5_spi_xfr_jtag(target, ctx->data_buffer, ctx->cmd_buffer, length + offset);
	memcpy(buffer, ctx->data_buffer + offset, length);
}

static void ecp5_spi_write(target_s *const target, const uint16_t command, const target_addr_t address,
	const void *const buffer, const size_t length)
{
	const ecp5_ctx_s *ctx = (ecp5_ctx_s *)target->priv;
	size_t offset = 0U;
	ctx->cmd_buffer[offset++] = SPI_FLASH_OPCODE(command);
	if ((command & SPI_FLASH_OPCODE_MODE_MASK) == SPI_FLASH_OPCODE_3B_ADDR) {
		ctx->cmd_buffer[offset++] = (address & 0xff0000U) >> 16U;
		ctx->cmd_buffer[offset++] = (address & 0x00ff00U) >> 8U;
		ctx->cmd_buffer[offset++] = address & 0x0000ffU;
	}

	const size_t dummy_len = (command & SPI_FLASH_DUMMY_MASK) >> SPI_FLASH_DUMMY_SHIFT;
	for (size_t dummy = 0U; dummy < dummy_len; ++dummy)
		ctx->cmd_buffer[offset++] = 0U;

	// Guard in the case buffer is `NULL`
	if (buffer)
		memcpy(ctx->cmd_buffer + offset, buffer, length);

	ecp5_spi_xfr_jtag(target, NULL, ctx->cmd_buffer, length + offset);
}

static void ecp5_spi_run_command(target_s *const target, const uint16_t command, const target_addr_t address)
{
	ecp5_spi_write(target, command, address, NULL, 0UL);
}

static void ecp5_spi_xfr_jtag(
	target_s *const target, uint8_t *const data_out, const uint8_t *const data_in, const size_t length)
{
	const ecp5_ctx_s *const ctx = (ecp5_ctx_s *)target->priv;
	const uint8_t dev_index = ctx->device_index;
	const jtag_dev_s *const device = &jtag_devs[dev_index];

	/* Switch into Shift-DR */
	jtagtap_shift_dr();
	/* Now we're in Shift-DR, clock out 1's till we hit the right device in the chain */
	jtag_proc.jtagtap_tdi_seq(false, ones, device->dr_prescan);

	uint8_t tap_out;
	for (size_t idx = 0U; idx < length; ++idx) {
		const uint8_t tap_in = reverse_bits8(data_in[idx]);
		jtag_proc.jtagtap_tdi_tdo_seq(&tap_out, (idx + 1U) == length && !device->dr_postscan, &tap_in, 8U);
		if (data_out)
			data_out[idx] = reverse_bits8(tap_out);
	}

	DEBUG_PROTO("%s: %" PRIu32 " cycles\n", __func__, (uint32_t)length);

	/* Make sure we're in Exit1-DR having clocked out 1's for any more devices on the chain */
	jtag_proc.jtagtap_tdi_seq(true, ones, device->dr_postscan);
	/* Now go through Update-DR and back to Idle */
	jtagtap_return_idle(1U);
}

static bool ecp5_sram_done(target_flash_s *const flash)
{
	target_s *const target = flash->t;
	ecp5_ctx_s *const ctx = (ecp5_ctx_s *)target->priv;
	const uint8_t dev_index = ctx->device_index;

	if (flash->operation == FLASH_OPERATION_WRITE) {
		// Exit configuration mode
		jtag_dev_write_ir(dev_index, CMD_ISC_DISABLE);
		jtag_proc.jtagtap_cycle(false, false, 50U);
	}

	return ecp5_check_error(target);
}

static bool ecp5_sram_erase(target_flash_s *const flash, const target_addr_t addr, const size_t length)
{
	(void)addr;
	(void)length;

	const target_s *const target = flash->t;
	const ecp5_ctx_s *const ctx = (ecp5_ctx_s *)target->priv;
	const uint8_t dev_index = ctx->device_index;

	// Erase configuration SRAM
	jtag_dev_write_ir(dev_index, CMD_ISC_ERASE);
	jtag_proc.jtagtap_cycle(false, false, 50U);

	// Wait for the configuration to be erased
	while (ECP5_STATUS_BUSY(ecp5_read32(dev_index, CMD_LSC_READ_STATUS)))
		platform_delay(100U);

	// Reset the configuration CRC SRAM
	jtag_dev_write_ir(dev_index, CMD_LSC_RESET_CRC);
	jtag_proc.jtagtap_cycle(false, false, 50U);

	return true;
}

static bool ecp5_sram_write(
	target_flash_s *const flash, const target_addr_t dest, const void *const buffer, const size_t length)
{
	(void)dest;

	const target_s *const target = flash->t;
	const ecp5_ctx_s *const ctx = (ecp5_ctx_s *)target->priv;
	const uint8_t dev_index = ctx->device_index;
	const jtag_dev_s *const device = &jtag_devs[dev_index];

	// Write bitstream to SRAM
	jtag_dev_write_ir(dev_index, CMD_LSC_BITSTREAM_BURST);
	jtag_proc.jtagtap_cycle(false, false, 50U);

	/* Switch into Shift-DR */
	jtagtap_shift_dr();
	/* Now we're in Shift-DR, clock out 1's till we hit the right device in the chain */
	jtag_proc.jtagtap_tdi_seq(false, ones, device->dr_prescan);

	uint8_t tap_out;
	const uint8_t *const data_in = buffer;
	for (size_t idx = 0U; idx < length; ++idx) {
		const uint8_t tap_in = reverse_bits8(data_in[idx]);
		jtag_proc.jtagtap_tdi_tdo_seq(&tap_out, (idx + 1U) == length && !device->dr_postscan, &tap_in, 8U);

		if (idx % 8192U) {
			DEBUG_TARGET("%s: %" PRIu32 "/%" PRIu32 " bytes written\n", __func__, idx, length);
		}
	}

	/* Make sure we're in Exit1-DR having clocked out 1's for any more devices on the chain */
	jtag_proc.jtagtap_tdi_seq(true, ones, device->dr_postscan);
	/* Now go through Update-DR and back to Idle */
	jtagtap_return_idle(1U);

	return true;
}

static bool ecp5_read_reg_status(target_s *target, int argc, const char **argv)
{
	(void)argc;
	(void)argv;

	const ecp5_ctx_s *const ctx = (ecp5_ctx_s *)target->priv;
	const uint32_t status_register = ecp5_read32(ctx->device_index, CMD_LSC_READ_STATUS);

#if CONFIG_LATTICE_ECP5_DECODE
	gdb_outf("Transparent: %" PRIu32 "\n", ECP5_STATUS_TRANSPARENT(status_register));
	gdb_outf("Configuration Target: %s\n", ECP5_STATUS_TARGET(status_register) ? "eFUSE" : "SRAM");
	gdb_outf("JTAG Active: %" PRIu32 "\n", ECP5_STATUS_JTAG_ACTIVE(status_register));
	gdb_outf("Password Protected: %" PRIu32 "\n", ECP5_STATUS_PASSWORD_PROTECTED(status_register));
	gdb_outf("Internal: %" PRIu32 "\n", ECP5_STATUS_INTERNAL0(status_register));
	gdb_outf("Encryption Enabled: %" PRIu32 "\n", ECP5_STATUS_DECRYPT_ENABLED(status_register));
	gdb_outf("Configuration Success: %" PRIu32 "\n", ECP5_STATUS_DONE(status_register));
	gdb_outf("ISC Enabled: %" PRIu32 "\n", ECP5_STATUS_ISC_ENABLED(status_register));
	gdb_outf("Configuration Writable: %" PRIu32 "\n", ECP5_STATUS_WRITE_ENABLED(status_register));
	gdb_outf("Configuration Readable: %" PRIu32 "\n", ECP5_STATUS_READ_ENABLED(status_register));
	gdb_outf("Configuration Busy: %" PRIu32 "\n", ECP5_STATUS_BUSY(status_register));
	gdb_outf("Last Command Failed: %" PRIu32 "\n", ECP5_STATUS_FAILURE(status_register));
	gdb_outf("Features are OTP: %" PRIu32 "\n", ECP5_STATUS_FEATURES_OTP(status_register));
	gdb_outf("Encrypted Bitstream Only: %" PRIu32 "\n", ECP5_STATUS_ENCRYPTED_ONLY(status_register));
	gdb_outf("Password Protection Enabled: %" PRIu32 "\n", ECP5_STATUS_PASSWORD_ENABLED(status_register));
	gdb_outf("Internal: %" PRIu32 "\n", ECP5_STATUS_INTERNAL1(status_register));
	gdb_outf("Encrypted Preamble: %" PRIu32 "\n", ECP5_STATUS_ENCRYPT_PREAMBLE(status_register));
	gdb_outf("Standard Preamble: %" PRIu32 "\n", ECP5_STATUS_STANDARD_PREAMBLE(status_register));
	gdb_outf("Primary Bitstream Failure: %" PRIu32 "\n", ECP5_STATUS_PRIMARY_CFG_FAIL(status_register));
	gdb_outf("BSE Status:\n");
	switch (ECP5_STATUS_BSE_ERROR(status_register)) {
	case 0x0U:
		gdb_outf("\tNo Errors\n");
		break;
	case 0x1U:
		gdb_outf("\tID Error\n");
		break;
	case 0x2U:
		gdb_outf("\tIllegal Command\n");
		break;
	case 0x3U:
		gdb_outf("\tCRC Error\n");
		break;
	case 0x4U:
		gdb_outf("\tPreamble Error\n");
		break;
	case 0x5U:
		gdb_outf("\tConfiguration Aborted By User\n");
		break;
	case 0x6U:
		gdb_outf("\tData Overflow\n");
		break;
	case 0x7U:
		gdb_outf("\tConfiguration too big for device SRAM\n");
		break;
	}
	gdb_outf("Execution Error: %" PRIu32 "\n", ECP5_STATUS_EXEC_ERROR(status_register));
	gdb_outf("ID Error: %" PRIu32 "\n", ECP5_STATUS_ID_ERROR(status_register));
	gdb_outf("Invalid Command: %" PRIu32 "\n", ECP5_STATUS_INVALID_COMMAND(status_register));
	gdb_outf("SED Error: %" PRIu32 "\n", ECP5_STATUS_SED_ERROR(status_register));
	gdb_outf("Bypass Mode: %" PRIu32 "\n", ECP5_STATUS_BYPASS_MODE(status_register));
	gdb_outf("Flow Through Mode: %" PRIu32 "\n", ECP5_STATUS_FLOW_MODE(status_register));
#else /* CONFIG_LATTICE_ECP5_DECODE */
	gdb_outf("Status: %08" PRIx32 "\n", status_register);
#endif

	return true;
}

static bool ecp5_read_reg_control(target_s *target, int argc, const char **argv)
{
	(void)argc;
	(void)argv;

	const ecp5_ctx_s *const ctx = (ecp5_ctx_s *)target->priv;
	const uint32_t control_register = ecp5_read32(ctx->device_index, CMD_LSC_READ_CTRL0);

#if CONFIG_LATTICE_ECP5_DECODE
	gdb_outf("MSPI Clock Divider: %" PRIu32 "\n", ECP5_CTRL0_MSPI_CLK(control_register));
	gdb_outf("\tSlew Rate: ");
	switch (ECP5_CTRL0_SLEW(control_register)) {
	case 0x0U:
		gdb_outf("Slow\n");
		break;
	case 0x1U:
		gdb_outf("Medium\n");
		break;
	default:
		gdb_outf("Fast\n");
		break;
	}
	gdb_outf("\tPROGRAM_DONE: ");
	switch (ECP5_CTRL0_PDONE(control_register)) {
	case 0x2U:
		gdb_outf("Overload with BYPASS\n");
		break;
	case 0x3U:
		gdb_outf("Overload with FLOW_THROUGH\n");
		break;
	default:
		gdb_outf("No Overload\n");
		break;
	}
	gdb_outf("\tNDR/TransFR: %" PRIu32 "\n", ECP5_CTRL0_NDR(control_register));
	gdb_outf("Wakeup Transparent: %" PRIu32 "\n", ECP5_CTRL0_WAKEUP_TRANS(control_register));

#else /* CONFIG_LATTICE_ECP5_DECODE */
	gdb_outf("Control: %08" PRIx32 "\n", control_register);
#endif

	return true;
}

static bool ecp5_read_reg_usercode(target_s *target, int argc, const char **argv)
{
	(void)argc;
	(void)argv;

	const ecp5_ctx_s *const ctx = (ecp5_ctx_s *)target->priv;
	const uint32_t usercode = ecp5_read32(ctx->device_index, CMD_USERCODE);

	gdb_outf("USERCODE: %08" PRIx32 "\n", usercode);

	return true;
}
