/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2020 Uwe Bonnes
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.	 If not, see <http://www.gnu.org/licenses/>.
 */
#if !defined(__BMP_REMOTE_H_)
#define      __BMP_REMOTE_H_
#include "swdptap.h"
#include "jtagtap.h"

#define REMOTE_MAX_MSG_SIZE (256)

int platform_buffer_write(const uint8_t *data, int size);
int platform_buffer_read(uint8_t *data, int size);

int remote_init(void);
int remote_swdptap_init(swd_proc_t *swd_proc);
int remote_jtagtap_init(jtag_proc_t *jtag_proc);
bool remote_target_get_power(void);
const char *remote_target_voltage(void);
void remote_target_set_power(bool power);
void remote_srst_set_val(bool assert);
bool remote_srst_get_val(void);
const char *platform_target_voltage(void);
#define __BMP_REMOTE_H_
#endif
