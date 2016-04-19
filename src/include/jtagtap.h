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

#ifndef __JTAGTAP_H
#define __JTAGTAP_H

/* Note: Signal names are as for the device under test. */

int jtagtap_init(void);

void jtagtap_reset(void);

uint8_t jtagtap_next(const uint8_t TMS, const uint8_t TDI);
/* tap_next executes one state transision in the JTAG TAP state machine:
 * - Ensure TCK is low
 * - Assert the values of TMS and TDI
 * - Assert TCK (TMS and TDO are latched on rising edge
 * - Caputure the value on TDO
 * - Release TCK.
 */

void jtagtap_tms_seq(uint32_t MS, int ticks);
void jtagtap_tdi_tdo_seq(uint8_t *DO, const uint8_t final_tms, const uint8_t *DI, int ticks);
void jtagtap_tdi_seq(const uint8_t final_tms, const uint8_t *DI, int ticks);
/* Shift out a sequence on MS and DI, capture data to DO.
 * - This is not endian safe: First byte will always be first shifted out.
 * - DO may be NULL to ignore captured data.
 * - DO may be point to the same address as DI.
 */

/* generic soft reset: 1, 1, 1, 1, 1, 0 */
#define jtagtap_soft_reset()	\
	jtagtap_tms_seq(0x1F, 6)

/* Goto Shift-IR: 1, 1, 0, 0 */
#define jtagtap_shift_ir()		\
	jtagtap_tms_seq(0x03, 4)

/* Goto Shift-DR: 1, 0, 0 */
#define jtagtap_shift_dr()		\
	jtagtap_tms_seq(0x01, 3)

/* Goto Run-test/Idle: 1, 1, 0 */
#define jtagtap_return_idle()	\
	jtagtap_tms_seq(0x01, 2)

#endif

