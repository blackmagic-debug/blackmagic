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

/* Provides an abstract 'target object', the 'methods' of which must be
 * implemented by a target driver when a supported device is detected.
 */

#ifndef __TARGET_H
#define __TARGET_H

#include "general.h"

typedef struct target_s target;

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

#define target_detach(target)	\
	(target)->detach(target)

#define target_check_error(target)	\
	(target)->check_error(target)

/* Memory access functions */
#define target_mem_read_words(target, dest, src, len)	\
	(target)->mem_read_words((target), (dest), (src), (len))

#define target_mem_write_words(target, dest, src, len)	\
	(target)->mem_write_words((target), (dest), (src), (len))

#define target_mem_read_bytes(target, dest, src, len)	\
	(target)->mem_read_bytes((target), (dest), (src), (len))

#define target_mem_write_bytes(target, dest, src, len)	\
	(target)->mem_write_bytes((target), (dest), (src), (len))


/* Register access functions */
#define target_regs_read(target, data)	\
	(target)->regs_read((target), (data))

#define target_regs_write(target, data)	\
	(target)->regs_write((target), (data))

#define target_pc_read(target)	\
	(target)->pc_read((target))

#define target_pc_write(target, val)	\
	(target)->pc_write((target), (val))


/* Halt/resume functions */
#define target_reset(target)	\
	(target)->reset(target)

#define target_halt_request(target)	\
	(target)->halt_request(target)

#define target_halt_wait(target)	\
	(target)->halt_wait(target)

#define target_halt_resume(target, step)	\
	(target)->halt_resume((target), (step))

/* Break-/watchpoint functions */
#define target_set_hw_bp(target, addr)	\
	(target)->set_hw_bp((target), (addr))

#define target_clear_hw_bp(target, addr)	\
	(target)->clear_hw_bp((target), (addr))


#define target_set_hw_wp(target, type, addr, len)	\
	(target)->set_hw_wp((target), (type), (addr), (len))

#define target_clear_hw_wp(target, type, addr, len)	\
	(target)->clear_hw_wp((target), (type), (addr), (len))


#define target_check_hw_wp(target, addr)	\
	((target)->check_hw_wp?(target)->check_hw_wp((target), (addr)):0)


/* Flash memory access functions */
#define target_flash_erase(target, addr, len)	\
	(target)->flash_erase((target), (addr), (len))

#define target_flash_write(target, dest, src, len)	\
	(target)->flash_write((target), (dest), (src), (len))

/* Host I/O */
#define target_hostio_reply(target, recode, errcode)	\
	(target)->hostio_reply((target), (retcode), (errcode))


struct target_s {
	/* Notify controlling debugger if target is lost */
	target_destroy_callback destroy_callback;

	/* Attach/Detach funcitons */
	bool (*attach)(struct target_s *target);
	void (*detach)(struct target_s *target);
	int (*check_error)(struct target_s *target);

	/* Memory access functions */
	int (*mem_read_words)(struct target_s *target, uint32_t *dest, uint32_t src,
				int len);
	int (*mem_write_words)(struct target_s *target, uint32_t dest,
				const uint32_t *src, int len);

	int (*mem_read_bytes)(struct target_s *target, uint8_t *dest, uint32_t src,
				int len);
	int (*mem_write_bytes)(struct target_s *target, uint32_t dest,
				const uint8_t *src, int len);

	/* Register access functions */
	int regs_size;
	const char *tdesc;
	int (*regs_read)(struct target_s *target, void *data);
	int (*regs_write)(struct target_s *target, const void *data);

	uint32_t (*pc_read)(struct target_s *target);
	int (*pc_write)(struct target_s *target, const uint32_t val);

	/* Halt/resume functions */
	void (*reset)(struct target_s *target);
	void (*halt_request)(struct target_s *target);
	int (*halt_wait)(struct target_s *target);
	void (*halt_resume)(struct target_s *target, bool step);

	/* Break-/watchpoint functions */
	int (*set_hw_bp)(struct target_s *target, uint32_t addr);
	int (*clear_hw_bp)(struct target_s *target, uint32_t addr);

	int (*set_hw_wp)(struct target_s *target, uint8_t type, uint32_t addr, uint8_t len);
	int (*clear_hw_wp)(struct target_s *target, uint8_t type, uint32_t addr, uint8_t len);

	int (*check_hw_wp)(struct target_s *target, uint32_t *addr);

	/* target-defined options */
	unsigned target_options;
	uint32_t idcode;

	/* Flash memory access functions */
	const char *xml_mem_map;
	int (*flash_erase)(struct target_s *target, uint32_t addr, int len);
	int (*flash_write)(struct target_s *target, uint32_t dest,
				const uint8_t *src, int len);

	/* Host I/O support */
	void (*hostio_reply)(target *t, int32_t retcode, uint32_t errcode);

	const char *driver;
	struct target_command_s *commands;

	int size;
	struct target_s *next;

	void *priv;
	void (*priv_free)(void *);
};

struct target_command_s {
	const char *specific_name;
	const struct command_s *cmds;
	struct target_command_s *next;
};

extern target *target_list;
extern bool connect_assert_srst;

target *target_new(unsigned size);
void target_list_free(void);
void target_add_commands(target *t, const struct command_s *cmds, const char *name);

/* Probe for various targets.
 * Actual functions implemented in their respective drivers.
 */
bool cortexm_probe(struct target_s *target);
bool stm32f1_probe(struct target_s *target);
bool stm32f4_probe(struct target_s *target);
bool stm32l1_probe(struct target_s *target);
bool lmi_probe(struct target_s *target);
bool lpc11xx_probe(struct target_s *target);
bool lpc43xx_probe(struct target_s *target);
bool sam3x_probe(struct target_s *target);
bool nrf51_probe(struct target_s *target);

#endif

