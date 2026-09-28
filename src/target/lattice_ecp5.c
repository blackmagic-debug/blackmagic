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

// Configuration target is the device SRAM array
#define ECP5_TARGET_SRAM 0U
// Configuration target is the device EFUSE array
#define ECP5_TARGET_EFUSE 1U

// No BSE error
#define ECP5_BSE_ERROR_NONE 0U
// ID error
#define ECP5_BSE_ERROR_ID 1U
// Illegal command
#define ECP5_BSE_ERROR_CMD 2U
// CRC error
#define ECP5_BSE_ERROR_CRC 3U
// Configuration preamble error
#define ECP5_BSE_ERROR_PRMB 4U
// Configuration aborted by user
#define ECP5_BSE_ERROR_ABRT 5U
// Data overflow error
#define ECP5_BSE_ERROR_OVFL 6U
// Configuration exceeded SRAM array size
#define ECP5_BSE_ERROR_SDM 7U

#define ECP5_STATUS_TRANSPARENT_MASK  1U
#define ECP5_STATUS_TRANSPARENT_SHIFT 0U
// If the device is in transparent mode
#define ECP5_STATUS_TRANSPARENT(reg) REGISTER_FIELD(reg, ECP5_STATUS_TRANSPARENT_MASK, ECP5_STATUS_TRANSPARENT_SHIFT)
#define ECP5_STATUS_TARGET_MASK      0x7U
#define ECP5_STATUS_TARGET_SHIFT     1U
// The current configuration target selection
#define ECP5_STATUS_TARGET(reg)       REGISTER_FIELD(reg, ECP5_STATUS_TARGET_MASK, ECP5_STATUS_TARGET_SHIFT)
#define ECP5_STATUS_JTAG_ACTIVE_MASK  1U
#define ECP5_STATUS_JTAG_ACTIVE_SHIFT 4U
// If the JTAG state machine is active or not
#define ECP5_STATUS_JTAG_ACTIVE(reg)         REGISTER_FIELD(reg, ECP5_STATUS_JTAG_ACTIVE_MASK, ECP5_STATUS_JTAG_ACTIVE_SHIFT)
#define ECP5_STATUS_PASSWORD_PROTECTED_MASK  1U
#define ECP5_STATUS_PASSWORD_PROTECTED_SHIFT 5U
// If the device configuration logic is password protected
#define ECP5_STATUS_PASSWORD_PROTECTED(reg) \
	REGISTER_FIELD(reg, ECP5_STATUS_PASSWORD_PROTECTED_MASK, ECP5_STATUS_PASSWORD_PROTECTED_SHIFT)
#define ECP5_STATUS_INTERNAL0_MASK  1U
#define ECP5_STATUS_INTERNAL0_SHIFT 6U
// Internal use only
#define ECP5_STATUS_INTERNAL0(reg)        REGISTER_FIELD(reg, ECP5_STATUS_INTERNAL0_MASK, ECP5_STATUS_INTERNAL0_SHIFT)
#define ECP5_STATUS_DECRYPT_ENABLED_MASK  1U
#define ECP5_STATUS_DECRYPT_ENABLED_SHIFT 7U
// Encrypted bitstreams are accepted on the device
#define ECP5_STATUS_DECRYPT_ENABLED(reg) \
	REGISTER_FIELD(reg, ECP5_STATUS_DECRYPT_ENABLED_MASK, ECP5_STATUS_DECRYPT_ENABLED_SHIFT)
