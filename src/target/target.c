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
#include "command.h"

#include <stdarg.h>
#include <assert.h>

#ifndef _MSC_VER
#include <unistd.h>
#endif

#if CONFIG_BMDA == 1
#include "platform.h"
#endif

/* Fixup for when _FILE_OFFSET_BITS == 64 as unistd.h screws this up for us */
#if defined(lseek)
#undef lseek
#endif

target_s *target_list = NULL;

#define FLASH_WRITE_BUFFER_CEILING 1024U

static bool target_cmd_mass_erase(target_s *target, int argc, const char **argv);
static bool target_cmd_range_erase(target_s *target, int argc, const char **argv);
static bool target_cmd_blank_check(target_s *target, int argc, const char **argv);
static bool target_cmd_redirect_output(target_s *target, int argc, const char **argv);

const command_s target_cmd_list[] = {
	{"erase_mass", target_cmd_mass_erase, "Erase whole device Flash"},
	{"erase_range", target_cmd_range_erase, "Erase a range of memory on a device"},
	{"blank_check", target_cmd_blank_check, "Blank-check device Flash"},
	{"redirect_stdout", target_cmd_redirect_output, "Redirect semihosting output to aux USB serial"},
	{NULL, NULL, NULL},
};

target_s *target_new(void)
{
	target_s *target = calloc(1, sizeof(*target));
	if (!target) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return NULL;
	}

	if (target_list) {
		target_s *last_target = target_list;
		while (last_target->next)
			last_target = last_target->next;
		last_target->next = target;
	} else
		target_list = target;

	target->target_storage = NULL;

	target_add_commands(target, target_cmd_list, "Target");
	return target;
}

size_t target_foreach(void (*callback)(size_t index, target_s *target, void *context), void *context)
{
	size_t idx = 0;
	for (target_s *target = target_list; target; target = target->next)
		callback(++idx, target, context);
	return idx;
}

void target_ram_map_free(target_s *target)
{
	while (target->ram) {
		target_ram_s *next = target->ram->next;
		free(target->ram);
		target->ram = next;
	}
}

void target_flash_map_free(target_s *target)
{
	while (target->flash) {
		target_flash_s *next = target->flash->next;
		if (target->flash->buf)
			free(target->flash->buf);
		free(target->flash);
		target->flash = next;
	}
}

void target_mem_map_free(target_s *target)
{
	target_ram_map_free(target);
	target_flash_map_free(target);
}

void target_list_free(void)
{
	target_s *volatile target = target_list;
	while (target) {
		target_s *next_target = target->next;
		TRY (EXCEPTION_ALL) {
			if (target->attached)
				target->detach(target);
		}
		CATCH () {
		default:
			DEBUG_ERROR("Exception caught while detaching from target: %s\n", exception_frame.msg);
			target->attached = false;
			break;
		}
		if (target->tc && target->tc->destroy_callback)
			target->tc->destroy_callback(target->tc, target);
		if (target->priv)
			target->priv_free(target->priv);
		while (target->commands) {
			target_command_s *const tc = target->commands->next;
			free(target->commands);
			target->commands = tc;
		}
		free(target->target_storage);
		target_mem_map_free(target);
		while (target->bw_list) {
			void *next = target->bw_list->next;
			free(target->bw_list);
			target->bw_list = next;
		}
		free(target);
		target = next_target;
	}
	target_list = NULL;
}

void target_add_commands(target_s *target, const command_s *cmds, const char *name)
{
	target_command_s *command = malloc(sizeof(*command));
	if (!command) { /* malloc failed: heap exhaustion */
		DEBUG_ERROR("malloc: failed in %s\n", __func__);
		return;
	}

	if (target->commands) {
		target_command_s *tail;
		for (tail = target->commands; tail->next; tail = tail->next)
			continue;
		tail->next = command;
	} else
		target->commands = command;

	command->specific_name = name;
	command->cmds = cmds;
	command->next = NULL;
}

