/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2022  1bitsquared - Mikaela Szekely <mikaela.szekely@qyriad.me>
 * Written by Mikaela Szekely <mikaela.szekely@qyriad.me>
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

#include "gdb_reg.h"

const char *gdb_arm_preamble_first = "<?xml version=\"1.0\"?>"
									 "<!DOCTYPE";

const char *gdb_arm_preamble_second = "SYSTEM "
									  "\"gdb-target.dtd\">"
									  "<target>"
									  "  <architecture>arm</architecture>";

const char *gdb_reg_type_strings[] = {
	"",                   // GDB_TYPE_UNSPECIFIED.
	" type=\"data_ptr\"", // GDB_TYPE_DATA_PTR.
	" type=\"code_ptr\"", // GDB_TYPE_CODE_PTR.
};

const char *gdb_reg_save_restore_strings[] = {
	"",                    // GDB_SAVE_RESTORE_UNSPECIFIED.
	" save-restore=\"no\"" // GDB_SAVE_RESTORE_NO.
};
