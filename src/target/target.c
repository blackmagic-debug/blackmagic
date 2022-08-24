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

#include "general.h"
#include "target_internal.h"
#include "gdb_packet.h"

#include <stdarg.h>
#include <unistd.h>

target *target_list = NULL;

#define STDOUT_READ_BUF_SIZE	64

static bool target_cmd_mass_erase(target *t, int argc, const char **argv);
static bool target_cmd_range_erase(target *t, int argc, const char **argv);

const struct command_s target_cmd_list[] = {
	{"erase_mass", (cmd_handler)target_cmd_mass_erase, "Erase whole device Flash"},
	{"erase_range", (cmd_handler)target_cmd_range_erase, "Erase a range of memory on a device"},
	{NULL, NULL, NULL}
};

static bool nop_function(void)
{
	return true;
}

static bool false_function(void)
{
	return false;
}

target *target_new(void)
{
	target *t = (void*)calloc(1, sizeof(*t));
	if (!t) {			/* calloc failed: heap exhaustion */
		DEBUG_WARN("calloc: failed in %s\n", __func__);
		return NULL;
	}

	if (target_list) {
		target *c = target_list;
		while (c->next)
			c = c->next;
		c->next = t;
	} else {
		target_list = t;
	}

	t->attach = (void*)nop_function;
	t->detach = (void*)nop_function;
	t->mem_read = (void*)nop_function;
	t->mem_write = (void*)nop_function;
	t->reg_read = (void*)nop_function;
	t->reg_write = (void*)nop_function;
	t->regs_read = (void*)nop_function;
	t->regs_write = (void*)nop_function;
	t->reset = (void*)nop_function;
	t->halt_request = (void*)nop_function;
	t->halt_poll = (void*)nop_function;
	t->halt_resume = (void*)nop_function;
	t->check_error = (void*)false_function;

	t->target_storage = NULL;

	target_add_commands(t, target_cmd_list, "Target");
	return t;
}

int target_foreach(void (*cb)(int, target *t, void *context), void *context)
{
	int i = 1;
	target *t = target_list;
	for (; t; t = t->next, i++)
		cb(i, t, context);
	return i;
}


void target_ram_map_free(target *t) {
	while (t->ram) {
		void * next = t->ram->next;
		free(t->ram);
		t->ram = next;
	}
}

void target_flash_map_free(target *t) {
	while (t->flash) {
		void * next = t->flash->next;
		if (t->flash->buf)
			free(t->flash->buf);
		free(t->flash);
		t->flash = next;
	}
}

void target_mem_map_free(target *t)
{
	target_ram_map_free(t);
	target_flash_map_free(t);
}

void target_list_free(void)
{
	struct target_command_s *tc;

	while (target_list) {
		target *t = target_list->next;
		if (target_list->tc && target_list->tc->destroy_callback)
			target_list->tc->destroy_callback(target_list->tc, target_list);
		if (target_list->priv)
			target_list->priv_free(target_list->priv);
		while (target_list->commands) {
			tc = target_list->commands->next;
			free(target_list->commands);
			target_list->commands = tc;
		}
		free(target_list->target_storage);
		target_mem_map_free(target_list);
		while (target_list->bw_list) {
			void * next = target_list->bw_list->next;
			free(target_list->bw_list);
			target_list->bw_list = next;
		}
		free(target_list);
		target_list = t;
	}
}

void target_add_commands(target *t, const struct command_s *cmds, const char *name)
{
	struct target_command_s *tc = malloc(sizeof(*tc));
	if (!tc) {			/* malloc failed: heap exhaustion */
		DEBUG_WARN("malloc: failed in %s\n", __func__);
		return;
	}

	if (t->commands) {
		struct target_command_s *tail;
		for (tail = t->commands; tail->next; tail = tail->next);
		tail->next = tc;
	} else
		t->commands = tc;

	tc->specific_name = name;
	tc->cmds = cmds;
	tc->next = NULL;
}

target *target_attach_n(const size_t n, struct target_controller *tc)
{
	target *t  = target_list;
	for (size_t i = 1; t; t = t->next, ++i) {
		if (i == n)
			return target_attach(t, tc);
	}
	return NULL;
}

target *target_attach(target *t, struct target_controller *tc)
{
	if (t->tc)
		t->tc->destroy_callback(t->tc, t);

	t->tc = tc;
	platform_target_clk_output_enable(true);

	if (!t->attach(t)) {
		platform_target_clk_output_enable(false);
		return NULL;
	}

	t->attached = true;
	return t;
}