target_s *target_attach_n(const size_t n, target_controller_s *controller)
{
	target_s *target = target_list;
	for (size_t idx = 1; target; target = target->next, ++idx) {
		if (idx == n)
			return target_attach(target, controller);
	}
	return NULL;
}

target_s *target_attach(target_s *target, target_controller_s *controller)
{
	if (target->tc)
		target->tc->destroy_callback(target->tc, target);

	target->tc = controller;
	platform_target_clk_output_enable(true);
	DEBUG_TARGET("Attaching to target..\n");

	if (target->attach && !target->attach(target)) {
		DEBUG_TARGET("Attach failed\n");
		platform_target_clk_output_enable(false);
		return NULL;
	}

	DEBUG_TARGET("Attach success\n");
	target->attached = true;
	return target;
}

void target_add_ram32(target_s *const target, const target_addr32_t start, const uint32_t len)
{
	target_add_ram64(target, start, len);
}

void target_add_ram64(target_s *const target, const target_addr64_t start, const uint64_t len)
{
	target_ram_s *ram = malloc(sizeof(*ram));
	if (!ram) { /* malloc failed: heap exhaustion */
		DEBUG_ERROR("malloc: failed in %s\n", __func__);
		return;
	}

	ram->start = start;
	ram->length = len;
	ram->next = target->ram;
	target->ram = ram;
}

void target_add_flash(target_s *target, target_flash_s *flash)
{
	if (flash->writesize == 0)
		flash->writesize = flash->blocksize;

	/* Automatically sized buffer */
	/* For targets with larger than FLASH_WRITE_BUFFER_CEILING write size, we use a buffer of write size */
	/* No point doing math if we can't fit at least 2 writesizes in a buffer */
	if (flash->writesize <= FLASH_WRITE_BUFFER_CEILING / 2U) {
		const size_t count = FLASH_WRITE_BUFFER_CEILING / flash->writesize;
		flash->writebufsize = flash->writesize * count;
	} else
		flash->writebufsize = flash->writesize;

	flash->t = target;
	flash->next = target->flash;
	target->flash = flash;
}

bool target_enter_flash_mode_stub(target_s *target)
{
	(void)target;
	return true;
}

static ssize_t map_ram(char *buf, size_t len, target_ram_s *ram)
{
	return snprintf(buf, len, "<memory type=\"ram\" start=\"0x%08" PRIx32 "\" length=\"0x%" PRIx32 "\"/>", ram->start,
		(uint32_t)ram->length);
}

static ssize_t map_flash(char *buf, size_t len, target_flash_s *flash)
{
	ssize_t offset = 0;
	offset += snprintf(&buf[offset], len - offset,
		"<memory type=\"flash\" start=\"0x%08" PRIx32 "\" length=\"0x%" PRIx32 "\">", flash->start,
		(uint32_t)flash->length);
	offset += snprintf(buf + offset, len - offset, "<property name=\"blocksize\">0x%" PRIx32 "</property></memory>",
		(uint32_t)flash->blocksize);
	return offset;
}

bool target_mem_map(target_s *target, char *tmp, size_t len)
{
	size_t offset = 0;
	offset = snprintf(tmp + offset, len - offset, "<memory-map>");
	/* Map each defined RAM */
	for (target_ram_s *ram = target->ram; ram; ram = ram->next)
		offset += map_ram(tmp + offset, len - offset, ram);
	/* Map each defined Flash */
	for (target_flash_s *flash = target->flash; flash; flash = flash->next)
		offset += map_flash(tmp + offset, len - offset, flash);
	offset += snprintf(tmp + offset, len - offset, "</memory-map>");
	return offset < len - 1U;
}

void target_print_progress(platform_timeout_s *const timeout)
{
	if (platform_timeout_is_expired(timeout)) {
		gdb_out(".");
		platform_timeout_set(timeout, 500);
	}
}

