/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2022-2023 1BitSquared <info@1bitsquared.com>
 * Written by Mikaela Szekely <mikaela.szekely@qyriad.me>
 * With contributions from Rachel Mant <git@dragonmux.network>
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

#ifndef TARGET_GDB_REG_H
#define TARGET_GDB_REG_H

// The beginning XML for GDB target descriptions that are common to all targets,
// save for one word: the word after DOCTYPE, which is "target" for Cortex-M, and "feature"
// for Cortex-A. The "preamble" is thus split into three parts, with this single word missing
// and as the split point.
extern const char *gdb_xml_preamble_first;

// The beginning XML for GDB target descriptions that are common to all targets,
// save for one word: the word after DOCTYPE, which is "target" for Cortex-M, and "feature"
// for Cortex-A. The "preamble" is thus split into three parts, with this single word missing
// and as the split point.
extern const char *gdb_xml_preamble_second;

// The beginning XML for GDB target descriptions that are common to all targets,
// save for one word: the word after <architecture>, which is "arm" for Cortex-*, and "avr"
// for AVR. The "preamble" is thus split into three parts, with this single word missing
// and as the split point.
extern const char *gdb_xml_preamble_third;

// The "type" field of a register tag.
typedef enum gdb_reg_type {
	GDB_TYPE_UNSPECIFIED = 0,
	GDB_TYPE_DATA_PTR,
	GDB_TYPE_CODE_PTR,
} gdb_reg_type_e;

// The strings for the "type" field of a register tag, respective to its gdb_reg_type_e value.
extern const char *gdb_reg_type_strings[];

// The "save-restore" field of a register tag.
typedef enum gdb_reg_save_restore {
	GDB_SAVE_RESTORE_UNSPECIFIED = 0,
	GDB_SAVE_RESTORE_NO,
} gdb_reg_save_restore_e;

// The strings for the "save-restore" field of a register tag, respective to its gdb_reg_save_restore_e value.
extern const char *gdb_reg_save_restore_strings[];

#endif /* TARGET_GDB_REG_H */
