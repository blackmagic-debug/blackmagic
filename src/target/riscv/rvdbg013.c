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

/**
 + This file implements the
 * RISC-V External Debug Support Version 0.13
 */

#include <limits.h>
#include "general.h"
#include "exception.h"
#include "target.h"
#include "target_internal.h"

#include "rvdbg.h"
#include "rv32i_isa.h"

enum DMI_OP {
	DMI_OP_NOP   = 0,
	DMI_OP_READ  = 1,
	DMI_OP_WRITE = 2,
};

enum DMI_REG {
	DMI_REG_ABSTRACTDATA_BEGIN = 0x04,
	DMI_REG_ABSTRACTDATA_END   = 0x0f,
	DMI_REG_DMCONTROL          = 0x10,
	DMI_REG_DMSTATUS           = 0x11,
	DMI_REG_HARTINFO           = 0x12,
	DMI_REG_HALTSUM1           = 0x13,
	DMI_REG_HAWINDOWSEL        = 0x14,
	DMI_REG_HAWINDOW           = 0x15,
	DMI_REG_ABSTRACT_CS        = 0x16,
	DMI_REG_ABSTRACT_CMD       = 0x17,
	DMI_REG_ABSTRACT_AUTOEXEC  = 0x18,
	DMI_REG_CONFSTR_PTR0       = 0x19,
	DMI_REG_CONFSTR_PTR1       = 0x1a,
	DMI_REG_CONFSTR_PTR2       = 0x1b,
	DMI_REG_CONFSTR_PTR3       = 0x1c,
	DMI_REG_NEXTDM_ADDR        = 0x1d,
	DMI_REG_PROGRAMBUF_BEGIN   = 0x20,
	DMI_REG_PROGRAMBUF_END     = 0x2f,
	DMI_REG_AUTHDATA		   = 0x30,
	DMI_REG_HALTSUM2           = 0x34,
	DMI_REG_HALTSUM3 		   = 0x35,
	DMI_REG_SBADDRESS3		   = 0x37,
	DMI_REG_SYSBUSCS           = 0x38,
	DMI_REG_SBADDRESS0		   = 0x39,
	DMI_REG_SBADDRESS1		   = 0x3a,
	DMI_REG_SBADDRESS2		   = 0x3b,
	DMI_REG_SBDATA0			   = 0x3c,
	DMI_REG_SBDATA1			   = 0x3d,
	DMI_REG_SBDATA2			   = 0x3e,
	DMI_REG_SBDATA3			   = 0x3f,
	DMI_REG_HALTSUM0	 	   = 0x40,
};

enum ABSTRACTCMD_TYPE {
	ABSTRACTCMD_TYPE_ACCESS_REGISTER = 0x0,
	ABSTRACTCMD_TYPE_QUICK_ACCESS    = 0x1,
	ABSTRACTCMD_TYPE_ACCESS_MEMORY   = 0x2,
};

enum ABSTRACTCMD_ERR {
	ABSTRACTCMD_ERR_NONE = 0x0,
	ABSTRACTCMD_ERR_BUSY = 0x1,
	ABSTRACTCMD_ERR_NOT_SUPPORTED = 0x2,
	ABSTRACTCMD_ERR_EXCEPTION = 0x3,
	ABSTRACTCMD_ERR_HALT_RESUME = 0x4,
	ABSTRACTCMD_ERR_BUS = 0x5,
	ABSTRACTCMD_ERR_OTHER = 0x7,
};

enum AUTOEXEC_STATE {
	AUTOEXEC_STATE_NONE, /* Ingnore autoexec */
	AUTOEXEC_STATE_INIT, /* Setup everything + AARAUTOINC */
	AUTOEXEC_STATE_CONT, /* Only access data0 register */
};

enum HART_REG {
	HART_REG_CSR_BEGIN   = 0x0000,
	HART_REG_CSR_MISA    = 0x0301,
	HART_REG_CSR_MHARTID = 0x0f14,
	HART_REG_CSR_END     = 0x0fff,
	HART_REG_GPR_BEGIN   = 0x1000,
	HART_REG_GPR_END     = 0x101f,
};

#define DMSTATUS_GET_VERSION(x)         DTMCS_GET_VERSION(x)
#define DMSTATUS_GET_CONFSTRPTRVALID(x) ((x >> 4) & 0x1)
#define DMSTATUS_GET_HASRESETHALTREQ(x) ((x >> 5) & 0x1)
#define DMSTATUS_GET_AUTHBUSY(x)		((x >> 6) & 0x1)
#define DMSTATUS_GET_AUTHENTICATED(x)   ((x >> 7) & 0x1)
#define DMSTATUS_GET_ANYNONEXISTENT(x)  ((x >> 14) & 0x1)
#define DMSTATUS_GET_ANYHAVERESET(x)    ((x >> 18) & 0x1)
#define DMSTATUS_GET_IMPEBREAK(x)	    ((x >> 22) & 0x1)
#define DMSTATUS_GET_ALLHALTED(x)       ((x >> 9) & 0x1)

