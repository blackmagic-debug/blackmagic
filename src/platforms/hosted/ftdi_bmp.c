/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
 * Copyright (C) 2018  Uwe Bonnes(bon@elektron.ikp.physik.tu-darmstadt.de)
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
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
#include "general.h"
#include "gdb_if.h"
#include "target.h"
#include "buffer_utils.h"

#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

#include "ftdi_bmp.h"
#include <ftdi.h>

/*
 * This file implements the generic and overarching logic and routines needed to talk
 * with FTDI devices to interface with JTAG and SWD via them.
 *
 * References:
 * AN_108 - Command Processor for MPSSE and MCU Host Bus Emulation Modes
 *   https://www.ftdichip.com/Support/Documents/AppNotes/AN_108_Command_Processor_for_MPSSE_and_MCU_Host_Bus_Emulation_Modes.pdf
 */

#if defined(USE_USB_VERSION_BIT)
typedef struct ftdi_transfer_control ftdi_transfer_control_s;
#endif

#define BUF_SIZE 4096U
static uint8_t outbuf[BUF_SIZE];
static uint16_t bufptr = 0;

cable_desc_s active_cable;
ftdi_port_state_s active_state;

const cable_desc_s cable_desc[] = {
	{
		/*
		 * Direct connection from FTDI to JTAG/SWD.
		 * Pin 6 direct connected to RST.
		 */
		.vendor = 0x0403U,
		.product = 0x6014U,
		.interface = INTERFACE_A,
		// No explicit reset
		.bb_swdio_in_port_cmd = GET_BITS_LOW,
		.bb_swdio_in_pin = MPSSE_CS,
		.description = "UM232H",
		.name = "um232h",
	},
	{
		/*
		 * Direct connection from FTDI to JTAG/SWD.
		 * Pin 6 direct connected to RST.
		 */
		.vendor = 0x0403U,
		.product = 0x6010U,
		.interface = INTERFACE_A,
		.init.data[0] = PIN6, /* PULL nRST high*/
		.bb_swdio_in_port_cmd = GET_BITS_LOW,
		.bb_swdio_in_pin = MPSSE_CS,
		.assert_nrst.data[0] = ~PIN6,
		.assert_nrst.dirs[0] = PIN6,
		.deassert_nrst.data[0] = PIN6,
		.deassert_nrst.dirs[0] = ~PIN6,
		.description = "FLOSS-JTAG",
		.name = "flossjtag",
	},
	{
		/*
		 * MPSSE_SK (DB0) ----------- SWDCK/JTCK
		 * MPSSE-DO (DB1) -- 470 R -- SWDIO/JTMS
		 * MPSSE-DI (DB2) ----------- SWDIO/JTMS
		 * DO is tristated with SWD read, so
		 * resistor is not necessary, but protects
		 * from contentions in case of errors.
		 * JTAG not possible
		 * PIN6     (DB6) ----------- NRST
		 */
		.vendor = 0x0403U,
		.product = 0x6010U, /*FT2232H*/
		.interface = INTERFACE_B,
		.init.data[0] = PIN4, /* Pull up pin 4 */
		.init.dirs[0] = PIN4, /* Pull up pin 4 */
		.mpsse_swd_read.set_data_low = MPSSE_DO,
		.mpsse_swd_write.set_data_low = MPSSE_DO,
		.assert_nrst.data[0] = ~PIN6,
		.assert_nrst.dirs[0] = PIN6,
		.deassert_nrst.data[0] = PIN6,
		.deassert_nrst.dirs[0] = ~PIN6,
		.target_voltage_cmd = GET_BITS_LOW,
		.target_voltage_pin = PIN4, /* Always read as target voltage present.*/
		.description = "USBMATE",
		.name = "usbmate",
	},
	{
		/*
		 * MPSSE_SK (DB0) ----------- SWDCK/JTCK
		 * MPSSE-DO (DB1) -- 470 R -- SWDIO/JTMS
		 * MPSSE-DI (DB2) ----------- SWDIO/JTMS
		 * DO is tristated with SWD read, so
		 * resistor is not necessary, but protects
		 * from contentions in case of errors.
		 * JTAG not possible.
		 */
		.vendor = 0x0403U,
		.product = 0x6014U, /*FT232H*/
		.interface = INTERFACE_A,
		.mpsse_swd_read.set_data_low = MPSSE_DO,
		.mpsse_swd_write.set_data_low = MPSSE_DO,
		.name = "ft232h_resistor_swd",
	},
	{
		/*
		 * Buffered connection from FTDI to JTAG/SWD.
		 * TCK and TMS are not independently switchable.
		 * => SWD is not possible.
		 * PIN4 low enables buffers
		 * PIN5 Low indicates VRef applied
		 * PIN6 reads back nRST
		 * CBUS PIN1 Sets nRST
		 * CBUS PIN2 low drives nRST
		 */
		.vendor = 0x0403U,
		.product = 0x6010U,
		.interface = INTERFACE_A,
		.init.dirs = {PIN4, PIN4 | PIN3 | PIN2 | PIN1 | PIN0},
		.init.data = {0, PIN4 | PIN3 | PIN2},
		.assert_nrst.data[1] = ~PIN3,
		.deassert_nrst.data[1] = PIN3,
		.nrst_get_port_cmd = GET_BITS_LOW,
		.nrst_get_pin = PIN6,
		.description = "FTDIJTAG",
		.name = "ftdijtag",
	},
	{
		/*
		 * UART/SWO on Interface A
		 * JTAG and control on INTERFACE_B
		 * Bit 5 high selects SWD-WRITE (TMS routed to MPSSE_DI)
		 * Bit 6 high selects JTAG vs SWD (TMS routed to MPSSE_CS)
		 * BCBUS 1 (Output) N_RST
		 * BCBUS 2 (Input/Internal pull-up) V_ISO available
		 *
		 * For bitbanged SWD, set Bit 5 low and select SWD read with
		 * Bit 6 low. Read Connector TMS as MPSSE_DI.
		 *
		 * TDO is routed to Interface 0 RXD as SWO or with UART
		 * Connector pin 10 pulled to ground will connect Interface 0 RXD
		 * to UART connector RXD
		 */
		.vendor = 0x0403U,
		.product = 0x6010U,
		.interface = INTERFACE_B,
		.init.data = {PIN6 | PIN5, PIN1 | PIN2},
		.init.dirs = {PIN6 | PIN5, 0},
		.assert_nrst.data[1] = ~PIN1,
		.assert_nrst.dirs[1] = PIN1,
		.deassert_nrst.data[1] = PIN1,
		.deassert_nrst.dirs[1] = ~PIN1,
		.mpsse_swd_read.clr_data_low = PIN5 | PIN6,
		.mpsse_swd_write.set_data_low = PIN5,
		.mpsse_swd_write.clr_data_low = PIN6,
		.jtag.set_data_low = PIN6,
		.target_voltage_cmd = GET_BITS_HIGH,
		.target_voltage_pin = ~PIN2,
		.name = "ftdiswd",
		.description = "FTDISWD",
	},
	{
		.vendor = 0x15b1U,
		.product = 0x0003U,
		.interface = INTERFACE_A,
		.init.dirs[0] = PIN5,
		.name = "olimex",
	},
	{
		/*
		 * Buffered connection from FTDI to JTAG/SWD.
		 * TCK and TMS are not independently switchable.
		 * => SWD is not possible.
		 * DBUS PIN4 / JTAGOE low enables buffers
		 * DBUS PIN5 / TRST high drives nTRST low OC
		 * DBUS PIN6 / RST high drives nRST low OC
		 * CBUS PIN0 reads back nRST
		 */
		.vendor = 0x0403U,
		.product = 0xbdc8U,
		.interface = INTERFACE_A,
		/* Drive low to activate JTAGOE and deassert TRST/RST.*/
		.init.data = {0, 0},
		.init.dirs = {PIN6 | PIN5 | PIN4, PIN2 /* One LED */},
		.assert_nrst.data[0] = PIN6,
		.deassert_nrst.data[0] = ~PIN6,
		.nrst_get_port_cmd = GET_BITS_HIGH,
		.nrst_get_pin = PIN0,
		.name = "turtelizer",
		.description = "Turtelizer JTAG/RS232 Adapter",
	},
	{
		/*
		 * https://reference.digilentinc.com/jtag_hs1/jtag_hs1
		 * No schmeatics available.
		 * Buffered from FTDI to JTAG/SWD announced
		 * Independent switch for TMS not known
		 * => SWD not possible. */
		.vendor = 0x0403U,
		.product = 0xbdc8U,
		.interface = INTERFACE_A,
		.name = "jtaghs1",
	},
	{
		/* Direct connection from FTDI to JTAG/SWD assumed.*/
		.vendor = 0x0403U,
		.product = 0xbdc8U,
		.interface = INTERFACE_A,
		.init.data[0] = MPSSE_CS | MPSSE_DO | MPSSE_DI,
		.init.dirs[0] = MPSSE_CS | MPSSE_DO | MPSSE_SK,
		.bb_swdio_in_port_cmd = GET_BITS_LOW,
		.bb_swdio_in_pin = MPSSE_CS,
		.name = "ftdi",
	},
	{
		/* Product name not unique! Assume SWD not possible.*/
		.vendor = 0x0403U,
		.product = 0x6014U,
		.interface = INTERFACE_A,
		.init.data = {PIN7, PIN5},
		.init.dirs = {PIN7, PIN5 | PIN4 | PIN3 | PIN2 | PIN1 | PIN0},
		.name = "digilent",
	},
	{
		/* Direct connection from FTDI to JTAG/SWD assumed.*/
		.vendor = 0x0403U,
		.product = 0x6014U,
		.interface = INTERFACE_A,
		.init.data[0] = MPSSE_CS | MPSSE_DO | MPSSE_DI,
		.init.dirs[0] = MPSSE_CS | MPSSE_DO | MPSSE_SK,
		.bb_swdio_in_port_cmd = GET_BITS_LOW,
		.bb_swdio_in_pin = MPSSE_CS,
		.name = "ft232h",
	},
	{
		/*
		 * MPSSE-SK (AD0) ----------- SWCLK/JTCK
		 * MPSSE-DO (AD1) ----------- SWDIO/JTMS
		 * MPSSE-DI (AD2) -- 330 R -- SWDIO/JTMS
		 *                  (470 R or similar also fine)
		 */
		.vendor = 0x0403U,
		.product = 0x6011U,
		.interface = INTERFACE_A,
		.mpsse_swd_read.set_data_low = MPSSE_DI,
		.mpsse_swd_write.set_data_low = MPSSE_DO,
		.description = "FT4232H-56Q MiniModule",
		.name = "ft4232h",
	},
	{
		/*
		 * http://www.olimex.com/dev/pdf/ARM-USB-OCD.pdf.
		 * DBUS 4 global enables JTAG Buffer.
		 * TCK and TMS are not independently switchable.
		 * => SWD is not possible.
		 */
		.vendor = 0x15baU,
		.product = 0x002bU,
		.interface = INTERFACE_A,
		.init.data = {0, PIN3 | PIN1 | PIN0},
		.init.dirs = {PIN4, PIN4 | PIN3 | PIN1 | PIN0},
		.name = "arm-usb-ocd-h",
	},
	{
		/*
		 * JTAG buffered on Interface A -> No SWD
		 * Standard VID/PID/Product
		 * No nRST on the 10 pin connectors
		 *
		 * This device has no explicit reset.
		 * => SWD is not possible.
		 *
		 * JTAG enabled by default, ESP_EN pulled up,
		 * inverted by U4 and enabling JTAG by U5
		 */
		.vendor = 0x0403U,
		.product = 0x6010U,
		.interface = INTERFACE_A,
		.name = "esp-prog",
	},
	{
		/*
		 * https://github.com/tigard-tools/tigard#pinouts
		 * MPSSE_SK (DB0) ----------- SWCLK/TCK
		 * Mode-Switch 1-2/4-5: JTAG
		 * MPSSE-DO (DB1) ----------- TDI
		 * MPSSE-DI (DB2) ----------- TDO
		 * MPSSE-CS (DB3) ----------- TMS
		 * Mode-Switch 3-2/6-5: SWD
		 * MPSSE-DO (DB1) -- 330 R -- SWDIO
		 * MPSSE-DI (DB2) ----------- SWDIO
		 * Indicate Mode-SW set to SWD with "-e" on the command line
		 * TRST is Push/Pull, not OD!
		 * PIN4     (DB4) ----------- nTRST
		 * nRST is Push/Pull, not OD! Keep DDR set.
		 * PIN5     (DB5) ----------- nRST
		 */
		.vendor = 0x0403U,
		.product = 0x6010U, /* FT2232H */
		.interface = INTERFACE_B,
		.init.data[1] = PIN4 | PIN5, /* High   on PIN4/5 */
		.init.dirs[1] = PIN4 | PIN5, /* Output on PIN4/5 */
		.assert_nrst.data[0] = ~PIN5,
		.assert_nrst.dirs[0] = PIN5,
		.deassert_nrst.data[0] = PIN5,
		.deassert_nrst.dirs[0] = PIN5,
		.nrst_get_pin = ~PIN5,
		.target_voltage_cmd = GET_BITS_LOW,
		.bb_swdio_in_port_cmd = GET_BITS_LOW,
		.bb_swdio_in_pin = MPSSE_DI,
		.mpsse_swd_read.set_data_low = MPSSE_DI,
		.mpsse_swd_write.set_data_low = MPSSE_DO,
		.description = "Tigard", /* The actual description string is "Tigard" followed by the version string */
		.name = "tigard",
	},
	{
		/*
		 * https://sifive.cdn.prismic.io/sifive/b5c95ddd-22af-4be0-8021-50327e186b07_hifive1-a-schematics.pdf
		 * Direct connection on Interface-A
		 * Reset on PIN5, Open-Drain, pulled up to 3.3V
		 * and decoupled from FE310 reset voa Schottky
		 */
		.vendor = 0x0403U,
		.product = 0x6010U,
		.interface = INTERFACE_A,
		.assert_nrst.data[0] = ~PIN5,
		.assert_nrst.dirs[0] = PIN5,
		.deassert_nrst.data[0] = PIN5,
		.deassert_nrst.dirs[0] = ~PIN5,
		.bb_swdio_in_port_cmd = GET_BITS_LOW,
		.bb_swdio_in_pin = MPSSE_CS,
		.name = "hifive1",
	},
	{
		/*
		 * https://www.olimex.com/Products/ARM/JTAG/ARM-USB-TINY-H/
		 *
		 * schematics not available
		 */
		.vendor = 0x15b1U,
		.product = 0x002aU,
		.interface = INTERFACE_A,
		.init.data = {PIN4, PIN2 | PIN4},
		.init.dirs = {PIN4 | PIN5, PIN4},
		.assert_nrst.data[1] = ~PIN2,
		.assert_nrst.dirs[1] = PIN2,
		.deassert_nrst.data[1] = PIN2,
		.deassert_nrst.dirs[1] = ~PIN2,
		.name = "arm-usb-tiny-h",
		.description = "Olimex OpenOCD JTAG ARM-USB-TINY-H",
	},
	{0},
};

