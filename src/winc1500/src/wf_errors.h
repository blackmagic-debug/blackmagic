/*==============================================================================
Copyright 2016 Microchip Technology Inc.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#ifndef __WF_ERRORS_H
#define __WF_ERRORS_H

#ifdef __cplusplus
     extern "C" {
#endif

typedef enum
{
    M2M_SUCCESS                        = 0,
    M2M_WIFI_INVALID_CHIP_REV_ERROR    = 1,     // Read incorrect WINC1500 revision.  Either invalid WINC1500 revision or SPI interface has an error.
    M2M_WIFI_SET_CUST_INFO_ERROR       = 2,     // NULL pointer parameter in m2m_wifi_set_cust_InfoElement() 
    M2M_WIFI_SET_CUST_INFO_LEN_ERROR   = 3,     // Custom info element is too large in m2m_wifi_set_cust_InfoElement() 
    M2M_WIFI_SCAN_OPTIONS_ERROR        = 4,     // Invalid parameter(s) in m2m_wifi_set_scan_options()
    M2M_WIFI_SCAN_REGION_ERROR         = 5,     // Invalid parameter in m2m_wifi_set_scan_region()
    M2M_WIFI_SCAN_IN_PROGRESS_ERROR    = 6,     // Scan already in progress; see m2m_wifi_request_scan(), m2m_wifi_request_scan_passive()  
    M2M_WIFI_SCAN_CHANNEL_ERROR        = 7,     // Invalid channel parameter in m2m_wifi_request_scan(), m2m_wifi_request_scan_passive()  
    M2M_WIFI_P2P_CHANNEL_ERROR         = 8,     // Invalid WPS channel parameter in m2m_wifi_p2p()
    M2M_WIFI_AP_CONFIG_ERROR           = 9,     // Invalid parameter(s) in m2m_wifi_enable_ap() or m2m_wifi_start_provision_mode())
    M2M_WIFI_REQ_SLEEP_ERROR           = 11,    // m2m_wifi_request_sleep() not allowed unless in M2M_WIFI_PS_MANUAL mode
    M2M_WIFI_DEVICE_NAME_TO_LONG_ERROR = 12,    // Name parameter too long in m2m_wifi_set_device_name()       
    M2M_WIFI_PROVISION_MODE_ERROR      = 13,    // Null pointer parameter in m2m_wifi_start_provision_mode()
    M2M_WIFI_CONNECT_ERROR             = 14,    // Invalid parameter(s) in m2m_wifi_connect()
    M2M_WIFI_FIRMWARE_READ_ERROR       = 15,    // Firmware read issue, read all 0's in nm_get_firmware_info()
    M2M_WIFI_FIRMWARE_MISMATCH_ERROR   = 16,    // Firmware revision mismatch in nm_get_firmware_info()
    M2M_WIFI_FIRMWARE_VERS_ZERO_ERROR  = 17,    // Read firmware version of 0 (invalid); see nm_get_firmware_full_info()
    M2M_WIFI_FIRMWARE_REG_READ_2_ERROR = 18,    // Firmware register read fail
    M2M_WIFI_PRNG_GET_ERROR            = 19,    // Invalid inputs to m2m_wifi_prng_get_random_bytes()
    M2M_WIFI_BOOTROM_LOAD_FAIL_ERROR   = 20,    // Failed to load bootrom from flash, see WaitForBootRom()
    M2M_WIFI_FIRMWARE_START_ERROR      = 21,    // Timed out waiting for WINC1500 firmware to start; see WaitForFirmwareStart()
    M2M_WIFI_WAKEUP_FAILED_ERROR       = 22,    // Wakeup failed, see ChipWake()
    M2M_WIFI_FALSE_INTERRUPT_ERROR     = 23,    // Received an interrupt with no known cause; see hif_isr()
    M2M_WIFI_INVALID_SIZE_ERROR        = 24,    // Received invalid size in hif_isr()
    M2M_WIFI_INVALID_GROUP_ERROR       = 25,    // Received invalid group in hif_isr()
    M2M_WIFI_INVALID_PACKET_SIZE_ERROR = 26,    // Received invalid packet size in hif_isr()
    M2M_WIFI_CHIP_REV_ERROR            = 27,    // Only support rev 3A0 or greater; see WaitForBootRom()
    M2M_WIFI_INVALID_WIFI_EVENT_ERROR  = 28,    // Invalid internal WiFi event, see WifiInternalEventHandler()
    M2M_WIFI_EVENT_READ_ERROR          = 30,    // Error reading event data; see WifiInternalEventHandler()
    M2M_WIFI_FAILED_TO_WAKE_CHIP_ERROR = 31,    // Failed to wake chip; see ChipWake()
            
    // internal errors
    M2M_WIFI_HIF_RECEIVE_1_ERROR        = 256,
    M2M_WIFI_HIF_RECEIVE_2_ERROR        = 257,
    M2M_WIFI_HIF_RECEIVE_3_ERROR        = 258,
    M2M_WIFI_HIF_RECEIVE_4_ERROR        = 259,
    M2M_WIFI_INVALID_OTA_RESPONSE_ERROR = 260,
    M2M_WIFI_MISMATCH_SESSION_ID_ERROR  = 261,    // mismatched session ID on socket send   
    M2M_WIFI_FLASH_WRITE_1_ERROR        = 262,
    M2M_WIFI_FLASH_WRITE_2_ERROR        = 263,
    M2M_WIFI_FLASH_WRITE_3_ERROR        = 264,
    M2M_WIFI_FLASH_READ_ERROR           = 265,
} t_m2mWifiErrorCodes;
         

#ifdef __cplusplus
     }
#endif

#endif // __WF_ERRORS_H     