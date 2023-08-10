/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2023 1BitSquared <info@1bitsquared.com>
 * Written by Rafael Silva <perigoso@riseup.net>
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

#ifndef PLATFORMS_HOSTED_WCHLINK_PROTOCOL_H
#define PLATFORMS_HOSTED_WCHLINK_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/*
 * This file contains the definitions for the WCH-Link USB Protocol, no public documentation is available
 * so these definitions are the result of reverse engineering the protocol and trial and error.
 *
 * !!! THIS IS LARGELY INCOMPLETE AND UNTESTED, DO NOT TAKE THIS AS A DEFINITIVE SOURCE OF INFORMATION !!!
 *
 * The WCH-Link has two modes of operation, DAPLink and RV (i.e. RISC-V).
 * This refers to the RV mode of operation only, changing the mode of operation is also out of scope.
 * This was based on probes with firmware v2.5 and v2.8, differences are expected on untested/future versions.
 *
 * Overview
 *
 * WCH-Link uses USB Bulk Transfers to communicate with the host
 *
 * The WCH-Link exposes 4 endpoints through a Vendor interface:
 * 0x82: EP 2 IN	(Raw data)
 * 0x02: EP 2 OUT	(Raw data)
 * 0x81: EP 1 IN	(Command packets)
 * 0x01: EP 1 OUT	(Command packets)
 * EP 1 IN/OUT is used for most of the communication, EP 2 IN/OUT is used for some flash related operations.
 *
 * Command packet format:
 * 	┌─────────────┬────────┬─────────┬──────────────┬──────────────────────────────┐
 * 	│    Byte     │   0    │    1    │      2       │            3:End             │
 * 	├─────────────┼────────┼─────────┼──────────────┼──────────────────────────────┤
 * 	│ Description │ Header │ Command │ Payload Size │ Payload (Sub-command + Data) │
 * 	└─────────────┴────────┴─────────┴──────────────┴──────────────────────────────┘
 *
 * 		Header:
 *      	- 0x81 for host command packets
 *      	- 0x82 for device response packets
 *
 * 		Command: command, used to identify how the payload will be interpreted.
 * 		Payload Size: length in bytes of the remaining command data.
 * 		Payload: command data, interpreted according to the command, most command have a subcommand as the 1st byte.
 *
 * Responses are sent in the same format, with the header set to 0x82 and the same command.
 * In case of an error, the response will contain the error value instead of the requested command in the command field.
 */

/* USB protocol */
#define WCH_USB_MODE_RV_CMD_EPT_ADDR  0x1U
#define WCH_USB_MODE_RV_RAW_EPT_ADDR  0x2U
#define WCH_USB_MODE_DAP_OUT_EPT_ADDR 0x2U
#define WCH_USB_MODE_DAP_IN_EPT_ADDR  0x3U

#define WCH_USB_TIMEOUT 5000U

#define WCH_USB_INTERFACE_SUBCLASS 0x80U

/* Command packet */
#define WCH_CMD_PACKET_HEADER_OFFSET    0U
#define WCH_CMD_PACKET_HEADER_OUT       0x81U
#define WCH_CMD_PACKET_HEADER_IN        0x82U
#define WCH_CMD_PACKET_CMD_ERROR_OFFSET 1U
#define WCH_CMD_PACKET_SIZE_OFFSET      2U
#define WCH_CMD_PACKET_PAYLOAD_OFFSET   3U

/* Error */
#define WCH_ERR_ATTACH 0x55U /* Failed to attach to target */

