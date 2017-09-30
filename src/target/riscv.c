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

#define IR_IDCODE     0x01
#define IR_DTMCONTROL 0x10
#define IR_DBUS       0x11
#define IR_BYPASS     0x1f

#define DTMCONTROL_DBUSRESET (1 << 16)

#define DBUS_NOP   0
#define DBUS_READ  1
#define DBUS_WRITE 2

#define DBUS_DMCONTROL 0x10
#define DBUS_DMINFO    0x11

#define DMCONTROL_INTERRUPT (1ull << 33)
#define DMCONTROL_HALTNOT (1ull << 32)

#define OP_ITYPE(opcode, funct, rd, imm, rs1) \
                 ((opcode) | ((funct) << 12) | ((rd) << 7) | ((rs1) << 15) | ((imm) << 20))
#define OP_STYPE(opcode, funct, rs1, imm, rs2) \
                 ((opcode) | ((funct) << 12) | ((rs1) << 15) | ((rs2) << 20) | \
		  (((imm) & 0x1f) << 7) | (((imm) & 0xfe0) << 20))
#define OPCODE_LOAD   0x03
#define OPCODE_STORE  0x23
#define OPCODE_OP_IMM 0x13
#define OPCODE_JUMP   0x6f
#define OP_ADDI       0
#define LB(rd, imm, base) OP_ITYPE(OPCODE_LOAD, 0, rd, imm, base)
#define LH(rd, imm, base) OP_ITYPE(OPCODE_LOAD, 1, rd, imm, base)
#define LW(rd, imm, base) OP_ITYPE(OPCODE_LOAD, 2, rd, imm, base)
#define SB(rs, imm, base) OP_STYPE(OPCODE_STORE, 0, base, imm, rs)
#define SH(rs, imm, base) OP_STYPE(OPCODE_STORE, 1, base, imm, rs)
#define SW(rs, imm, base) OP_STYPE(OPCODE_STORE, 2, base, imm, rs)
#define J(imm)            (OPCODE_JUMP | ((imm) << 20))
#define ADDI(rd, rs, imm) OP_ITYPE(OPCODE_OP_IMM, OP_ADDI, rd, imm, rs)
#define S0 8
#define S1 9
#define T0 5
#define JRESUME(n)        (J(0x804 - (0x400 + ((n) * 4))))

#define CSR_TSELECT  0x7a0
#define CSR_MCONTROL 0x7a1
#define CSR_TDATA2   0x7a2

#define CSR_DCSR     0x7b0
#define CSR_DPC      0x7b1
#define CSR_DSCRATCH 0x7b2

#define CSR_MCONTROL_DMODE        (1<<(32-5))
#define CSR_MCONTROL_ENABLE_MASK  (0xf << 3)
#define CSR_MCONTROL_R            (1 << 0)
#define CSR_MCONTROL_W            (1 << 1)
#define CSR_MCONTROL_X            (1 << 2)
#define CSR_MCONTROL_RW           (CSR_MCONTROL_R | CSR_MCONTROL_W)
#define CSR_MCONTROL_RWX          (CSR_MCONTROL_RW | CSR_MCONTROL_X)
#define CSR_MCONTROL_ACTION_DEBUG (1 << 12)

/* GDB register map / target description */
static const char tdesc_rv32[] =
"<?xml version=\"1.0\"?>"
"<target>"
"  <architecture>riscv:rv32</architecture>"
"</target>";

struct riscv_dtm {
	uint8_t dtm_index;
	uint8_t version; /* As read from dmtcontrol */
	uint8_t abits; /* Debug bus address bits (6 bits wide) */
	uint8_t idle; /* Number of cycles required in run-test/idle */
	uint8_t dramsize; /* Size of debug ram in words - 1 */
	bool error;
	bool exception;
	uint64_t lastdbus;
	bool halt_requested;
};

static int riscv_breakwatch_set(target *t, struct breakwatch *);
static int riscv_breakwatch_clear(target *t, struct breakwatch *);

static void riscv_dtm_reset(struct riscv_dtm *dtm)
{
	jtag_dev_write_ir(&jtag_proc, dtm->dtm_index, IR_DTMCONTROL);
	uint32_t dtmcontrol = DTMCONTROL_DBUSRESET;
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
		jtag_dev_write_ir(&jtag_proc, dtm->dtm_index, IR_DBUS);
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
	                ((data & 0x3ffffffffull) << 2) | DBUS_WRITE;
	riscv_dtm_low_access(dtm, dbus);
}

