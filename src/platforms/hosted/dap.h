/*
 * Copyright (c) 2013-2015, Alex Taradov <alex@taradov.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef PLATFORMS_HOSTED_DAP_H
#define PLATFORMS_HOSTED_DAP_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "adiv5.h"

typedef enum dap_info {
	DAP_INFO_VENDOR = 0x01U,
	DAP_INFO_PRODUCT = 0x02U,
	DAP_INFO_SER_NUM = 0x03U,
	DAP_INFO_FW_VER = 0x04U,
	DAP_INFO_DEVICE_VENDOR = 0x05U,
	DAP_INFO_DEVICE_NAME = 0x06U,
	DAP_INFO_CAPABILITIES = 0xf0U,
	DAP_INFO_TDT = 0xf1U,
	DAP_INFO_SWO_BUF_SIZE = 0xfdU,
	DAP_INFO_PACKET_COUNT = 0xfeU,
	DAP_INFO_PACKET_SIZE = 0xffU,
} dap_info_e;

typedef enum dap_cap {
	DAP_CAP_SWD = (1U << 0U),
	DAP_CAP_JTAG = (1U << 1U),
	DAP_CAP_SWO_UART = (1U << 2U),
	DAP_CAP_SWO_MANCHESTER = (1U << 3U),
	DAP_CAP_ATOMIC_CMD = (1U << 4U),
	DAP_CAP_TDT = (1U << 5U),
	DAP_CAP_SWO_STREAMING = (1U << 6U),
} dap_cap_e;

void dap_led(int index, int state);
void dap_connect(bool jtag);
void dap_disconnect(void);
void dap_transfer_configure(uint8_t idle, uint16_t count, uint16_t retry);
void dap_swd_configure(uint8_t cfg);
size_t dap_info(dap_info_e info, uint8_t *data, size_t size);
void dap_reset_target(void);
void dap_nrst_set_val(bool assert);
void dap_trst_reset(void);
void dap_reset_target_hw(int state);
void dap_reset_pin(int state);
void dap_line_reset(void);
uint32_t dap_read_reg(adiv5_debug_port_s *dp, uint8_t reg);
void dap_write_reg(adiv5_debug_port_s *dp, uint8_t reg, uint32_t data);
void dap_reset_link(bool jtag);
uint32_t dap_read_idcode(adiv5_debug_port_s *dp);
bool dap_read_block(adiv5_access_port_s *ap, void *dest, uint32_t src, size_t len, align_e align);
bool dap_write_block(adiv5_access_port_s *ap, uint32_t dest, const void *src, size_t len, align_e align);
void dap_ap_mem_access_setup(adiv5_access_port_s *ap, uint32_t addr, align_e align);
uint32_t dap_ap_read(adiv5_access_port_s *ap, uint16_t addr);
void dap_ap_write(adiv5_access_port_s *ap, uint16_t addr, uint32_t value);
void dap_read_single(adiv5_access_port_s *ap, void *dest, uint32_t src, align_e align);
void dap_write_single(adiv5_access_port_s *ap, uint32_t dest, const void *src, align_e align);
ssize_t dbg_dap_cmd(uint8_t *data, size_t response_length, size_t request_length);
bool dap_run_cmd(const void *request_data, size_t request_length, void *response_data, size_t response_length);
void dap_jtagtap_tdi_tdo_seq(
	uint8_t *data_out, bool final_tms, const uint8_t *tms, const uint8_t *data_in, size_t clock_cycles);
int dap_jtag_configure(void);

uint32_t dap_swdptap_seq_in(size_t clock_cycles);
bool dap_swdptap_seq_in_parity(uint32_t *result, size_t clock_cycles);
void dap_swdptap_seq_out(uint32_t tms_states, size_t clock_cycles);
void dap_swdptap_seq_out_parity(uint32_t tms_states, size_t clock_cycles);

#endif /* PLATFORMS_HOSTED_DAP_H */