/*
 * Search the adapter descriptor table for probes matching the VID/PID for the given probe.
 * If a single match is found, place the adapter descriptor pointer into the cl_opts structure
 * and return true. Otherwise return false.
 */
bool ftdi_lookup_adapter_from_vid_pid(bmda_cli_options_s *const cl_opts, const probe_info_s *const probe)
{
	/* If the user entered a serial number, check if the attached probe is the right one */
	if (cl_opts->opt_serial && strstr(cl_opts->opt_serial, probe->serial))
		return true;

	/* If the user entered an adapter name use it */
	if (cl_opts->opt_cable)
		return true;

	size_t adapter_count = 0;
	const cable_desc_s *selection = NULL;

	for (const cable_desc_s *cable = &cable_desc[0]; cable->vendor; ++cable) {
		if (cable->vendor == probe->vid && cable->product == probe->pid) {
			++adapter_count;
			selection = cable;
		}
	}
	/* If we've found only one adaptor, place the adapter name into cl_opts */
	if (adapter_count == 1)
		cl_opts->opt_cable = selection->name;
	return adapter_count == 1;
}

bool ftdi_lookup_cable_by_product(bmda_cli_options_s *cl_opts, const char *product)
{
	if (cl_opts->opt_cable)
		return true;

	for (const cable_desc_s *cable = &cable_desc[0]; cable->vendor; ++cable) {
		if (cable->description && strstr(product, cable->description) != 0) {
			cl_opts->opt_cable = cable->name;
			return true;
		}
	}
	return false;
}