/* Wrapper functions */
void target_detach(target_s *target)
{
	DEBUG_TARGET("Detaching from target\n");
	if (target->detach)
		target->detach(target);
	platform_target_clk_output_enable(false);
	target->attached = false;
#if CONFIG_BMDA == 1
	platform_buffer_flush();
#endif
}

bool target_check_error(target_s *target)
{
	if (target && target->check_error)
		return target->check_error(target);
	return false;
}

/* Memory access functions */
bool target_mem32_read(target_s *const target, void *const dest, const target_addr_t src, const size_t len)
{
	return target_mem64_read(target, dest, src, len);
}

bool target_mem64_read(target_s *const target, void *const dest, const target_addr64_t src, const size_t len)
{
	/* If we're processing a semihosting syscall and it needs IO redirected, handle that instead */
	if (target->target_options & TOPT_IN_SEMIHOSTING_SYSCALL) {
		/* Make sure we can't go over the bounds of the buffer */
		const size_t amount = MIN(len, target->tc->semihosting_buffer_len);
		/* Copy data into the request destination buffer from the semihosting buffer */
		memcpy(dest, target->tc->semihosting_buffer_ptr, amount);
		return false;
	}
	/* Otherwise if the target defines a memory read function, call that instead and check for errors */
	if (target->mem_read)
		target->mem_read(target, dest, src, len);
	return target_check_error(target);
}

bool target_mem32_write(target_s *const target, const target_addr_t dest, const void *const src, const size_t len)
{
	return target_mem64_write(target, dest, src, len);
}

bool target_mem64_write(target_s *const target, const target_addr64_t dest, const void *const src, const size_t len)
{
	/* If we're processing a semihosting syscall and it needs IO redirected, handle that instead */
	if (target->target_options & TOPT_IN_SEMIHOSTING_SYSCALL) {
		/* Make sure we can't go over the bounds of the buffer */
		const size_t amount = MIN(len, target->tc->semihosting_buffer_len);
		/* Copy data into the semihosting buffer from the request source buffer */
		memcpy(target->tc->semihosting_buffer_ptr, src, amount);
		return false;
	}
	/* Otherwise if the target defines a memory write function, call that instead and check for errors */
	if (target->mem_write)
		target->mem_write(target, dest, src, len);
	return target_check_error(target);
}

/* Returns true if the target needs halting to access memory on it */
bool target_mem_access_needs_halt(target_s *target)
{
	return !(target->target_options & TOPT_NON_HALTING_MEM_IO);
}

/* Register access functions */
size_t target_reg_read(target_s *target, uint32_t reg, void *data, size_t max)
{
	if (target->reg_read)
		return target->reg_read(target, reg, data, max);
	return 0;
}

size_t target_reg_write(target_s *target, uint32_t reg, const void *data, size_t size)
{
	if (target->reg_write)
		return target->reg_write(target, reg, data, size);
	return 0;
}

void target_regs_read(target_s *target, void *data)
{
	if (target->regs_read)
		target->regs_read(target, data);
	else {
		for (size_t offset = 0, i = 0; offset < target->regs_size;)
			offset += target_reg_read(target, i++, (uint8_t *)data + offset, target->regs_size - offset);
	}
}

void target_regs_write(target_s *target, const void *data)
{
	if (target->regs_write)
		target->regs_write(target, data);
	else {
		for (size_t offset = 0, i = 0; offset < target->regs_size;)
			offset += target_reg_write(target, i++, (const uint8_t *)data + offset, target->regs_size - offset);
	}
}

/* Halt/resume functions */
void target_reset(target_s *target)
{
	DEBUG_TARGET("Resetting target\n");
	if (target->reset)
		target->reset(target);
}

void target_halt_request(target_s *target)
{
	DEBUG_TARGET("Halting target\n");
	if (target->halt_request)
		target->halt_request(target);
}

