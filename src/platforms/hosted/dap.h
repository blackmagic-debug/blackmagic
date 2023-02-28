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
	DAP_INFO_CMSIS_DAP_VERSION = 0x04U,
	DAP_INFO_DEVICE_VENDOR = 0x05U,
	DAP_INFO_DEVICE_NAME = 0x06U,
	DAP_INFO_ADAPTOR_VERSION = 0x09U,
	DAP_INFO_CAPABILITIES = 0xf0U,
	DAP_INFO_TDT = 0xf1U,
	DAP_INFO_SWO_BUF_SIZE = 0xfdU,
	DAP_INFO_PACKET_COUNT = 0xfeU,
	DAP_INFO_PACKET_SIZE = 0xffU,
} dap_info_e;

typedef enum dap_cap {
	DAP_CAP_SWD = (1U << 0U),
	DAP_CAP_JTAG = (1U << 1U),
	DAP_CAP_SWO_ASYNC = (1U << 2U),
	DAP_CAP_SWO_MANCHESTER = (1U << 3U),
	DAP_CAP_ATOMIC_CMDS = (1U << 4U),
	DAP_CAP_TDT = (1U << 5U),
	DAP_CAP_SWO_STREAMING = (1U << 6U),
} dap_cap_e;

typedef enum dap_led_type {
	DAP_LED_CONNECT = 0U,
	DAP_LED_RUNNING = 1U,
} dap_led_type_e;

#define DAP_QUIRK_NO_JTAG_MUTLI_TAP (1U << 0U)

extern uint8_t dap_caps;
extern dap_cap_e dap_mode;
extern uint8_t dap_quirks;
extern bool dap_has_swd_sequence;

bool dap_connect(void);
bool dap_disconnect(void);
bool dap_led(dap_led_type_e type, bool state);
size_t dap_info(dap_info_e requested_info, void *buffer, size_t buffer_length);
void dap_reset_target(void);
void dap_nrst_set_val(bool assert);
void dap_reset_target_hw(int state);
bool dap_set_reset_state(bool nrst_state);
uint32_t dap_read_reg(adiv5_debug_port_s *target_dp, uint8_t reg);
void dap_write_reg(adiv5_debug_port_s *target_dp, uint8_t reg, uint32_t data);
void dap_reset_link(adiv5_debug_port_s *target_dp);
bool dap_read_block(adiv5_access_port_s *target_ap, void *dest, uint32_t src, size_t len, align_e align);
bool dap_write_block(adiv5_access_port_s *target_ap, uint32_t dest, const void *src, size_t len, align_e align);
void dap_ap_mem_access_setup(adiv5_access_port_s *target_ap, uint32_t addr, align_e align);
uint32_t dap_ap_read(adiv5_access_port_s *target_ap, uint16_t addr);
void dap_ap_write(adiv5_access_port_s *target_ap, uint16_t addr, uint32_t value);
void dap_read_single(adiv5_access_port_s *target_ap, void *dest, uint32_t src, align_e align);
void dap_write_single(adiv5_access_port_s *target_ap, uint32_t dest, const void *src, align_e align);
bool dap_run_cmd(const void *request_data, size_t request_length, void *response_data, size_t response_length);
bool dap_jtag_configure(void);

void dap_dp_abort(adiv5_debug_port_s *target_dp, uint32_t abort);
uint32_t dap_dp_low_access(adiv5_debug_port_s *target_dp, uint8_t rnw, uint16_t addr, uint32_t value);
uint32_t dap_dp_read_reg(adiv5_debug_port_s *target_dp, uint16_t addr);

#endif /* PLATFORMS_HOSTED_DAP_H */
