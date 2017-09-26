/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2017  Black Sphere Technologies Ltd.
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

/* This file implements debugging functionality specific to RISC-V targets.
 * According to risv-debug-spec 0.11nov12 November 12, 2016
 */
#define DEBUG DEBUG_WARN

#include "general.h"
#include "jtagtap.h"
#include "jtag_scan.h"
#include "target.h"
#include "target_internal.h"

#include <assert.h>

#define RISCV_IR_IDCODE     0x01
#define RISCV_IR_DTMCONTROL 0x10
#define RISCV_IR_DBUS       0x11
#define RISCV_IR_BYPASS     0x1f

#define RISCV_DTMCONTROL_DBUSRESET (1 << 16)

#define RISCV_DBUS_NOP   0
#define RISCV_DBUS_READ  1
#define RISCV_DBUS_WRITE 2

#define RISCV_DMCONTROL 0x10
#define RISCV_DMINFO    0x11

#define RISCV_DMCONTROL_INTERRUPT (1ull << 33)
#define RISCV_DMCONTROL_HALTNOT (1ull << 32)

struct riscv_dtm {
	uint8_t dtm_index;
	uint8_t version; /* As read from dmtcontrol */
	uint8_t abits; /* Debug bus address bits (6 bits wide) */
	uint8_t idle; /* Number of cycles required in run-test/idle */
	bool error;
	uint64_t lastdbus;
};

static void riscv_dtm_reset(struct riscv_dtm *dtm)
{
	jtag_dev_write_ir(&jtag_proc, dtm->dtm_index, RISCV_IR_DTMCONTROL);
	uint32_t dtmcontrol = RISCV_DTMCONTROL_DBUSRESET;
	jtag_dev_shift_dr(&jtag_proc, dtm->dtm_index, (void*)&dtmcontrol, (void*)&dtmcontrol, 32);
	DEBUG("after dbusreset: dtmcontrol = 0x%08x\n", dtmcontrol);
}

static uint64_t riscv_dtm_low_access(struct riscv_dtm *dtm, uint64_t dbus)
{
	if (dtm->error)
		return 0;

	uint64_t ret = 0;
	/* Do not smash the stack is abits has gone astray!*/
	if (dtm->abits > (64 - 36)) {
		DEBUG("Abits overflow in  riscv_dtm_low_access: %d\n", dtm->abits);
		return 0;
	}
retry:
	DEBUG("out %"PRIx64"\n", dbus);
	jtag_dev_shift_dr(&jtag_proc, dtm->dtm_index, (void*)&ret, (const void*)&dbus,
					  36 + dtm->abits);
	switch (ret & 3) {
	case 3:
		riscv_dtm_reset(dtm);
		jtag_dev_write_ir(&jtag_proc, dtm->dtm_index, RISCV_IR_DBUS);
		DEBUG("retry out %"PRIx64"\n", dbus);
		jtag_dev_shift_dr(&jtag_proc, dtm->dtm_index,
		                  (void*)&ret, (const void*)&dtm->lastdbus,
		                  dtm->abits + 36);
		DEBUG("in %"PRIx64"\n", ret);
		jtag_proc.jtagtap_tms_seq(0, dtm->idle);
		goto retry;
	case 0:
		dtm->lastdbus = dbus;
		break;
	case 2:
	default:
		DEBUG("Set sticky error!");
		dtm->error = true;
		return 0;
	}
	jtag_proc.jtagtap_tms_seq(0, dtm->idle);
	return (ret >> 2) & 0x3ffffffffull;
}

static void riscv_dtm_write(struct riscv_dtm *dtm, uint32_t addr, uint64_t data)
{
	uint64_t dbus = ((uint64_t)addr << 36) |
	                ((data & 0x3ffffffffull) << 2) | RISCV_DBUS_WRITE;
	riscv_dtm_low_access(dtm, dbus);
}

static uint64_t riscv_dtm_read(struct riscv_dtm *dtm, uint32_t addr)
{
	riscv_dtm_low_access(dtm, ((uint64_t)addr << 36) | RISCV_DBUS_READ);
	return riscv_dtm_low_access(dtm, RISCV_DBUS_NOP);
}
static uint32_t riscv_debug_ram_exec(struct riscv_dtm *dtm,
                                     const uint32_t code[], int count)
{
	int i;
	for (i = 0; i < count - 1; i++) {
		riscv_dtm_write(dtm, i, code[i]);
	}
	riscv_dtm_write(dtm, i, code[i] | RISCV_DMCONTROL_INTERRUPT);
	uint64_t ret;
	do {
		ret = riscv_dtm_read(dtm, count);
	} while (ret & RISCV_DMCONTROL_INTERRUPT);
	return ret;
}

