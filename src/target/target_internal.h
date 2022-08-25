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

#ifndef TARGET_TARGET_INTERNAL_H
#define TARGET_TARGET_INTERNAL_H

#include "platform_support.h"

extern target *target_list;
target *target_new(void);

struct target_ram {
	target_addr_t start;
	size_t length;
	struct target_ram *next;
};

typedef struct target_flash target_flash_s;

typedef bool (*flash_prepare_func)(target_flash_s *f);
typedef bool (*flash_erase_func)(target_flash_s *f, target_addr_t addr, size_t len);
typedef bool (*flash_write_func)(target_flash_s *f, target_addr_t dest, const void *src, size_t len);
typedef bool (*flash_done_func)(target_flash_s *f);

struct target_flash {
	target *t;                   /* Target this flash is attached to */
	target_addr_t start;         /* start address of flash */
	size_t length;               /* flash length */
	size_t blocksize;            /* erase block size */
	size_t writesize;            /* write operation size, must be <= blocksize/writebufsize */
	size_t writebufsize;         /* size of write buffer */
	uint8_t erased;              /* byte erased state */
	bool ready;                  /* true if flash is in flash mode/prepared */
	flash_prepare_func prepare;  /* prepare for flash operations */
	flash_erase_func erase;      /* erase a range of flash */
	flash_write_func write;      /* write to flash */
	flash_done_func done;        /* finish flash operations */
	void *buf;                   /* buffer for flash operations */
	target_addr_t buf_addr_base; /* address of block this buffer is for */
	target_addr_t buf_addr_low;  /* address of lowest byte written */
	target_addr_t buf_addr_high; /* address of highest byte written */
	target_flash_s *next;        /* next flash in list */
};

typedef bool (*cmd_handler)(target *t, int argc, const char **argv);

typedef struct command_s {
	const char *cmd;
	cmd_handler handler;
	const char *help;
} command_t;

struct target_command_s {
	const char *specific_name;
	const struct command_s *cmds;
	struct target_command_s *next;
};

struct breakwatch {
	struct breakwatch *next;
	enum target_breakwatch type;
	target_addr_t addr;
	size_t size;
	uint32_t reserved[4]; /* for use by the implementing driver */
};

#define MAX_CMDLINE 81

struct target_s {
	bool attached;
	struct target_controller *tc;

	/* Attach/Detach funcitons */
	bool (*attach)(target *t);
	void (*detach)(target *t);
	bool (*check_error)(target *t);

	/* Memory access functions */
	void (*mem_read)(target *t, void *dest, target_addr_t src, size_t len);
	void (*mem_write)(target *t, target_addr_t dest, const void *src, size_t len);

	/* Register access functions */
	size_t regs_size;
	char *tdesc;
	void (*regs_read)(target *t, void *data);
	void (*regs_write)(target *t, const void *data);
	ssize_t (*reg_read)(target *t, int reg, void *data, size_t max);
	ssize_t (*reg_write)(target *t, int reg, const void *data, size_t size);

	/* Halt/resume functions */
	void (*reset)(target *t);
	void (*extended_reset)(target *t);
	void (*halt_request)(target *t);
	enum target_halt_reason (*halt_poll)(target *t, target_addr_t *watch);
	void (*halt_resume)(target *t, bool step);

	/* Break-/watchpoint functions */
	int (*breakwatch_set)(target *t, struct breakwatch *);
	int (*breakwatch_clear)(target *t, struct breakwatch *);
	struct breakwatch *bw_list;

	/* Recovery functions */
	bool (*mass_erase)(target *t);

	/* Flash functions */
	bool (*enter_flash_mode)(target *t);
	bool (*exit_flash_mode)(target *t);
	bool flash_mode;

	/* target-defined options */
	unsigned target_options;

	void *target_storage;
	union {
		bool unsafe_enabled;
		bool ke04_mode;
	};

	struct target_ram *ram;
	target_flash_s *flash;

	/* Other stuff */
	const char *driver;
	uint32_t cpuid;
	char *core;
	char cmdline[MAX_CMDLINE];
	target_addr_t heapinfo[4];
	struct target_command_s *commands;
#ifdef PLATFORM_HAS_USBUART
	bool stdout_redirected;
#endif

	struct target_s *next;

	void *priv;
	void (*priv_free)(void *);

	/* Target designer and id / partno */
	uint16_t designer_code;
	/* targetid partno if available (>= DPv2)
	 * fallback to ap partno
	 */
	uint16_t part_id;
};

void target_print_progress(platform_timeout *timeout);
void target_ram_map_free(target *t);
void target_flash_map_free(target *t);
void target_mem_map_free(target *t);
void target_add_commands(target *t, const struct command_s *cmds, const char *name);
void target_add_ram(target *t, target_addr_t start, uint32_t len);
void target_add_flash(target *t, target_flash_s *f);

target_flash_s *target_flash_for_addr(target *t, uint32_t addr);

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
int tc_open(target *, target_addr_t path, size_t plen, enum target_open_flags flags, mode_t mode);
int tc_close(target *t, int fd);
int tc_read(target *t, int fd, target_addr_t buf, unsigned int count);
int tc_write(target *t, int fd, target_addr_t buf, unsigned int count);
long tc_lseek(target *t, int fd, long offset, enum target_seek_flag flag);
int tc_rename(target *t, target_addr_t oldpath, size_t oldlen, target_addr_t newpath, size_t newlen);
int tc_unlink(target *t, target_addr_t path, size_t plen);
int tc_stat(target *t, target_addr_t path, size_t plen, target_addr_t buf);
int tc_fstat(target *t, int fd, target_addr_t buf);
int tc_gettimeofday(target *t, target_addr_t tv, target_addr_t tz);
int tc_isatty(target *t, int fd);
int tc_system(target *t, target_addr_t cmd, size_t cmdlen);

#endif /* TARGET_TARGET_INTERNAL_H */
