/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2026 1BitSquared <info@1bitsquared.com>
 * Written by Aki Van Ness <aki@lethalbit.net>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLEs
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef TARGET_LATTICE_COMMON_H
#define TARGET_LATTICE_COMMON_H

#include <stdint.h>

// Read out the 32-bit IDCODE of the device
#define CMD_READ_ID 0xe0U
// Read 32-bit user code
#define CMD_USERCODE 0xc0U
// Read out internal status
#define CMD_LSC_READ_STATUS 0x3cU
// Read 1 bit busy flag to check the command execution status
#define CMD_LSC_CHECK_BSY 0xf0U
// Equivalent to toggle PROGRAMN pin
#define CMD_LSC_REFRESH 0x79U
// Enable the Offline configuration mode
#define CMD_ISC_ENABLE 0xc6U
// Enable the Transparent configuration mode
#define CMD_ISC_ENABLE_X 0x74U
// Disable the configuration operation
#define CMD_ISC_DISABLE 0x26U
// Write the 32-bit new USERCODE data to USERCODE register
#define CMD_ISC_PROGRAM_USERCODE 0xc2U
// Bulk erase the memory array base on the access mode and array selection
#define CMD_ISC_ERASE 0x0eU
// Program the DONE bit if the device is in Configuration state.
#define CMD_ISC_PROGRAM_DONE 0x5eU
// Program the Security bit if the device is in Configuration state.
#define CMD_ISC_PROGRAM_SECURITY 0xceU
// Initialize the Address Shift Register
#define CMD_LSC_INIT_ADDRESS 0x46U
// Write the 16 bit Address Register to move the address quickly
#define CMD_LSC_WRITE_ADDRESS 0xb4U
// Program the device the whole bitstream sent in as the command operand
#define CMD_LSC_BITSTREAM_BURST 0x7aU
/*
 * Write configuration data to the configuration memory frame at current address
 * and post increment the address.
 *
 * Byte 2~0 of the opcode indicate number of the frames included in the operand field.
 */
#define CMD_LSC_PROG_INCR_RTI 0x82U
// Encrypt the configuration data then write
#define CMD_LSC_PROG_INCR_ENC 0xb6U
// Decompress the configuration data, then write
#define CMD_LSC_PROG_INCR_CMP 0xb8U
// Decompress and Encrypt the configuration data, then write
#define CMD_LSC_PROG_INCR_CNE 0xbaU
// Read back the configuration memory frame selected by the address register and post increment the address
#define CMD_LSC_VERIFY_INCR_RTI 0x6aU
// Modify the Control Register 0
#define CMD_LSC_PROG_CTRL0 0x22U
// Read the Control Register 0
#define CMD_LSC_READ_CTRL0 0x20U
// Reset 16-bit frame CRC register to 0x0000
#define CMD_LSC_RESET_CRC 0x3bU
// Read 16-bit frame CRC register content
#define CMD_LSC_READ_CRC 0x60U
// Program the calculated 32-bit CRC based on configuration bit values only into overall CRC register
#define CMD_LSC_PROG_SED_CRC 0xa2U
// Read the 32-bit SED CRC
#define CMD_LSC_READ_SED_CRC 0xa4U
// Program 64-bit password into the non-volatile memory (Efuse)
#define CMD_LSC_PROG_PASSWORD 0xf1U
// Read out the 64-bit password before activated for verification
#define CMD_LSC_READ_PASSWORD 0xf2U
// Shift in the password to unlock for re-configuration (necessary when password protection feature is active).
#define CMD_LSC_SHIFT_PASSWORD 0xbcU
// Program the 128-bit cipher key into Efuse
#define CMD_LSC_PROG_CIPHER_KEY 0xf3U
// Read out the 128-bit cipher key before activated for verification
#define CMD_LSC_READ_CIPHER_KEY 0xf4U
// Program User Feature, such as Customer ID, I2C Slave Address, Unique ID Header
#define CMD_LSC_PROG_FEATURE 0xe4U
// Read User Feature, such as Customer ID, I2C Slave Address, Unique ID Header
#define CMD_LSC_READ_FEATURE 0xe7U
// Program User Feature Bits, such as CFG port and pin persistence, PWD_EN, PWD_ALL, DEC_ONLY, Feature Row Lock etc.
#define CMD_LSC_PROG_FEABITS 0xf8U
// Read User Feature Bits, such as CFH port and pin persistence, PWD_EN, PWD_ALL, DEC_ONLY, Feature Row Lock etc.
#define CMD_LSC_READ_FEABITS 0xfbU
// Program OTP bits, to set Memory Sectors One Time Programmable
#define CMD_LSC_PROG_OTP 0xf9U
// Read OTP bits setting
#define CMD_LSC_READ_OTP 0xfaU
// Enable SPI Programming via JTAG
#define CMD_LSC_BACKGROUND_SPI 0x3aU

#endif /* TARGET_LATTICE_COMMON_H */