#define DMCONTROL_GET_HARTSEL(x)      (((x >> 16) & 0x3ff) | (((x >> 6) & 0x3ff) << 10))
#define DMCONTROL_MK_HARTSEL(s)       (((s) & 0x3ff) << 16) | ((s) & (0x3ff << 10) >> 4)
#define DMCONTROL_HASEL               (0x1 << 26)
#define DMCONTROL_HALTREQ             (0x1U << 31)
#define DMCONTROL_HARTRESET           (0x1U << 29)
#define DMCONTROL_DMACTIVE            (0x1)
#define DMCONTROL_NDMRESET		      (0x1U << 1)
#define DMCONTROL_ACKHAVERESET        (0x1 << 28)
#define DMCONTROL_SRESETHALTREQ       (0x1U << 3)
#define DMCONTROL_CRESETHALTREQ       (0x1U << 2)

#define ABSTRACTCS_GET_DATACOUNT(x)   (x & 0xf)
#define ABSTRACTCS_GET_CMDERR(x)      ((x >> 8) & 0x7)
#define ABSTRACTCS_CLEAR_CMDERR(t) do { t |= (0x7 << 8);} while (0)
#define ABSTRACTCS_GET_BUSY(x)		  ((x >> 12) & 0x1)
#define ABSTRACTCS_GET_PROGBUFSIZE(x) ((x >> 24) & 0x1f)

#define ABSTRACTCMD_SET_TYPE(t, s) do { \
	t &= ~(0xff << 24); \
	t |= (s & 0xff) << 24; } while (0)
#define ABSTRACTCMD_ACCESS_REGISTER_SET_AARSIZE(t, s) do { \
	t &= ~(0x7 << 20); \
	t |= (s & 0x7) << 20; } while (0)
#define ABSTRACTCMD_ACCESS_REGISTER_SET_AARPOSTINCREMENT(t, s) do { \
	t &= ~(0x1 << 19); \
	t |= (s & 0x1) << 19; } while (0)
#define ABSTRACTCMD_ACCESS_REGISTER_SET_POSTEXEC(t, s) do { \
	t &= ~(0x1 << 18); \
	t |= (s & 0x1) << 18; } while (0)
#define ABSTRACTCMD_ACCESS_REGISTER_SET_TRANSFER(t, s) do { \
	t &= ~(0x1 << 17); \
	t |= (s & 0x1) << 17; } while (0)
#define ABSTRACTCMD_ACCESS_REGISTER_SET_WRITE(t, s)    do { \
	t &= ~(0x1 << 16); \
	t |= (s & 0x1) << 16; } while (0)
#define ABSTRACTCMD_ACCESS_REGISTER_SET_REGNO(t, s)    do { \
	t &= ~(0xffff); \
	t |= s & 0xffff; } while (0)

#define ABSTRACTAUTO_SOME_PATTEN		(0b101010101010)
#define ABSTRACTAUTO_GET_DATA(x)        (x & 0xfff)
#define ABSTRACTAUTO_SET_DATA(t, s)     do { \
	t &= ~(0xfff); \
	t |= s & 0xfff; } while (0)

#define RISCV_MAX_HARTS 32U

void rvdbd_dmi_ref(RVDBGv013_DMI_t *dtm)
{
    dtm->refcnt++;
}

void rvdbd_dmi_unref(RVDBGv013_DMI_t *dtm)
{
    if (--dtm->refcnt == 0) {
	dtm->rvdbg_dmi_free(dtm);
    }
}

/* Busy is only seen with the second dmi access */
static int rvdbg_dmi_write(RVDBGv013_DMI_t *dmi, uint32_t addr, uint32_t data)
{
	int res = -1;
	dmi->rvdbg_dmi_low_access(
		dmi, NULL, ((uint64_t)addr << DMI_BASE_BIT_COUNT) | (data << 2) | DMI_OP_WRITE);
	res  = dmi->rvdbg_dmi_low_access(dmi, NULL, DMI_OP_NOP);
	DEBUG_TARGET("DMI write add %08" PRIx32 ", data %08" PRIx32 "\n", addr, data);
	return res;
}

static int rvdbg_dmi_read(RVDBGv013_DMI_t *dmi, uint32_t addr, uint32_t *data)
{
	int res = 0;
	dmi->rvdbg_dmi_low_access(dmi, NULL, ((uint64_t)addr << DMI_BASE_BIT_COUNT) | DMI_OP_READ);
	res = dmi->rvdbg_dmi_low_access(dmi, data, DMI_OP_NOP);
	DEBUG_TARGET("DMI Read addr %x%s:data %x\n", *data, (res == -1) ? "failed" : "", *data);
	return res;
}

int rvdbg_set_debug_version(RVDBGv013_DMI_t *dmi, uint8_t version)
{
	switch (version) {
		case RISCV_DEBUG_VERSION_013:
			dmi->debug_version = version;
			break;
		case RISCV_DEBUG_VERSION_011:
			DEBUG_WARN("Error: RISC-V debug 0.11 not supported\n");
			return -1;
		case RISCV_DEBUG_VERSION_UNKNOWN:
		default:
			DEBUG_WARN("RISC-V target unknown debug spec verson: %d\n", version);
			return -1;
	}

	return 0;
}

