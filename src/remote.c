/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2019  Black Sphere Technologies Ltd.
 * Written by Dave Marples <dave@marples.net>
 * Modified 2020 - 2021 by Uwe Bonnes (bon@elektron.ikp.physik.tu-darmstadt.de)
 * Copyright (C) 2022-2023 1BitSquared <info@1bitsquared.com>
 * Modified by Rachel Mant <git@dragonmux.network>
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

#include <stdarg.h>
#include "general.h"
#include "platform.h"
#include "remote.h"
#include "gdb_main.h"
#include "gdb_packet.h"
#include "gdb_if.h"
#include "jtagtap.h"
#include "swd.h"
#include "spi.h"
#include "sfdp.h"
#include "target.h"
#include "adiv5.h"
#if defined(ENABLE_RISCV_ACCEL) && ENABLE_RISCV_ACCEL == 1
#include "riscv_debug.h"
#endif
#include "version.h"
#include "exception.h"
#include "hex_utils.h"

#if PC_HOSTED == 0
/* hex-ify and send a buffer of data */
static void remote_send_buf(const void *const buffer, const size_t len)
{
	char hex[2] = {0};
	const uint8_t *const data = (const uint8_t *)buffer;
	for (size_t offset = 0; offset < len; ++offset) {
		hexify(hex, data + offset, 1U);
		gdb_if_putchar(hex[0], 0);
		gdb_if_putchar(hex[1], 0);
	}
}

/* Send a response with some data following */
static void remote_respond_buf(const char response_code, const void *const buffer, const size_t len)
{
	gdb_if_putchar(REMOTE_RESP, 0);
	gdb_if_putchar(response_code, 0);

	remote_send_buf(buffer, len);

	gdb_if_putchar(REMOTE_EOM, 1);
}

/* Send a response with a simple result code parameter */
static void remote_respond(const char response_code, uint64_t param)
{
	/* Put out the start of response marker and response code */
	gdb_if_putchar(REMOTE_RESP, false);
	gdb_if_putchar(response_code, false);

	/* This defines space for exactly a 64-bit number expressed in hex */
	char response[16];
	size_t idx = 0;

	/* Convert the response to hexadecimal */
	for (; idx < 16U && param; ++idx) {
		response[idx] = hex_digit(param & 0xfU);
		param >>= 4U;
	}
	/* Adjust for 0 responses */
	if (!idx)
		response[idx++] = '0';

	/* response now contains the response but logically backwards, so iterate backwards through it sending it */
	for (; idx; --idx)
		gdb_if_putchar(response[idx - 1U], false);
	/* Finish with the endof response marker */
	gdb_if_putchar(REMOTE_EOM, true);
}

/* Send a response with a string following */
static void remote_respond_string(const char response_code, const char *const str)
{
	gdb_if_putchar(REMOTE_RESP, 0);
	gdb_if_putchar(response_code, 0);
	const size_t str_length = strlen(str);
	for (size_t idx = 0; idx < str_length; ++idx) {
		const char chr = str[idx];
		/* Replace problematic/illegal characters with a space to not disturb the protocol */
		if (chr == '$' || chr == REMOTE_SOM || chr == REMOTE_EOM)
			gdb_if_putchar(' ', 0);
		else
			gdb_if_putchar(chr, 0);
	}
	gdb_if_putchar(REMOTE_EOM, 1);
}

/*
 * This faked ADIv5 DP structure holds the currently used low-level implementation functions (SWD vs JTAG)
 * and basic DP state for remote protocol requests made. This is shared between remote_packet_process_swd()
 * and remote_packet_process_jtag() for use by remote_packet_process_adiv5() so its faked AP can do the right
 * thing.
 *
 * REMOTE_INIT for SWD and JTAG rewrite the {read,write}_no_check, dp_read, error, low_access and abort function
 * pointers to reconfigure this structure appropriately.
 */
static adiv5_debug_port_s remote_dp = {
	.ap_read = adiv5_ap_reg_read,
	.ap_write = adiv5_ap_reg_write,
	.mem_read = advi5_mem_read_bytes,
	.mem_write = adiv5_mem_write_bytes,
};

