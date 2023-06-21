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

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "platform_support.h"
#include "target_probe.h"

extern target_s *target_list;
target_s *target_new(void);

typedef enum flash_operation {
	FLASH_OPERATION_NONE,
	FLASH_OPERATION_ERASE,
	FLASH_OPERATION_WRITE,
} flash_operation_e;

typedef struct target_ram target_ram_s;

struct target_ram {
	target_addr_t start;
	size_t length;
	target_ram_s *next;
};

typedef struct target_flash target_flash_s;

typedef bool (*flash_prepare_func)(target_flash_s *flash);
typedef bool (*flash_erase_func)(target_flash_s *flash, target_addr_t addr, size_t len);
typedef bool (*flash_write_func)(target_flash_s *flash, target_addr_t dest, const void *src, size_t len);
typedef bool (*flash_done_func)(target_flash_s *flash);

struct target_flash {
	target_s *t;                 /* Target this flash is attached to */
	target_addr_t start;         /* Start address of flash */
	size_t length;               /* Flash length */
	size_t blocksize;            /* Erase block size */
	size_t writesize;            /* Write operation size, must be <= blocksize/writebufsize */
	size_t writebufsize;         /* Size of write buffer, this is calculated and not set in target code */
	uint8_t erased;              /* Byte erased state */
	uint8_t operation;           /* Current Flash operation (none means it's idle/unprepared) */
	flash_prepare_func prepare;  /* Prepare for flash operations */
	flash_erase_func erase;      /* Erase a range of flash */
	flash_write_func write;      /* Write to flash */
	flash_done_func done;        /* Finish flash operations */
	void *buf;                   /* Buffer for flash operations */
	target_addr_t buf_addr_base; /* Address of block this buffer is for */
	target_addr_t buf_addr_low;  /* Address of lowest byte written */
	target_addr_t buf_addr_high; /* Address of highest byte written */
	target_flash_s *next;        /* Next flash in list */
};

typedef bool (*cmd_handler_fn)(target_s *target, int argc, const char **argv);

typedef struct command {
	const char *cmd;
	cmd_handler_fn handler;
	const char *help;
} command_s;

typedef struct target_command target_command_s;

struct target_command {
	const char *specific_name;
	const command_s *cmds;
	target_command_s *next;
};

typedef struct breakwatch breakwatch_s;

struct breakwatch {
	breakwatch_s *next;
	target_breakwatch_e type;
	target_addr_t addr;
	size_t size;
	uint32_t reserved[4]; /* For use by the implementing driver */
};

#define MAX_CMDLINE 81

struct target {
	target_controller_s *tc;

	/* Attach/Detach functions */
	bool (*attach)(target_s *target);
	void (*detach)(target_s *target);
	bool (*check_error)(target_s *target);

	/* Memory access functions */
	void (*mem_read)(target_s *target, void *dest, target_addr_t src, size_t len);
	void (*mem_write)(target_s *target, target_addr_t dest, const void *src, size_t len);

	/* Register access functions */
	size_t regs_size;
	const char *(*regs_description)(target_s *target);
	void (*regs_read)(target_s *target, void *data);
	void (*regs_write)(target_s *target, const void *data);
	ssize_t (*reg_read)(target_s *target, uint32_t reg, void *data, size_t max);
	ssize_t (*reg_write)(target_s *target, uint32_t reg, const void *data, size_t size);

	/* Halt/resume functions */
	void (*reset)(target_s *target);
	void (*extended_reset)(target_s *target);
	void (*halt_request)(target_s *target);
	target_halt_reason_e (*halt_poll)(target_s *target, target_addr_t *watch);
	void (*halt_resume)(target_s *target, bool step);

	/* Break-/watchpoint functions */
	int (*breakwatch_set)(target_s *target, breakwatch_s *);
	int (*breakwatch_clear)(target_s *target, breakwatch_s *);
	breakwatch_s *bw_list;

	/* Recovery functions */
	bool (*mass_erase)(target_s *target);

	/* Flash functions */
	bool (*enter_flash_mode)(target_s *target);
	bool (*exit_flash_mode)(target_s *target);
	bool flash_mode;

	/* Target-defined options */
	uint32_t target_options;

	void *target_storage;

	union {
		bool unsafe_enabled;
		bool ke04_mode;
	};

	target_ram_s *ram;
	target_flash_s *flash;

	/* Other stuff */
	const char *driver;
	uint32_t cpuid;
	char *core;
	char cmdline[MAX_CMDLINE];
	target_addr_t heapinfo[4];
	target_command_s *commands;
#if PC_HOSTED == 0
	bool stdout_redirected;
#endif

	target_s *next;

	void *priv;
	void (*priv_free)(void *);

	/* Target designer and ID / partno */
	uint16_t designer_code;
	/*
	 * Target ID partno if available (>= DPv2)
	 * fallback to AP partno
	 */
	uint16_t part_id;

	bool attached;
};

void target_print_progress(platform_timeout_s *timeout);
void target_ram_map_free(target_s *target);
void target_flash_map_free(target_s *target);
void target_mem_map_free(target_s *target);
void target_add_commands(target_s *target, const command_s *cmds, const char *name);
void target_add_ram(target_s *target, target_addr_t start, uint32_t len);
void target_add_flash(target_s *target, target_flash_s *flash);

target_flash_s *target_flash_for_addr(target_s *target, uint32_t addr);

/* Convenience function for MMIO access */
uint32_t target_mem_read32(target_s *target, uint32_t addr);
uint16_t target_mem_read16(target_s *target, uint32_t addr);
uint8_t target_mem_read8(target_s *target, uint32_t addr);
void target_mem_write32(target_s *target, uint32_t addr, uint32_t value);
void target_mem_write16(target_s *target, uint32_t addr, uint16_t value);
void target_mem_write8(target_s *target, uint32_t addr, uint8_t value);
bool target_check_error(target_s *target);

/* Access to host controller interface */
void tc_printf(target_s *target, const char *fmt, ...);

/* Interface to host system calls */
int tc_open(target_s *, target_addr_t path, size_t plen, target_open_flags_e flags, mode_t mode);
int tc_close(target_s *target, int fd);
int tc_read(target_s *target, int fd, target_addr_t buf, unsigned int count);
int tc_write(target_s *target, int fd, target_addr_t buf, unsigned int count);
long tc_lseek(target_s *target, int fd, long offset, target_seek_flag_e flag);
int tc_rename(target_s *target, target_addr_t oldpath, size_t oldlen, target_addr_t newpath, size_t newlen);
int tc_unlink(target_s *target, target_addr_t path, size_t plen);
int tc_stat(target_s *target, target_addr_t path, size_t plen, target_addr_t buf);
int tc_fstat(target_s *target, int fd, target_addr_t buf);
int tc_gettimeofday(target_s *target, target_addr_t tv, target_addr_t tz);
int tc_isatty(target_s *target, int fd);
int tc_system(target_s *target, target_addr_t cmd, size_t cmdlen);

#endif /* TARGET_TARGET_INTERNAL_H */