bool ftdi_lookup_adaptor_descriptor(bmda_cli_options_s *cl_opts, const probe_info_s *probe)
{
	return ftdi_lookup_cable_by_product(cl_opts, probe->product);
}

bool ftdi_bmp_init(bmda_cli_options_s *const cl_opts)
{
	int err;
	const cable_desc_s *cable = cable_desc;
	for (; cable->name; cable++) {
		if (strncmp(cable->name, cl_opts->opt_cable, strlen(cable->name)) == 0)
			break;
	}

	if (!cable->name) {
		DEBUG_ERROR("No adaptor matching found for %s\n", cl_opts->opt_cable);
		return false;
	}

	active_cable = *cable;
	memcpy(&active_state, &active_cable.init, sizeof(ftdi_port_state_s));
	/* If the adaptor being used is Tigard, NULL the description out as libftdi can't deal with the partial match. */
	if (active_cable.description && memcmp(active_cable.description, "Tigard", 7) == 0)
		active_cable.description = NULL;
	/*
	 * If swd_(read|write) is not given for the selected cable and
	 * the 'e' command line argument is give, assume resistor SWD
	 * connection.
	 */
	if (cl_opts->external_resistor_swd && active_cable.mpsse_swd_read.set_data_low == 0 &&
		active_cable.mpsse_swd_read.clr_data_low == 0 && active_cable.mpsse_swd_read.set_data_high == 0 &&
		active_cable.mpsse_swd_read.clr_data_high == 0 && active_cable.mpsse_swd_write.set_data_low == 0 &&
		active_cable.mpsse_swd_write.clr_data_low == 0 && active_cable.mpsse_swd_write.set_data_high == 0 &&
		active_cable.mpsse_swd_write.clr_data_high == 0) {
		DEBUG_INFO("Using external resistor SWD\n");
		active_cable.mpsse_swd_read.set_data_low = MPSSE_DO;
		active_cable.mpsse_swd_write.set_data_low = MPSSE_DO;
	} else if (!ftdi_swd_possible() && cl_opts->opt_scanmode != BMP_SCAN_JTAG) {
		DEBUG_WARN("SWD with adaptor not possible, trying JTAG\n");
		cl_opts->opt_scanmode = BMP_SCAN_JTAG;
	}

	ftdi_context_s *ctx = ftdi_new();
	if (ctx == NULL) {
		DEBUG_ERROR("ftdi_new: %s\n", ftdi_get_error_string(ctx));
		abort();
	}
	err = ftdi_set_interface(ctx, active_cable.interface);
	if (err != 0) {
		DEBUG_ERROR("ftdi_set_interface: %d: %s\n", err, ftdi_get_error_string(ctx));
		goto error_1;
	}
	err = ftdi_usb_open_desc(
		ctx, active_cable.vendor, active_cable.product, active_cable.description, cl_opts->opt_serial);
	if (err != 0) {
		DEBUG_ERROR("unable to open ftdi device: %d (%s)\n", err, ftdi_get_error_string(ctx));
		goto error_1;
	}
	err = ftdi_set_latency_timer(ctx, 1);
	if (err != 0) {
		DEBUG_ERROR("ftdi_set_latency_timer: %d: %s\n", err, ftdi_get_error_string(ctx));
		goto error_2;
	}
	err = ftdi_set_baudrate(ctx, 1000000);
	if (err != 0) {
		DEBUG_ERROR("ftdi_set_baudrate: %d: %s\n", err, ftdi_get_error_string(ctx));
		goto error_2;
	}
	err = ftdi_write_data_set_chunksize(ctx, BUF_SIZE);
	if (err != 0) {
		DEBUG_ERROR("ftdi_write_data_set_chunksize: %d: %s\n", err, ftdi_get_error_string(ctx));
		goto error_2;
	}
	assert(ctx != NULL);
#ifdef _Ftdi_Pragma
	err = ftdi_tcioflush(ctx);
#else
	err = ftdi_usb_purge_buffers(ctx);
#endif
	if (err != 0) {
		DEBUG_ERROR("ftdi_tcioflush(ftdi_usb_purge_buffer): %d: %s\n", err, ftdi_get_error_string(ctx));
		goto error_2;
	}
	/* Reset MPSSE controller. */
	err = ftdi_set_bitmode(ctx, 0, BITMODE_RESET);
	if (err != 0) {
		DEBUG_ERROR("ftdi_set_bitmode: %d: %s\n", err, ftdi_get_error_string(ctx));
		goto error_2;
	}
	/* Enable MPSSE controller. Pin directions are set later.*/
	err = ftdi_set_bitmode(ctx, 0, BITMODE_MPSSE);
	if (err != 0) {
		DEBUG_ERROR("ftdi_set_bitmode: %d: %s\n", err, ftdi_get_error_string(ctx));
		goto error_2;
	}
	uint8_t ftdi_init[16];
	/* Test for pending garbage.*/
	int garbage = ftdi_read_data(ctx, ftdi_init, sizeof(ftdi_init));
	if (garbage > 0) {
		DEBUG_WARN("FTDI init garbage at start:");
		for (int i = 0; i < garbage; i++)
			DEBUG_WARN(" %02x", ftdi_init[i]);
		DEBUG_WARN("\n");
	}
	size_t index = 0;
	ftdi_init[index++] = LOOPBACK_END; /* FT2232D gets upset otherwise*/
	switch (ctx->type) {
	case TYPE_2232H:
	case TYPE_4232H:
	case TYPE_232H:
		ftdi_init[index++] = DIS_DIV_5;
		break;
	case TYPE_2232C:
		break;
	default:
		DEBUG_ERROR("FTDI Chip has no MPSSE\n");
		goto error_2;
	}

	bmda_probe_info.ftdi_ctx = ctx;
	ftdi_init[index++] = TCK_DIVISOR;
	/* Use CLK/2 for about 50 % SWDCLK duty cycle on FT2232c.*/
	ftdi_init[index++] = 1;
	ftdi_init[index++] = 0;
	ftdi_init[index++] = SET_BITS_LOW;
	ftdi_init[index++] = active_state.data[0];
	ftdi_init[index++] = active_state.dirs[0];
	ftdi_init[index++] = SET_BITS_HIGH;
	ftdi_init[index++] = active_state.data[1];
	ftdi_init[index++] = active_state.dirs[1];
	ftdi_buffer_write(ftdi_init, index);
	ftdi_buffer_flush();
	garbage = ftdi_read_data(ctx, ftdi_init, sizeof(ftdi_init));
	if (garbage > 0) {
		DEBUG_WARN("FTDI init garbage at end:");
		for (int i = 0; i < garbage; i++)
			DEBUG_WARN(" %02x", ftdi_init[i]);
		DEBUG_WARN("\n");
	}
	return true;

error_2:
	ftdi_usb_close(ctx);
error_1:
	ftdi_free(ctx);
	return false;
}