static uint32_t riscv_mem_read32(struct riscv_dtm *dtm, uint32_t addr)
{
	/* Debug RAM stub
	 * 400:   41002403   lw   s0, 0x410(zero)
	 * 404:   00042483   lw   s1, 0(s0)
	 * 408:   40902a23   sw   s1, 0x414(zero)
	 * 40c:   3f80006f   j    0 <resume>
	 * 410:              dw   addr
	 * 414:              dw   data
	 */
	uint32_t ram[] = {0x41002403, 0x42483, 0x40902a23, 0x3f80006f, addr};
	return riscv_debug_ram_exec(dtm, ram, 5);
}

static void riscv_mem_write32(struct riscv_dtm *dtm,
                              uint32_t addr, uint32_t val)
{
	/* Debug RAM stub
	 * 400:   41002403   lw   s0, 0x410(zero)
	 * 408:   41402483   lw   s1, 0x414(zero)
	 * 404:   00942023   sw   s1, 0(s0)
	 * 40c:   3f80006f   j    0 <resume>
	 * 410:              dw   addr
	 * 414:              dw   data
	 */
	uint32_t ram[] = {0x41002403, 0x41402483, 0x942023, 0x3f80006f, addr, val};
	riscv_debug_ram_exec(dtm, ram, 5);
}

static uint32_t riscv_gpreg_read(struct riscv_dtm *dtm, uint8_t reg)
{
	/* Debug RAM stub
	 * 400:   40x02423   sw    <rx>, 0x408(zero)
	 * 40c:   4000006f   j     0 <resume>
	 */
	uint32_t ram[] = {0x40002423, 0x4000006f};
	ram[0] |= reg << 20;
	uint32_t val = riscv_debug_ram_exec(dtm, ram, 2);
	DEBUG("x%d = 0x%x\n", reg, val);
	return val;
}

static void riscv_gpreg_write(struct riscv_dtm *dtm, uint8_t reg, uint32_t val)
{
	/* Debug RAM stub
	 * 400:   40802403   lw    <rx>, 0x408(zero)
	 * 40c:   4000006f   j     0 <resume>
	 */
	uint32_t ram[] = {0x40002423, 0x4000006f, val};
	ram[0] |= reg << 7;
	riscv_debug_ram_exec(dtm, ram, 3);
}

static void riscv_halt_request(target *t)
{
	struct riscv_dtm *dtm = t->priv;
	/* Debug RAM stub
	 * 400:   7b026073   csrsi dcsr,4
	 * 40c:   4000006f   j     0 <resume>
	 */
	uint32_t ram[] = {0x7b026073, 0x4000006f};
	riscv_debug_ram_exec(dtm, ram, 2);
}

static void riscv_halt_resume(target *t, bool step)
{
	assert(!step);
	struct riscv_dtm *dtm = t->priv;
	/* Debug RAM stub
	 * 400:   7b027073   csrci dcsr,4
	 * 40c:   4000006f   j     0 <resume>
	 */
	uint32_t ram[] = {0x7b027073, 0x4000006f};
	riscv_debug_ram_exec(dtm, ram, 2);
}

static void riscv_mem_read(target *t, void *dest, target_addr src, size_t len)
{
	uint32_t *d = dest;
	assert((src & 3) == 0);
	assert((len & 3) == 0);
	while (len) {
		*d++ = riscv_mem_read32(t->priv, src);
		src += 4;
		len -= 4;
	}
}

static void riscv_mem_write(target *t, target_addr dest, const void *src, size_t len)
{
	const uint32_t *s = src;
	assert((dest & 3) == 0);
	assert((len & 3) == 0);
	while (len) {
		riscv_mem_write32(t->priv, dest, *s++);
		dest += 4;
		len -= 4;
	}
}

static void riscv_reset(target *t)
{
	(void)t;
	DEBUG("RISC-V reset not implemented!\n");
}

bool riscv_check_error(target *t)
{
	struct riscv_dtm *dtm = t->priv;
	if (dtm->error) {
		riscv_dtm_reset(dtm);
		dtm->error = false;
		return true;
	}
	return false;
}

static bool riscv_attach(target *t)
{
	target_halt_request(t);
	return true;
}

static void riscv_detach(target *t)
{
	target_halt_resume(t, false);
}

static void riscv_regs_read(target *t, void *data)
{
	uint32_t *reg = data;
	for (int i = 0; i < 32; i++)
		*reg++ = riscv_gpreg_read(t->priv, i);
}

static void riscv_regs_write(target *t, const void *data)
{
	const uint32_t *reg = data;
	for (int i = 0; i < 32; i++)
		riscv_gpreg_write(t->priv, i, *reg++);
}