#define ECP5_STATUS_DONE_MASK  1U
#define ECP5_STATUS_DONE_SHIFT 8U
// If the configuration engine `DONE` bit is set
#define ECP5_STATUS_DONE(reg)         REGISTER_FIELD(reg, ECP5_STATUS_DONE_MASK, ECP5_STATUS_DONE_SHIFT)
#define ECP5_STATUS_ISC_ENABLED_MASK  1U
#define ECP5_STATUS_ISC_ENABLED_SHIFT 9U
// If JTAG instructions are being executed with ISC enabled
#define ECP5_STATUS_ISC_ENABLED(reg)    REGISTER_FIELD(reg, ECP5_STATUS_ISC_ENABLED_MASK, ECP5_STATUS_ISC_ENABLED_SHIFT)
#define ECP5_STATUS_WRITE_ENABLED_MASK  1U
#define ECP5_STATUS_WRITE_ENABLED_SHIFT 10U
/*
 * If the selected configuration target is writable or not.
 *
 * Two situations may make the configuration target non-writeable:
 *  - Security bit is set
 *  - Password protection is enabled and the passwords don't match
 */
#define ECP5_STATUS_WRITE_ENABLED(reg) \
	REGISTER_FIELD(reg, ECP5_STATUS_WRITE_ENABLED_MASK, ECP5_STATUS_WRITE_ENABLED_SHIFT)
#define ECP5_STATUS_READ_ENABLED_MASK  1U
#define ECP5_STATUS_READ_ENABLED_SHIFT 11U
/*
 * If the device is read-protected from one of the following sources:
 *  - Security bit is set
 *  - Password protection is enabled and the passwords don't match
 */
#define ECP5_STATUS_READ_ENABLED(reg) REGISTER_FIELD(reg, ECP5_STATUS_READ_ENABLED_MASK, ECP5_STATUS_READ_ENABLED_SHIFT)
#define ECP5_STATUS_BUSY_MASK         1U
#define ECP5_STATUS_BUSY_SHIFT        12U
// If the device configuration logic is busy or not
#define ECP5_STATUS_BUSY(reg)     REGISTER_FIELD(reg, ECP5_STATUS_BUSY_MASK, ECP5_STATUS_BUSY_SHIFT)
#define ECP5_STATUS_FAILURE_MASK  1U
#define ECP5_STATUS_FAILURE_SHIFT 13U
// If the last command/instruction failed or not
#define ECP5_STATUS_FAILURE(reg)       REGISTER_FIELD(reg, ECP5_STATUS_FAILURE_MASK, ECP5_STATUS_FAILURE_SHIFT)
#define ECP5_STATUS_FEATURES_OTP_MASK  1U
#define ECP5_STATUS_FEATURES_OTP_SHIFT 14U
// If the current configuration feature row is set to be OTP
#define ECP5_STATUS_FEATURES_OTP(reg)    REGISTER_FIELD(reg, ECP5_STATUS_FEATURES_OTP_MASK, ECP5_STATUS_FEATURES_OTP_SHIFT)
#define ECP5_STATUS_ENCRYPTED_ONLY_MASK  1U
#define ECP5_STATUS_ENCRYPTED_ONLY_SHIFT 15U
// If this device only accepts encrypted bitstreams or not
#define ECP5_STATUS_ENCRYPTED_ONLY(reg) \
	REGISTER_FIELD(reg, ECP5_STATUS_ENCRYPTED_ONLY_MASK, ECP5_STATUS_ENCRYPTED_ONLY_SHIFT)
#define ECP5_STATUS_PASSWORD_ENABLED_MASK  1U
#define ECP5_STATUS_PASSWORD_ENABLED_SHIFT 16U
// If this device has password protection enabled or not
#define ECP5_STATUS_PASSWORD_ENABLED(reg) \
	REGISTER_FIELD(reg, ECP5_STATUS_PASSWORD_ENABLED_MASK, ECP5_STATUS_PASSWORD_ENABLED_SHIFT)
#define ECP5_STATUS_INTERNAL1_MASK  0x7U
#define ECP5_STATUS_INTERNAL1_SHIFT 17U
// Internal use only
#define ECP5_STATUS_INTERNAL1(reg)         REGISTER_FIELD(reg, ECP5_STATUS_INTERNAL1_MASK, ECP5_STATUS_INTERNAL1_SHIFT)
#define ECP5_STATUS_ENCRYPT_PREAMBLE_MASK  1U
#define ECP5_STATUS_ENCRYPT_PREAMBLE_SHIFT 20U
// If an encrypted configuration preamble was detected
#define ECP5_STATUS_ENCRYPT_PREAMBLE(reg) \
	REGISTER_FIELD(reg, ECP5_STATUS_ENCRYPT_PREAMBLE_MASK, ECP5_STATUS_ENCRYPT_PREAMBLE_SHIFT)