static void libftdi_set_data(ftdi_port_state_s *data)
{
	uint8_t cmd[6];
	size_t index = 0;
	if (data->data[0] || data->dirs[0]) {
		/* If non-zero and positive if signed */
		if (data->data[0] && !(data->data[0] & 0x8000U))
			active_state.data[0] |= data->data[0] & 0xffU;
		/* If negative if signed */
		else if (data->data[0] & 0x8000U)
			active_state.data[0] &= data->data[0] & 0xffU;

		/* If non-zero and positive if signed */
		if (data->dirs[0] && !(data->dirs[0] & 0x8000U))
			active_state.dirs[0] |= data->dirs[0] & 0xffU;
		/* If negative if signed */
		else if (data->dirs[0] & 0x8000U)
			active_state.dirs[0] &= data->dirs[0] & 0xffU;

		/* Having adjusted the active state, configure the pins */
		cmd[index + 0U] = SET_BITS_LOW;
		cmd[index + 1U] = active_state.data[0];
		cmd[index + 2U] = active_state.dirs[0];
		index += 3U;
	}
	if (data->data[1] || data->dirs[1]) {
		/* If non-zero and positive if signed */
		if (data->data[1] && !(data->data[1] & 0x8000U))
			active_state.data[1] |= data->data[1] & 0xffU;
		/* If negative if signed */
		else if (data->data[1] & 0x8000U)
			active_state.data[1] &= data->data[1] & 0xffU;

		/* If non-zero and positive if signed */
		if (data->dirs[1] && !(data->dirs[1] & 0x8000U))
			active_state.dirs[1] |= data->dirs[1] & 0xffU;
		/* If negative if signed */
		else if (data->dirs[1] & 0x8000U)
			active_state.dirs[1] &= data->dirs[1] & 0xffU;

		/* Having adjusted the active state, configure the pins */
		cmd[index + 0U] = SET_BITS_HIGH;
		cmd[index + 1U] = active_state.data[1];
		cmd[index + 2U] = active_state.dirs[1];
		index += 3U;
	}
	/* If any adjustments needed to be made, send the commands and flush */
	if (index) {
		ftdi_buffer_write(cmd, index);
		ftdi_buffer_flush();
	}
}

