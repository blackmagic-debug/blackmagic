/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2023 1BitSquared <info@1bitsquared.com>
 * Written by Rachel Mant <git@dragonmux.network>
 * Modified by Rafael Silva <perigoso@riseup.net>
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

#ifndef PLATFORMS_HOSTED_JLINK_PROTOCOL_H
#define PLATFORMS_HOSTED_JLINK_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/*
 * This file contains the definitions for the J-Link USB Protocol as defined in the RM08001 Reference manual (Chapter §5)
 * 
 * Overview
 * The J-Link firmware uses several commands in a request reply topology to communicate with the host software
 * Communication is always initiated by the host, which sends an 8 bit command to the probe followed by optional parameters
 * USB bulk communication is used to transfer data between host and J-Link
 * All data units larger than a single byte are transferred little endian, meaning least significant bytes are transferred first
 * All USB operations use a 5 second timeout
 * 
 * The commands have been reordered and renamed in the context of Black Magic Debug in an effort to make them more intuitive, consistent and easier to use.
 * The mapping between the J-Link USB Protocol reference manual commands and the new Black Magic Debug commands is listed at the start of each command group below.
 */

/* System information commands
 *
 * ┌────────────────────────────────────────────────┬──────────────────────────────────┐
 * │              BMDA J-Link command               │  RM08001 J-Link USB Protocol RM  │
 * ├────────────────────────────────────────────────┼──────────────────────────────────┤
 * │ JLINK_CMD_INFO_GET_FIRMWARE_VERSION            │ §5.3.1 EMU_CMD_VERSION           │
 * │ JLINK_CMD_INFO_GET_HARDWARE_VERSION            │ §5.3.6 EMU_CMD_GET_HW_VERSION    │
 * │ JLINK_CMD_INFO_GET_PROBE_CAPABILITIES          │ §5.3.4 EMU_CMD_GET_CAPS          │
 * │ JLINK_CMD_INFO_GET_PROBE_EXTENDED_CAPABILITIES │ §5.3.5 EMU_CMD_GET_CAPS_EX       │
 * │ JLINK_CMD_INFO_GET_MAX_MEM_BLOCK               │ §5.3.3 EMU_CMD_GET_MAX_MEM_BLOCK │
 * └────────────────────────────────────────────────┴──────────────────────────────────┘
 */
#define JLINK_CMD_INFO_GET_FIRMWARE_VERSION            0x01U /* Get probe firmware version string */
#define JLINK_CMD_INFO_GET_HARDWARE_VERSION            0xf0U /* Get probe hardware version */
#define JLINK_CMD_INFO_GET_PROBE_CAPABILITIES          0xe8U /* Get probe capabilities */
#define JLINK_CMD_INFO_GET_PROBE_EXTENDED_CAPABILITIES 0xedU /* Get probe extended capabilities */
#define JLINK_CMD_INFO_GET_MAX_MEM_BLOCK               0xd4U /* Get the maximum memory blocksize */

/* Interface commands
 *
 * ┌────────────────────────────────────────┬────────────────────────────────┐
 * │          BMDA J-Link command           │ RM08001 J-Link USB Protocol RM │
 * ├────────────────────────────────────────┼────────────────────────────────┤
 * │ JLINK_CMD_INTERFACE_GET_BASE_FREQUENCY │ §5.3.2 EMU_CMD_GET_SPEEDS      │
 * │ JLINK_CMD_INTERFACE_SET_FREQUENCY_KHZ  │ §5.5.2 EMU_CMD_SET_SPEED       │
 * │ JLINK_CMD_INTERFACE_SET_SELECTED       │ §5.5.3 EMU_CMD_SELECT_IF       │
 * │ JLINK_CMD_INTERFACE_GET                │ §5.5.3 EMU_CMD_SELECT_IF       │
 * └────────────────────────────────────────┴────────────────────────────────┘
 */