#define ECP5_STATUS_STANDARD_PREAMBLE_MASK  1U
#define ECP5_STATUS_STANDARD_PREAMBLE_SHIFT 21U
// If a standard configuration preamble was detected
#define ECP5_STATUS_STANDARD_PREAMBLE(reg) \
	REGISTER_FIELD(reg, ECP5_STATUS_STANDARD_PREAMBLE_MASK, ECP5_STATUS_STANDARD_PREAMBLE_SHIFT)
#define ECP5_STATUS_PRIMARY_CFG_FAIL_MASK  1U
#define ECP5_STATUS_PRIMARY_CFG_FAIL_SHIFT 22U
// If the device failed to configure from the primary pattern
#define ECP5_STATUS_PRIMARY_CFG_FAIL(reg) \
	REGISTER_FIELD(reg, ECP5_STATUS_PRIMARY_CFG_FAIL_MASK, ECP5_STATUS_PRIMARY_CFG_FAIL_SHIFT)
#define ECP5_STATUS_BSE_ERROR_MASK  0x7U
#define ECP5_STATUS_BSE_ERROR_SHIFT 23U
// BSE(?) error code
#define ECP5_STATUS_BSE_ERROR(reg)   REGISTER_FIELD(reg, ECP5_STATUS_BSE_ERROR_MASK, ECP5_STATUS_BSE_ERROR_SHIFT)
#define ECP5_STATUS_EXEC_ERROR_MASK  1U
#define ECP5_STATUS_EXEC_ERROR_SHIFT 26U
// If there was an error during execution of a command
#define ECP5_STATUS_EXEC_ERROR(reg) REGISTER_FIELD(reg, ECP5_STATUS_EXEC_ERROR_MASK, ECP5_STATUS_EXEC_ERROR_SHIFT)
#define ECP5_STATUS_ID_ERROR_MASK   1U
#define ECP5_STATUS_ID_ERROR_SHIFT  27U
// If there was an ID mismatch from a `verify_id` command
#define ECP5_STATUS_ID_ERROR(reg)         REGISTER_FIELD(reg, ECP5_STATUS_ID_ERROR_MASK, ECP5_STATUS_ID_ERROR_SHIFT)
#define ECP5_STATUS_INVALID_COMMAND_MASK  1U
#define ECP5_STATUS_INVALID_COMMAND_SHIFT 28U
// If the device received and invalid command
#define ECP5_STATUS_INVALID_COMMAND(reg) \
	REGISTER_FIELD(reg, ECP5_STATUS_INVALID_COMMAND_MASK, ECP5_STATUS_INVALID_COMMAND_SHIFT)
#define ECP5_STATUS_SED_ERROR_MASK  1U
#define ECP5_STATUS_SED_ERROR_SHIFT 29U
// There was an SED(?) error
#define ECP5_STATUS_SED_ERROR(reg)    REGISTER_FIELD(reg, ECP5_STATUS_SED_ERROR_MASK, ECP5_STATUS_SED_ERROR_SHIFT)
#define ECP5_STATUS_BYPASS_MODE_MASK  1U
#define ECP5_STATUS_BYPASS_MODE_SHIFT 30U
// If the device is in bypass mode
#define ECP5_STATUS_BYPASS_MODE(reg) REGISTER_FIELD(reg, ECP5_STATUS_BYPASS_MODE_MASK, ECP5_STATUS_BYPASS_MODE_SHIFT)
#define ECP5_STATUS_FLOW_MODE_MASK   1U
#define ECP5_STATUS_FLOW_MODE_SHIFT  31U
// If the device is in flow-through mode
#define ECP5_STATUS_FLOW_MODE(reg) REGISTER_FIELD(reg, ECP5_STATUS_FLOW_MODE_MASK, ECP5_STATUS_FLOW_MODE_SHIFT)

