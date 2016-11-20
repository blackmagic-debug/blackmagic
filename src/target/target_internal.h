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
target *target_new(void);

struct target_ram {
	target_addr start;
	size_t length;
	struct target_ram *next;
};

struct target_flash;
typedef int (*flash_erase_func)(struct target_flash *f, target_addr addr, size_t len);
typedef int (*flash_write_func)(struct target_flash *f, target_addr dest,
                                const void *src, size_t len);
typedef int (*flash_done_func)(struct target_flash *f);
struct target_flash {
	target_addr start;
	size_t length;
	size_t blocksize;
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
	target_addr buf_addr;
	void *buf;
};

typedef bool (*cmd_handler)(target *t, int argc, const char **argv);

struct command_s {
	const char *cmd;
	cmd_handler handler;
	const char *help;
};

struct target_command_s {
	const char *specific_name;
	const struct command_s *cmds;
	struct target_command_s *next;
};

struct breakwatch {
	struct breakwatch *next;
	enum target_breakwatch type;
	target_addr addr;
	size_t size;
	uint32_t reserved[4]; /* for use by the implementing driver */
};

struct target_s {
	bool attached;
	struct target_controller *tc;

	/* Attach/Detach funcitons */
	bool (*attach)(target *t);
	void (*detach)(target *t);
	bool (*check_error)(target *t);

	/* Memory access functions */
	void (*mem_read)(target *t, void *dest, target_addr src,
	                 size_t len);
	void (*mem_write)(target *t, target_addr dest,
	                  const void *src, size_t len);

	/* Register access functions */
	size_t regs_size;
	const char *tdesc;
	void (*regs_read)(target *t, void *data);
	void (*regs_write)(target *t, const void *data);

	/* Halt/resume functions */
	void (*reset)(target *t);
	void (*extended_reset)(target *t);
	void (*halt_request)(target *t);
	enum target_halt_reason (*halt_poll)(target *t, target_addr *watch);
	void (*halt_resume)(target *t, bool step);

	/* Break-/watchpoint functions */
	int (*breakwatch_set)(target *t, struct breakwatch*);
	int (*breakwatch_clear)(target *t, struct breakwatch*);
	struct breakwatch *bw_list;

	/* target-defined options */
	unsigned target_options;
	uint32_t idcode;

	/* Target memory map */
	char *dyn_mem_map;
	struct target_ram *ram;
	struct target_flash *flash;

	/* Other stuff */
	const char *driver;
	struct target_command_s *commands;

	struct target_s *next;

	void *priv;
	void (*priv_free)(void *);
};

void target_add_commands(target *t, const struct command_s *cmds, const char *name);
void target_add_ram(target *t, target_addr start, uint32_t len);
void target_add_flash(target *t, struct target_flash *f);
int target_flash_write_buffered(struct target_flash *f,
                                target_addr dest, const void *src, size_t len);
int target_flash_done_buffered(struct target_flash *f);

/* Convenience function for MMIO access */
uint32_t target_mem_read32(target *t, uint32_t addr);
uint16_t target_mem_read16(target *t, uint32_t addr);
uint8_t target_mem_read8(target *t, uint32_t addr);
void target_mem_write32(target *t, uint32_t addr, uint32_t value);
void target_mem_write16(target *t, uint32_t addr, uint16_t value);
void target_mem_write8(target *t, uint32_t addr, uint8_t value);
bool target_check_error(target *t);

/* Access to host controller interface */
void tc_printf(target *t, const char *fmt, ...);

/* Interface to host system calls */
int tc_open(target *, target_addr path, size_t plen,
            enum target_open_flags flags, mode_t mode);
int tc_close(target *t, int fd);
int tc_read(target *t, int fd, target_addr buf, unsigned int count);
int tc_write(target *t, int fd, target_addr buf, unsigned int count);
long tc_lseek(target *t, int fd, long offset,
              enum target_seek_flag flag);
int tc_rename(target *t, target_addr oldpath, size_t oldlen,
                         target_addr newpath, size_t newlen);
int tc_unlink(target *t, target_addr path, size_t plen);
int tc_stat(target *t, target_addr path, size_t plen, target_addr buf);
int tc_fstat(target *t, int fd, target_addr buf);
int tc_gettimeofday(target *t, target_addr tv, target_addr tz);
int tc_isatty(target *t, int fd);
int tc_system(target *t, target_addr cmd, size_t cmdlen);

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
bool sam4l_probe(target *t);
bool nrf51_probe(target *t);
bool samd_probe(target *t);
bool kinetis_probe(target *t);
bool efm32_probe(target *t);

#endif

