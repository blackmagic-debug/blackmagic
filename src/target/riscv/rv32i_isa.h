#ifndef __RV32I_ISA_H
#define __RV32I_ISA_H

/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2019  Black Sphere Technologies Ltd.
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
 + This file defines some macros, to encode / decode RV32I ISA instructions.
 */

#define RV_ISA_R_TYPE(funct7, rs2, rs1, funct3, rd, opcode) (\
	(opcode & 0x3f) \
	| ((rd & 0x1f) << 7) \
	| ((funct3 & 0x7) << 12) \
	| ((rs1 & 0x1f) << 15) \
	| ((rs2 & 0x1f) << 20) \
	| ((funct7 & 0x7f) << 25) \
    )
#define RV_ISA_I_TYPE(imm11_0, rs1, funct3, rd, opcode) (\
	(opcode & 0x3f) \
	| ((rd & 0x1f) << 7) \
	| (((funct3) & 0x7) << 12) \
	| ((rs1 & 0x1f) << 15) \
	| ((imm11_0 & 0x7ff) << 20) \
    )
#define RV_ISA_S_TYPE(imm11_0, rs2, rs1, funct3, opcode) (\
	(opcode & 0x3f) \
	| ((imm11_0 & 0x1f) << 7) \
	| ((funct3 & 0x7) << 12) \
	| ((rs1 & 0x1f) << 15) \
	| ((rs2 & 0x1f) << 20) \
	| (((imm11_0 >> 5) & 0x7f) << 25) \
    )
#define RV_ISA_B_TYPE(imm12_1, rs2, rs1, funct3, opcode) (\
	(opcode & 0x3f) \
	| (((imm12_1 >> 10) & 0x1) << 7) \
	| ((imm12_1 & 0xf) << 8) \
	| ((funct3 & 0x7) << 12) \
	| ((rs1 & 0x1f) << 15) \
	| ((rs2 & 0x1f) << 20) \
	| (((imm12_1 >> 4) & 0x3f) << 25) \
	| (((imm12_1 >> 11) & 0x1) << 31) \
    )
#define RV_ISA_U_TYPE(imm31_12, rd, opcode) (\
	(opcode & 0x3f) \
	| ((rd & 0x1f) << 7) \
	| ((imm31_12 & 0xfffff) << 12) \
    )
#define RV_ISA_J_TYPE(imm20_1, rd, opcode) (\
	(opcode & 0x3f) \
	| ((rd & 0x1f) << 7) \
	| (((imm20_1 >> 11) & 0xff) << 12) \
	| (((imm20_1 >> 10) & 0x1) << 20) \
	| ((imm20_1 & 0x3ff) << 21) \
	| (((imm20_1 >> 19) & 0x1) << 31) \
    )

#define RV32I_ISA_OP_SYSTEM (0x73)
#define RV32I_ISA_OP_LOAD   (0x03)
#define RV32I_ISA_OP_STORE  (0x23)

#define RV32I_ISA_LOAD_ZERO_EXTEND (0x4)

// Used for reading / writing memory
#define RV32I_ISA_LOAD(rd, width, zextend, base, offset) \
    RV_ISA_I_TYPE(offset, base, (width) | (zextend), rd, RV32I_ISA_OP_LOAD)
#define RV32I_ISA_STORE(value, width, base, offset) \
    RV_ISA_S_TYPE(offset, value, base, width, RV32I_ISA_OP_STORE)

// Used for reading a CSR
#define RV32I_ISA_CSRRS(dst, csr, src) \
    RV_ISA_I_TYPE(csr, src, 0x2, dst, RV32I_ISA_OP_SYSTEM)

// ebreak is used to jump from program buffer back to normal
// debug mode.
#define RV32I_ISA_EBREAK RV_ISA_S_TYPE(0x1, 0, 0, 0, RV32I_ISA_OP_SYSTEM)

#endif /* __RV32I_ISA_H */