#define ECP5_CTRL_SLEW_SLOW 0U
#define ECP5_CTRL_SLEW_MED  1U
#define ECP5_CTRL_SLEW_FAST 2U

// No overload behaviour
#define ECP5_CTRL_PDONE_NONE 0U // or 1U
// Overload `PROGRAM_DONE` with `BYPASS`
#define ECP5_CTRL_PDONE_BYPASS 2U
// Overload `PROGRAM_DONE` with `FLOW_THROUGH`
#define ECP5_CTRL_PDONE_FLOW 3U

#define ECP5_CTRL0_MSPI_CLK_MASK  0x1fU
#define ECP5_CTRL0_MSPI_CLK_SHIFT 0U
// SPI Controller clock division ratio
#define ECP5_CTRL0_MSPI_CLK(reg) REGISTER_FIELD(reg, ECP5_CTRL0_MSPI_CLK_MASK, ECP5_CTRL0_MSPI_CLK_SHIFT)
#define ECP5_CTRL0_SLEW_MASK     0x3U
#define ECP5_CTRL0_SLEW_SHIFT    6U
// Configuration output slew rate
#define ECP5_CTRL0_SLEW(reg)   REGISTER_FIELD(reg, ECP5_CTRL0_SLEW_MASK, ECP5_CTRL0_SLEW_SHIFT)
#define ECP5_CTRL0_RSVD0_MASK  0x1ffU
#define ECP5_CTRL0_RSVD0_SHIFT 8U
// Reserved
#define ECP5_CTRL0_RSVD0(reg)  REGISTER_FIELD(reg, ECP5_CTRL0_RSVD0_MASK, ECP5_CTRL0_RSVD0_SHIFT)
#define ECP5_CTRL0_PDONE_MASK  0x3U
#define ECP5_CTRL0_PDONE_SHIFT 18U
// PROGRAM_DONE overload control
#define ECP5_CTRL0_PDONE(reg)  REGISTER_FIELD(reg, ECP5_CTRL0_PDONE_MASK, ECP5_CTRL0_PDONE_SHIFT)
#define ECP5_CTRL0_RSVD1_MASK  0x80U
#define ECP5_CTRL0_RSVD1_SHIFT 20U
// Reserved
#define ECP5_CTRL0_RSVD1(reg) REGISTER_FIELD(reg, ECP5_CTRL0_RSVD1_MASK, ECP5_CTRL0_RSVD1_SHIFT)
#define ECP5_CTRL0_NDR_MASK   1U
#define ECP5_CTRL0_NDR_SHIFT  28U
// Non-Disturbing reconfiguration (I/O buffers are left configured and in their current state)
#define ECP5_CTRL0_NDR(reg)           REGISTER_FIELD(reg, ECP5_CTRL0_NDR_MASK, ECP5_CTRL0_NDR_SHIFT)
#define ECP5_CTRL0_WAKEUP_TRANS_MASK  1U
#define ECP5_CTRL0_WAKEUP_TRANS_SHIFT 29U
// Transparent configuration `PROGRAMN` control
#define ECP5_CTRL0_WAKEUP_TRANS(reg) REGISTER_FIELD(reg, ECP5_CTRL0_WAKEUP_TRANS_MASK, ECP5_CTRL0_WAKEUP_TRANS_SHIFT)
#define ECP5_CTRL0_RSVD2_MASK        0x3U
#define ECP5_CTRL0_RSVD2_SHIFT       30U
// Reserved
#define ECP5_CTRL0_RSVD2(reg) REGISTER_FIELD(reg, ECP5_CTRL0_RSVD2_MASK, ECP5_CTRL0_RSVD2_SHIFT)

#define ECP5_SRAM_BASE  0x00000000U
#define ECP5_FLASH_BASE 0x04000000U