void libftdi_nrst_set_val(bool assert)
{
	if (assert)
		libftdi_set_data(&active_cable.assert_nrst);
	else
		libftdi_set_data(&active_cable.deassert_nrst);
}

bool ftdi_nrst_get_val(void)
{
	uint8_t cmd;
	uint8_t pin = 0;
	if (active_cable.nrst_get_port_cmd && active_cable.nrst_get_pin) {
		cmd = active_cable.nrst_get_port_cmd;
		pin = active_cable.nrst_get_pin;
	} else if (active_cable.assert_nrst.data[0] && active_cable.assert_nrst.dirs[0]) {
		cmd = GET_BITS_LOW;
		pin = active_cable.assert_nrst.data[0];
	} else if (active_cable.assert_nrst.data[1] && active_cable.assert_nrst.dirs[1]) {
		cmd = GET_BITS_HIGH;
		pin = active_cable.assert_nrst.data[1];
	} else
		return false;

	uint8_t data;
	ftdi_buffer_write_val(cmd);
	ftdi_buffer_read_val(data);
	bool res = false;
	if (pin < 0x7fU || pin == PIN7)
		res = data & pin;
	else
		res = !(data & ~pin);
	return res;
}

#if defined(USE_USB_VERSION_BIT)
static ftdi_transfer_control_s *tc_write = NULL;
#endif