#ifndef DEBUG_TARGET_IS_NOOP
static const char *target_halt_reason_str(const target_halt_reason_e reason)
{
	switch (reason) {
	case TARGET_HALT_RUNNING:
		return "Target is still running";
	case TARGET_HALT_ERROR:
		return "An error occured";
	case TARGET_HALT_REQUEST:
		return "Halt requested";
	case TARGET_HALT_STEPPING:
		return "Step complete";
	case TARGET_HALT_BREAKPOINT:
		return "Breakpoint hit";
	case TARGET_HALT_WATCHPOINT:
		return "Watchpoint hit";
	case TARGET_HALT_FAULT:
		return "A fault occured on the target";
	default:
		return "Unknown halt reason";
	}
}
#endif

target_halt_reason_e target_halt_poll(target_s *target, target_addr64_t *watch)
{
	if (target->halt_poll) {
		const target_halt_reason_e reason = target->halt_poll(target, watch);
#ifndef DEBUG_TARGET_IS_NOOP
		if (reason != TARGET_HALT_RUNNING)
			DEBUG_TARGET("Target halted: %s\n", target_halt_reason_str(reason));
#endif
		return reason;
	}
	/* XXX: Is this actually the desired fallback behaviour? */
	return TARGET_HALT_RUNNING;
}

void target_halt_resume(target_s *target, bool step)
{
	DEBUG_TARGET("%s target\n", step ? "Single stepping" : "Resuming");
	if (target->halt_resume)
		target->halt_resume(target, step);
}

/* Command line for semihosting SYS_GET_CMDLINE */
void target_set_cmdline(target_s *target, const char *const cmdline, const size_t cmdline_len)
{
	/* This assertion is really expensive, so only include it on BMDA builds */
#if CONFIG_BMDA == 1
	/* Check and make sure that we don't exceed the target buffer size */
	assert(cmdline_len < MAX_CMDLINE);
#endif
	memcpy(target->cmdline, cmdline, cmdline_len + 1U);
	DEBUG_INFO("cmdline: >%s<\n", target->cmdline);
}

/* Set heapinfo for semihosting */
void target_set_heapinfo(target_s *target, target_addr_t heap_base, target_addr_t heap_limit, target_addr_t stack_base,
	target_addr_t stack_limit)
{
	if (target == NULL)
		return;
	target->heapinfo[0] = heap_base;
	target->heapinfo[1] = heap_limit;
	target->heapinfo[2] = stack_base;
	target->heapinfo[3] = stack_limit;
}

/* Break-/watchpoint functions */
int target_breakwatch_set(target_s *target, target_breakwatch_e type, target_addr_t addr, size_t len)
{
	breakwatch_s bw = {
		.type = type,
		.addr = addr,
		.size = len,
	};
	int ret = 1;

	if (target->breakwatch_set)
		ret = target->breakwatch_set(target, &bw);

	if (ret == 0) {
		/* Success, make a heap copy */
		breakwatch_s *bwm = malloc(sizeof(bw));
		if (!bwm) { /* malloc failed: heap exhaustion */
			DEBUG_ERROR("malloc: failed in %s\n", __func__);
			return 1;
		}
		memcpy(bwm, &bw, sizeof(bw));

		/* Add to list */
		bwm->next = target->bw_list;
		target->bw_list = bwm;
	}

	return ret;
}

int target_breakwatch_clear(target_s *target, target_breakwatch_e type, target_addr_t addr, size_t len)
{
	breakwatch_s *bwp = NULL, *bw;
	for (bw = target->bw_list; bw; bwp = bw, bw = bw->next) {
		if (bw->type == type && bw->addr == addr && bw->size == len)
			break;
	}

	if (bw == NULL)
		return -1;

	int ret = 1;
	if (target->breakwatch_clear)
		ret = target->breakwatch_clear(target, bw);

	if (ret == 0) {
		if (bwp == NULL)
			target->bw_list = bw->next;
		else
			bwp->next = bw->next;
		free(bw);
	}
	return ret;
}

/* Target-specific commands */
static bool target_cmd_mass_erase(target_s *const target, const int argc, const char **const argv)
{
	(void)argc;
	(void)argv;
	if (!target) {
		gdb_out("Mass erase not available\n");
		return true;
	}
	gdb_out("Erasing device Flash: ");
	const bool result = target_flash_mass_erase(target);
	gdb_out("done\n");
	return result;
}