static enum target_halt_reason riscv_halt_poll(target *t, target_addr *watch)
{
	(void)watch;
	struct riscv_dtm *dtm = t->priv;
	uint64_t dmcontrol = riscv_dtm_read(dtm, RISCV_DMCONTROL);
	if (dmcontrol & RISCV_DMCONTROL_HALTNOT)
		return TARGET_HALT_REQUEST;
	return TARGET_HALT_RUNNING;
}

void riscv_jtag_handler(jtag_dev_t *jd)
{
	uint32_t dtmcontrol = 0;
	DEBUG("Scanning RISC-V jtag dev at pos %d, idcode %08" PRIx32 "\n",
		  jd->jd_dev, jd->jd_idcode);
	jtag_dev_write_ir(&jtag_proc, jd->jd_dev, RISCV_IR_DTMCONTROL);
	jtag_dev_shift_dr(&jtag_proc, jd->jd_dev, (void*)&dtmcontrol, (void*)&dtmcontrol, 32);
	DEBUG("dtmcontrol = 0x%08x\n", dtmcontrol);
	uint8_t version = dtmcontrol & 0xf;

	if (version > 0) {
		DEBUG("Only DTM version 0 handle. Version is %d\n", version);
// FIXME		return; /* We'll come back to this someday */
	}

	struct riscv_dtm *dtm = alloca(sizeof(*dtm));
	memset(dtm, 0, sizeof(*dtm));
	dtm->dtm_index = jd->jd_dev;
	dtm->abits = ((dtmcontrol >> 13) & 3) << 4 |
	              ((dtmcontrol >> 4) & 0xf);
	dtm->idle = (dtmcontrol >> 10) & 7;
	DEBUG("abits = %d\n", dtm->abits);
	DEBUG("idle = %d\n", dtm->idle);
	DEBUG("dbusstat = %d\n", (dtmcontrol >> 8) & 3);
	riscv_dtm_reset(dtm);

	jtag_dev_write_ir(&jtag_proc, jd->jd_dev, RISCV_IR_DBUS);

	uint32_t dminfo = riscv_dtm_read(dtm, RISCV_DMINFO);
	uint8_t dmversion = ((dminfo >> 4) & 0xc) | (dminfo & 3);
#if defined(ENABLE_DEBUG) && defined(PLATFORM_HAS_DEBUG)
	uint64_t dmcontrol = riscv_dtm_read(dtm, RISCV_DMCONTROL);
	DEBUG("dmcontrol = %"PRIx64"\n", dmcontrol);
	DEBUG("dminfo = %"PRIx32"\n", dminfo);
	DEBUG("\tloversion = %d\n", dmversion);
#endif
	if (dmversion != 1)
		return;

	uint8_t authenticated = (dminfo >> 5) & 1;
#if defined(ENABLE_DEBUG) && defined(PLATFORM_HAS_DEBUG)
	uint8_t authtype = (dminfo >> 2) & 3;
	uint8_t authbusy = (dminfo >> 4) & 1;
	DEBUG("\tauthtype = %d, authbusy = %d, authenticated = %d\n",
	      authtype, authbusy, authenticated);
#endif
	if (authenticated != 1)
		return;

#if defined(ENABLE_DEBUG) && defined(PLATFORM_HAS_DEBUG)
	uint8_t dramsize = (dminfo >> 10) & 0x3f;
	DEBUG("\tdramsize = %d (%d bytes)\n", dramsize, (dramsize + 1) * 4);

	riscv_dtm_write(dtm, 0, 0xbeefcafe);
	riscv_dtm_write(dtm, 1, 0xdeadbeef);
	DEBUG("%"PRIx32"\n", (uint32_t)riscv_dtm_read(dtm, 0));
	DEBUG("%"PRIx32"\n", (uint32_t)riscv_dtm_read(dtm, 1));
	for (int i = 0; i < dramsize + 1; i++) {
		DEBUG("DebugRAM[%d] = %08"PRIx32"\n", i,
			  riscv_mem_read32(dtm, 0x400 + i*4));
	}
#else
	(void)riscv_dtm_write;
	(void)riscv_mem_read32;
#endif
	/* Allocate and set up new target */
	target *t = target_new();
	t->priv = malloc(sizeof(*dtm));
	memcpy(t->priv, dtm, sizeof(*dtm));
	dtm = t->priv;
	t->priv_free = free;
	t->driver = "RISC-V";
	t->mem_read = riscv_mem_read;
	t->mem_write = riscv_mem_write;
	t->attach = riscv_attach;
	t->detach = riscv_detach;
	t->check_error = riscv_check_error;
	t->regs_read = riscv_regs_read;
	t->regs_write = riscv_regs_write;
	t->reset = riscv_reset;
	t->halt_request = riscv_halt_request;
	t->halt_poll = riscv_halt_poll;
	t->halt_resume = riscv_halt_resume;
	t->regs_size = 33 * 4;
}
