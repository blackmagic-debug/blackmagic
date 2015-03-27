/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2012  Black Sphere Technologies Ltd.
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

target *target_list = NULL;
bool connect_assert_srst;

target *target_new(unsigned size)
{
	target *t = (void*)calloc(1, size);
	t->next = target_list;
	target_list = t;

	return t;
}

void target_list_free(void)
{
	struct target_command_s *tc;

	while(target_list) {
		target *t = target_list->next;
		if (target_list->destroy_callback)
			target_list->destroy_callback(target_list);
		if (target_list->priv)
			target_list->priv_free(target_list->priv);
		while (target_list->commands) {
			tc = target_list->commands->next;
			free(target_list->commands);
			target_list->commands = tc;
		}
		if (target_list->dyn_mem_map)
			free(target_list->dyn_mem_map);
		while (target_list->ram) {
			void * next = target_list->ram->next;
			free(target_list->ram);
			target_list->ram = next;
		}
		while (target_list->flash) {
			void * next = target_list->flash->next;
			free(target_list->flash);
			target_list->flash = next;
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

target *target_attach(target *t, target_destroy_callback destroy_cb)
{
	if (t->destroy_callback)
		t->destroy_callback(t);

	t->destroy_callback = destroy_cb;

	if (!t->attach(t))
		return NULL;

	return t;
}

void target_add_ram(target *t, uint32_t start, uint32_t len)
{
	struct target_ram *ram = malloc(sizeof(*ram));
	ram->start = start;
	ram->length = len;
	ram->next = t->ram;
	t->ram = ram;
}

void target_add_flash(target *t, struct target_flash *f)
{
	f->t = t;
	f->next = t->flash;
	t->flash = f;
}

static ssize_t map_ram(char *buf, size_t len, struct target_ram *ram)
{
	return snprintf(buf, len, "<memory type=\"ram\" start=\"0x%08"PRIx32
	                          "\" length=\"0x%08"PRIx32"\"/>",
	                          ram->start, ram->length);
}

static ssize_t map_flash(char *buf, size_t len, struct target_flash *f)
{
	int i = 0;
	i += snprintf(&buf[i], len - i, "<memory type=\"flash\" start=\"0x%08"PRIx32
	                                "\" length=\"0x%08"PRIx32"\">",
	                                f->start, f->length);
	i += snprintf(&buf[i], len - i, "<property name=\"blocksize\">0x%08"PRIx32
	                            "</property></memory>",
	                            f->blocksize);
	return i;
}

const char *target_mem_map(target *t)
{
	/* Deprecated static const memory map */
	if (t->xml_mem_map)
		return t->xml_mem_map;

	if (t->dyn_mem_map)
		return t->dyn_mem_map;

	/* FIXME size buffer */
	size_t len = 1024;
	char *tmp = malloc(len);
	size_t i = 0;
	i = snprintf(&tmp[i], len - i, "<memory-map>");
	/* Map each defined RAM */
	for (struct target_ram *r = t->ram; r; r = r->next)
		i += map_ram(&tmp[i], len - i, r);
	/* Map each defined Flash */
	for (struct target_flash *f = t->flash; f; f = f->next)
		i += map_flash(&tmp[i], len - i, f);
	i += snprintf(&tmp[i], len - i, "</memory-map>");

	t->dyn_mem_map = tmp;

	return t->dyn_mem_map;
}

static struct target_flash *flash_for_addr(target *t, uint32_t addr)
{
	for (struct target_flash *f = t->flash; f; f = f->next)
		if ((f->start <= addr) &&
		    (addr < (f->start + f->length)))
			return f;
	return NULL;
}

int target_flash_erase(target *t, uint32_t addr, size_t len)
{
	if (t->flash_write)
		return t->flash_erase(t, addr, len);

	int ret = 0;
	while (len) {
		struct target_flash *f = flash_for_addr(t, addr);
		size_t tmplen = MIN(len, f->length - (addr % f->length));
		ret |= f->erase(f, addr, tmplen);
		addr += tmplen;
		len -= tmplen;
	}
	return ret;
}

int target_flash_write(target *t,
                       uint32_t dest, const void *src, size_t len)
{
	if (t->flash_write)
		return t->flash_write(t, dest, src, len);

	int ret = 0;
	while (len) {
		struct target_flash *f = flash_for_addr(t, dest);
		size_t tmplen = MIN(len, f->length - (dest % f->length));
		if (f->align > 1) {
			uint32_t offset = dest % f->align;
			uint8_t data[ALIGN(offset + len, f->align)];
			memset(data, f->erased, sizeof(data));
			memcpy((uint8_t *)data + offset, src, len);
			ret |= f->write(f, dest - offset, data, sizeof(data));
		} else {
			ret |= f->write(f, dest, src, tmplen);
		}
		src += tmplen;
		len -= tmplen;
	}
	return ret;
}

int target_flash_done(target *t)
{
	for (struct target_flash *f = t->flash; f; f = f->next) {
		if (f->done) {
			int tmp = f->done(f);
			if (tmp)
				return tmp;
		}
	}
	return 0;
}