static void remote_packet_process_swd(const char *const packet, const size_t packet_len)
{
	switch (packet[1]) {
	case REMOTE_INIT: /* SS = initialise =============================== */
		if (packet_len == 2) {
			remote_dp.write_no_check = adiv5_swd_write_no_check;
			remote_dp.read_no_check = adiv5_swd_read_no_check;
			remote_dp.dp_read = adiv5_swd_read;
			remote_dp.error = adiv5_swd_clear_error;
			remote_dp.low_access = adiv5_swd_raw_access;
			remote_dp.abort = adiv5_swd_abort;
			swdptap_init();
			remote_respond(REMOTE_RESP_OK, 0);
		} else
			remote_respond(REMOTE_RESP_ERR, REMOTE_ERROR_WRONGLEN);
		break;

	case REMOTE_IN_PAR: { /* SI = In parity ============================= */
		const size_t clock_cycles = hex_string_to_num(2, packet + 2);
		uint32_t result = 0;
		const bool parity_ok = swd_proc.seq_in_parity(&result, clock_cycles);
		remote_respond(parity_ok ? REMOTE_RESP_OK : REMOTE_RESP_PARERR, result);
		break;
	}

	case REMOTE_IN: { /* Si = In ======================================= */
		const size_t clock_cycles = hex_string_to_num(2, packet + 2);
		const uint32_t result = swd_proc.seq_in(clock_cycles);
		remote_respond(REMOTE_RESP_OK, result);
		break;
	}

	case REMOTE_OUT: { /* So = Out ====================================== */
		const size_t clock_cycles = hex_string_to_num(2, packet + 2);
		const uint32_t data = hex_string_to_num(-1, packet + 4);
		swd_proc.seq_out(data, clock_cycles);
		remote_respond(REMOTE_RESP_OK, 0);
		break;
	}

	case REMOTE_OUT_PAR: { /* SO = Out parity ========================== */
		const size_t clock_cycles = hex_string_to_num(2, packet + 2);
		const uint32_t data = hex_string_to_num(-1, packet + 4);
		swd_proc.seq_out_parity(data, clock_cycles);
		remote_respond(REMOTE_RESP_OK, 0);
		break;
	}

	default:
		remote_respond(REMOTE_RESP_ERR, REMOTE_ERROR_UNRECOGNISED);
		break;
	}
}

static void remote_packet_process_jtag(const char *const packet, const size_t packet_len)
{
	switch (packet[1]) {
	case REMOTE_INIT: /* JS = initialise ============================= */
		remote_dp.write_no_check = NULL;
		remote_dp.read_no_check = NULL;
		remote_dp.dp_read = adiv5_jtag_read;
		remote_dp.error = adiv5_jtag_clear_error;
		remote_dp.low_access = adiv5_jtag_raw_access;
		remote_dp.abort = adiv5_jtag_abort;
		jtagtap_init();
		remote_respond(REMOTE_RESP_OK, 0);
		break;

	case REMOTE_RESET: /* JR = reset ================================= */
		jtag_proc.jtagtap_reset();
		remote_respond(REMOTE_RESP_OK, 0);
		break;

	case REMOTE_TMS: { /* JT = TMS Sequence ============================ */
		const size_t clock_cycles = hex_string_to_num(2, packet + 2);
		const uint32_t tms_states = hex_string_to_num(2, packet + 4);

		if (packet_len < 4U)
			remote_respond(REMOTE_RESP_ERR, REMOTE_ERROR_WRONGLEN);
		else {
			jtag_proc.jtagtap_tms_seq(tms_states, clock_cycles);
			remote_respond(REMOTE_RESP_OK, 0);
		}
		break;
	}

	case REMOTE_CYCLE: { /* JC = clock cycle ============================ */
		const size_t clock_cycles = hex_string_to_num(8, packet + 4);
		const bool tms = packet[2] != '0';
		const bool tdi = packet[3] != '0';
		jtag_proc.jtagtap_cycle(tms, tdi, clock_cycles);
		remote_respond(REMOTE_RESP_OK, 0);
		break;
	}

	case REMOTE_TDITDO_TMS: /* JD = TDI/TDO  ========================================= */
	case REMOTE_TDITDO_NOTMS: {
		if (packet_len < 5U)
			remote_respond(REMOTE_RESP_ERR, REMOTE_ERROR_WRONGLEN);
		else {
			const size_t clock_cycles = hex_string_to_num(2, packet + 2);
			const uint64_t data_in = hex_string_to_num(-1, packet + 4);
			uint64_t data_out = 0;
			jtag_proc.jtagtap_tdi_tdo_seq(
				(uint8_t *)&data_out, packet[1] == REMOTE_TDITDO_TMS, (const uint8_t *)&data_in, clock_cycles);

			remote_respond(REMOTE_RESP_OK, data_out);
		}
		break;
	}

	case REMOTE_NEXT: { /* JN = NEXT ======================================== */
		if (packet_len != 4U)
			remote_respond(REMOTE_RESP_ERR, REMOTE_ERROR_WRONGLEN);
		else {
			const bool tdo = jtag_proc.jtagtap_next(packet[2] == '1', packet[3] == '1');
			remote_respond(REMOTE_RESP_OK, tdo ? 1U : 0U);
		}
		break;
	}

	default:
		remote_respond(REMOTE_RESP_ERR, REMOTE_ERROR_UNRECOGNISED);
		break;
	}
}