#define JLINK_CMD_INTERFACE_GET_BASE_FREQUENCY 0xc0U /* Get base frequency and minimum divider of selected interface */
#define JLINK_CMD_INTERFACE_SET_FREQUENCY_KHZ  0x05U /* Sets the interface speed in kHz*/
#define JLINK_CMD_INTERFACE_SET_SELECTED       0xc7U /* Select the probe interface */
#define JLINK_CMD_INTERFACE_GET                0xc7U /* Get current selected interface or available interfaces */

/* Target power commands
 *
 * ┌───────────────────────────────┬────────────────────────────────┐
 * │      BMDA J-Link command      │ RM08001 J-Link USB Protocol RM │
 * ├───────────────────────────────┼────────────────────────────────┤
 * │ JLINK_CMD_POWER_SET_KICKSTART │ §5.5.4 EMU_CMD_SET_KS_POWER    │
 * │ JLINK_CMD_POWER_GET_STATE     │ §5.4.2 EMU_CMD_GET_HW_INFO     │
 * └───────────────────────────────┴────────────────────────────────┘
 */
#define JLINK_CMD_POWER_SET_KICKSTART 0x08U /* Set KickStart power state on pin 19 (J-Link 20 pin connector) */
#define JLINK_CMD_POWER_GET_STATE     0xc1U /* Get Kickstart power state and overcurrent timers */

/* Low level hardware commands
 *
 * ┌──────────────────────────────┬────────────────────────────────┐
 * │     BMDA J-Link command      │ RM08001 J-Link USB Protocol RM │
 * ├──────────────────────────────┼────────────────────────────────┤
 * │ JLINK_CMD_SIGNAL_GET_STATE   │ §5.4.1  EMU_CMD_GET_STATE      │
 * │ JLINK_CMD_SIGNAL_CLEAR_RESET │ §5.6.4  EMU_CMD_HW_RESET0      │
 * │ JLINK_CMD_SIGNAL_SET_RESET   │ §5.6.5  EMU_CMD_HW_RESET1      │
 * │ JLINK_CMD_SIGNAL_PULSE_RESET │ §5.6.1  EMU_CMD_RESET_TARGET   │
 * │ JLINK_CMD_SIGNAL_CLEAR_TRST  │ §5.5.15 EMU_CMD_HW_TRST0       │
 * │ JLINK_CMD_SIGNAL_SET_TRST    │ §5.5.16 EMU_CMD_HW_TRST1       │
 * │ JLINK_CMD_SIGNAL_PULSE_TRST  │ §5.5.1  EMU_CMD_RESET_TRST     │
 * │ JLINK_CMD_SIGNAL_CLEAR_TMS   │ §5.5.6  EMU_CMD_HW_TMS0        │
 * │ JLINK_CMD_SIGNAL_SET_TMS     │ §5.5.7  EMU_CMD_HW_TMS1        │
 * │ JLINK_CMD_SIGNAL_CLEAR_TDI   │ §5.5.8  EMU_CMD_HW_DATA0       │
 * │ JLINK_CMD_SIGNAL_SET_TDI     │ §5.5.9  EMU_CMD_HW_DATA1       │
 * └──────────────────────────────┴────────────────────────────────┘
 */
#define JLINK_CMD_SIGNAL_GET_STATE   0x07U /* Get target voltage and pin logic states */
#define JLINK_CMD_SIGNAL_CLEAR_RESET 0xdcU /* Assert target reset */
#define JLINK_CMD_SIGNAL_SET_RESET   0xddU /* Deassert target reset */
#define JLINK_CMD_SIGNAL_PULSE_RESET 0x03U /* Assert target reset for 2ms */
#define JLINK_CMD_SIGNAL_CLEAR_TRST  0xdeU /* Clear TRST */
#define JLINK_CMD_SIGNAL_SET_TRST    0xdfU /* Set TRST */
#define JLINK_CMD_SIGNAL_PULSE_TRST  0x02U /* Pulse TRST LOW for 2ms */
#define JLINK_CMD_SIGNAL_CLEAR_TMS   0xc9U /* Clear TMS pin */
#define JLINK_CMD_SIGNAL_SET_TMS     0xcaU /* Set TMS pin */
#define JLINK_CMD_SIGNAL_CLEAR_TDI   0xcbU /* Clear TDI pin */
#define JLINK_CMD_SIGNAL_SET_TDI     0xccU /* Set TDI pin */