void ftdi_buffer_flush(void)
{
	if (!bufptr)
		return;
	DEBUG_WIRE("%s: %u bytes\n", __func__, bufptr);
#if defined(USE_USB_VERSION_BIT)
	if (tc_write)
		ftdi_transfer_data_done(tc_write);
	tc_write = ftdi_write_data_submit(bmda_probe_info.ftdi_ctx, outbuf, bufptr);
#else
	assert(ftdi_write_data(bmda_probe_info.ftdi_ctx, outbuf, bufptr) == bufptr);
#endif
	bufptr = 0;
}

size_t ftdi_buffer_write(const void *const buffer, const size_t size)
{
	if ((bufptr + size) / BUF_SIZE > 0)
		ftdi_buffer_flush();

	const uint8_t *const data = (const uint8_t *)buffer;
	DEBUG_WIRE("%s: %zu bytes:", __func__, size);
	for (size_t i = 0; i < size; i++) {
		DEBUG_WIRE(" %02x", data[i]);
		if (i && (i & 0xfU) == 0xfU)
			DEBUG_WIRE("\n\t");
	}
	DEBUG_WIRE("\n");
	memcpy(outbuf + bufptr, buffer, size);
	bufptr += size;
	return size;
}

size_t ftdi_buffer_read(void *const buffer, const size_t size)
{
	if (bufptr) {
		const uint8_t cmd = SEND_IMMEDIATE;
		ftdi_buffer_write(&cmd, 1);
		ftdi_buffer_flush();
	}

	uint8_t *const data = (uint8_t *)buffer;
#if defined(USE_USB_VERSION_BIT)
	ftdi_transfer_control_s *transfer = ftdi_read_data_submit(bmda_probe_info.ftdi_ctx, data, (int)size);
	ftdi_transfer_data_done(transfer);
#else
	for (size_t index = 0; index < size;)
		index += ftdi_read_data(bmda_probe_info.ftdi_ctx, data + index, size - index);
#endif

	DEBUG_WIRE("%s: %zu bytes:", __func__, size);
	for (size_t i = 0; i < size; i++) {
		DEBUG_WIRE(" %02x", data[i]);
		if ((i & 0xfU) == 0xfU)
			DEBUG_WIRE("\n\t");
	}
	DEBUG_WIRE("\n");
	return size;
}