#if !defined(BOARD_IDENT) && defined(BOARD_IDENT)
#define PLATFORM_IDENT BOARD_IDENT
#endif

static void remote_packet_process_general(char *packet, const size_t packet_len)
{
	(void)packet_len;
	switch (packet[1]) {
	case REMOTE_VOLTAGE:
		remote_respond_string(REMOTE_RESP_OK, platform_target_voltage());
		break;
	case REMOTE_NRST_SET:
		platform_nrst_set_val(packet[2] == '1');
		remote_respond(REMOTE_RESP_OK, 0);
		break;
	case REMOTE_NRST_GET:
		remote_respond(REMOTE_RESP_OK, platform_nrst_get_val());
		break;
	case REMOTE_FREQ_SET:
		platform_max_frequency_set(hex_string_to_num(8, packet + 2));
		remote_respond(REMOTE_RESP_OK, 0);
		break;
	case REMOTE_FREQ_GET: {
		const uint32_t freq = platform_max_frequency_get();
		remote_respond_buf(REMOTE_RESP_OK, (uint8_t *)&freq, 4);
		break;
	}
	case REMOTE_PWR_SET:
#ifdef PLATFORM_HAS_POWER_SWITCH
		if (packet[2] == '1' && !platform_target_get_power() &&
			platform_target_voltage_sense() > POWER_CONFLICT_THRESHOLD) {
			/* want to enable target power, but voltage > 0.5V sensed
			 * on the pin -> cancel
			 */
			remote_respond(REMOTE_RESP_ERR, 0);
		} else {
			const bool result = platform_target_set_power(packet[2] == '1');
			remote_respond(result ? REMOTE_RESP_OK : REMOTE_RESP_ERR, 0);
		}
#else
		remote_respond(REMOTE_RESP_NOTSUP, 0);
#endif
		break;
	case REMOTE_PWR_GET:
#ifdef PLATFORM_HAS_POWER_SWITCH
		remote_respond(REMOTE_RESP_OK, platform_target_get_power());
#else
		remote_respond(REMOTE_RESP_NOTSUP, 0);
#endif
		break;
	case REMOTE_START:
#if ENABLE_DEBUG == 1 && defined(PLATFORM_HAS_DEBUG)
		debug_bmp = true;
#endif
		remote_respond_string(REMOTE_RESP_OK, PLATFORM_IDENT "" FIRMWARE_VERSION);
		break;
	case REMOTE_TARGET_CLK_OE:
		platform_target_clk_output_enable(packet[2] != '0');
		remote_respond(REMOTE_RESP_OK, 0);
		break;
	default:
		remote_respond(REMOTE_RESP_ERR, REMOTE_ERROR_UNRECOGNISED);
		break;
	}
}