/* RISC-V targets AKA "riscvchip" */
#define WCH_RISCVCHIP_CH32V103 0x01U /* CH32V103 RISC-V3A series */
#define WCH_RISCVCHIP_CH57X    0x02U /* CH571/CH573 RISC-V3A BLE 4.2 series */
#define WCH_RISCVCHIP_CH56X    0x03U /* CH565/CH569 RISC-V3A series */
#define WCH_RISCVCHIP_CH32V20X 0x05U /* CH32V20X RISC-V4B/V4C series */
#define WCH_RISCVCHIP_CH32V30X 0x06U /* CH32V30X RISC-V4C/V4F series */
#define WCH_RISCVCHIP_CH58X    0x07U /* CH581/CH582/CH583 RISC-V4A BLE 5.3 series */
#define WCH_RISCVCHIP_CH32V003 0x09U /* CH32V003 RISC-V2A series */
#define WCH_RISCVCHIP_CH59X    0x0bU /* CH59x RISC-V4C BLE 5.4 series */
#define WCH_RISCVCHIP_CH32X035 0x0dU /* CH32X035 RISC-V4C series */

/* Commands */
#define WCH_CMD_ADDR_N_SIZE   0x01U /* Set address and size command */
#define WCH_CMD_FLASH         0x02U /* Flash command */
#define WCH_CMD_READ_MEM      0x03U /* Memory read command */
#define WCH_CMD_PROTECT       0x06U /* Flash potection command */
#define WCH_CMD_DMI           0x08U /* DMI transfer command */
#define WCH_CMD_RESET         0x0bU /* Reset command */
#define WCH_CMD_PROBE_CONTROL 0x0cU /* Probe control command */
#define WCH_CMD_CONTROL       0x0dU /* Control command */
#define WCH_CMD_RV_DIS_DBG    0x0eU /* RV disable debug command */
#define WCH_CMD_VERIFY        0x0fU /* Verify command */
#define WCH_CMD_UID           0x11U /* Chip UID command */
#define WCH_CMD_MODDE_SWITCH  0xffU /* Switch probe mode command */

/*
 * Set address and size command - WCH_CMD_ADDR_N_SIZE
 *
 * This command does not have a sub-command byte,
 * the payload is the address followed by the size
 *
 * ┌──────┬──────┐
 * │ 0:4  │ 5:8  │
 * ├──────┼──────┤
 * │ ADDR │ SIZE │
 * └──────┴──────┘
 */

/* Flash command - WCH_CMD_FLASH */
#define WCH_FLASH_SUBCMD_CHIPERASE         0x01U /* Chip erase */
#define WCH_FLASH_SUBCMD_BEGIN_WRITE_FLASH 0x02U /* Begin write flash - ?? */
#define WCH_FLASH_SUBCMD_EXEC_RAM          0x03U /* Execute ram - ?? */
#define WCH_FLASH_SUBCMD_BEGIN_WRITE_MEM   0x05U /* Begin transfer - ?? */
#define WCH_FLASH_SUBCMD_PREPARE           0x06U /* ?? */
#define WCH_FLASH_SUBCMD_EXEC_MEM          0x07U /* ?? */
#define WCH_FLASH_SUBCMD_TERMINATE         0x08U /* ?? */
#define WCH_FLASH_SUBCMD_READY_WRITE       0x09U /* End transfer - ?? */
#define WCH_FLASH_SUBCMD_VERIFY2           0x0aU /* Verify - ?? */
#define WCH_FLASH_SUBCMD_RV_VERIFY         0x0bU /* Verify - ?? */
#define WCH_FLASH_SUBCMD_BEGIN_READ_MEM    0x0cU /* ?? */

/*
 * Memory read commands - WCH_CMD_READ_MEM
 *
 * This command does not have a sub-command byte,
 * the payload is the address to read from followed by the number of bytes to read
 *
 * ┌──────┬────────┐
 * │ 0:4  │  5:8   │
 * ├──────┼────────┤
 * │ ADDR │ LENGTH │
 * └──────┴────────┘
 */

/*
 * Flash potection command - WCH_CMD_PROTECT
 *
 * Not supported on riscvchip:
 * - 0x01: CH32V103
 * - 0x09: CH32V003
 * - 0x05: CH32V20X
 * - 0x06: CH32V30X
 * - 0x0d: CH32X035
 */