// 4KiB Data buffer + 4 byte SPI command
#define ECP5_XFR_BUFFER_SIZE  0x1004U
#define ECP5_SRAM_BUFFER_SIZE 0x1000U

static const uint8_t ecp5_spi_unlock[2U] = {0xfeU, 0x68U};

typedef struct ecp5_ctx {
	uint8_t device_index;
	uint8_t xfr_buffer[ECP5_XFR_BUFFER_SIZE];
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
static void ecp5_spi_xfr_jtag(target_s *target, uint8_t *data_out, const uint8_t *data_in, size_t length, bool inhibit);

static bool ecp5_sram_prepare(target_flash_s *flash);
static bool ecp5_sram_done(target_flash_s *flash);
static bool ecp5_sram_erase(target_flash_s *flash, target_addr_t addr, size_t length);
static bool ecp5_sram_mass_erase(target_flash_s *flash, platform_timeout_s *print_progess);
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

	target->priv_free = free;
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
			flash->writesize = ECP5_SRAM_BUFFER_SIZE;
			flash->done = ecp5_sram_done;
			flash->prepare = ecp5_sram_prepare;
			flash->mass_erase = ecp5_sram_mass_erase;
			flash->erase = ecp5_sram_erase;
			flash->write = ecp5_sram_write;

			target_add_flash(target, flash);
			break;
		}
	}

	ecp5_ctx_s *ctx = target->priv;
	ctx->device_index = dev_index;
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
	const jtag_dev_s *const device = &jtag_devs[ctx->device_index];

	if (ECP5_STATUS_ENCRYPTED_ONLY(status))
		DEBUG_WARN("This FPGA only accepts encrypted bitstreams!\n");

	if (ECP5_STATUS_DONE(status))
		DEBUG_INFO("FPGA is configured\n");

	if (device->dr_postscan) {
		DEBUG_WARN("Transparent SPI Flash not possible, not first device in the chain\n");
		return true;
	}

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

	// Make sure to reset the FPGA so we don't leave it in a coma
	ecp5_spi_flash_done(&flash);
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
	jtag_dev_write_ir(dev_index, CMD_LSC_RESET_CRC);
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

	const bool result =
		(ECP5_STATUS_BSE_ERROR(status) || ECP5_STATUS_ID_ERROR(status) || ECP5_STATUS_EXEC_ERROR(status) ||
			ECP5_STATUS_PRIMARY_CFG_FAIL(status) || ECP5_STATUS_FAILURE(status) || ECP5_STATUS_INVALID_COMMAND(status));

	if (result)
		DEBUG_ERROR("Bitstream programming failed: %" PRIu32 "\n", status);

	return !result;
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
	ecp5_ctx_s *const ctx = (ecp5_ctx_s *)target->priv;

	size_t offset = 0U;
	ctx->xfr_buffer[offset++] = SPI_FLASH_OPCODE(command);
	if ((command & SPI_FLASH_OPCODE_MODE_MASK) == SPI_FLASH_OPCODE_3B_ADDR) {
		ctx->xfr_buffer[offset++] = (address & 0xff0000U) >> 16U;
		ctx->xfr_buffer[offset++] = (address & 0x00ff00U) >> 8U;
		ctx->xfr_buffer[offset++] = address & 0x0000ffU;
	}

	const size_t dummy_len = (command & SPI_FLASH_DUMMY_MASK) >> SPI_FLASH_DUMMY_SHIFT;
	for (size_t dummy = 0U; dummy < dummy_len; ++dummy)
		ctx->xfr_buffer[offset++] = 0U;

	/// Clear out the remaining buffer
	memset(ctx->xfr_buffer + offset, 0, length);

	ecp5_spi_xfr_jtag(target, ctx->xfr_buffer, ctx->xfr_buffer, length + offset, false);
	memcpy(buffer, ctx->xfr_buffer + offset, length);
}