#ifdef ENABLE_DEBUG
static const char* rvdbg_version_tostr(enum RISCV_DEBUG_VERSION version)
{
	switch (version) {
		case RISCV_DEBUG_VERSION_011:
			return "0.11";
		case RISCV_DEBUG_VERSION_013:
			return "0.13";
		case RISCV_DEBUG_VERSION_UNKNOWN:
		default:
			return "UNKNOWN";
	}
}
#endif /* ENABLE_DEBUG */

// TODO: Remove
__attribute__((used))
static int rvdbg_halt_current_hart(RVDBGv013_DMI_t *dmi)
{
	uint32_t dmstatus, dmcontrol;

	DEBUG_INFO("current hart = %d\n", dmi->current_hart);

	// Trigger the halt request
	if (rvdbg_dmi_read(dmi, DMI_REG_DMCONTROL, &dmcontrol) < 0)
			return -1;

	dmcontrol |= DMCONTROL_HALTREQ;
	if (rvdbg_dmi_write(dmi, DMI_REG_DMCONTROL, dmcontrol) < 0)
		return -1;

	// Now wait for the hart to halt
	for (unsigned int i = 0; i < 512; i++) {
		if (rvdbg_dmi_read(dmi, DMI_REG_DMSTATUS, &dmstatus) < 0)
			return -1;
		if (DMSTATUS_GET_ANYHAVERESET(dmstatus)) {
			DEBUG_WARN("RISC-V: got reset, while trying to halt\n");
			if (rvdbg_dmi_write(dmi, DMI_REG_DMCONTROL, dmcontrol | DMCONTROL_ACKHAVERESET) < 0)
				return -1;
		}
		if (DMSTATUS_GET_ALLHALTED(dmstatus))
			break;
	}

	if (!DMSTATUS_GET_ALLHALTED(dmstatus)) {
		if (rvdbg_dmi_read(dmi, DMI_REG_DMSTATUS, &dmstatus) < 0)
			return -1;

		// DEBUG("RISC-V: error, can not halt hart %d, dmstatus = 0x%08x\n",
		// 	dmi->current_hart, dmstatus);

		DEBUG_WARN("RISC-V: error, can not halt hart %d, dmstatus = 0x%08x -> trying resethaltreq\n",
			dmi->current_hart, dmstatus);

		if (dmi->support_resethaltreq) {
			if (rvdbg_dmi_write(dmi, DMI_REG_DMCONTROL, dmcontrol | DMCONTROL_SRESETHALTREQ) < 0)
				return -1;
		}

		if (rvdbg_dmi_write(dmi, DMI_REG_DMCONTROL, dmcontrol | DMCONTROL_NDMRESET | DMCONTROL_HARTRESET) < 0)
			return -1;

		platform_delay(1000);

		if (rvdbg_dmi_write(dmi, DMI_REG_DMCONTROL, dmcontrol) < 0)
			return -1;

		if (dmi->support_resethaltreq) {
			if (rvdbg_dmi_write(dmi, DMI_REG_DMCONTROL, dmcontrol | DMCONTROL_CRESETHALTREQ) < 0)
				return -1;
		}
	}

	// Now wait for the hart to halt
	for (unsigned int i = 0; i < 512; i++) {
		if (rvdbg_dmi_read(dmi, DMI_REG_DMSTATUS, &dmstatus) < 0)
			return -1;
		if (DMSTATUS_GET_ANYHAVERESET(dmstatus)) {
			DEBUG_WARN("RISC-V: got reset, while trying to halt\n");
			if (rvdbg_dmi_write(dmi, DMI_REG_DMCONTROL, dmcontrol | DMCONTROL_ACKHAVERESET) < 0)
				return -1;
		}
		if (DMSTATUS_GET_ALLHALTED(dmstatus))
			break;
	}

	if (!DMSTATUS_GET_ALLHALTED(dmstatus)) {
		if (rvdbg_dmi_read(dmi, DMI_REG_DMSTATUS, &dmstatus) < 0)
			return -1;

		DEBUG_WARN("RISC-V: error, can not halt hart %d, dmstatus = 0x%08x -> giving up\n",
			dmi->current_hart, dmstatus);
	}

	return 0;
}

static int rvdbg_discover_hart(RVDBGv013_DMI_t *dmi)
{
	uint32_t hartinfo;
	HART_t *hart = &dmi->harts[dmi->current_hart];

	if (rvdbg_dmi_read(dmi, DMI_REG_HARTINFO, &hartinfo) < 0)
		return -1;

	hart->dataaddr = hartinfo & 0xfff;
	hart->datasize = (hartinfo >> 12) & 0xf;
	hart->dataaccess = (hartinfo >> 16) & 0x1;
	hart->nscratch = (hartinfo >> 20) & 0xf;

	return 0;
}