#define WCH_PROTECT_SUBCMD_CHECK           0x01U /* Check if flash is read-protected */
#define WCH_PROTECT_SUBCMD_FLASH_UNPROTECT 0x02U /* Set flash read unprotected */
#define WCH_PROTECT_SUBCMD_FLASH_PROTECT   0x03U /* Set flash read protected */
#define WCH_PROTECT_SUBCMD_CHECK_V2        0x04U /* Check if flash is read-protected - Firmware >= v2.9 */
/* PROTECT_V2 and UNPROTECT_V2 require `0xbf ff ff ff ff ff ff` as payload */
#define WCH_PROTECT_SUBCMD_FLASH_UNPROTECT_V2 0xf2U /* Set flash read unprotected - Firmware >= v2.9 */
#define WCH_PROTECT_SUBCMD_FLASH_PROTECT_V2   0xf3U /* Set flash read protected - Firmware >= v2.9 */

#define WCH_PROTECTED      0x01U /* Protected, memory read returns random data */
#define WCH_UNPROTECTED    0x02U /* Unprotected */
#define WCH_PROTECTED_V2   0x01U /* Protected, memory read returns random data */
#define WCH_UNPROTECTED_V2 0x00U /* Unprotected */

/*
 * DMI transfer command - WCH_CMD_DMI
 *
 * This command does not have a sub-command byte
 *
 * ┌────────────────────────────┐
 * │          Payload           │
 * ├─────────┬──────┬───────────┤
 * │    0    │ 1:4  │     5     │
 * ├─────────┼──────┼───────────┤
 * │ Address │ Data │ Operation │
 * └─────────┴──────┴───────────┘
 * ┌────────────────────────────┐
 * │      Response payload      │
 * ├─────────┬──────┬───────────┤
 * │    0    │ 1:4  │     5     │
 * ├─────────┼──────┼───────────┤
 * │ Address │ Data │  Status   │
 * └─────────┴──────┴───────────┘
 *
 * Operation and Status correspond to the same values
 * found in the JTAG implementation of RISC-V DMI:
 * 
 * Operation:
 * - 0x00: no-op
 * - 0x01: read
 * - 0x02: write
 * 
 * Status:
 * - 0x00: success
 * - 0x01: error
 * - 0x03: busy
 */
#define WCH_DMI_ADDR_OFFSET      0U
#define WCH_DMI_DATA_OFFSET      1U
#define WCH_DMI_OP_STATUS_OFFSET 5U

/* Reset command - WCH_CMD_RESET */
#define WCH_RESET_SUBCMD_RELEASE 0x01U /* Release reset (after 300ms delay) */
/*
 * There are two _SUBCMD_ASSERT sub-commands, used depending on the riscvchip:
 *
 * ASSERT2 used for riscvchip:
 * - 0x02: CH57X
 * - 0x07: CH58X
 * - 0x0b: CH59X
 */
#define WCH_RESET_SUBCMD_ASSERT  0x03U /* Reset */
#define WCH_RESET_SUBCMD_ASSERT2 0x02U /* Reset */

/*
 * Probe constrol command - WCH_CMD_PROBE_CONTROL
 *
 * This command does not have a sub-command byte,
 * the payload is the the riscvchip number followed by the speed
 *
 * ┌───────────┬───────┐
 * │     0     │   1   │
 * ├───────────┼───────┤
 * │ RISCVCHIP │ Speed │
 * └───────────┴───────┘
 *
 * Response is one byte, 0x01 meaning success
 */
#define WCH_SPEED_LOW      0x03U
#define WCH_SPEED_MEDIUM   0x02U
#define WCH_SPEED_HIGH     0x01U
#define WCH_SPEED_VERYHIGH 0x00U

#define WCH_PROBE_CONTROL_OK 0x01U /* Success response */