/* Low level IO commands
 *
 * ┌────────────────────────────────────┬────────────────────────────────────┐
 * │        BMDA J-Link command         │   RM08001 J-Link USB Protocol RM   │
 * ├────────────────────────────────────┼────────────────────────────────────┤
 * │ JLINK_CMD_IO_PULSE_CLOCK           │ §5.5.5  EMU_CMD_HW_CLOCK           │
 * │ JLINK_CMD_IO_TRANSACTION           │ §5.5.12 EMU_CMD_HW_JTAG3           │
 * │ JLINK_CMD_IO_WRITE                 │ §5.5.13 EMU_CMD_HW_JTAG_WRITE      │
 * │ JLINK_CMD_IO_GET_WRITE_RESULT      │ §5.5.14 EMU_CMD_HW_JTAG_GET_RESULT │
 * │ JLINK_CMD_IO_WRITE_DCC             │ §5.5.17 EMU_CMD_WRITE_DCC          │
 * │ JLINK_CMD_IO_TRANSACTION_OBSOLETE1 │ §5.5.10 EMU_CMD_HW_JTAG            │
 * │ JLINK_CMD_IO_TRANSACTION_OBSOLETE2 │ §5.5.11 EMU_CMD_HW_JTAG2           │
 * └────────────────────────────────────┴────────────────────────────────────┘
 */
#define JLINK_CMD_IO_PULSE_CLOCK           0xc8U /* Generate one clock cycle and return TDI value on falling edge */
#define JLINK_CMD_IO_TRANSACTION           0xcfU /* Send data on TDI and TMS (SWDIO on SWD) and return TDO (SWDIO on SWD) */
#define JLINK_CMD_IO_WRITE                 0xd5U /* Same as IO_TRANSACTION w/o response data */
#define JLINK_CMD_IO_GET_WRITE_RESULT      0xd6U /* Status of sticky error by CMD_IO_WRITE */
#define JLINK_CMD_IO_WRITE_DCC             0xf1U /* Write to JTAG using DCC */
#define JLINK_CMD_IO_TRANSACTION_OBSOLETE1 0xcdU /* Obsolete: Send data on TDI and TMS and return TDO */
#define JLINK_CMD_IO_TRANSACTION_OBSOLETE2 0xceU /* Obsolete: Send data on TDI and TMS and return TDO  */

/* High level target commands
 *
 * ┌─────────────────────────────────────────────┬────────────────────────────────────────────┐
 * │             BMDA J-Link command             │       RM08001 J-Link USB Protocol RM       │
 * ├─────────────────────────────────────────────┼────────────────────────────────────────────┤
 * │ JLINK_CMD_TARGET_RELEASE_RESET_HALT_RETRY   │ §5.6.2 EMU_CMD_HW_RELEASE_RESET_STOP_EX    │
 * │ JLINK_CMD_TARGET_RELEASE_RESET_HALT_TIMEOUT │ §5.6.3 EMU_CMD_HW_RELEASE_RESET_STOP_TIMED │
 * │ JLINK_CMD_TARGET_GET_CPU_CAPABILITIES       │ §5.6.6 EMU_CMD_GET_CPU_CAPS                │
 * │ JLINK_CMD_TARGET_EXECUTE_CPU_CMD            │ §5.6.7 EMU_CMD_EXEC_CPU_CMD                │
 * │ JLINK_CMD_TARGET_WRITE_MEMORY               │    -   EMU_CMD_WRITE_MEM                   │
 * │ JLINK_CMD_TARGET_READ_MEMORY                │    -   EMU_CMD_READ_MEM                    │
 * │ JLINK_CMD_TARGET_WRITE_MEMORY_ARM79         │ §5.6.8 EMU_CMD_WRITE_MEM_ARM79             │
 * │ JLINK_CMD_TARGET_READ_MEMORY_ARM79          │ §5.6.9 EMU_CMD_READ_MEM_ARM79              │
 * │ JLINK_CMD_TARGET_MEASURE_RTCK_REACTION_TIME │ §5.4.4 EMU_CMD_MEASURE_RTCK_REACT          │
 * │ JLINK_CMD_TARGET_GET_CONNECTION_STATE       │ §5.4.3 EMU_CMD_GET_COUNTERS                │
 * └─────────────────────────────────────────────┴────────────────────────────────────────────┘
 */
