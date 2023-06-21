/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2022-2023 1BitSquared <info@1bitsquared.com>
 * Written by Rachel Mant <git@dragonmux.network>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.	 If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef PLATFORMS_HOSTED_STLINKV2_PROTOCOL_H
#define PLATFORMS_HOSTED_STLINKV2_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "adiv5.h"

#define STLINK_DEBUG_ERR_OK            0x80U
#define STLINK_DEBUG_ERR_FAULT         0x81U
#define STLINK_JTAG_UNKNOWN_JTAG_CHAIN 0x04U
#define STLINK_NO_DEVICE_CONNECTED     0x05U
#define STLINK_JTAG_COMMAND_ERROR      0x08U
#define STLINK_JTAG_GET_IDCODE_ERROR   0x09U
#define STLINK_JTAG_DBG_POWER_ERROR    0x0bU
#define STLINK_ERROR_WRITE             0x0cU
#define STLINK_ERROR_WRITE_VERIFY      0x0dU
#define STLINK_ERROR_AP_WAIT           0x10U
#define STLINK_ERROR_AP_FAULT          0x11U
#define STLINK_ERROR_AP                0x12U
#define STLINK_ERROR_AP_PARITY         0x13U
#define STLINK_ERROR_DP_WAIT           0x14U
#define STLINK_ERROR_DP_FAULT          0x15U
#define STLINK_ERROR_DP                0x16U
#define STLINK_ERROR_DP_PARITY         0x17U
#define STLINK_SWD_AP_WDATA_ERROR      0x18U
#define STLINK_SWD_AP_STICKY_ERROR     0x19U
#define STLINK_SWD_AP_STICKYORUN_ERROR 0x1aU
#define STLINK_ERROR_BAD_AP            0x1dU
#define STLINK_TOO_MANY_AP_ERROR       0x29U
#define STLINK_JTAG_UNKNOWN_CMD        0x42U

#define STLINK_CORE_RUNNING      0x80U
#define STLINK_CORE_HALTED       0x81U
#define STLINK_CORE_STAT_UNKNOWN (-1)

#define STLINK_GET_VERSION        0xf1U
#define STLINK_DEBUG_COMMAND      0xf2U
#define STLINK_DFU_COMMAND        0xf3U
#define STLINK_SWIM_COMMAND       0xf4U
#define STLINK_GET_CURRENT_MODE   0xf5U
#define STLINK_GET_TARGET_VOLTAGE 0xf7U

#define STLINK_DEV_DFU_MODE        0x00U
#define STLINK_DEV_MASS_MODE       0x01U
#define STLINK_DEV_DEBUG_MODE      0x02U
#define STLINK_DEV_SWIM_MODE       0x03U
#define STLINK_DEV_BOOTLOADER_MODE 0x04U
#define STLINK_DEV_UNKNOWN_MODE    (-1)

#define STLINK_DFU_EXIT 0x07U

#define STLINK_SWIM_ENTER          0x00U
#define STLINK_SWIM_EXIT           0x01U
#define STLINK_SWIM_READ_CAP       0x02U
#define STLINK_SWIM_SPEED          0x03U
#define STLINK_SWIM_ENTER_SEQ      0x04U
#define STLINK_SWIM_GEN_RST        0x05U
#define STLINK_SWIM_RESET          0x06U
#define STLINK_SWIM_ASSERT_RESET   0x07U
#define STLINK_SWIM_DEASSERT_RESET 0x08U
#define STLINK_SWIM_READSTATUS     0x09U
#define STLINK_SWIM_WRITEMEM       0x0aU
#define STLINK_SWIM_READMEM        0x0bU
#define STLINK_SWIM_READBUF        0x0cU

#define STLINK_DEBUG_GETSTATUS           0x01U
#define STLINK_DEBUG_FORCEDEBUG          0x02U
#define STLINK_DEBUG_APIV1_RESETSYS      0x03U
#define STLINK_DEBUG_APIV1_READALLREGS   0x04U
#define STLINK_DEBUG_APIV1_READREG       0x05U
#define STLINK_DEBUG_APIV1_WRITEREG      0x06U
#define STLINK_DEBUG_READMEM_32BIT       0x07U
#define STLINK_DEBUG_WRITEMEM_32BIT      0x08U
#define STLINK_DEBUG_RUNCORE             0x09U
#define STLINK_DEBUG_STEPCORE            0x0aU
#define STLINK_DEBUG_APIV1_SETFP         0x0bU
#define STLINK_DEBUG_READMEM_8BIT        0x0cU
#define STLINK_DEBUG_WRITEMEM_8BIT       0x0dU
#define STLINK_DEBUG_APIV1_CLEARFP       0x0eU
#define STLINK_DEBUG_APIV1_WRITEDEBUGREG 0x0fU
#define STLINK_DEBUG_APIV1_SETWATCHPOINT 0x10U

#define STLINK_DEBUG_ENTER_JTAG_RESET    0x00U
#define STLINK_DEBUG_ENTER_SWD_NO_RESET  0xa3U
#define STLINK_DEBUG_ENTER_JTAG_NO_RESET 0xa4U

#define STLINK_DEBUG_APIV1_ENTER 0x20U
#define STLINK_DEBUG_EXIT        0x21U
#define STLINK_DEBUG_READCOREID  0x22U