static void remote_packet_process_high_level(const char *packet, const size_t packet_len)
{
	SET_IDLE_STATE(0);
	switch (packet[1]) {
	case REMOTE_HL_CHECK: /* HC = check the version of the protocol */
		remote_respond(REMOTE_RESP_OK, REMOTE_HL_VERSION);
		break;

	case REMOTE_HL_ADD_JTAG_DEV: { /* HJ = fill firmware jtag_devs */
		/* Check the packet is an appropriate length */
		if (packet_len < 22U) {
			remote_respond(REMOTE_RESP_ERR, REMOTE_ERROR_WRONGLEN);
			break;
		}

		jtag_dev_s jtag_dev = {0};
		const uint8_t index = hex_string_to_num(2, packet + 2);
		jtag_dev.dr_prescan = hex_string_to_num(2, packet + 4);
		jtag_dev.dr_postscan = hex_string_to_num(2, packet + 6);
		jtag_dev.ir_len = hex_string_to_num(2, packet + 8);
		jtag_dev.ir_prescan = hex_string_to_num(2, packet + 10);
		jtag_dev.ir_postscan = hex_string_to_num(2, packet + 12);
		jtag_dev.current_ir = hex_string_to_num(8, packet + 14);
		jtag_add_device(index, &jtag_dev);
		remote_respond(REMOTE_RESP_OK, 0);
		break;
	}

	case REMOTE_HL_ACCEL: { /* HA = request what accelerations are available */
		/* Build a response value that depends on what things are built into the firmare */
		remote_respond(REMOTE_RESP_OK,
			REMOTE_ACCEL_ADIV5
#if defined(ENABLE_RISCV_ACCEL) && ENABLE_RISCV_ACCEL == 1
				| REMOTE_ACCEL_RISCV
#endif
		);
		break;
	}

	default:
		remote_respond(REMOTE_RESP_ERR, REMOTE_ERROR_UNRECOGNISED);
		break;
	}
	SET_IDLE_STATE(1);
}

static void remote_adiv5_respond(const void *const data, const size_t length)
{
	if (remote_dp.fault)
		/* If the request didn't work and caused a fault, tell the host */
		remote_respond(REMOTE_RESP_ERR, REMOTE_ERROR_FAULT | ((uint16_t)remote_dp.fault << 8U));
	else {
		/* Otherwise reply back with the data */
		if (data)
			remote_respond_buf(REMOTE_RESP_OK, data, length);
		else
			remote_respond(REMOTE_RESP_OK, 0);
	}
}

static void remote_packet_process_adiv5(const char *const packet, const size_t packet_len)
{
	/* Our shortest ADIv5 packet is 8 bytes long, check that we have at least that */
	if (packet_len < 8U) {
		remote_respond(REMOTE_RESP_PARERR, 0);
		return;
	}

	/* Set up the DP and a fake AP structure to perform the access with */
	remote_dp.dev_index = hex_string_to_num(2, packet + 2);
	remote_dp.fault = 0U;
	adiv5_access_port_s remote_ap;
	remote_ap.apsel = hex_string_to_num(2, packet + 4);
	remote_ap.dp = &remote_dp;

	SET_IDLE_STATE(0);
	switch (packet[1]) {
	/* DP access commands */
	case REMOTE_DP_READ: { /* Ad = Read from DP register */
		/* Grab the address to read from and try to perform the access */
		const uint16_t addr = hex_string_to_num(4, packet + 6);
		const uint32_t data = adiv5_dp_read(&remote_dp, addr);
		remote_adiv5_respond(&data, 4U);
		break;
	}
	/* Raw access comands */
	case REMOTE_ADIV5_RAW_ACCESS: { /* AR = Perform a raw ADIv5 access */
		/* Grab the address to perform an access against and the value to work with */
		const uint16_t addr = hex_string_to_num(4, packet + 6);
		const uint32_t value = hex_string_to_num(8, packet + 10);
		/* Try to perform the access using the AP selection value as R/!W */
		const uint32_t data = adiv5_dp_low_access(&remote_dp, remote_ap.apsel, addr, value);
		remote_adiv5_respond(&data, 4U);
		break;
	}
	/* AP access commands */
	case REMOTE_AP_READ: { /* Aa = Read from AP register */
		/* Grab the AP address to read from and try to perform the access */
		const uint16_t addr = hex_string_to_num(4, packet + 6);
		const uint32_t data = adiv5_ap_read(&remote_ap, addr);
		remote_adiv5_respond(&data, 4U);
		break;
	}
	case REMOTE_AP_WRITE: { /* AA = Write to AP register */
		/* Grab the AP address to write to and the data to write then try to perform the access */
		const uint16_t addr = hex_string_to_num(4, packet + 6);
		const uint32_t value = hex_string_to_num(8, packet + 10);
		adiv5_ap_write(&remote_ap, addr, value);
		remote_adiv5_respond(NULL, 0U);
		break;
	}
	/* Memory access commands */
	case REMOTE_MEM_READ: { /* Am = Read from memory */
		/* Grab the CSW value to use in the access */
		remote_ap.csw = hex_string_to_num(8, packet + 6);
		/* Grab the start address for the read */
		const target_addr64_t address = hex_string_to_num(16, packet + 14U);
		/* And how many bytes to read, validating it for buffer overflows */
		const uint32_t length = hex_string_to_num(8, packet + 30U);
		if (length > GDB_PACKET_BUFFER_SIZE - REMOTE_ADIV5_MEM_READ_LENGTH) {
			remote_respond(REMOTE_RESP_PARERR, 0);
			break;
		}
		/* Get the aligned packet buffer to reuse for the data read */
		void *data = gdb_packet_buffer();
		/* Perform the read and send back the results */
		adiv5_mem_read(&remote_ap, data, address, length);
		remote_adiv5_respond(data, length);
		break;
	}
	case REMOTE_MEM_WRITE: { /* AM = Write to memory */
		/* Grab the CSW value to use in the access */
		remote_ap.csw = hex_string_to_num(8, packet + 6);
		/* Grab the alignment for the access */
		const align_e align = hex_string_to_num(2, packet + 14U);
		/* Grab the start address for the write */
		const target_addr64_t address = hex_string_to_num(16, packet + 16U);
		/* And how many bytes to read, validating it for buffer overflows */
		const uint32_t length = hex_string_to_num(8, packet + 32U);
		if (length > GDB_PACKET_BUFFER_SIZE - REMOTE_ADIV5_MEM_WRITE_LENGTH) {
			remote_respond(REMOTE_RESP_PARERR, 0);
			break;
		}
		/* Validate the alignment is suitable */
		if (length & ((1U << align) - 1U)) {
			remote_respond(REMOTE_RESP_PARERR, 0);
			break;
		}
		/* Get the aligned packet buffer to reuse for the data to write */
		void *data = gdb_packet_buffer();
		/* And decode the data from the packet into it */
		unhexify(data, packet + 40U, length);
		/* Perform the write and report success/failures */
		adiv5_mem_write_aligned(&remote_ap, address, data, length, align);
		remote_adiv5_respond(NULL, 0);
		break;
	}

	default:
		remote_respond(REMOTE_RESP_ERR, REMOTE_ERROR_UNRECOGNISED);
		break;
	}
	SET_IDLE_STATE(1);
}