static int rvdbg_discover_harts(RVDBGv013_DMI_t *dmi)
{
	uint32_t hart_idx, hartsellen, dmstatus, dmcontrol;

	dmi->current_hart = 0;

	// Set all 20 bits of hartsel
	dmcontrol = DMCONTROL_DMACTIVE | DMCONTROL_MK_HARTSEL(0xfffff);

	if (rvdbg_dmi_write(dmi, DMI_REG_DMCONTROL, dmcontrol) < 0)
		return -1;

	if (rvdbg_dmi_read(dmi, DMI_REG_DMCONTROL, &dmcontrol) < 0)
		return -1;

	hartsellen = DMCONTROL_GET_HARTSEL(dmcontrol);
	dmi->hartsellen = 0;

	while (hartsellen & 0x1) {
		dmi->hartsellen++;
		hartsellen >>= 1;
	}

	DEBUG_INFO("hartsellen = %d\n", dmi->hartsellen);

	// Iterate over all possible harts
	for (hart_idx = 0; hart_idx < MIN(1U << dmi->hartsellen, RISCV_MAX_HARTS)
			&& dmi->num_harts < ARRAY_NUMELEM(dmi->harts); hart_idx++) {
		dmcontrol = DMCONTROL_DMACTIVE | DMCONTROL_MK_HARTSEL(hart_idx);
	dmi->current_hart = hart_idx;

		if (rvdbg_dmi_write(dmi, DMI_REG_DMCONTROL, dmcontrol) < 0)
			return -1;

		// Check if anynonexist is true -> abort
		if (rvdbg_dmi_read(dmi, DMI_REG_DMSTATUS, &dmstatus) < 0)
			return -1;

		if (DMSTATUS_GET_ANYNONEXISTENT(dmstatus)) {
			DEBUG_WARN("Hart idx 0x%05x does not exist\n", hart_idx);
			break;
		}

		if (DMSTATUS_GET_ANYHAVERESET(dmstatus)) {
			DEBUG_INFO("Hart idx 0x%05x has reset, acknowledge\n", hart_idx);
			dmcontrol = DMCONTROL_DMACTIVE | DMCONTROL_MK_HARTSEL(hart_idx) | DMCONTROL_ACKHAVERESET;
			if (rvdbg_dmi_write(dmi, DMI_REG_DMCONTROL, dmcontrol) < 0)
				return -1;
		}

		if (rvdbg_discover_hart(dmi) < 0)
			return -1;

		dmi->num_harts++;
	}

	DEBUG_INFO("num_harts = %d\n", dmi->num_harts);

	// Select hart0 as current
	dmcontrol = DMCONTROL_DMACTIVE | DMCONTROL_MK_HARTSEL(0);
	if (rvdbg_dmi_write(dmi, DMI_REG_DMCONTROL, dmcontrol) < 0)
		return -1;
	dmi->current_hart = 0;

	return 0;
}

/**
 * Returns negative error, or positive cmderror
 */
static int rvdbg_abstract_command_run(RVDBGv013_DMI_t *dmi, uint32_t command)
{
	uint32_t abstractcs;
	uint8_t cmderror;

retry:
	if (rvdbg_dmi_write(dmi, DMI_REG_ABSTRACT_CMD, command) < 0)
		return -1;

	// Wait until the abstract command finished
	do {
		if (rvdbg_dmi_read(dmi, DMI_REG_ABSTRACT_CS, &abstractcs) < 0)
			return -1;
	} while (ABSTRACTCS_GET_BUSY(abstractcs));

	cmderror = ABSTRACTCS_GET_CMDERR(abstractcs);

	if (cmderror != ABSTRACTCMD_ERR_NONE) {
		// Clear the error
		abstractcs = 0;
		ABSTRACTCS_CLEAR_CMDERR(abstractcs);
		if (rvdbg_dmi_write(dmi, DMI_REG_ABSTRACT_CS, abstractcs) < 0)
			return -1;

		// Handle ERR_BUSY retries automatically
		if (cmderror == ABSTRACTCMD_ERR_BUSY) {
			DEBUG_WARN("RISC-V abstract command busy, retry...\n");
			goto retry;
		} else if (cmderror == ABSTRACTCMD_ERR_HALT_RESUME) {
			DEBUG_WARN("RISC-V abstract command 0x%08x not supported in run/halt state\n", command);
		}
	}

	return cmderror;
}

static int rvdbg_read_single_reg(RVDBGv013_DMI_t *dmi, uint16_t reg_idx, uint32_t *out,
	enum AUTOEXEC_STATE astate)
{
	uint32_t command = 0;
	uint32_t abstractcs;
	int ret;

	// Construct abstract command
	// TODO: Do not expect XLEN of 32 by default
	ABSTRACTCMD_SET_TYPE(command, ABSTRACTCMD_TYPE_ACCESS_REGISTER);
	ABSTRACTCMD_ACCESS_REGISTER_SET_AARSIZE(command, BUS_ACCESS_32);
	ABSTRACTCMD_ACCESS_REGISTER_SET_TRANSFER(command, 1);
	ABSTRACTCMD_ACCESS_REGISTER_SET_REGNO(command, reg_idx);
	ABSTRACTCMD_ACCESS_REGISTER_SET_AARPOSTINCREMENT(command,
		astate == AUTOEXEC_STATE_INIT ? 1 : 0);