#define JLINK_CMD_TARGET_RELEASE_RESET_HALT_RETRY   0xd0U /* Resets the CPU and halts ASAP (fails after n retries) */
#define JLINK_CMD_TARGET_RELEASE_RESET_HALT_TIMEOUT 0xd1U /* Resets the CPU and halts ASAP (fails after timeout) */
#define JLINK_CMD_TARGET_GET_CPU_CAPABILITIES       0xe9U /* Get the capabilities of the target CPU */
#define JLINK_CMD_TARGET_EXECUTE_CPU_CMD            0xeaU /* Executes target CPU functions */
#define JLINK_CMD_TARGET_WRITE_MEMORY               0xf4U /* Write to target memory */
#define JLINK_CMD_TARGET_READ_MEMORY                0xf5U /* Read from target memory */
#define JLINK_CMD_TARGET_WRITE_MEMORY_ARM79         0xf7U /* Write to target memory on ARM 7/9 targets */
#define JLINK_CMD_TARGET_READ_MEMORY_ARM79          0xf8U /* Read from target memory on ARM 7/9 targets */
#define JLINK_CMD_TARGET_MEASURE_RTCK_REACTION_TIME 0xf6U /* Measure RTCK reaction time of the target device */
#define JLINK_CMD_TARGET_GET_CONNECTION_STATE       0xc2U /* Get target connection timer counters */

/* Configuration commands
 *
 * ┌────────────────────────┬────────────────────────────────┐
 * │  BMDA J-Link command   │ RM08001 J-Link USB Protocol RM │
 * ├────────────────────────┼────────────────────────────────┤
 * │ JLINK_CMD_CONFIG_READ  │ §5.7.1 EMU_CMD_READ_CONFIG     │
 * │ JLINK_CMD_CONFIG_WRITE │ §5.7.2 EMU_CMD_WRITE_CONFIG    │
 * └────────────────────────┴────────────────────────────────┘
 */
#define JLINK_CMD_CONFIG_READ  0xf2U /* Read the probe configuration */
#define JLINK_CMD_CONFIG_WRITE 0xf3U /* Write the probe configuration */

/* 
 * The hardware version is returned as a 32 bit value with the following format:
 * TTMMmmrr
 * TT: Hardware type
 * MM: Major version
 * mm: Minor version
 * rr: Revision
 * 
 * Note for clarification of the reference manual:
 * This is a 32 bit decimal value! not hex! (ugh... let's not question how long it took to figure that one out)
*/
#define JLINK_HARDWARE_VERSION_TYPE(v)     ((v / 1000000U) % 100U) /* Get hardware type from hardware version value */
#define JLINK_HARDWARE_VERSION_MAJOR(v)    ((v / 10000U) % 100U)   /* Get major version from hardware version value */
#define JLINK_HARDWARE_VERSION_MINOR(v)    ((v / 100U) % 100U)     /* Get minor version from hardware version value */
#define JLINK_HARDWARE_VERSION_REVISION(v) (v % 100U)              /* Get revision from hardware version value */

/* J-Link hardware version - JLINK_CMD_INFO_GET_HARDWARE_VERSION */
#define JLINK_HARDWARE_VERSION_TYPE_JLINK    0U
#define JLINK_HARDWARE_VERSION_TYPE_JTRACE   1U
#define JLINK_HARDWARE_VERSION_TYPE_FLASHER  2U
#define JLINK_HARDWARE_VERSION_TYPE_JLINKPRO 3U
#define JLINK_HARDWARE_VERSION_TYPE_LPCLINK2 18U

