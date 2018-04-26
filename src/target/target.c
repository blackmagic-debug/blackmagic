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
#include "target.h"
#include "target_internal.h"

#include <stdarg.h>

target *target_list = NULL;

static int target_flash_write_buffered(struct target_flash *f,
                                       target_addr dest, const void *src, size_t len);
static int target_flash_done_buffered(struct target_flash *f);

target *target_new(void)
{
	target *t = (void*)calloc(1, sizeof(*t));
	if (target_list) {
		target *c = target_list;
		while (c->next)
			c = c->next;
		c->next = t;
	} else {
		target_list = t;
	}

	return t;
}

bool target_foreach(void (*cb)(int, target *t, void *context), void *context)
{
	int i = 1;
	target *t = target_list;
	for (; t; t = t->next, i++)
		cb(i, t, context);
	return target_list != NULL;
}

void target_mem_map_free(target *t)
{
	while (t->ram) {
		void * next = t->ram->next;
		free(t->ram);
		t->ram = next;
	}
	while (t->flash) {
		void * next = t->flash->next;
		if (t->flash->buf)
			free(t->flash->buf);
		free(t->flash);
		t->flash = next;
	}
}

void target_list_free(void)
{
	struct target_command_s *tc;

	while(target_list) {
		target *t = target_list->next;
		if (target_list->tc)
			target_list->tc->destroy_callback(target_list->tc, target_list);
		if (target_list->priv)
			target_list->priv_free(target_list->priv);
		while (target_list->commands) {
			tc = target_list->commands->next;
			free(target_list->commands);
			target_list->commands = tc;
		}
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
	struct target_command_s *tc;
	if (t->commands) {
		for (tc = t->commands; tc->next; tc = tc->next);
		tc = tc->next = malloc(sizeof(*tc));
	} else {
		t->commands = tc = malloc(sizeof(*tc));
	}
	tc->specific_name = name;
	tc->cmds = cmds;
	tc->next = NULL;
}

target *target_attach_n(int n, struct target_controller *tc)
{
	target *t;
	int i;
	for(t = target_list, i = 1; t; t = t->next, i++)
		if(i == n)
			return target_attach(t, tc);
	return NULL;
}

target *target_attach(target *t, struct target_controller *tc)
{
	if (t->tc)
		t->tc->destroy_callback(t->tc, t);

	t->tc = tc;

	if (!t->attach(t))
		return NULL;

	t->attached = true;
	return t;
}

void target_add_ram(target *t, target_addr start, uint32_t len)
{
	struct target_ram *ram = malloc(sizeof(*ram));
	ram->start = start;
	ram->length = len;
	ram->next = t->ram;
	t->ram = ram;
}

void target_add_flash(target *t, struct target_flash *f)
{
	if (f->buf_size == 0)
		f->buf_size = MIN(f->blocksize, 0x400);
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

static ssize_t map_flash(char *buf, size_t len, struct target_flash *f)
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
	for (struct target_flash *f = t->flash; f; f = f->next)
		i += map_flash(&tmp[i], len - i, f);
	i += snprintf(&tmp[i], len - i, "</memory-map>");

	if (i > (len -2))
		return false;
	return true;
}

static struct target_flash *flash_for_addr(target *t, uint32_t addr)
{
	for (struct target_flash *f = t->flash; f; f = f->next)
		if ((f->start <= addr) &&
		    (addr < (f->start + f->length)))
			return f;
	return NULL;
}

int target_flash_erase(target *t, target_addr addr, size_t len)
{
	int ret = 0;
	while (len) {
		struct target_flash *f = flash_for_addr(t, addr);
		size_t tmptarget = MIN(addr + len, f->start + f->length);
		size_t tmplen = tmptarget - addr;
		ret |= f->erase(f, addr, tmplen);
		addr += tmplen;
		len -= tmplen;
	}
	return ret;
}

int target_flash_write(target *t,
                       target_addr dest, const void *src, size_t len)
{
	int ret = 0;
	while (len) {
		struct target_flash *f = flash_for_addr(t, dest);
		size_t tmptarget = MIN(dest + len, f->start + f->length);
		size_t tmplen = tmptarget - dest;
		ret |= target_flash_write_buffered(f, dest, src, tmplen);
		dest += tmplen;
		src += tmplen;
		len -= tmplen;
	}
	return ret;
}

int target_flash_done(target *t)
{
	for (struct target_flash *f = t->flash; f; f = f->next) {
		int tmp = target_flash_done_buffered(f);
		if (tmp)
			return tmp;
		if (f->done) {
			int tmp = f->done(f);
			if (tmp)
				return tmp;
		}
	}
	return 0;
}

int target_flash_write_buffered(struct target_flash *f,
                                target_addr dest, const void *src, size_t len)
{
	int ret = 0;

	if (f->buf == NULL) {
		/* Allocate flash sector buffer */
		f->buf = malloc(f->buf_size);
		f->buf_addr = -1;
	}
	while (len) {
		uint32_t offset = dest % f->buf_size;
		uint32_t base = dest - offset;
		if (base != f->buf_addr) {
			if (f->buf_addr != (uint32_t)-1) {
				/* Write sector to flash if valid */
				ret |= f->write(f, f->buf_addr,
				                f->buf, f->buf_size);
			}
			/* Setup buffer for a new sector */
			f->buf_addr = base;
			memset(f->buf, f->erased, f->buf_size);
		}
		/* Copy chunk into sector buffer */
		size_t sectlen = MIN(f->buf_size - offset, len);
		memcpy(f->buf + offset, src, sectlen);
		dest += sectlen;
		src += sectlen;
		len -= sectlen;
	}
	return ret;
}

int target_flash_done_buffered(struct target_flash *f)
{
	int ret = 0;
	if ((f->buf != NULL) &&(f->buf_addr != (uint32_t)-1)) {
		/* Write sector to flash if valid */
		ret = f->write(f, f->buf_addr, f->buf, f->buf_size);
		f->buf_addr = -1;
		free(f->buf);
		f->buf = NULL;
	}

	return ret;
}

/* Wrapper functions */
void target_detach(target *t)
{
	t->detach(t);
	t->attached = false;
}

bool target_check_error(target *t) { return t->check_error(t); }
bool target_attached(target *t) { return t->attached; }

/* Memory access functions */
int target_mem_read(target *t, void *dest, target_addr src, size_t len)
{
	t->mem_read(t, dest, src, len);
	return target_check_error(t);
}

int target_mem_write(target *t, target_addr dest, const void *src, size_t len)
{
	t->mem_write(t, dest, src, len);
	return target_check_error(t);
}

/* Register access functions */
void target_regs_read(target *t, void *data) { t->regs_read(t, data); }
void target_regs_write(target *t, const void *data) { t->regs_write(t, data); }

/* Halt/resume functions */
void target_reset(target *t) { t->reset(t); }
void target_halt_request(target *t) { t->halt_request(t); }
enum target_halt_reason target_halt_poll(target *t, target_addr *watch)
{
	return t->halt_poll(t, watch);
}

void target_halt_resume(target *t, bool step) { t->halt_resume(t, step); }

/* Break-/watchpoint functions */
int target_breakwatch_set(target *t,
                          enum target_breakwatch type, target_addr addr, size_t len)
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
		/* Success, make a heap copy and add to list */
		struct breakwatch *bwm = malloc(sizeof bw);
		memcpy(bwm, &bw, sizeof(bw));
		bwm->next = t->bw_list;
		t->bw_list = bwm;
	}

	return ret;
}

