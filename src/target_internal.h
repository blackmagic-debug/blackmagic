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

#ifndef __TARGET_INTERNAL_H
#define __TARGET_INTERNAL_H

extern target *target_list;
target *target_new(unsigned size);

struct target_ram {
	uint32_t start;
	uint32_t length;
	struct target_ram *next;
};

struct target_flash;
typedef int (*flash_erase_func)(struct target_flash *f, uint32_t addr, size_t len);
typedef int (*flash_write_func)(struct target_flash *f, uint32_t dest,
                                const void *src, size_t len);
typedef int (*flash_done_func)(struct target_flash *f);
struct target_flash {
	uint32_t start;
	uint32_t length;
	uint32_t blocksize;
	flash_erase_func erase;
	flash_write_func write;
	flash_done_func done;
	target *t;
	struct target_flash *next;
	int align;
	uint8_t erased;

	/* For buffered flash */
	size_t buf_size;
	flash_write_func write_buf;
	uint32_t buf_addr;
	void *buf;
};

struct target_s {
	/* Notify controlling debugger if target is lost */
	target_destroy_callback destroy_callback;

	/* Attach/Detach funcitons */
	bool (*attach)(target *t);
	void (*detach)(target *t);
	bool (*check_error)(target *t);

	/* Memory access functions */
	void (*mem_read)(target *t, void *dest, uint32_t src,
	                 size_t len);
	void (*mem_write)(target *t, uint32_t dest,
	                  const void *src, size_t len);

	/* Register access functions */
	int regs_size;
	const char *tdesc;
	void (*regs_read)(target *t, void *data);
	void (*regs_write)(target *t, const void *data);

	/* Halt/resume functions */
	void (*reset)(target *t);
	void (*halt_request)(target *t);
	int (*halt_wait)(target *t);
	void (*halt_resume)(target *t, bool step);

	/* Break-/watchpoint functions */
	int (*set_hw_bp)(target *t, uint32_t addr, uint8_t len);
	int (*clear_hw_bp)(target *t, uint32_t addr, uint8_t len);

	int (*set_hw_wp)(target *t, uint8_t type, uint32_t addr, uint8_t len);
	int (*clear_hw_wp)(target *t, uint8_t type, uint32_t addr, uint8_t len);

	int (*check_hw_wp)(target *t, uint32_t *addr);

	/* target-defined options */
	unsigned target_options;
	uint32_t idcode;

	/* Target memory map */
	char *dyn_mem_map;
	struct target_ram *ram;
	struct target_flash *flash;

	/* Host I/O support */
	void (*hostio_reply)(target *t, int32_t retcode, uint32_t errcode);

	const char *driver;
	struct target_command_s *commands;

	struct target_s *next;

	void *priv;
	void (*priv_free)(void *);
};

void target_add_commands(target *t, const struct command_s *cmds, const char *name);
void target_add_ram(target *t, uint32_t start, uint32_t len);
void target_add_flash(target *t, struct target_flash *f);
int target_flash_write_buffered(struct target_flash *f,
                                uint32_t dest, const void *src, size_t len);
int target_flash_done_buffered(struct target_flash *f);

/* Probe for various targets.
 * Actual functions implemented in their respective drivers.
 */
bool stm32f1_probe(target *t);
bool stm32f4_probe(target *t);
bool stm32l0_probe(target *t);
bool stm32l1_probe(target *t);
bool stm32l4_probe(target *t);
bool lmi_probe(target *t);
bool lpc11xx_probe(target *t);
bool lpc15xx_probe(target *t);
bool lpc43xx_probe(target *t);
bool sam3x_probe(target *t);
bool nrf51_probe(target *t);
bool samd_probe(target *t);
bool kinetis_probe(target *t);
bool efm32_probe(target *t);

#endif