#if defined(ENABLE_RISCV_ACCEL) && ENABLE_RISCV_ACCEL == 1
/*
 * This faked RISC-V DMI structure holds the currently used low-level implementation functions and basic DMI
 * state for remote protocol requests made. This is for use by remote_packet_process_riscv() so it can do the right
 * thing.
 *
 * REMOTE_INIT for RISC-V Debug rewrite the read and write function pointers to reconfigure this structure appropriately.
 */
static riscv_dmi_s remote_dmi = {
	.read = NULL,
	.write = NULL,
};

void remote_packet_process_riscv(const char *const packet, const size_t packet_len)
{
	/* Our shortest RISC-V Debug protocol packet is 2 bytes long, check that we have at least that */
	if (packet_len < 2U) {
		remote_respond(REMOTE_RESP_PARERR, 0);
		return;
	}

	/* Check for and handle the protocols packet */
	if (packet[1U] == REMOTE_RISCV_PROTOCOLS) {
		/* Validate the length of the packet, then handle it if that checks out */
		if (packet_len != 2U)
			remote_respond(REMOTE_RESP_PARERR, 0);
		else
			remote_respond(REMOTE_RESP_OK, REMOTE_RISCV_PROTOCOL_JTAG);
		return;
	}
	/* Check for and handle the initialisation packet */
	else if (packet[1U] == REMOTE_INIT) {
		/* Check the length of the packet */
		if (packet_len != 3U) {
			remote_respond(REMOTE_RESP_PARERR, 0);
			return;
		}

		/* We got a good packet, so handle initialisation accordingly */
		switch (packet[2U]) {
		case REMOTE_RISCV_JTAG:
			remote_dmi.read = riscv_jtag_dmi_read;
			remote_dmi.write = riscv_jtag_dmi_write;
			remote_respond(REMOTE_RESP_OK, 0);
			break;
		/* If the protocol requested is not supported, bubble that up to the host */
		default:
			remote_respond(REMOTE_RESP_PARERR, REMOTE_ERROR_UNRECOGNISED);
			break;
		}
		return;
	}
	/* Our shortest RISC-V protocol packet is 16 bytes long, check that we have at least that */
	else if (packet_len < 16U) {
		remote_respond(REMOTE_RESP_PARERR, 0);
		return;
	}

	/* Having dealt with the other requests, set up the fake DMI structure to perform the access with */
	remote_dmi.dev_index = hex_string_to_num(2, packet + 2);
	remote_dmi.idle_cycles = hex_string_to_num(2, packet + 4);
	remote_dmi.address_width = hex_string_to_num(2, packet + 6);
	remote_dmi.fault = 0U;

	switch (packet[1U]) {
	case REMOTE_RISCV_DMI_READ: {
		/* Grab the DMI address to read from and try to perform the access */
		const uint32_t addr = hex_string_to_num(8, packet + 8);
		uint32_t value = 0;
		if (!remote_dmi.read(&remote_dmi, addr, &value))
			/* If the request didn't work, and caused a fault, tell the host */
			remote_respond(REMOTE_RESP_ERR, REMOTE_ERROR_FAULT | ((uint16_t)remote_dmi.fault << 8U));
		else
			/* Otherwise reply back with the read data */
			remote_respond_buf(REMOTE_RESP_OK, &value, 4U);
		break;
	}
	case REMOTE_RISCV_DMI_WRITE: {
		/* Write packets are 24 bytes long, verify we have enough bytes */
		if (packet_len != 24U) {
			remote_respond(REMOTE_RESP_PARERR, 0);
			break;
		}
		/* Grab the DMI address to write to and the data to write then try to perform the access */
		const uint32_t addr = hex_string_to_num(8, packet + 8);
		const uint32_t value = hex_string_to_num(8, packet + 16);
		if (!remote_dmi.write(&remote_dmi, addr, value))
			/* If the request didn't work, and caused a fault, tell the host */
			remote_respond(REMOTE_RESP_ERR, REMOTE_ERROR_FAULT | ((uint16_t)remote_dmi.fault << 8U));
		else
			/* Otherwise inform the host the request succeeded */
			remote_respond(REMOTE_RESP_OK, 0);
		break;
	}
	default:
		remote_respond(REMOTE_RESP_ERR, REMOTE_ERROR_UNRECOGNISED);
		break;
	}
}
#endif

