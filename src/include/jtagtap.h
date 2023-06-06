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

#ifndef INCLUDE_JTAGTAP_H
#define INCLUDE_JTAGTAP_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct jtag_proc {
	/* Note: Signal names are as for the device under test. */

	void (*jtagtap_reset)(void);

	/*
	 * tap_next executes one state transition in the JTAG TAP state machine:
	 * - Ensure TCK is low
	 * - Assert the values of TMS and TDI
	 * - Assert TCK (TMS and TDO are latched on rising edge
	 * - Capture the value on TDO
	 * - Release TCK.
	 */
	bool (*jtagtap_next)(const bool tms, const bool tdi);
	void (*jtagtap_tms_seq)(uint32_t tms_states, size_t clock_cycles);

	/*
	 * Shift out a sequence on MS and DI, capture data to DO.
	 * - This is not endian safe: First byte will always be first shifted out.
	 * - DO may be NULL to ignore captured data.
	 * - DO may be point to the same address as DI.
	 */
	void (*jtagtap_tdi_tdo_seq)(uint8_t *data_out, const bool final_tms, const uint8_t *data_in, size_t clock_cycles);
	void (*jtagtap_tdi_seq)(const bool final_tms, const uint8_t *data_in, size_t clock_cycles);
	void (*jtagtap_cycle)(const bool tms, const bool tdi, const size_t clock_cycles);

	/*
	 * Some debug controllers such as the RISC-V debug controller use idle
	 * cycles during operations as part of their function, while others
	 * allow the desirable skipping of the entire state under some circumstances.
	 */
	uint8_t tap_idle_cycles;
} jtag_proc_s;

extern jtag_proc_s jtag_proc;

/* generic soft reset: 1, 1, 1, 1, 1, 0 */
#define jtagtap_soft_reset() jtag_proc.jtagtap_tms_seq(0x1fU, 6)

/* Goto Shift-IR: 1, 1, 0, 0 */
#define jtagtap_shift_ir() jtag_proc.jtagtap_tms_seq(0x03U, 4)

/* Goto Shift-DR: 1, 0, 0 */
#define jtagtap_shift_dr() jtag_proc.jtagtap_tms_seq(0x01U, 3)

/* Goto Run-test/Idle: 1, 1, 0 */
#define jtagtap_return_idle(cycles) jtag_proc.jtagtap_tms_seq(0x01, (cycles) + 1U)

#if PC_HOSTED == 1
bool bmda_jtag_init(void);
#else
void jtagtap_init(void);
#endif

#endif /* INCLUDE_JTAGTAP_H */