void target_add_ram(target *t, target_addr_t start, uint32_t len)
{
	struct target_ram *ram = malloc(sizeof(*ram));
	if (!ram) {			/* malloc failed: heap exhaustion */
		DEBUG_WARN("malloc: failed in %s\n", __func__);
		return;
	}

	ram->start = start;
	ram->length = len;
	ram->next = t->ram;
	t->ram = ram;
}

void target_add_flash(target *t, target_flash_s *f)
{
	if (f->writesize == 0)
		f->writesize = f->blocksize;
	if (f->writebufsize == 0)
		f->writebufsize = f->writesize;
	f->t = t;
	f->next = t->flash;
	t->flash = f;
}

static ssize_t map_ram(char *buf, size_t len, struct target_ram *ram)
{
	return snprintf(buf, len, "<memory type=\"ram\" start=\"0x%08"PRIx32
	                          "\" length=\"0x%"PRIx32"\"/>",
	                          ram->start, (uint32_t)ram->length);
}

static ssize_t map_flash(char *buf, size_t len, target_flash_s *f)
{
	int i = 0;
	i += snprintf(&buf[i], len - i, "<memory type=\"flash\" start=\"0x%08"PRIx32
	                                "\" length=\"0x%"PRIx32"\">",
	                                f->start, (uint32_t)f->length);
	i += snprintf(&buf[i], len - i, "<property name=\"blocksize\">0x%"PRIx32
	                            "</property></memory>",
	                            (uint32_t)f->blocksize);
	return i;
}

bool target_mem_map(target *t, char *tmp, size_t len)
{
	size_t i = 0;
	i = snprintf(&tmp[i], len - i, "<memory-map>");
	/* Map each defined RAM */
	for (struct target_ram *r = t->ram; r; r = r->next)
		i += map_ram(&tmp[i], len - i, r);
	/* Map each defined Flash */
	for (target_flash_s *f = t->flash; f; f = f->next)
		i += map_flash(&tmp[i], len - i, f);
	i += snprintf(&tmp[i], len - i, "</memory-map>");

	if (i > (len -2))
		return false;
	return true;
}

void target_print_progress(platform_timeout *const timeout)
{
	if (platform_timeout_is_expired(timeout)) {
		gdb_out(".");
		platform_timeout_set(timeout, 500);
	}
}

/* Wrapper functions */
void target_detach(target *t)
{
	t->detach(t);
	platform_target_clk_output_enable(false);
	t->attached = false;
#if PC_HOSTED == 1
	platform_buffer_flush();
#endif
}

bool target_check_error(target *t) {
	if (t)
		return t->check_error(t);
	return false;
}

bool target_attached(target *t) { return t->attached; }

/* Memory access functions */
int target_mem_read(target *t, void *dest, target_addr_t src, size_t len)
{
	t->mem_read(t, dest, src, len);
	return target_check_error(t);
}

int target_mem_write(target *t, target_addr_t dest, const void *src, size_t len)
{
	t->mem_write(t, dest, src, len);
	return target_check_error(t);
}

/* Register access functions */
ssize_t target_reg_read(target *t, int reg, void *data, size_t max)
{
	return t->reg_read(t, reg, data, max);
}

ssize_t target_reg_write(target *t, int reg, const void *data, size_t size)
{
	return t->reg_write(t, reg, data, size);
}

void target_regs_read(target *t, void *data)
{
	if (t->regs_read) {
		t->regs_read(t, data);
		return;
	}
	for (size_t x = 0, i = 0; x < t->regs_size; ) {
		x += t->reg_read(t, i++, data + x, t->regs_size - x);
	}
}
void target_regs_write(target *t, const void *data)
{
	if (t->regs_write) {
		t->regs_write(t, data);
		return;
	}
	for (size_t x = 0, i = 0; x < t->regs_size; ) {
		x += t->reg_write(t, i++, data + x, t->regs_size - x);
	}
}

/* Halt/resume functions */
void target_reset(target *t) { t->reset(t); }
void target_halt_request(target *t) { t->halt_request(t); }
enum target_halt_reason target_halt_poll(target *t, target_addr_t *watch)
{
	return t->halt_poll(t, watch);
}

void target_halt_resume(target *t, bool step) { t->halt_resume(t, step); }

/* Command line for semihosting get_cmdline */
void target_set_cmdline(target *t, char *cmdline) {
	uint32_t len_dst;
	len_dst = sizeof(t->cmdline)-1;
	strncpy(t->cmdline, cmdline, len_dst -1);
	t->cmdline[strlen(t->cmdline)]='\0';
	DEBUG_INFO("cmdline: >%s<\n", t->cmdline);
}