	// Avoid wrinting command, when in autoexec cont mode
	if (astate != AUTOEXEC_STATE_CONT) {
		// Initiate register read command
		if ((ret = rvdbg_abstract_command_run(dmi, command)) < 0)
			return -1;

		// Handle error
		switch (ret) {
			case ABSTRACTCMD_ERR_NONE:
				break;
			case ABSTRACTCMD_ERR_EXCEPTION:
				// TODO: This check becomes invalid as soon as postexec is set.
				DEBUG_WARN("RISC-V register 0x%"PRIx16"\n does not exist", reg_idx);
				return -1;
			default:
				DEBUG_WARN("RISC-V abstract command error: %d\n", ret);
				return -1;
		}
	}

	if (rvdbg_dmi_read(dmi, DMI_REG_ABSTRACTDATA_BEGIN, out) < 0)
		return -1;

	if (astate == AUTOEXEC_STATE_CONT) {
		// In cont mode, only read when not busy (not guarded by rvdbg_abstract_command_run)
		do {
			if (rvdbg_dmi_read(dmi, DMI_REG_ABSTRACT_CS, &abstractcs) < 0)
				return -1;
		} while (ABSTRACTCS_GET_BUSY(abstractcs));
	}

	return 0;
}

static int rvdbg_write_single_reg(RVDBGv013_DMI_t *dmi, uint16_t reg_id, uint32_t value,
	enum AUTOEXEC_STATE astate)
{
	uint32_t command = 0;
	uint32_t abstractcs;
	int ret;

	// Write value to data0
	if (rvdbg_dmi_write(dmi, DMI_REG_ABSTRACTDATA_BEGIN, value) < 0)
		return -1;

	// Construct abstract command
	// TODO: Do not expect XLEN of 32 by default
	ABSTRACTCMD_SET_TYPE(command, ABSTRACTCMD_TYPE_ACCESS_REGISTER);
	ABSTRACTCMD_ACCESS_REGISTER_SET_AARSIZE(command, BUS_ACCESS_32);
	ABSTRACTCMD_ACCESS_REGISTER_SET_TRANSFER(command, 1);
	ABSTRACTCMD_ACCESS_REGISTER_SET_WRITE(command, 1);
	ABSTRACTCMD_ACCESS_REGISTER_SET_REGNO(command, reg_id);
	ABSTRACTCMD_ACCESS_REGISTER_SET_AARPOSTINCREMENT(command,
		astate == AUTOEXEC_STATE_INIT ? 1 : 0);

	// Only initiate the write, if not in autoexec cont state
	if (astate != AUTOEXEC_STATE_CONT) {
		// Initiate register write command
		if ((ret = rvdbg_abstract_command_run(dmi, command)) < 0)
			return -1;

		// Handle error
		switch (ret) {
			case ABSTRACTCMD_ERR_NONE:
				break;
			case ABSTRACTCMD_ERR_EXCEPTION:
				// TODO: This check becomes invalid as soon as postexec is set.
				DEBUG_WARN("RISC-V register 0x%"PRIx16"\n does not exist", reg_id);
				return -1;
			default:
				DEBUG_WARN("RISC-V abstract command error: %d\n", ret);
				return -1;
		}
	} else {
		// When in cont state, make sure to wait until write is done
		do {
			if (rvdbg_dmi_read(dmi, DMI_REG_ABSTRACT_CS, &abstractcs) < 0)
				return -1;
		} while (ABSTRACTCS_GET_BUSY(abstractcs));
	}


	return 0;
}

static int rvdbg_write_regs(RVDBGv013_DMI_t *dmi, uint16_t reg_id, const uint32_t *values,
	uint16_t len)
{
	enum AUTOEXEC_STATE astate = AUTOEXEC_STATE_NONE;
	uint32_t abstractauto;
	uint16_t i;
	int err = 0;

	// When more than one reg written and autoexec support
	if (len > 1  && dmi->support_autoexecdata) {
		astate = AUTOEXEC_STATE_INIT;
		abstractauto = 0;
		ABSTRACTAUTO_SET_DATA(abstractauto, ABSTRACTAUTO_SOME_PATTEN);
		if (rvdbg_dmi_write(dmi, DMI_REG_ABSTRACT_AUTOEXEC, abstractauto) < 0)
			return -1;
	}

	for (i = 0; i < len; i++) {
		if (rvdbg_write_single_reg(dmi, reg_id + i, values[i], astate) < 0) {
			err = -1;
			break;
		}
		if (astate == AUTOEXEC_STATE_INIT)
			astate = AUTOEXEC_STATE_CONT;
	}

	// Reset auto exec state
	if (astate != AUTOEXEC_STATE_NONE) {
		abstractauto = 0;
		ABSTRACTAUTO_SET_DATA(abstractauto, 0);
		if (rvdbg_dmi_write(dmi, DMI_REG_ABSTRACT_AUTOEXEC, abstractauto) < 0)
			return -1;
	}

	return err;
}