static void remote_spi_respond(const bool result)
{
	if (result)
		/* If the request succeeded, send an OK response */
		remote_respond(REMOTE_RESP_OK, 0);
	else
		/* Otherwise signal that something went wrong with the request */
		remote_respond(REMOTE_RESP_ERR, REMOTE_ERROR_FAULT);
}

void remote_packet_process_spi(const char *const packet, const size_t packet_len)
{
	/* Our shortest SPI packet is 4 bytes long, check that we have at least that */
	if (packet_len < 4U) {
		remote_respond(REMOTE_RESP_PARERR, 0);
		return;
	}

	const uint8_t spi_bus = hex_string_to_num(2, packet + 2);

	switch (packet[1]) {
	/* Bus initialisation/deinitialisation commands */
	case REMOTE_SPI_BEGIN:
		remote_spi_respond(platform_spi_init(spi_bus));
		break;
	case REMOTE_SPI_END:
		remote_spi_respond(platform_spi_deinit(spi_bus));
		break;
	/* Selects a SPI device to talk with */
	case REMOTE_SPI_CHIP_SELECT:
		/* For this command, spi_bus above is actually the device ID + whether to select it */
		remote_spi_respond(platform_spi_chip_select(spi_bus));
		break;
	/* Performs a single byte SPI transfer */
	case REMOTE_SPI_TRANSFER: {
		const uint8_t data_in = hex_string_to_num(2, packet + 4);
		const uint8_t data_out = platform_spi_xfer(spi_bus, data_in);
		remote_respond(REMOTE_RESP_OK, data_out);
		break;
	}
	/* Perform a complete read cycle with a SPI Flash of up to 256 bytes */
	case REMOTE_SPI_READ: {
		/*
		 * Decode the device to talk to, what command to send, and the addressing
		 * and length information for that command
		 */
		const uint8_t spi_device = hex_string_to_num(2, packet + 4);
		const uint16_t command = hex_string_to_num(4, packet + 6);
		const target_addr_t address = hex_string_to_num(6, packet + 10);
		const size_t length = hex_string_to_num(4, packet + 16);
		/* Validate the data length isn't overly long */
		if (length > 256U) {
			remote_respond(REMOTE_RESP_PARERR, 0);
			break;
		}
		/* Get the aligned packet buffer to reuse for the data read */
		void *data = gdb_packet_buffer();
		/* Perform the read cycle */
		bmp_spi_read(spi_bus, spi_device, command, address, data, length);
		remote_respond_buf(REMOTE_RESP_OK, data, length);
		break;
	}
	/* Perform a complete write cycle with a SPI Flash of up to 256 bytes */
	case REMOTE_SPI_WRTIE: {
		/*
		 * Decode the device to talk to, what command to send, and the addressing
		 * and length information for that command
		 */
		const uint8_t spi_device = hex_string_to_num(2, packet + 4);
		const uint16_t command = hex_string_to_num(4, packet + 6);
		const target_addr_t address = hex_string_to_num(6, packet + 10);
		const size_t length = hex_string_to_num(4, packet + 16);
		/* Validate the data length isn't overly long */
		if (length > 256U) {
			remote_respond(REMOTE_RESP_PARERR, 0);
			break;
		}
		/* Get the aligned packet buffer to reuse for the data to write */
		void *data = gdb_packet_buffer();
		/* And decode the data from the packet into it */
		unhexify(data, packet + 20U, length);
		/* Perform the write cycle */
		bmp_spi_write(spi_bus, spi_device, command, address, data, length);
		remote_respond(REMOTE_RESP_OK, 0);
		break;
	}
	/* Get the JEDEC device ID for a Flash device */
	case REMOTE_SPI_CHIP_ID: {
		/* Decoder the device to talk to */
		const uint8_t spi_device = hex_string_to_num(2, packet + 4);
		/* Set up a suitable buffer for and read the JEDEC ID */
		spi_flash_id_s flash_id;
		bmp_spi_read(spi_bus, spi_device, SPI_FLASH_CMD_READ_JEDEC_ID, 0, &flash_id, sizeof(flash_id));
		/* Respond with the ID */
		remote_respond_buf(REMOTE_RESP_OK, &flash_id, sizeof(flash_id));
		break;
	}
	/* Run a command against a SPI Flash device */
	case REMOTE_SPI_RUN_COMMAND: {
		/* Decode the device to talk to, what command to send, and the addressing information for that command */
		const uint8_t spi_device = hex_string_to_num(2, packet + 4);
		const uint16_t command = hex_string_to_num(4, packet + 6);
		const target_addr_t address = hex_string_to_num(6, packet + 10);
		/* Execute the command and signal success */
		bmp_spi_run_command(spi_bus, spi_device, command, address);
		remote_respond(REMOTE_RESP_OK, 0);
		break;
	}
	default:
		remote_respond(REMOTE_RESP_ERR, REMOTE_ERROR_UNRECOGNISED);
		break;
	}
}

