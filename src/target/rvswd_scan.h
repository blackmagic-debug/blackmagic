/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2023 1BitSquared <info@1bitsquared.com>
 * Written by Rafael Silva <perigoso@riseup.net>
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
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef TARGET_RVSWD_SCAN_H
#define TARGET_RVSWD_SCAN_H

// #include <stddef.h>
#include "rvswd.h"

// #define JTAG_MAX_DEVS   32U
// #define JTAG_MAX_IR_LEN 32U /* NOTE: This is not long enough for all Xilinx devices */

// typedef struct jtag_dev {
// 	uint32_t jd_idcode;
// 	uint32_t current_ir;

// 	/* The DR prescan doubles as the device index */
// 	uint8_t dr_prescan;
// 	uint8_t dr_postscan;

// 	uint8_t ir_len;
// 	uint8_t ir_prescan;
// 	uint8_t ir_postscan;
// } jtag_dev_s;

// extern jtag_dev_s jtag_devs[JTAG_MAX_DEVS];
// extern uint32_t jtag_dev_count;
// extern const uint8_t ones[8];

// void jtag_dev_write_ir(uint8_t jd_index, uint32_t ir);
// void jtag_dev_shift_dr(uint8_t jd_index, uint8_t *dout, const uint8_t *din, size_t ticks);
// void jtag_add_device(uint32_t dev_index, const jtag_dev_s *jtag_dev);

#endif /* TARGET_RVSWD_SCAN_H */