static int rvdbg_read_regs(RVDBGv013_DMI_t *dmi, uint16_t reg_id, uint32_t *values,
	uint16_t len)
{
	enum AUTOEXEC_STATE astate = AUTOEXEC_STATE_NONE;
	uint32_t abstractauto;
	uint16_t i;
	int err = 0;

	// When more than one reg read and autoexec support
	if (len > 1  && dmi->support_autoexecdata) {
		astate = AUTOEXEC_STATE_INIT;
		abstractauto = 0;
		ABSTRACTAUTO_SET_DATA(abstractauto, ABSTRACTAUTO_SOME_PATTEN);
		if (rvdbg_dmi_write(dmi, DMI_REG_ABSTRACT_AUTOEXEC, abstractauto) < 0)
			return -1;
	}

	for (i = 0; i < len; i++) {
		if (rvdbg_read_single_reg(dmi, reg_id + i, &values[i], astate) < 0) {
			err = -1;
			break;
		}
		if (astate == AUTOEXEC_STATE_INIT)
			astate = AUTOEXEC_STATE_CONT;
	}

	// Reset auto exec state
	if (astate != AUTOEXEC_STATE_NONE) {
		abstractauto = 0;
		ABSTRACTAUTO_SET_DATA(abstractauto, 0);
		if (rvdbg_dmi_write(dmi, DMI_REG_ABSTRACT_AUTOEXEC, abstractauto) < 0)
			return -1;
	}

	return err;
}

static int rvdbg_progbuf_upload(RVDBGv013_DMI_t *dmi, const uint32_t* buffer, uint8_t buffer_len)
{
	uint8_t i;
	uint8_t total_avail_size = dmi->progbuf_size - (dmi->impebreak ? 0 : 1);

	if (buffer_len > total_avail_size) {
		DEBUG_WARN("RISC-V: progbuf upload size %d too big\n", buffer_len);
		return -1;
	}

	for (i = DMI_REG_PROGRAMBUF_BEGIN; i < buffer_len; i++) {
		if (rvdbg_dmi_write(dmi, DMI_REG_PROGRAMBUF_BEGIN + i, buffer[i]) < 0)
			return -1;
	}

	// Add ebreak, if there is extra space.
	if (i < total_avail_size) {
		if (rvdbg_dmi_write(dmi, DMI_REG_PROGRAMBUF_BEGIN + i, RV32I_ISA_EBREAK) < 0)
			return -1;
	}

	return 0;
}

// TODO: Backup and restore registers externally for performance opt
static int rvdbg_progbuf_exec(RVDBGv013_DMI_t *dmi, uint32_t *args, uint8_t argin_len,
	uint8_t argout_len)
{
	int ret;
	uint8_t backup_len;
	uint32_t command = 0;
	// Back up registers for progbuf communication (excludes x0)
	// TODO: Do not assume XLEN 32
	uint32_t gp_register_backup[31];
	ABSTRACTCMD_SET_TYPE(command, ABSTRACTCMD_TYPE_ACCESS_REGISTER);
	ABSTRACTCMD_ACCESS_REGISTER_SET_POSTEXEC(command, 1);

	// How many registers have to be backed up?
	backup_len = MAX(argin_len, argout_len);

	if (backup_len > 31) {
		DEBUG_WARN("RISC-V: Too many requested argument registers\n");
		return -1;
	}

	// Backup argument registers
	if (rvdbg_read_regs(dmi, HART_REG_GPR_BEGIN + 1, gp_register_backup,
			backup_len) < 0)
		return -1;

	// Write all in arguments to GPRs
	if (rvdbg_write_regs(dmi, HART_REG_GPR_BEGIN + 1, args, argin_len) < 0)
		return -1;

	// Start command
	if ((ret = rvdbg_abstract_command_run(dmi, command)) < 0)
		return -1;

	// Handle cmderror
	switch (ret) {
		case ABSTRACTCMD_ERR_EXCEPTION:
			DEBUG_WARN("RISC-V: Exception in progbuf execution\n");
			return -1;
		default:
			DEBUG_WARN("RISC-V: Failed to execute progbuf, error %d\n", ret);
			return -1;
	}

	// Copy result
	if (rvdbg_read_regs(dmi, HART_REG_GPR_BEGIN + 1, args, argout_len) < 0)
		return -1;

	// Restore backup regs
	if (rvdbg_write_regs(dmi, HART_REG_GPR_BEGIN + 1,
			gp_register_backup,
			backup_len) < 0)
		return -1;

	return 0;
}

static int rvdbg_read_csr_progbuf(RVDBGv013_DMI_t *dmi, uint16_t reg_id, uint32_t* value)
{
	// Store result in x1
	uint32_t program[] = {
		RV32I_ISA_CSRRS(1, reg_id, 0)
	};

	if (rvdbg_progbuf_upload(dmi, program, ARRAY_NUMELEM(program)) < 0)
		return -1;

	// exec with 0 in registers and 1 out register, this reserves x1 as an output register
	if (rvdbg_progbuf_exec(dmi, value, 0, 1) < 0)
		return -1;

	return 0;
}