/* J-Link capabilities - JLINK_CMD_INFO_GET_PROBE_CAPABILITIES 
 *
 * ┌─────┬──────────────────────────────────────────┬────────────────────────────┐
 * │ Bit │          BMDA J-Link capability          │  §5.3.4 EMU_CMD_GET_CAPS   │
 * ├─────┼──────────────────────────────────────────┼────────────────────────────┤
 * │   1 │ JLINK_CAPABILITY_RESERVED                │ EMU_CAP_RESERVED           │
 * │   2 │ JLINK_CAPABILITY_HARDWARE_VERSION        │ EMU_CAP_GET_HW_VERSION     │
 * │   3 │ JLINK_CAPABILITY_WRITE_DCC               │ EMU_CAP_WRITE_DCC          │
 * │   4 │ JLINK_CAPABILITY_ADAPTIVE_CLOCKING       │ EMU_CAP_ADAPTIVE_CLOCKING  │
 * │   5 │ JLINK_CAPABILITY_READ_CONFIG             │ EMU_CAP_READ_CONFIG        │
 * │   6 │ JLINK_CAPABILITY_WRITE_CONFIG            │ EMU_CAP_WRITE_CONFIG       │
 * │   7 │ JLINK_CAPABILITY_TRACE                   │ EMU_CAP_TRACE              │
 * │   8 │ JLINK_CAPABILITY_WRITE_MEMORY            │ EMU_CAP_WRITE_MEM          │
 * │   9 │ JLINK_CAPABILITY_READ_MEMORY             │ EMU_CAP_READ_MEM           │
 * │  10 │ JLINK_CAPABILITY_INTERFACE_FREQUENCY     │ EMU_CAP_SPEED_INFO         │
 * │  11 │ JLINK_CAPABILITY_EXECUTE_CODE            │ EMU_CAP_EXEC_CODE          │
 * │  12 │ JLINK_CAPABILITY_MAX_MEM_BLOCK           │ EMU_CAP_GET_MAX_BLOCK_SIZE │
 * │  13 │ JLINK_CAPABILITY_POWER_STATE             │ EMU_CAP_GET_HW_INFO        │
 * │  14 │ JLINK_CAPABILITY_KICKSTART_POWER         │ EMU_CAP_SET_KS_POWER       │
 * │  15 │ JLINK_CAPABILITY_HALT_TIMEOUT            │ EMU_CAP_RESET_STOP_TIMED   │
 * │  15 │ JLINK_CAPABILITY_RESERVED2               │ -                          │
 * │  16 │ JLINK_CAPABILITY_MEASURE_RTCK_REACT      │ EMU_CAP_MEASURE_RTCK_REACT │
 * │  17 │ JLINK_CAPABILITY_INTERFACES              │ EMU_CAP_SELECT_IF          │
 * │  18 │ JLINK_CAPABILITY_MEMORY_ARM79            │ EMU_CAP_RW_MEM_ARM79       │
 * │  19 │ JLINK_CAPABILITY_CONNECTION_STATE        │ EMU_CAP_GET_COUNTERS       │
 * │  20 │ JLINK_CAPABILITY_READ_DCC                │ EMU_CAP_READ_DCC           │
 * │  21 │ JLINK_CAPABILITY_TARGET_CPU_CAPABILITIES │ EMU_CAP_GET_CPU_CAPS       │
 * │  22 │ JLINK_CAPABILITY_TARGET_EXECUTE_CPU_CMD  │ EMU_CAP_EXEC_CPU_CMD       │
 * │  23 │ JLINK_CAPABILITY_SWO                     │ EMU_CAP_SWO                │
 * │  24 │ JLINK_CAPABILITY_WRITE_DCC_EX            │ EMU_CAP_WRITE_DCC_EX       │
 * │  25 │ JLINK_CAPABILITY_UPDATE_FIRMWARE_EX      │ EMU_CAP_UPDATE_FIRMWARE_EX │
 * │  26 │ JLINK_CAPABILITY_FILE_IO                 │ EMU_CAP_FILE_IO            │
 * │  27 │ JLINK_CAPABILITY_REGISTER                │ EMU_CAP_REGISTER           │
 * │  28 │ JLINK_CAPABILITY_INDICATORS              │ EMU_CAP_INDICATORS         │
 * │  29 │ JLINK_CAPABILITY_TEST_NET_SPEED          │ EMU_CAP_TEST_NET_SPEED     │
 * │  30 │ JLINK_CAPABILITY_RAWTRACE                │ EMU_CAP_RAWTRACE           │
 * │  31 │ JLINK_CAPABILITY_EXTENDED_CAPABILITIES   │ EMU_CAP_EX_GET_CAPS_EX     │
 * │  32 │ JLINK_CAPABILITY_CMD_IO_WRITE            │ EMU_CAP_EX_HW_JTAG_WRITE   │
 * └─────┴──────────────────────────────────────────┴────────────────────────────┘
 * 
 * 'Undocumented' - The command/capability is not documented in the reference manual nor listed on this page
 */
