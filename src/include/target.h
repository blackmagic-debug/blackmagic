/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2016  Black Sphere Technologies Ltd.
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

/* Provides an abstract 'target object', the 'methods' of which must be
 * implemented by a target driver when a supported device is detected.
 */

#ifndef __TARGET_H
#define __TARGET_H

typedef struct target_s target;

int adiv5_swdp_scan(void);
int jtag_scan(const uint8_t *lrlens);

bool target_foreach(void (*cb)(int i, target *t, void *context), void *context);
void target_list_free(void);

/* The destroy callback function will be called by target_list_free() just
 * before the target is free'd.  This may be because we're scanning for new
 * targets, or because of a communication failure.  The target data may
 * be assumed to be intact, but the communication medium may not be available,
 * so access methods shouldn't be called.
 *
 * The callback is installed by target_attach() and only removed by attaching
 * with a different callback.  It remains intact after target_detach().
 */
typedef void (*target_destroy_callback)(target *t);

/* Halt/resume functions */
target *target_attach(target *t, target_destroy_callback destroy_cb);
target *target_attach_n(int n, target_destroy_callback destroy_cb);
void target_detach(target *t);
bool target_check_error(target *t);
bool target_attached(target *t);

/* Memory access functions */
void target_mem_read(target *t, void *dest, uint32_t src, size_t len);
void target_mem_write(target *t, uint32_t dest, const void *src, size_t len);

/* Register access functions */
void target_regs_read(target *t, void *data);
void target_regs_write(target *t, const void *data);

/* Halt/resume functions */
void target_reset(target *t);
void target_halt_request(target *t);
int target_halt_wait(target *t);
void target_halt_resume(target *t, bool step);

/* Break-/watchpoint functions */
int target_set_hw_bp(target *t, uint32_t addr, uint8_t len);
int target_clear_hw_bp(target *t, uint32_t addr, uint8_t len);

int target_set_hw_wp(target *t, uint8_t type, uint32_t addr, uint8_t len);
int target_clear_hw_wp(target *t, uint8_t type, uint32_t addr, uint8_t len);
int target_check_hw_wp(target *t, uint32_t *addr);

/* Flash memory access functions */
int target_flash_erase(target *t, uint32_t addr, size_t len);
int target_flash_write(target *t, uint32_t dest, const void *src, size_t len);
int target_flash_done(target *t);

/* Host I/O */
void target_hostio_reply(target *t, int32_t retcode, uint32_t errcode);

/* Accessor functions */
int target_regs_size(target *t);
const char *target_tdesc(target *t);
const char *target_mem_map(target *t);
const char *target_driver_name(target *t);

struct target_command_s {
	const char *specific_name;
	const struct command_s *cmds;
	struct target_command_s *next;
};

#include "target_internal.h"

#endif