/* Set heapinfo for semihosting */
void target_set_heapinfo(target *t, target_addr_t heap_base, target_addr_t heap_limit,
	target_addr_t stack_base, target_addr_t stack_limit) {
	if (t == NULL) return;
	t->heapinfo[0] = heap_base;
	t->heapinfo[1] = heap_limit;
	t->heapinfo[2] = stack_base;
	t->heapinfo[3] = stack_limit;
}

/* Break-/watchpoint functions */
int target_breakwatch_set(target *t,
                          enum target_breakwatch type, target_addr_t addr, size_t len)
{
	struct breakwatch bw = {
		.type = type,
		.addr = addr,
		.size = len,
	};
	int ret = 1;

	if (t->breakwatch_set)
		ret = t->breakwatch_set(t, &bw);

	if (ret == 0) {
		/* Success, make a heap copy */
		struct breakwatch *bwm = malloc(sizeof bw);
		if (!bwm) {			/* malloc failed: heap exhaustion */
			DEBUG_WARN("malloc: failed in %s\n", __func__);
			return 1;
		}
		memcpy(bwm, &bw, sizeof(bw));

		/* Add to list */
		bwm->next = t->bw_list;
		t->bw_list = bwm;
	}

	return ret;
}

int target_breakwatch_clear(target *t,
                            enum target_breakwatch type, target_addr_t addr, size_t len)
{
	struct breakwatch *bwp = NULL, *bw;
	int ret = 1;
	for (bw = t->bw_list; bw; bwp = bw, bw = bw->next)
		if ((bw->type == type) &&
		    (bw->addr == addr) &&
		    (bw->size == len))
			break;

	if (bw == NULL)
		return -1;

	if (t->breakwatch_clear)
		ret = t->breakwatch_clear(t, bw);

	if (ret == 0) {
		if (bwp == NULL) {
			t->bw_list = bw->next;
		} else {
			bwp->next = bw->next;
		}
		free(bw);
	}
	return ret;
}

/* Target-specific commands */
static bool target_cmd_mass_erase(target *const t, const int argc, const char **const argv)
{
	(void)argc;
	(void)argv;
	if (!t || !t->mass_erase) {
		gdb_out("Mass erase not implemented for target");
		return true;
	}
	gdb_out("Erasing device Flash: ");
	const bool result = t->mass_erase(t);
	gdb_out("done\n");
	return result;
}

static bool target_cmd_range_erase(target *const t, const int argc, const char **const argv)
{
	if (argc < 3) {
		gdb_out("usage: monitor erase_range <address> <count>");
		gdb_out("\t<address> is an address in the first page to erase");
		gdb_out("\t<count> is the number bytes after that to erase, rounded to the next higher whole page");
		return true;
	}
	const uint32_t addr = strtoul(argv[1], NULL, 0);
	const uint32_t length = strtoul(argv[2], NULL, 0);

	return target_flash_erase(t, addr, length);
}

/* Accessor functions */
size_t target_regs_size(target *t)
{
	return t->regs_size;
}

const char *target_tdesc(target *t)
{
	return t->tdesc ? t->tdesc : "";
}

const char *target_driver_name(target *t)
{
	return t->driver;
}

const char *target_core_name(target *t)
{
	return t->core;
}

unsigned int target_designer(target *t)
{
	return t->designer_code;
}

unsigned int target_part_id(target *t)
{
	return t->part_id;
}

uint32_t target_mem_read32(target *t, uint32_t addr)
{
	uint32_t ret;
	t->mem_read(t, &ret, addr, sizeof(ret));
	return ret;
}

void target_mem_write32(target *t, uint32_t addr, uint32_t value)
{
	t->mem_write(t, addr, &value, sizeof(value));
}

uint16_t target_mem_read16(target *t, uint32_t addr)
{
	uint16_t ret;
	t->mem_read(t, &ret, addr, sizeof(ret));
	return ret;
}

void target_mem_write16(target *t, uint32_t addr, uint16_t value)
{
	t->mem_write(t, addr, &value, sizeof(value));
}

uint8_t target_mem_read8(target *t, uint32_t addr)
{
	uint8_t ret;
	t->mem_read(t, &ret, addr, sizeof(ret));
	return ret;
}

void target_mem_write8(target *t, uint32_t addr, uint8_t value)
{
	t->mem_write(t, addr, &value, sizeof(value));
}