static void ecp5_spi_write(target_s *const target, const uint16_t command, const target_addr_t address,
	const void *const buffer, const size_t length)
{
	ecp5_ctx_s *const ctx = (ecp5_ctx_s *)target->priv;

	size_t offset = 0U;
	ctx->xfr_buffer[offset++] = SPI_FLASH_OPCODE(command);
	if ((command & SPI_FLASH_OPCODE_MODE_MASK) == SPI_FLASH_OPCODE_3B_ADDR) {
		ctx->xfr_buffer[offset++] = (address & 0xff0000U) >> 16U;
		ctx->xfr_buffer[offset++] = (address & 0x00ff00U) >> 8U;
		ctx->xfr_buffer[offset++] = address & 0x0000ffU;
	}

	const size_t dummy_len = (command & SPI_FLASH_DUMMY_MASK) >> SPI_FLASH_DUMMY_SHIFT;
	for (size_t dummy = 0U; dummy < dummy_len; ++dummy)
		ctx->xfr_buffer[offset++] = 0U;

	// Guard in the case buffer is `NULL`
	if (buffer)
		memcpy(ctx->xfr_buffer + offset, buffer, length);

	ecp5_spi_xfr_jtag(target, NULL, ctx->xfr_buffer, length + offset, true);
}

static void ecp5_spi_run_command(target_s *const target, const uint16_t command, const target_addr_t address)
{
	ecp5_spi_write(target, command, address, NULL, 0UL);
}

static void ecp5_spi_xfr_jtag(target_s *const target, uint8_t *const data_out, const uint8_t *const data_in,
	const size_t length, const bool inhibit)
{
	const ecp5_ctx_s *const ctx = (ecp5_ctx_s *)target->priv;
	const uint8_t dev_index = ctx->device_index;
	const jtag_dev_s *const device = &jtag_devs[dev_index];
	const bool inhibit_prescan = device->dr_prescan == 0U || inhibit;

	/* Switch into Shift-DR */
	jtagtap_shift_dr();

	uint8_t tap_out;
	for (size_t idx = 0U; idx < length; ++idx) {
		const uint8_t tap_in = reverse_bits8(data_in[idx]);
		jtag_proc.jtagtap_tdi_tdo_seq(&tap_out, (idx + 1U) == length && inhibit_prescan, &tap_in, 8U);
		if (data_out)
			data_out[idx] = reverse_bits8(tap_out);
	}

	if (data_out) {
		/* Fixup the TDO to TDI skew caused by extra devices on the JTAG chain in bypass mode */
		for (size_t scan = 0U; scan < device->dr_prescan; ++scan) {
			uint8_t trailing_bit = 0U;
			for (size_t offset = 0U; offset < length; ++offset) {
				const size_t index = length - 1U - offset;
				const uint8_t carry_bit = data_out[index] & 0x80U;
				data_out[index] <<= 1U;
				data_out[index] |= trailing_bit >> 7U;
				trailing_bit = carry_bit;
			}
		}
	}

	if (!inhibit_prescan) {
		/* Squeeze the trailing bits from the SPI transaction out of the chain */
		uint8_t trailing_bits[4];
		jtag_proc.jtagtap_tdi_tdo_seq(trailing_bits, true, NULL, device->dr_prescan);

		if (data_out) {
			/* forcibly re-align the data buffer's chakra */
			size_t bit_len = length * 8U;
			for (size_t offset = bit_len - device->dr_prescan, idx = 0; offset < bit_len; ++offset, ++idx) {
				const size_t output_byte = offset >> 3U; /* Divide by 8 */
				const size_t output_bit = 7U - (offset & 7U);

				const size_t input_byte = idx >> 3U;
				const size_t input_bit = idx & 7U;

				const uint8_t trailing_bit = trailing_bits[input_byte] >> input_bit;
				data_out[output_byte] |= trailing_bit << output_bit;
			}
		}
	}

	/* Now go through Update-DR and back to Idle */
	jtagtap_return_idle(1U);
}