/* Control command - WCH_CMD_CONTROL */
#define WCH_CONTROL_SUBCMD_GET_PROBE_INFO 0x01U /* Firmware version and hardware type */
#define WCH_CONTROL_SUBCMD_ATTACH         0x02U /* Attach to target */
/*
 * On some riscvchip targets, a _SUBCMD_UNKNOWN is issued after attach
 *
 * Issued on riscvchip:
 * - 0x01: CH32V103
 * - 0x05: CH32V20X
 * - 0x06: CH32V30X
 * - 0x09: CH32V003
 */
#define WCH_CONTROL_SUBCMD_UNKNOWN 0x03U /* ?? - issued after attach */
/*
 * _GET_MEMORY_INFO
 *
 * Supported on riscvchip:
 * - 0x05: CH32V20X
 * - 0x06: CH32V30X
 */
#define WCH_CONTROL_SUBCMD_GET_MEMORY_INFO 0x04U /* RAM size, flash size and addr */
#define WCH_CONTROL_SUBCMD_CLOSE           0xffU /* Terminate connection (unsure what this entails) */

/* Probe info subcommand - WCH_SYS_SUBCMD_GET_PROBE_INFO */
#define WCH_VERSION_MAJOR_OFFSET 0U /* 8 bit */
#define WCH_VERSION_MINOR_OFFSET 1U /* 8 bit */

#define WCH_HARDWARE_TYPE_OFFSET    2U
#define WCH_HARDWARE_TYPE_WCHLINK   1U  /* WCH-Link (CH549) *does not support SDIO (single wire debug) */
#define WCH_HARDWARE_TYPE_WCHLINKE  2U  /* WCH-LinkE (CH32V305) */
#define WCH_HARDWARE_TYPE_WCHLINKS  3U  /* WCH-LinkS (CH32V203) */
#define WCH_HARDWARE_TYPE_WCHLINKB  4U  /* WCH-LinkB */
#define WCH_HARDWARE_TYPE_WCHLINKW  5U  /* WCH-LinkW (CH32V208) *wireless */
#define WCH_HARDWARE_TYPE_WCHLINKE2 18U /* WCH-LinkE (CH32V305) - ?? */

/* Attach to target subcommand - WCH_CONTROL_SUBCMD_ATTACH */
#define WCH_RISCVCHIP_OFFSET 0U /* 8 bit */
#define WCH_IDCODDE_OFFSET   1U /* 32 bit */

/*
 * RV disable debug command - WCH_CMD_RV_DIS_DBG
 *
 * Supported on riscvchip:
 * - 0x02: CH57X
 * - 0x03: CH56X
 * - 0x07: CH58X
 * - 0x0b: CH59X
 */
#define WCH_RV_DIS_DBG_SUBCMD_DISABLE 0x01U /* Disable debug */

/*
 * Verify command - WCH_CMD_VERIFY
 * FIXME: TBD
 */

/*
 * Chip UID command - WCH_CMD_UID
 *
 * The reply does not use the standard format.
 *
 * Raw response: ffff00 20 aeb4abcd 16c6bc45 e339e339e339e339
 * Corresponding UID: 0xcdabb4ae45bcc616
 * Unknown value: e339e339e339e339 -> inital value for erased flash
 */
#define WCH_UID_SUBCMD_GET    0x09U /* Get chip UID */
#define WCH_UID_SUBCMD_GET_V2 0x06U /* Get chip UID - Firmware >= v2.9 */

/* Switch probe mode command - WCH_CMD_MODDE_SWITCH */
#define WCH_MODDE_SWITCH_SUBCMD_SUPPORTED 0x41U /* Check if mode switching is supported - ?? */
#define WCH_MODDE_SWITCH_SUBCMD_DAP_TO_RV 0x52U /* Switch to RV mode - ?? */

bool wchlink_command_send_recv(uint8_t command, uint8_t subcommand, const void *payload, size_t payload_length,
	void *response, size_t response_length);

#endif /* PLATFORMS_HOSTED_WCHLINK_PROTOCOL_H */
