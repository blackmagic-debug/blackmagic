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

#ifndef INCLUDE_TARGET_H
#define INCLUDE_TARGET_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

typedef struct target target_s;
typedef uint32_t target_addr_t;
typedef struct target_controller target_controller_s;

#if PC_HOSTED == 1
bool bmda_swd_scan(uint32_t targetid);
bool bmda_jtag_scan(void);
#endif
bool adiv5_swd_scan(uint32_t targetid);
bool jtag_scan(void);

size_t target_foreach(void (*callback)(size_t index, target_s *target, void *context), void *context);
void target_list_free(void);

/* Attach/detach functions */
target_s *target_attach(target_s *target, target_controller_s *controller);
target_s *target_attach_n(size_t n, target_controller_s *controller);
void target_detach(target_s *target);
bool target_attached(target_s *target);
const char *target_driver_name(target_s *target);
const char *target_core_name(target_s *target);
unsigned int target_designer(target_s *target);
unsigned int target_part_id(target_s *target);

/* Memory access functions */
bool target_mem_map(target_s *target, char *buf, size_t len);
int target_mem_read(target_s *target, void *dest, target_addr_t src, size_t len);
int target_mem_write(target_s *target, target_addr_t dest, const void *src, size_t len);
bool target_mem_access_needs_halt(target_s *target);
/* Flash memory access functions */
bool target_flash_erase(target_s *target, target_addr_t addr, size_t len);
bool target_flash_write(target_s *target, target_addr_t dest, const void *src, size_t len);
bool target_flash_complete(target_s *target);

/* Register access functions */
size_t target_regs_size(target_s *target);
const char *target_regs_description(target_s *target);
void target_regs_read(target_s *target, void *data);
void target_regs_write(target_s *target, const void *data);
ssize_t target_reg_read(target_s *target, uint32_t reg, void *data, size_t max);
ssize_t target_reg_write(target_s *target, uint32_t reg, const void *data, size_t size);

/* Halt/resume functions */
typedef enum target_halt_reason {
	TARGET_HALT_RUNNING = 0, /* Target not halted */
	TARGET_HALT_ERROR,       /* Failed to read target status */
	TARGET_HALT_REQUEST,
	TARGET_HALT_STEPPING,
	TARGET_HALT_BREAKPOINT,
	TARGET_HALT_WATCHPOINT,
	TARGET_HALT_FAULT,
} target_halt_reason_e;

void target_reset(target_s *target);
void target_halt_request(target_s *target);
target_halt_reason_e target_halt_poll(target_s *target, target_addr_t *watch);
void target_halt_resume(target_s *target, bool step);
void target_set_cmdline(target_s *target, char *cmdline);
void target_set_heapinfo(target_s *target, target_addr_t heap_base, target_addr_t heap_limit, target_addr_t stack_base,
	target_addr_t stack_limit);

/* Break-/watchpoint functions */
typedef enum target_breakwatch {
	TARGET_BREAK_SOFT,
	TARGET_BREAK_HARD,
	TARGET_WATCH_WRITE,
	TARGET_WATCH_READ,
	TARGET_WATCH_ACCESS,
} target_breakwatch_e;

int target_breakwatch_set(target_s *target, target_breakwatch_e, target_addr_t, size_t);
int target_breakwatch_clear(target_s *target, target_breakwatch_e, target_addr_t, size_t);

/* Command interpreter */
void target_command_help(target_s *target);
int target_command(target_s *target, int argc, const char *argv[]);

/* keep target_errno in sync with errno values in gdb/include/gdb/fileio.h */
typedef enum target_errno {
	TARGET_EPERM = 1,
	TARGET_ENOENT = 2,
	TARGET_EINTR = 4,
	TARGET_EIO = 5,
	TARGET_EBADF = 9,
	TARGET_EACCES = 13,
	TARGET_EFAULT = 14,
	TARGET_EBUSY = 16,
	TARGET_EEXIST = 17,
	TARGET_ENODEV = 19,
	TARGET_ENOTDIR = 20,
	TARGET_EISDIR = 21,
	TARGET_EINVAL = 22,
	TARGET_ENFILE = 23,
	TARGET_EMFILE = 24,
	TARGET_EFBIG = 27,
	TARGET_ENOSPC = 28,
	TARGET_ESPIPE = 29,
	TARGET_EROFS = 30,
	TARGET_ENOSYS = 88,
	TARGET_ENAMETOOLONG = 91,
	TARGET_EUNKNOWN = 9999,
} target_errno_e;

typedef enum target_open_flags {
	TARGET_O_RDONLY = 0x0,
	TARGET_O_WRONLY = 0x1,
	TARGET_O_RDWR = 0x2,
	TARGET_O_APPEND = 0x8,
	TARGET_O_CREAT = 0x200,
	TARGET_O_TRUNC = 0x400,
} target_open_flags_e;

typedef enum target_seek_flag {
	TARGET_SEEK_SET = 0,
	TARGET_SEEK_CUR = 1,
	TARGET_SEEK_END = 2,
} target_seek_flag_e;

struct target_controller {
	void (*destroy_callback)(target_controller_s *, target_s *target);
	void (*printf)(target_controller_s *, const char *fmt, va_list);

	/* Interface to host system calls */
	int (*open)(target_controller_s *, target_addr_t path, size_t path_len, target_open_flags_e flags, mode_t mode);
	int (*close)(target_controller_s *, int fd);
	int (*read)(target_controller_s *, int fd, target_addr_t buf, unsigned int count);
	int (*write)(target_controller_s *, int fd, target_addr_t buf, unsigned int count);
	long (*lseek)(target_controller_s *, int fd, long offset, target_seek_flag_e flag);
	int (*rename)(target_controller_s *, target_addr_t oldpath, size_t old_len, target_addr_t newpath, size_t new_len);
	int (*unlink)(target_controller_s *, target_addr_t path, size_t path_len);
	int (*stat)(target_controller_s *, target_addr_t path, size_t path_len, target_addr_t buf);
	int (*fstat)(target_controller_s *, int fd, target_addr_t buf);
	int (*gettimeofday)(target_controller_s *, target_addr_t tv, target_addr_t tz);
	int (*isatty)(target_controller_s *, int fd);
	int (*system)(target_controller_s *, target_addr_t cmd, size_t cmd_len);
	target_errno_e errno_;
	bool interrupted;
};

#endif /* INCLUDE_TARGET_H */