static bool ecp5_sram_prepare(target_flash_s *const flash)
{
	target_s *const target = flash->t;
	ecp5_ctx_s *const ctx = (ecp5_ctx_s *)target->priv;
	const uint8_t dev_index = ctx->device_index;
	const jtag_dev_s *const device = &jtag_devs[dev_index];

	if (flash->operation == FLASH_OPERATION_WRITE) {
		// Write bitstream to SRAM
		jtag_dev_write_ir(dev_index, CMD_LSC_BITSTREAM_BURST);
		jtag_proc.jtagtap_cycle(false, false, 50U);

		/* Switch into Shift-DR */
		jtagtap_shift_dr();
		/* clock out 1's till we hit the right device in the chain */
		jtag_proc.jtagtap_tdi_seq(false, ones, device->dr_prescan);
	}
	return true;
}

static bool ecp5_sram_done(target_flash_s *const flash)
{
	target_s *const target = flash->t;
	ecp5_ctx_s *const ctx = (ecp5_ctx_s *)target->priv;
	const uint8_t dev_index = ctx->device_index;
	const jtag_dev_s *const device = &jtag_devs[dev_index];

	if (flash->operation == FLASH_OPERATION_WRITE) {
		/* Make sure we're in Exit1-DR having clocked out 1's for any more devices on the chain */
		jtag_proc.jtagtap_tdi_seq(true, ones, device->dr_postscan);
		/* Now go through Update-DR and back to Idle */
		jtagtap_return_idle(1U);

		// Exit configuration mode
		jtag_dev_write_ir(dev_index, CMD_ISC_DISABLE);
		jtag_proc.jtagtap_cycle(false, false, 50U);
	}

	return ecp5_check_error(target);
}

static inline bool ecp5_sram_erase(target_flash_s *const flash, const target_addr_t addr, const size_t length)
{
	(void)addr;
	(void)length;

	if (addr == flash->start)
		return ecp5_sram_mass_erase(flash, NULL);
	return true;
}

static bool ecp5_sram_mass_erase(target_flash_s *flash, platform_timeout_s *print_progess)
{
	(void)print_progess;

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
	(void)length;

	const target_s *const target = flash->t;
	const ecp5_ctx_s *const ctx = (ecp5_ctx_s *)target->priv;
	const uint8_t dev_index = ctx->device_index;
	const jtag_dev_s *const device = &jtag_devs[dev_index];

	// Due to bmd wanting to do full `flash->writesize` writes, it pads the final write,
	// however due to how the SRAM programming works, we can't do that, so we need to calculate
	// the actual write length to use so we don't write garbage into the FPGA
	const uint32_t write_length = flash->buf_addr_high - (flash->buf_addr_low & ~(flash->writesize - 1U));

	uint8_t tap_out;
	const uint8_t *const data_in = buffer;
	for (size_t idx = 0U; idx < write_length; ++idx) {
		const uint8_t tap_in = reverse_bits8(data_in[idx]);
		// Only emit the final TMS if appropriate
		jtag_proc.jtagtap_tdi_tdo_seq(&tap_out,
			(idx + 1U) == write_length && !device->dr_postscan && write_length != ECP5_SRAM_BUFFER_SIZE, &tap_in, 8U);
	}

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
	case ECP5_BSE_ERROR_NONE:
		gdb_outf("\tNo Errors\n");
		break;
	case ECP5_BSE_ERROR_ID:
		gdb_outf("\tID Error\n");
		break;
	case ECP5_BSE_ERROR_CMD:
		gdb_outf("\tIllegal Command\n");
		break;
	case ECP5_BSE_ERROR_CRC:
		gdb_outf("\tCRC Error\n");
		break;
	case ECP5_BSE_ERROR_PRMB:
		gdb_outf("\tPreamble Error\n");
		break;
	case ECP5_BSE_ERROR_ABRT:
		gdb_outf("\tConfiguration Aborted By User\n");
		break;
	case ECP5_BSE_ERROR_OVFL:
		gdb_outf("\tData Overflow\n");
		break;
	case ECP5_BSE_ERROR_SDM:
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