void remote_packet_process(char *const packet, const size_t packet_length)
{
	switch (packet[0]) {
	case REMOTE_SWDP_PACKET:
		remote_packet_process_swd(packet, packet_length);
		break;

	case REMOTE_JTAG_PACKET:
		remote_packet_process_jtag(packet, packet_length);
		break;

	case REMOTE_GEN_PACKET:
		remote_packet_process_general(packet, packet_length);
		break;

	case REMOTE_HL_PACKET:
		remote_packet_process_high_level(packet, packet_length);
		break;

	case REMOTE_ADIV5_PACKET: {
		/* Setup an exception frame to try the ADIv5 operation in */
		TRY (EXCEPTION_ALL) {
			remote_packet_process_adiv5(packet, packet_length);
		}
		CATCH () {
		/* Handle any exception we've caught by translating it into a remote protocol response */
		default:
			remote_respond(REMOTE_RESP_ERR, REMOTE_ERROR_EXCEPTION | ((uint64_t)exception_frame.type << 8U));
		}
		break;
	}

#if defined(ENABLE_RISCV_ACCEL) && ENABLE_RISCV_ACCEL == 1
	case REMOTE_RISCV_PACKET:
		remote_packet_process_riscv(packet, packet_length);
		break;
#endif

	case REMOTE_SPI_PACKET:
		remote_packet_process_spi(packet, packet_length);
		break;

	default: /* Oh dear, unrecognised, return an error */
		remote_respond(REMOTE_RESP_ERR, REMOTE_ERROR_UNRECOGNISED);
		break;
	}
}
#endif