int target_breakwatch_clear(target *t,
                            enum target_breakwatch type, target_addr addr, size_t len)
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
				return !c->handler(t, argc, argv);
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
	va_end(ap);
}

/* Interface to host system calls */
int tc_open(target *t, target_addr path, size_t plen,
            enum target_open_flags flags, mode_t mode)
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

int tc_read(target *t, int fd, target_addr buf, unsigned int count)
{
	if (t->tc->read == NULL)
		return 0;
	return t->tc->read(t->tc, fd, buf, count);
}

int tc_write(target *t, int fd, target_addr buf, unsigned int count)
{
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

int tc_rename(target *t, target_addr oldpath, size_t oldlen,
                         target_addr newpath, size_t newlen)
{
	if (t->tc->rename == NULL) {
		t->tc->errno_ = TARGET_ENOENT;
		return -1;
	}
	return t->tc->rename(t->tc, oldpath, oldlen, newpath, newlen);
}

int tc_unlink(target *t, target_addr path, size_t plen)
{
	if (t->tc->unlink == NULL) {
		t->tc->errno_ = TARGET_ENOENT;
		return -1;
	}
	return t->tc->unlink(t->tc, path, plen);
}

int tc_stat(target *t, target_addr path, size_t plen, target_addr buf)
{
	if (t->tc->stat == NULL) {
		t->tc->errno_ = TARGET_ENOENT;
		return -1;
	}
	return t->tc->stat(t->tc, path, plen, buf);
}

int tc_fstat(target *t, int fd, target_addr buf)
{
	if (t->tc->fstat == NULL) {
		return 0;
	}
	return t->tc->fstat(t->tc, fd, buf);
}

int tc_gettimeofday(target *t, target_addr tv, target_addr tz)
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

int tc_system(target *t, target_addr cmd, size_t cmdlen)
{
	if (t->tc->system == NULL) {
		return -1;
	}
	return t->tc->system(t->tc, cmd, cmdlen);
}