void ftdi_jtag_tdi_tdo_seq(uint8_t *data_out, const bool final_tms, const uint8_t *data_in, size_t clock_cycles)
{
	if (!clock_cycles || (!data_in && !data_out))
		return;

	DEBUG_PROBE("%s: %s %zu clock cycles\n", __func__,
		data_in && data_out ? "read/write" :
			data_in         ? "write" :
							  "read",
		clock_cycles);

	/* Start by calculating the number of full bytes we can send and how many residual bits there will be */
	const size_t bytes = (clock_cycles - (final_tms ? 1U : 0U)) >> 3U;
	size_t bits = clock_cycles & 7U;
	/* If the transfer would be a whole number of bytes if not for final_tms, adjust bits accordingly */
	if (!bits && final_tms)
		bits = 8U;
	const size_t final_byte = (clock_cycles - 1U) >> 3U;
	const size_t final_bit = (clock_cycles - 1U) & 7U;

	/* Set up a suitable initial transfer command for the data */
	const uint8_t cmd =
		(data_out ? MPSSE_DO_READ : 0U) | (data_in ? (MPSSE_DO_WRITE | MPSSE_WRITE_NEG) : 0U) | MPSSE_LSB;

	/* Set up the transfer for the number of whole bytes specified */
	if (bytes) {
		ftdi_mpsse_cmd_s command = {cmd};
		write_le2(command.length, 0, bytes - 1U);
		ftdi_buffer_write_val(command);
		/* If there's data to send, queue it */
		if (data_in)
			ftdi_buffer_write(data_in, bytes);
	}

	/* Now set up a transfer for the residual bits needed */
	if (bits - (final_tms ? 1U : 0U)) {
		/* Set up the bitwise command and its length */
		const ftdi_mpsse_cmd_bits_s command = {cmd | MPSSE_BITMODE, bits - (final_tms ? 2U : 1U)};
		ftdi_buffer_write_val(command);
		/* If there's data to send, queue it */
		if (data_in)
			ftdi_buffer_write_val(data_in[bytes]);
	}

	/* Finally, if TMS should be 1 after we get done, set up the final command to do this. */
	if (final_tms) {
		/* The command length byte is 0 after this, indicating 1 bit to go */
		ftdi_mpsse_cmd_bits_s command = {
			MPSSE_WRITE_TMS | (data_out ? MPSSE_DO_READ : 0) | MPSSE_LSB | MPSSE_BITMODE | MPSSE_WRITE_NEG};
		ftdi_buffer_write_val(command);
		/* The LSb determins what TMS gets set to */
		uint8_t data = 1U;
		/* If there's data to send, queue it */
		if (data_in) {
			/* The final bit to send has to go into the MSb of the data byte */
			const uint8_t value = (data_in[final_byte] >> final_bit) & 1U;
			data |= value << 7U;
		}
		/* Queue the data portion of the operation */
		ftdi_buffer_write_val(data);
	}

	/* If we're expecting data back, start reading */
	if (data_out) {
		/* Read the whole bytes */
		if (bytes)
			ftdi_buffer_read(data_out, bytes);
		/* Read the residual bits */
		if (bits) {
			ftdi_buffer_read_val(data_out[bytes]);
			/* Because of a quirk in how the FTDI device works, the bits will be MSb aligned, so shift them down */
			const size_t shift = bits - (final_tms ? 1U : 0U);
			data_out[bytes] >>= 8U - shift;
		}
		/* And read the data assocated with the TMS transaction and adjust the final byte */
		if (final_tms) {
			uint8_t value = 0;
			ftdi_buffer_read_val(value);
			data_out[final_byte] |= (value & 0x80U) >> (7U - final_bit);
		}
	}
}

