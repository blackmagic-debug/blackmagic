#ifndef __RV32I_ISA_H
#define __RV32I_ISA_H

/*
 * This file is part of the Black Magic Debug project.
 *
 * MIT License
 *
 * Copyright (c) 2019 Roland Ruckerbauer <roland.rucky@gmail.com>
 * based on similar work by Gareth McMullin <gareth@blacksphere.co.nz>
 * Copyright (c) 2020-21 Uwe Bonnes <bon@elektron.ikp.physik.tu-darmstadt.de>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
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