static uint64_t riscv_dtm_read(struct riscv_dtm *dtm, uint32_t addr)
{
	riscv_dtm_low_access(dtm, ((uint64_t)addr << 36) | DBUS_READ);
	return riscv_dtm_low_access(dtm, DBUS_NOP);
}

static void ram_stub_write(struct riscv_dtm *dtm, int i, uint32_t inst, bool run)
{
	riscv_dtm_write(dtm, i, run ? DMCONTROL_INTERRUPT | inst : inst);
}

static uint32_t ram_stub_result(struct riscv_dtm *dtm, int i)
{
	uint64_t ex;
	do {
		ex = riscv_dtm_read(dtm, dtm->dramsize);
	} while (ex & DMCONTROL_INTERRUPT);
	if ((uint32_t)ex != 0) {
		DEBUG("%s exception 0x%"PRIx32"\n", __func__, (uint32_t)ex);
		dtm->exception = true;
		return 0;
	}
	return riscv_dtm_read(dtm, i);
}

static uint32_t riscv_debug_ram_exec(struct riscv_dtm *dtm,
                                     const uint32_t code[], int count)
{
	int i;
	for (i = 0; i < count - 1; i++) {
		ram_stub_write(dtm, i, code[i], false);
	}
	ram_stub_write(dtm, i, code[i], true);
	return ram_stub_result(dtm, count);
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

static uint32_t riscv_csreg_read(struct riscv_dtm *dtm, uint16_t csr)
{
	/* Debug RAM stub
	 * 400:   xxx02473   csrr  s0, <csr>
	 * 404:   40802623   sw    s0, 0x40c(zero)
	 * 408:   3fc0006f   j     0 <resume>
	 * 40c:              dw    data
	 */
	uint32_t ram[] = {0x00002473, 0x40802623, 0x3fc0006f};
	ram[0] |= (uint32_t)csr << 20;
	uint32_t val = riscv_debug_ram_exec(dtm, ram, 3);
	DEBUG("CSR(%03x) = 0x%x\n", csr, val);
	return val;
}

static void riscv_csreg_write(struct riscv_dtm *dtm, uint16_t csr, uint32_t val)
{
	/* Debug RAM stub
	 * 404:   40c02403   lw    s0, 0x40c(zero)
	 * 400:   xxx41073   csrw  s0, <csr>
	 * 408:   3fc0006f   j     0 <resume>
	 * 40c:              dw    data
	 */
	uint32_t ram[] = {0x40c02403, 0x00041073, 0x3fc0006f, val};
	ram[1] |= (uint32_t)csr << 20;
	riscv_debug_ram_exec(dtm, ram, 4);
}

static void riscv_gpreg_write(struct riscv_dtm *dtm, uint8_t reg, uint32_t val)
{
	/* Debug RAM stub
	 * 400:   40802403   lw    <rx>, 0x408(zero)
	 * 40c:   4000006f   j     0 <resume>
	 */
	uint32_t ram[] = {
		LW(reg, 0x408, 0),
		JRESUME(1),
		val
	};
	riscv_debug_ram_exec(dtm, ram, 3);
}

static void riscv_halt_request(target *t)
{
	DEBUG("Halt requested!\n");
	struct riscv_dtm *dtm = t->priv;
	/* Debug RAM stub
	 * 400:   7b046073   csrsi dcsr, halt
	 * 404:   4000006f   j     0 <resume>
	 */
	uint32_t ram[] = {0x7b046073, 0x4000006f};
	riscv_debug_ram_exec(dtm, ram, 2);
	dtm->halt_requested = true;
}

static void riscv_halt_resume(target *t, bool step)
{
	DEBUG("Resume requested! step=%d\n", step);
	struct riscv_dtm *dtm = t->priv;
	/* Debug RAM stub - we patch in step bit as needed
	 * 400:   7b006073   csrsi dcsr, 0
	 * 404:   7b047073   csrci dcsr, halt
	 * 408:   4000006f   j     0 <resume>
	 */
	uint32_t ram[] = {0x7b006073, 0x7b047073, 0x3fc0006f};
	if (step)
		ram[0] |= 4 << 15;
	else
		ram[1] |= 4 << 15;
	riscv_debug_ram_exec(dtm, ram, 3);
	dtm->halt_requested = false;
}

static void riscv_mem_read(target *t, void *dest, target_addr src, size_t len)
{
	struct riscv_dtm *dtm = t->priv;
	int size = 0;
	uint32_t load = 0;
	switch ((src | len) & 3) {
	case 0:
		load = LW(S1, 0, T0);
		size = 4;
		break;
	case 2:
		load = LH(S1, 0, T0);
		size = 2;
		break;
	case 1:
	case 3:
		load = LB(S1, 0, T0);
		size = 1;
		break;
	}
	uint32_t t0 = riscv_gpreg_read(dtm, T0);
	ram_stub_write(dtm, 0, LW(T0, 0x410, 0), false);
	ram_stub_write(dtm, 1, load, false);
	ram_stub_write(dtm, 2, SW(S1, 0x410, 0), false);
	ram_stub_write(dtm, 3, JRESUME(3), false);
	ram_stub_write(dtm, 4, src, true);
	while (len) {
		uint32_t r = ram_stub_result(dtm, 4);
		switch (size) {
		case 1: *(uint8_t*)dest = r;  break;
		case 2: *(uint16_t*)dest = r; break;
		case 4: *(uint32_t*)dest = r; break;
		}
		len -= size;
		dest += size;
		if (len)
			ram_stub_write(dtm, 0, ADDI(T0, T0, size), true);
	}
	riscv_gpreg_write(dtm, T0, t0);
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
	DEBUG("Resetting!\n");
	riscv_csreg_write(t->priv, CSR_DCSR, 1 << 29);
}

bool riscv_check_error(target *t)
{
	struct riscv_dtm *dtm = t->priv;
	if (dtm->error) {
		riscv_dtm_reset(dtm);
		dtm->error = false;
		dtm->exception = false;
		return true;
	}
	if (dtm->exception) {
		dtm->exception = false;
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

static ssize_t riscv_reg_read(target *t, int reg, void *data, size_t s)
{
	(void)s;
	struct riscv_dtm *dtm = t->priv;
	uint32_t *val = data;
	switch (reg) {
	case 0:
		*val = 0;
		break;
	case 8:
		*val = riscv_csreg_read(dtm, CSR_DSCRATCH);
		break;
	case 9:
		*val = riscv_dtm_read(dtm, dtm->dramsize);
		break;
	case 32:
		*val = riscv_csreg_read(dtm, CSR_DPC);
		break;
	case 65 ... 65 + 4095:
		*val = riscv_csreg_read(dtm, reg - 65);
		break;
	case 1 ... 7:
	case 10 ... 31:
		*val = riscv_gpreg_read(dtm, reg);
		break;
	}
	return sizeof(*val);
}

static void riscv_regs_write(target *t, const void *data)
{
	struct riscv_dtm *dtm = t->priv;
	const uint32_t *reg = data;
	for (int i = 0; i < 33; i++) {
		switch (i) {
		case 0:
			break;
		case 8:
			riscv_csreg_write(dtm, CSR_DSCRATCH, reg[i]);
			break;
		case 9:
			riscv_dtm_write(dtm, dtm->dramsize, reg[i]);
			break;
		case 32:
			riscv_csreg_write(dtm, CSR_DPC, reg[i]);
			break;
		default:
			riscv_gpreg_write(dtm, i, reg[i]);
		}
	}
}

static enum target_halt_reason riscv_halt_poll(target *t, target_addr *watch)
{
	(void)watch;
	struct riscv_dtm *dtm = t->priv;
	uint64_t dmcontrol = riscv_dtm_read(dtm, DBUS_DMCONTROL);
	DEBUG("dmcontrol = 0x%"PRIx64"\n", dmcontrol);
	if (!dtm->halt_requested && (dmcontrol & DMCONTROL_HALTNOT) == 0)
		return TARGET_HALT_RUNNING;

	uint32_t dcsr = riscv_csreg_read(dtm, CSR_DCSR);
	uint8_t cause = (dcsr >> 6) & 7;
	DEBUG_WARN("cause = %d\n", cause);
	switch (cause) {
	case 0: return TARGET_HALT_RUNNING;
	case 1: /* Software breakpoint */
	case 2: /* Hardware trigger breakpoint */
		return TARGET_HALT_BREAKPOINT;
	case 3: return TARGET_HALT_REQUEST;
	case 4: return TARGET_HALT_STEPPING;
	case 5: return TARGET_HALT_REQUEST;
	default:
		return TARGET_HALT_ERROR;
	}
}

void riscv_jtag_handler(jtag_dev_t *jd)
{
	uint32_t dtmcontrol = 0;
	DEBUG("Scanning RISC-V jtag dev at pos %d, idcode %08" PRIx32 "\n",
		  jd->jd_dev, jd->jd_idcode);
	jtag_dev_write_ir(&jtag_proc, jd->jd_dev, IR_DTMCONTROL);
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

	jtag_dev_write_ir(&jtag_proc, jd->jd_dev, IR_DBUS);

	uint32_t dminfo = riscv_dtm_read(dtm, DBUS_DMINFO);
	DEBUG("dminfo = %"PRIx32"\n", dminfo);
	uint8_t dmversion = ((dminfo >> 4) & 0xc) | (dminfo & 3);
	DEBUG("dminfo = %"PRIx32"\n", dminfo);
	DEBUG("\tloversion = %d\n", dmversion);
	if (dmversion != 1)
		return;

	uint8_t authenticated = (dminfo >> 5) & 1;
	DEBUG("\tauthenticated = %d\n", authenticated);
	if (authenticated != 1)
		return;

#if defined(ENABLE_DEBUG) && defined(PLATFORM_HAS_DEBUG)
	dtm->dramsize = (dminfo >> 10) & 0x3f;
	DEBUG("\tdramsize = %d (%d bytes)\n", dtm->dramsize, (dtm->dramsize + 1) * 4);

	riscv_dtm_write(dtm, 0, 0xbeefcafe);
	riscv_dtm_write(dtm, 1, 0xdeadbeef);
	DEBUG("%"PRIx32"\n", (uint32_t)riscv_dtm_read(dtm, 0));
	DEBUG("%"PRIx32"\n", (uint32_t)riscv_dtm_read(dtm, 1));
	for (int i = 0; i < 	dtm->dramsize + 1; i++) {
		DEBUG("DebugRAM[%d] = %08"PRIx64"\n", i,
			  riscv_dtm_read(dtm, 0x400 + i*4));
	}
#else
	(void)riscv_dtm_write;
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
	t->reg_read = riscv_reg_read;
	t->regs_write = riscv_regs_write;
	t->reset = riscv_reset;
	t->halt_request = riscv_halt_request;
	t->halt_poll = riscv_halt_poll;
	t->halt_resume = riscv_halt_resume;
	t->regs_size = 33 * 4;
	t->tdesc = tdesc_rv32;

	t->breakwatch_set = riscv_breakwatch_set;
	t->breakwatch_clear = riscv_breakwatch_clear;
}

static int riscv_breakwatch_set(target *t, struct breakwatch *bw)
{
	struct riscv_dtm *dtm = t->priv;
	unsigned i;
	uint32_t mcontrol = CSR_MCONTROL_DMODE | CSR_MCONTROL_ACTION_DEBUG |
	                    CSR_MCONTROL_ENABLE_MASK;

	switch (bw->type) {
	case TARGET_BREAK_HARD:
		mcontrol |= CSR_MCONTROL_X;
		break;
	case TARGET_WATCH_WRITE:
		mcontrol |= CSR_MCONTROL_W;
		break;
	case TARGET_WATCH_READ:
		mcontrol |= CSR_MCONTROL_R;
		break;
	case TARGET_WATCH_ACCESS:
		mcontrol |= CSR_MCONTROL_RW;
		break;
	default:
		return 1;
	}

	uint32_t tselect_saved = riscv_csreg_read(dtm, CSR_TSELECT);

	for (i = 0; ; i++) {
		riscv_csreg_write(dtm, CSR_TSELECT, i);
		if (riscv_csreg_read(dtm, CSR_TSELECT) != i)
			return -1;
		uint32_t tdata1 = riscv_csreg_read(dtm, CSR_MCONTROL);
		uint8_t type = (tdata1 >> (32-4)) & 0xf;
		if ((type == 0))
			return -1;
		if ((type == 2)  &&
		    (((tdata1 & CSR_MCONTROL_RWX) == 0) ||
		     ((tdata1 & CSR_MCONTROL_ENABLE_MASK) == 0)))
			break;
	}
	/* if we get here tselect = i is the index of our trigger */
	bw->reserved[0] = i;

	riscv_csreg_write(dtm, CSR_MCONTROL, mcontrol);
	riscv_csreg_write(dtm, CSR_TDATA2, bw->addr);

	/* Restore saved tselect */
	riscv_csreg_write(dtm, CSR_TSELECT, tselect_saved);
	return 0;
}

static int riscv_breakwatch_clear(target *t, struct breakwatch *bw)
{
	struct riscv_dtm *dtm = t->priv;
	unsigned i = bw->reserved[0];
	uint32_t tselect_saved = riscv_csreg_read(dtm, CSR_TSELECT);

	riscv_csreg_write(dtm, CSR_TSELECT, i);
	riscv_csreg_write(dtm, CSR_MCONTROL, 0);

	/* Restore saved tselect */
	riscv_csreg_write(dtm, CSR_TSELECT, tselect_saved);
	return 0;
}