static bool target_cmd_range_erase(target_s *const target, const int argc, const char **const argv)
{
	if (argc < 3) {
		gdb_out("usage: monitor erase_range <address> <count>\n");
		gdb_out("\t<address> is an address in the first page to erase\n");
		gdb_out("\t<count> is the number bytes after that to erase, rounded to the next higher whole page\n");
		return true;
	}
	const uint32_t addr = strtoul(argv[1], NULL, 0);
	const uint32_t length = strtoul(argv[2], NULL, 0);

	return target_flash_erase(target, addr, length);
}

static bool target_cmd_blank_check(target_s *const target, const int argc, const char **const argv)
{
	(void)argc;
	(void)argv;
	gdb_out("Blank-checking device Flash: ");
	const bool result = target_flash_blank_check(target);
	gdb_out("done\n");
	return result;
}

static bool target_cmd_redirect_output(target_s *target, int argc, const char **argv)
{
	if (argc == 1) {
		gdb_outf("Semihosting stdout redirection: %s\n", target->stdout_redirected ? "enabled" : "disabled");
		return true;
	}
	return parse_enable_or_disable(argv[1], &target->stdout_redirected);
}

/* Accessor functions */
size_t target_regs_size(target_s *target)
{
	return target->regs_size;
}

/*
 * Get an XML description of the target's registers. Called during the attach phase when
 * GDB supplies request `qXfer:features:read:target.xml:`. The pointer returned by this call
 * must be passed to `free()` on conclusion of its use.
 */
const char *target_regs_description(target_s *target)
{
	if (target->regs_description)
		return target->regs_description(target);
	return NULL;
}

uint32_t target_mem32_read32(target_s *target, target_addr32_t addr)
{
	uint32_t result = 0;
	target_mem32_read(target, &result, addr, sizeof(result));
	return result;
}

bool target_mem32_write32(target_s *target, target_addr32_t addr, uint32_t value)
{
	return target_mem32_write(target, addr, &value, sizeof(value));
}

uint16_t target_mem32_read16(target_s *target, target_addr32_t addr)
{
	uint16_t result = 0;
	target_mem32_read(target, &result, addr, sizeof(result));
	return result;
}

bool target_mem32_write16(target_s *target, target_addr32_t addr, uint16_t value)
{
	return target_mem32_write(target, addr, &value, sizeof(value));
}

uint8_t target_mem32_read8(target_s *target, target_addr32_t addr)
{
	uint8_t result = 0;
	target_mem32_read(target, &result, addr, sizeof(result));
	return result;
}

bool target_mem32_write8(target_s *target, target_addr32_t addr, uint8_t value)
{
	return target_mem32_write(target, addr, &value, sizeof(value));
}

void target_command_help(target_s *target)
{
	for (const target_command_s *target_commands = target->commands; target_commands;
		 target_commands = target_commands->next) {
		tc_printf(target, "%s specific commands:\n", target_commands->specific_name);
		for (const command_s *command = target_commands->cmds; command->cmd; command++)
			tc_printf(target, "\t%s -- %s\n", command->cmd, command->help);
	}
}

int target_command(target_s *target, int argc, const char *argv[])
{
	for (const target_command_s *target_commands = target->commands; target_commands;
		 target_commands = target_commands->next) {
		for (const command_s *command = target_commands->cmds; command->cmd; command++) {
			if (!strncmp(argv[0], command->cmd, strlen(argv[0])))
				return command->handler(target, argc, argv) ? 0 : 1;
		}
	}
	return -1;
}

void tc_printf(target_s *target, const char *fmt, ...)
{
	if (target->tc == NULL)
		return;

	va_list ap;
	va_start(ap, fmt);
	target->tc->printf(target->tc, fmt, ap);
	fflush(stdout);
	va_end(ap);
}