const char *ftdi_target_voltage(void)
{
	uint8_t pin = active_cable.target_voltage_pin;
	if (active_cable.target_voltage_cmd && pin) {
		ftdi_buffer_write(&active_cable.target_voltage_cmd, 1);
		uint8_t data[1];
		ftdi_buffer_read(data, 1);
		bool res = false;
		if (pin < 0x7fU || pin == PIN7)
			res = data[0] & pin;
		else
			res = !(data[0] & ~pin);
		if (res)
			return "Present";
		return "Absent";
	}
	return NULL;
}

static uint16_t divisor;

void ftdi_max_frequency_set(uint32_t freq)
{
	uint32_t clock;
	if (bmda_probe_info.ftdi_ctx->type == TYPE_2232C)
		clock = 12U * 1000U * 1000U;
	else
		/* Undivided clock set during startup*/
		clock = 60U * 1000U * 1000U;

	uint32_t div = (clock + 2U * freq - 1U) / freq;
	if (div < 4U && bmda_probe_info.ftdi_ctx->type == TYPE_2232C)
		div = 4U; /* Avoid bad asymmetric FT2232C clock at 6 MHz*/
	divisor = div / 2U - 1U;
	uint8_t buf[3];
	buf[0] = TCK_DIVISOR;
	buf[1] = divisor & 0xffU;
	buf[2] = (divisor >> 8U) & 0xffU;
	ftdi_buffer_write_arr(buf);
}

uint32_t libftdi_max_frequency_get(void)
{
	uint32_t clock;
	if (bmda_probe_info.ftdi_ctx->type == TYPE_2232C)
		clock = 12U * 1000U * 1000U;
	else
		/* Undivided clock set during startup*/
		clock = 60U * 1000U * 1000U;
	return clock / (2U * (divisor + 1U));
}