// static int rvdbg_write_csr_progbuf(RVDBGv013_DMI_t *dmi, uint16_t reg_id, uint32_t value) { }

static int rvdbg_read_mem_progbuf(RVDBGv013_DMI_t *dmi, uint32_t address, uint32_t len, uint8_t* value)
{
	// Select optimal transfer size
	enum BUS_ACCESS width;
	uint32_t width_bytes;
	uint32_t i;
	uint32_t args[2];

	if (address % 4 == 0 && len > 4) {
		width = BUS_ACCESS_32;
		width_bytes = 4;
	} else if (address % 2 == 0 && len > 2) {
		width = BUS_ACCESS_16;
		width_bytes = 2;
	} else {
		width = BUS_ACCESS_8;
		width_bytes = 1;
	}

	// Load instruction with zero extend, x1 is target for data,
	// x2 is load address.
	uint32_t program[] = {
		RV32I_ISA_LOAD(1, width, RV32I_ISA_LOAD_ZERO_EXTEND, 2, 0),
	};

	if (rvdbg_progbuf_upload(dmi, program, ARRAY_NUMELEM(program)) < 0)
		return -1;

	// Go over memory addresses in width steps, copy from x1
	// result to value.
	for (i = 0; i < len; i += width_bytes) {
		// Set x2
		args[1] = address + i;
		if (rvdbg_progbuf_exec(dmi, args, 1, 2) < 0)
			return -1;
		memcpy(value + i, &args[0], width_bytes);
	}

	// If i is not exactly len some spare bytes are left,
	// call function recursively with remainder.
	if (i != len) {
		i -= width_bytes;
		return rvdbg_read_mem_progbuf(dmi, address + i, len - i, value + i);
	}

	return 0;
}

// static void rvdbg_write_mem_progbuf(RVDBGv013_DMI_t *dmi, uint32_t address, uint32_t len, const uint8_t *value); { }


static int rvdbg_select_mem_and_csr_access_impl(RVDBGv013_DMI_t *dmi)
{
	uint32_t abstractcs, abstractauto;
	uint8_t total_avail_progbuf;

	if (rvdbg_dmi_read(dmi, DMI_REG_ABSTRACT_CS, &abstractcs) < 0)
		return -1;

	dmi->progbuf_size = ABSTRACTCS_GET_PROGBUFSIZE(abstractcs);
	dmi->abstract_data_count = ABSTRACTCS_GET_DATACOUNT(abstractcs);

	if (dmi->abstract_data_count < 1 || dmi->abstract_data_count > 12) {
		// Invalid count of abstract data
		DEBUG_WARN("RISC-V: Invalid count of abstract data: %d\n", dmi->abstract_data_count);
		return -1;
	}

	if (dmi->progbuf_size > 16) {
		// Invalid progbuf size
		DEBUG_WARN("RISC-V: progbufsize is too large: %d\n", dmi->progbuf_size);
		return -1;
	} else if (dmi->progbuf_size == 1 && !dmi->impebreak) {
		// When progbufsize is 1, impebreak is required.
		DEBUG_WARN("RISC-V: progbufsize 1 requires impebreak feature\n");
		return -1;
	}

	// DEBUG("datacount = %d\n", dmi->abstract_data_count);

	// Check if a program buffer is supported, and it is sufficient for accessing
	// CSR and / or MEMORY.
	// At minimum one available instruction required for csr and mem access over
	// progbuf.
	// --------------------------------------------------------------------------
	total_avail_progbuf = dmi->progbuf_size - (dmi->impebreak ? 0 : 1);
	if (total_avail_progbuf >= 1) {
		// PROGBUF supported
		DEBUG_INFO("RISC-V: Program buffer with available size %d supported.\n", total_avail_progbuf);

		dmi->read_csr = rvdbg_read_csr_progbuf;
		// dmi->write_csr = rvdbg_write_csr_progbuf;
		dmi->read_mem = rvdbg_read_mem_progbuf;
		// dmi->write_mem = rvdbg_write_mem_progbuf;
	}

	// Check if autoexecdata feature can be used
	// -----------------------------------------
	abstractauto = 0;
	ABSTRACTAUTO_SET_DATA(abstractauto, ABSTRACTAUTO_SOME_PATTEN);
	if (rvdbg_dmi_write(dmi, DMI_REG_ABSTRACT_AUTOEXEC, abstractauto) < 0)
		return -1;
	if (rvdbg_dmi_read(dmi, DMI_REG_ABSTRACT_AUTOEXEC, &abstractauto) < 0)
		return -1;

	if (ABSTRACTAUTO_GET_DATA(abstractauto) == ABSTRACTAUTO_SOME_PATTEN) {
		DEBUG_INFO("RISC-V: autoexecdata feature supported\n");
		dmi->support_autoexecdata = true;

		ABSTRACTAUTO_SET_DATA(abstractauto, 0);
		if (rvdbg_dmi_write(dmi, DMI_REG_ABSTRACT_AUTOEXEC, abstractauto) < 0)
			return -1;
	}

	return 0;
}