void target_command_help(target *t)
{
	for (struct target_command_s *tc = t->commands; tc; tc = tc->next) {
		tc_printf(t, "%s specific commands:\n", tc->specific_name);
		for(const struct command_s *c = tc->cmds; c->cmd; c++)
			tc_printf(t, "\t%s -- %s\n", c->cmd, c->help);
	}
}

int target_command(target *t, int argc, const char *argv[])
{
	for (struct target_command_s *tc = t->commands; tc; tc = tc->next)
		for(const struct command_s *c = tc->cmds; c->cmd; c++)
			if(!strncmp(argv[0], c->cmd, strlen(argv[0])))
				return (c->handler(t, argc, argv)) ? 0 : 1;
	return -1;
}

void tc_printf(target *t, const char *fmt, ...)
{
	(void)t;
	va_list ap;

	if (t->tc == NULL)
		return;

	va_start(ap, fmt);
	t->tc->printf(t->tc, fmt, ap);
	fflush(stdout);
	va_end(ap);
}

/* Interface to host system calls */
int tc_open(target *t, target_addr_t path, size_t plen, enum target_open_flags flags, mode_t mode)
{
	if (t->tc->open == NULL) {
		t->tc->errno_ = TARGET_ENFILE;
		return -1;
	}
	return t->tc->open(t->tc, path, plen, flags, mode);
}

int tc_close(target *t, int fd)
{
	if (t->tc->close == NULL) {
		t->tc->errno_ = TARGET_EBADF;
		return -1;
	}
	return t->tc->close(t->tc, fd);
}

int tc_read(target *t, int fd, target_addr_t buf, unsigned int count)
{
	if (t->tc->read == NULL)
		return 0;
	return t->tc->read(t->tc, fd, buf, count);
}

int tc_write(target *t, int fd, target_addr_t buf, unsigned int count)
{
#ifdef PLATFORM_HAS_USBUART
	if (t->stdout_redirected && (fd == STDOUT_FILENO || fd == STDERR_FILENO)) {
		while (count) {
			uint8_t tmp[STDOUT_READ_BUF_SIZE];
			unsigned int cnt = sizeof(tmp);
			if (cnt > count)
				cnt = count;
			target_mem_read(t, tmp, buf, cnt);
			debug_serial_send_stdout(tmp, cnt);
			count -= cnt;
			buf += cnt;
		}
		return 0;
	}
#endif

	if (t->tc->write == NULL)
		return 0;
	return t->tc->write(t->tc, fd, buf, count);
}

long tc_lseek(target *t, int fd, long offset, enum target_seek_flag flag)
{
	if (t->tc->lseek == NULL)
		return 0;
	return t->tc->lseek(t->tc, fd, offset, flag);
}

int tc_rename(target *t, target_addr_t oldpath, size_t oldlen, target_addr_t newpath, size_t newlen)
{
	if (t->tc->rename == NULL) {
		t->tc->errno_ = TARGET_ENOENT;
		return -1;
	}
	return t->tc->rename(t->tc, oldpath, oldlen, newpath, newlen);
}

int tc_unlink(target *t, target_addr_t path, size_t plen)
{
	if (t->tc->unlink == NULL) {
		t->tc->errno_ = TARGET_ENOENT;
		return -1;
	}
	return t->tc->unlink(t->tc, path, plen);
}

int tc_stat(target *t, target_addr_t path, size_t plen, target_addr_t buf)
{
	if (t->tc->stat == NULL) {
		t->tc->errno_ = TARGET_ENOENT;
		return -1;
	}
	return t->tc->stat(t->tc, path, plen, buf);
}

int tc_fstat(target *t, int fd, target_addr_t buf)
{
	if (t->tc->fstat == NULL) {
		return 0;
	}
	return t->tc->fstat(t->tc, fd, buf);
}

int tc_gettimeofday(target *t, target_addr_t tv, target_addr_t tz)
{
	if (t->tc->gettimeofday == NULL) {
		return -1;
	}
	return t->tc->gettimeofday(t->tc, tv, tz);
}

int tc_isatty(target *t, int fd)
{
	if (t->tc->isatty == NULL) {
		return 1;
	}
	return t->tc->isatty(t->tc, fd);
}

int tc_system(target *t, target_addr_t cmd, size_t cmdlen)
{
	if (t->tc->system == NULL) {
		return -1;
	}
	return t->tc->system(t->tc, cmd, cmdlen);
}