#define JLINK_CAPABILITY_RESERVED                (1U << 0U)  /* Always 1 */
#define JLINK_CAPABILITY_HARDWARE_VERSION        (1U << 1U)  /* Supports JLINK_CMD_INFO_GET_HARDWARE_VERSION */
#define JLINK_CAPABILITY_WRITE_DCC               (1U << 2U)  /* Supports JLINK_CMD_IO_WRITE_DCC */
#define JLINK_CAPABILITY_ADAPTIVE_CLOCKING       (1U << 3U)  /* Supports adaptive clocking */
#define JLINK_CAPABILITY_READ_CONFIG             (1U << 4U)  /* Supports JLINK_CMD_CONFIG_READ */
#define JLINK_CAPABILITY_WRITE_CONFIG            (1U << 5U)  /* Supports JLINK_CMD_CONFIG_WRITE */
#define JLINK_CAPABILITY_TRACE                   (1U << 6U)  /* Supports trace commands */
#define JLINK_CAPABILITY_WRITE_MEMORY            (1U << 7U)  /* Supports JLINK_CMD_TARGET_WRITE_MEMORY */
#define JLINK_CAPABILITY_READ_MEMORY             (1U << 8U)  /* Supports JLINK_CMD_TARGET_READ_MEMORY */
#define JLINK_CAPABILITY_INTERFACE_FREQUENCY     (1U << 9U)  /* Supports JLINK_CMD_INTERFACE_GET_BASE_FREQUENCY */
#define JLINK_CAPABILITY_EXECUTE_CODE            (1U << 10U) /* Undocumented: Supports EMU_CMD_CODE_... commands */
#define JLINK_CAPABILITY_MAX_MEM_BLOCK           (1U << 11U) /* Supports JLINK_CMD_INFO_GET_MAX_MEM_BLOCK */
#define JLINK_CAPABILITY_POWER_STATE             (1U << 12U) /* Supports JLINK_CMD_POWER_GET_STATE */
#define JLINK_CAPABILITY_KICKSTART_POWER         (1U << 13U) /* Supports Kickstart power on pin 19 (J-Link 20 pin connector) */
#define JLINK_CAPABILITY_HALT_TIMEOUT            (1U << 14U) /* Supports JLINK_CMD_TARGET_RELEASE_RESET_HALT_TIMEOUT */
#define JLINK_CAPABILITY_RESERVED2               (1U << 0U)  /* 15 Unknown Reserved */
#define JLINK_CAPABILITY_MEASURE_RTCK_REACT      (1U << 16U) /* Supports JLINK_CMD_TARGET_MEASURE_RTCK_REACTION_TIME */
#define JLINK_CAPABILITY_INTERFACES              (1U << 17U) /* Supports JLINK_CMD_INTERFACE_GET/SET_SELECTED */
#define JLINK_CAPABILITY_MEMORY_ARM79            (1U << 18U) /* Supports JLINK_CMD_TARGET_READ/WRITE_MEMORY_ARM79 */
#define JLINK_CAPABILITY_CONNECTION_STATE        (1U << 19U) /* Supports JLINK_CMD_TARGET_GET_CONNECTION_STATE */
#define JLINK_CAPABILITY_READ_DCC                (1U << 20U) /* Undocumented: Supports READ_DCC */
#define JLINK_CAPABILITY_TARGET_CPU_CAPABILITIES (1U << 21U) /* Supports JLINK_CMD_TARGET_GET_CPU_CAPABILITIES */
#define JLINK_CAPABILITY_TARGET_EXECUTE_CPU_CMD  (1U << 22U) /* Supports JLINK_CMD_TARGET_EXECUTE_CPU_CMD */
#define JLINK_CAPABILITY_SWO                     (1U << 23U) /* Supports SWO */
#define JLINK_CAPABILITY_WRITE_DCC_EX            (1U << 24U) /* Undocumented: Supports WRITE_DCC_EX */
#define JLINK_CAPABILITY_UPDATE_FIRMWARE_EX      (1U << 25U) /* Undocumented: Supports UPDATE_FIRMWARE_EX */
#define JLINK_CAPABILITY_FILE_IO                 (1U << 26U) /* Undocumented: Supports FILE_IO */
#define JLINK_CAPABILITY_REGISTER                (1U << 27U) /* Undocumented: Supports REGISTER */
#define JLINK_CAPABILITY_INDICATORS              (1U << 28U) /* Undocumented: Supports INDICATORS */
#define JLINK_CAPABILITY_TEST_NET_SPEED          (1U << 29U) /* Undocumented: Supports TEST_NET_SPEED */
#define JLINK_CAPABILITY_RAWTRACE                (1U << 30U) /* Undocumented: Supports RAWTRACE */
#define JLINK_CAPABILITY_EXTENDED_CAPABILITIES   (1U << 31U) /* Supports extended capabilities */
#define JLINK_CAPABILITY_CMD_IO_WRITE            (1U << 32U) /* Supports JLINK_CMD_IO_WRITE */