#define STLINK_DEBUG_APIV2_ENTER         0x30U
#define STLINK_DEBUG_APIV2_READ_IDCODES  0x31U
#define STLINK_DEBUG_APIV2_RESETSYS      0x32U
#define STLINK_DEBUG_APIV2_READREG       0x33U
#define STLINK_DEBUG_APIV2_WRITEREG      0x34U
#define STLINK_DEBUG_APIV2_WRITEDEBUGREG 0x35U
#define STLINK_DEBUG_APIV2_READDEBUGREG  0x36U

#define STLINK_DEBUG_APIV2_READALLREGS     0x3aU
#define STLINK_DEBUG_APIV2_GETLASTRWSTATUS 0x3bU
#define STLINK_DEBUG_APIV2_DRIVE_NRST      0x3cU

#define STLINK_DEBUG_APIV2_GETLASTRWSTATUS2 0x3eU

#define STLINK_DEBUG_APIV2_START_TRACE_RX 0x40U
#define STLINK_DEBUG_APIV2_STOP_TRACE_RX  0x41U
#define STLINK_DEBUG_APIV2_GET_TRACE_NB   0x42U
#define STLINK_DEBUG_APIV2_SWD_SET_FREQ   0x43U
#define STLINK_DEBUG_APIV2_JTAG_SET_FREQ  0x44U
#define STLINK_DEBUG_APIV2_READ_DAP_REG   0x45U
#define STLINK_DEBUG_APIV2_WRITE_DAP_REG  0x46U
#define STLINK_DEBUG_APIV2_READMEM_16BIT  0x47U
#define STLINK_DEBUG_APIV2_WRITEMEM_16BIT 0x48U

#define STLINK_DEBUG_APIV2_INIT_AP      0x4bU
#define STLINK_DEBUG_APIV2_CLOSE_AP_DBG 0x4cU

#define STLINK_APIV3_SET_COM_FREQ 0x61U
#define STLINK_APIV3_GET_COM_FREQ 0x62U

#define STLINK_APIV3_GET_VERSION_EX 0xfbU

#define STLINK_DEBUG_APIV2_DRIVE_NRST_LOW   0x00U
#define STLINK_DEBUG_APIV2_DRIVE_NRST_HIGH  0x01U
#define STLINK_DEBUG_APIV2_DRIVE_NRST_PULSE 0x02U

#define STLINK_TRACE_SIZE   4096U
#define STLINK_TRACE_MAX_HZ 2000000U

#define STLINK_V3_FREQ_ENTRY_COUNT 10U

#define STLINK_DEBUG_PORT 0xffffU

typedef struct stlink_simple_command {
	uint8_t command;
	uint8_t operation;
	uint8_t reserved[14];
} stlink_simple_command_s;

typedef struct stlink_simple_request {
	uint8_t command;
	uint8_t operation;
	uint8_t param;
	uint8_t reserved[13];
} stlink_simple_request_s;

typedef struct stlink_adiv5_reg_read {
	uint8_t command;
	uint8_t operation;
	uint8_t apsel[2];
	uint8_t address[2];
	uint8_t reserved[10];
} stlink_adiv5_reg_read_s;

typedef struct stlink_adiv5_reg_write {
	uint8_t command;
	uint8_t operation;
	uint8_t apsel[2];
	uint8_t address[2];
	uint8_t value[4];
	uint8_t reserved[6];
} stlink_adiv5_reg_write_s;

typedef struct stlink_arm_reg_read {
	uint8_t command;
	uint8_t operation;
	uint8_t reg_num;
	uint8_t apsel;
	uint8_t reserved[12];
} stlink_arm_reg_read_s;

typedef struct stlink_arm_reg_write {
	uint8_t command;
	uint8_t operation;
	uint8_t reg_num;
	uint8_t value[4];
	uint8_t apsel;
	uint8_t reserved[8];
} stlink_arm_reg_write_s;

typedef struct stlink_mem_command {
	uint8_t command;
	uint8_t operation;
	uint8_t address[4];
	uint8_t length[2];
	uint8_t apsel;
	uint8_t reserved[7];
} stlink_mem_command_s;

typedef struct stlink_v2_set_freq {
	uint8_t command;
	uint8_t operation;
	uint8_t divisor[2];
	uint8_t reserved[12];
} stlink_v2_set_freq_s;

typedef struct stlink_v3_set_freq {
	uint8_t command;
	uint8_t operation;
	uint8_t mode;
	uint8_t reserved1;
	uint8_t frequency[4];
	uint8_t reserved2[8];
} stlink_v3_set_freq_s;

int stlink_simple_query(uint8_t command, uint8_t operation, void *rx_buffer, size_t rx_len);
int stlink_simple_request(uint8_t command, uint8_t operation, uint8_t param, void *rx_buffer, size_t rx_len);
int stlink_send_recv_retry(const void *req_buffer, size_t req_len, void *rx_buffer, size_t rx_len);

bool stlink_leave_state(void);
int stlink_usb_error_check(uint8_t *data, bool verbose);

uint32_t stlink_raw_access(adiv5_debug_port_s *dp, uint8_t rnw, uint16_t addr, uint32_t value);
uint32_t stlink_adiv5_clear_error(adiv5_debug_port_s *dp, bool protocol_recovery);
void stlink_dp_abort(adiv5_debug_port_s *dp, uint32_t abort);

#endif /*PLATFORMS_HOSTED_STLINKV2_PROTOCOL_H*/