static bool rvdbg_attach(target *t) {
	RVDBGv013_DMI_t *dmi = t->priv;

	DEBUG_TARGET("Attach\n");
	// Activate the debug module
	if (rvdbg_dmi_write(dmi, DMI_REG_DMCONTROL, DMCONTROL_DMACTIVE | DMCONTROL_MK_HARTSEL(dmi->current_hart)) < 0) {
		dmi->error = true;
		return false;
	}

	// TODO: Implement
	return true;
}

static void rvdbg_detach(target *t) {
	RVDBGv013_DMI_t *dmi = t->priv;

	// Deactivate the debug module
	if (rvdbg_dmi_write(dmi, DMI_REG_DMCONTROL, 0) < 0)
		dmi->error = true;
}

static bool rvdbg_check_error(target *t) {
	RVDBGv013_DMI_t *dmi = t->priv;

	return dmi->error;
}

int rvdbg_dmi_init(RVDBGv013_DMI_t *dmi)
{
	uint8_t version;
	uint32_t dmstatus, nextdmaddr, dmcontrol;
	target *t;

    DEBUG_INFO("  debug version = %s\n  abits = %d\n  idle = ",
		rvdbg_version_tostr(dmi->debug_version), dmi->abits);

	dmi->error = false;

	switch (dmi->idle) {
		case 0:
			DEBUG_INFO("no run/test state\n");
			break;
		case 1:
			DEBUG_INFO("leave run/test immediately\n");
			break;
		default:
			DEBUG_INFO("stay %d cycles in run/test\n", dmi->idle - 1);
			break;
	}

	dmi->rvdbg_dmi_reset(dmi, false);

	// Read dmcontrol and store for reference
	if (rvdbg_dmi_read(dmi, DMI_REG_DMCONTROL, &dmcontrol) < 0)
		return -1;
	DEBUG_INFO("dmactive = %d\n", !!(dmcontrol & DMCONTROL_DMACTIVE));

	// Activate when not already activated
	if (!(dmcontrol & DMCONTROL_DMACTIVE)) {
		DEBUG_INFO("RISC-V: dmactive disabled, enabling...\n");
		dmcontrol |= DMCONTROL_DMACTIVE;
		if (rvdbg_dmi_write(dmi, DMI_REG_DMCONTROL, dmcontrol) < 0)
			return -1;
	}

	if (rvdbg_dmi_read(dmi, DMI_REG_DMSTATUS, &dmstatus) < 0)
		return -1;

	DEBUG_INFO("dmstatus = 0x%08x\n", dmstatus);

	version = DMSTATUS_GET_VERSION(dmstatus);
	if (version == 0) {
		DEBUG_WARN("No debug module present\n");
	} else if ((uint8_t)(version - 1) != dmi->debug_version) {
		DEBUG_WARN("dtmcs and dmstatus debug version mismatch\n");
		// Trust the dmstatus register. Ignore error, and leave
		// previous version active
		// ----------------------------------------------------
		if (version != (uint8_t)RISCV_DEBUG_VERSION_UNKNOWN)
			rvdbg_set_debug_version(dmi, version - 1);
	}

	// TODO: Implement authentification plugins
	if (!DMSTATUS_GET_AUTHENTICATED(dmstatus)) {
		// Not authentificated -> not supported
		DEBUG_WARN("RISC-V DM requires authentification!\n");
		return -1;
	}

	if (DMSTATUS_GET_CONFSTRPTRVALID(dmstatus)) {
		DEBUG_INFO("RISC-V configuration string available\n");
	}

	dmi->support_resethaltreq = DMSTATUS_GET_HASRESETHALTREQ(dmstatus);
	if (dmi->support_resethaltreq) {
		DEBUG_INFO("Supports set/clear-resethaltreq\n");
	}

	if (rvdbg_dmi_read(dmi, DMI_REG_NEXTDM_ADDR, &nextdmaddr) < 0)
		return -1;
	if (nextdmaddr) {
		// Multiple DM per DMI not yet supported
		DEBUG_WARN("Warning: Detected multiple RISC-V debug modules, only one supported!\n");
	}

	// Get impebreak before selecting mem and csr access impl
	dmi->impebreak = DMSTATUS_GET_IMPEBREAK(dmstatus);

	if (rvdbg_select_mem_and_csr_access_impl(dmi) < 0) {
		DEBUG_WARN("RISC-V: no compatible MEM / CSR access implementation detected.\n");
		return -1;
	}

    // Discover harts, add targets
	if (rvdbg_discover_harts(dmi) < 0)
		return -1;

	// Disable the debug module
	if (rvdbg_dmi_write(dmi, DMI_REG_DMCONTROL, 0) < 0)
		return -1;

	t = target_new();

	rvdbd_dmi_ref(dmi);

	t->priv = dmi;
	t->priv_free = (void (*)(void *))rvdbd_dmi_unref;
	t->driver = dmi->descr;
	t->core = "Generic RVDBG 0.13";

	t->attach = rvdbg_attach;
	t->detach = rvdbg_detach;
	t->check_error = rvdbg_check_error;

	return 0;
}