/* Interface base frequency and min divider - JLINK_CMD_INTERFACE_GET_BASE_FREQUENCY */
#define JLINK_INTERFACE_BASE_FREQUENCY_OFFSET 0U /* 32 bit value */
#define JLINK_INTERFACE_MIN_DIV_OFFSET        4U /* 8 bit value */

/* Interface get - JLINK_CMD_INTERFACE_GET */
#define JLINK_INTERFACE_GET_AVAILABLE 0xffU /* returns 32 bit bitfield of available interfaces */
#define JLINK_INTERFACE_GET_CURRENT   0xfeU /* return currently selected interface number */

#define JLINK_INTERFACE_AVAILABLE(i) (1U << (i)) /* Convert interface number to bitfield bit */

/* Interfaces */
#define JLINK_INTERFACE_MAX  32U
#define JLINK_INTERFACE_JTAG 0U
#define JLINK_INTERFACE_SWD  1U
/* The following interfaces were obtained from libjaylink, with no official documentation to back them up */
#define JLINK_INTERFACE_BDM3          2U /* Background Debug Mode 3 (BDM3) */
#define JLINK_INTERFACE_FINE          3U /* Renesas’ single-wire debug interface (FINE) */
#define JLINK_INTERFACE_2W_JTAG_PIC32 4U /* 2-wire JTAG for PIC32 compliant devices */
#define JLINK_INTERFACE_SPI           5U /* Serial Peripheral Interface (SPI) */
#define JLINK_INTERFACE_C2            6U /* Silicon Labs 2-wire interface (C2) */
#define JLINK_INTERFACE_CJTAG         7U /* Compact JTAG (cJTAG) */

/* Kickstart power - JLINK_CMD_POWER_SET_KICKSTART */
#define JLINK_POWER_KICKSTART_ENABLE 0x01U /* Set Kickstart power on */

/* Power state - JLINK_CMD_POWER_GET_STATE */
#define JLINK_POWER_STATE_KICKSTART_ENABLED_MASK           (1U << 0U) /* Retrieves Kickstart power status */
#define JLINK_POWER_STATE_OVERCURRENT_MASK                 (1U << 1U) /* Information about why the target power was switched off */
#define JLINK_POWER_STATE_ITARGET_MASK                     (1U << 2U)  /* Target consumption(mA)  */
#define JLINK_POWER_STATE_ITARGET_PEAK_MASK                (1U << 3U)  /* Peak target consumption(mA) */
#define JLINK_POWER_STATE_ITARGET_PEAK_OPERATION_MASK      (1U << 4U)  /* Peak operation target consumption(mA) */
#define JLINK_POWER_STATE_ITARGET_MAX_TIME_2MS_3A_MASK     (1U << 10U) /* Time(ms) target consumption exceeded 3A */
#define JLINK_POWER_STATE_ITARGET_MAX_TIME_10MS_1A_MASK    (1U << 11U) /* Time(ms) target consumption exceeded 1A */
#define JLINK_POWER_STATE_ITARGET_MAX_TIME_40MS_400MA_MASK (1U << 12U) /* Time(ms) target consumption exceeded 400MA */
#define JLINK_POWER_STATE_VUSB_MASK                        (1U << 23U) /* USB voltage in mV */

#define JLINK_POWER_STATE_KICKSTART_ENABLED      0x1U /* Kickstart power is on */
#define JLINK_POWER_STATE_OVERCURRENT_NORMAL     0x0U /* Everything is normal */
#define JLINK_POWER_STATE_OVERCURRENT_2MS_3A     0x1U /* 2ms @ 3000mA */
#define JLINK_POWER_STATE_OVERCURRENT_10MS_1A    0x2U /* 10ms @ 1000mA */
#define JLINK_POWER_STATE_OVERCURRENT_40MS_400MA 0x3U /* 40ms @ 400mA */

/* Signal state - JLINK_CMD_SIGNAL_GET_STATE */
#define JLINK_SIGNAL_STATE_VOLTAGE_OFFSET 0U /* 16 bit value */
#define JLINK_SIGNAL_STATE_TCK_OFFSET     2U /* 1 bit value */
#define JLINK_SIGNAL_STATE_TDI_OFFSET     3U /* 1 bit value */
#define JLINK_SIGNAL_STATE_TDO_OFFSET     4U /* 1 bit value */
#define JLINK_SIGNAL_STATE_TMS_OFFSET     5U /* 1 bit value */
#define JLINK_SIGNAL_STATE_TRES_OFFSET    6U /* 1 bit value */
#define JLINK_SIGNAL_STATE_TRST_OFFSET    7U /* 1 bit value */

/* J-Link USB protocol constants */
#define JLINK_USB_TIMEOUT 5000U /* 5 seconds */

typedef enum jlink_swd_dir {
	JLINK_SWD_OUT,
	JLINK_SWD_IN,
} jlink_swd_dir_e;

typedef struct jlink_io_transact {
	/* This must always be set to JLINK_CMD_IO_TRANSACTION */
	uint8_t command;
	/* This value exists for alignment purposes and must be 0 */
	uint8_t reserved;
	/* clock_cycles defines how many bits need transferring */
	uint8_t clock_cycles[2];
} jlink_io_transact_s;

bool jlink_simple_query(uint8_t command, void *rx_buffer, size_t rx_len);
bool jlink_simple_request_8(uint8_t command, uint8_t operation, void *rx_buffer, size_t rx_len);
bool jlink_simple_request_16(uint8_t command, uint16_t operation, void *rx_buffer, size_t rx_len);
bool jlink_simple_request_32(uint8_t command, uint32_t operation, void *rx_buffer, size_t rx_len);
bool jlink_transfer(uint16_t clock_cycles, const uint8_t *tms, const uint8_t *tdi, uint8_t *tdo);
bool jlink_transfer_fixed_tms(uint16_t clock_cycles, bool final_tms, const uint8_t *tdi, uint8_t *tdo);
bool jlink_transfer_swd(uint16_t clock_cycles, jlink_swd_dir_e direction, const uint8_t *data_in, uint8_t *data_out);
bool jlink_select_interface(const uint8_t interface);

#endif /*PLATFORMS_HOSTED_JLINK_PROTOCOL_H*/
