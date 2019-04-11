/*******************************************************************************
   File Name:
    winc1500_api.h

  Summary:
    Primary include file for the WINC1500.

  Description:
    This file contains:
        1) General purpose constants, data types, and function prototypes
        2) Wi-Fi API constants, data types, and function prototypes
        3) Socket API via include file
*******************************************************************************/

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

#ifndef __WINC1500_API_H
#define __WINC1500_API_H

#ifdef __cplusplus
     extern "C" {
#endif

//============================================================================
// INCLUDES
//============================================================================
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include "winc1500_driver_config.h"
#include "wf_types.h"
#include "wf_socket.h"
#include "wf_utils.h"
#include "wf_errors.h"
#include "wf_ota.h"

//============================================================================
// CONSTANTS
//============================================================================
// Host Driver version  number
#define M2M_FIRMWARE_VERSION_MAJOR_NO     (19)
#define M2M_FIRMWARE_VERSION_MINOR_NO     (3)
#define M2M_FIRMWARE_VERSION_PATCH_NO     (0)

#define M2M_MAC_ADDRESS_LEN  6
#define M2M_MAX_PRNG_BYTES   16
//
// Control debug printing
//
#ifdef ENABLE_DEBUG
#define dprintf printf
#else
#define dprintf
#endif

//============================================================================
// DATA TYPES
//============================================================================
typedef enum
{
    M2M_WIFI_PIN_LOW  = 0,
    M2M_WIFI_PIN_HIGH = 1
} t_m2mWifiPinAction;


// Wi-Fi events. See m2m_wifi_handle_events().)
typedef enum
{
    M2M_WIFI_DRIVER_INIT_EVENT         = 0,
    M2M_WIFI_RSSI_EVENT                = 1,
    M2M_WIFI_DEFAULT_CONNNECT_EVENT    = 2,
    M2M_WIFI_CONN_STATE_CHANGED_EVENT  = 3,
    M2M_WIFI_WPS_EVENT                 = 4,
    M2M_WIFI_CONN_INFO_RESPONSE_EVENT  = 5,
    M2M_WIFI_PROVISION_INFO_EVENT      = 6,
    M2M_WIFI_SCAN_DONE_EVENT           = 7,
    M2M_WIFI_SCAN_RESULT_EVENT         = 8,
    M2M_WIFI_SYS_TIME_EVENT            = 9,
    M2M_WIFI_PRNG_EVENT                = 10,
    M2M_WIFI_IP_ADDRESS_ASSIGNED_EVENT = 11,
    M2M_WIFI_IP_CONFLICT_EVENT         = 12,

    M2M_WIFI_INVALID_WIFI_EVENT        = 255
} t_m2mWifiEventType;

// WINC1500 firmware revision information
typedef struct
{
    uint32_t    u32Chipid;              // HW revision (chip ID)
    uint8_t     u8FirmwareMajor;        // Version Major Number which represents the official release base
    uint8_t     u8FirmwareMinor;        // Version Minor Number which represents the engineering release base
    uint8_t     u8FirmwarePatch;        // Version Patch Number which represents the patches release base
    uint8_t     u8DriverMajor;          // Version Major Number which represents the official release base
    uint8_t     u8DriverMinor;          // Version Minor Number which represents the engineering release base
    uint8_t     u8DriverPatch;          // Version Patch Number which represents the patches release base
    uint8_t     BuildDate[sizeof(__DATE__)];    // date of WINC1500 firmware build
    uint8_t     BuildTime[sizeof(__TIME__)];    // time of WINC1500 firmware build
    uint8_t     padding1;
    uint16_t    u16FirmwareSvnNum;      // not used
    uint16_t    padding2[2];
} tstrM2mRev;

// Event data for M2M_WIFI_SCAN_DONE_EVENT.  See m2m_wifi_handle_events().
typedef struct
{
    uint8_t     u8NumofCh;     // number of found AP's
    int8_t      scanState;     // scan state
    uint8_t     padding[2];    // not used
} tstrM2mScanDone;

// Event data for M2M_WIFI_SCAN_RESULT_EVENT.  See m2m_wifi_handle_events().
typedef struct
{
    uint8_t     u8Index;                // AP index in the scan result list
    int8_t      s8Rssi;                 // AP signal strength
    uint8_t     u8AuthType;             // AP authentication type
    uint8_t     u8ch;                   // AP RF channel
    uint8_t     au8BSSID[6];            // BSSID of the AP
    char        au8SSID[M2M_MAX_SSID_LEN]; // AP SSID
    uint8_t     padding;                // not used
} tstrM2mWifiscanResult;

// Event data for M2M_WIFI_CONN_STATE_CHANGED_EVENT.  See m2m_wifi_handle_events().
typedef struct
{
    uint8_t     u8CurrState;           // see tenuM2mConnState
    uint8_t     u8ErrCode;             // Error type; see tenuM2mConnChangedErrcode
    uint8_t     padding[2];            // not used
} tstrM2mWifiStateChanged;

// Event data for M2M_WIFI_SYS_TIME_EVENT.  See m2m_wifi_handle_events().
typedef struct
{
    uint16_t   u16Year;
    uint8_t    u8Month;
    uint8_t    u8Day;
    uint8_t    u8Hour;
    uint8_t    u8Minute;
    uint8_t    u8Second;
    uint8_t    padding;
} tstrSystemTime;

// Event data M2M_WIFI_CONN_INFO_RESPONSE_EVENT.  See m2m_wifi_handle_events().
typedef struct
{
    char        acSSID[M2M_MAX_SSID_LEN];  // AP connection SSID name.  Only valid in
                                           //  station mode.  Will be NULL in SoftAP
                                           //  mode or P2P mode.
    uint8_t     u8SecType;                 // security type; see tenuM2mSecType
    uint8_t     au8IPAddr[4];              // connection IP address
    uint8_t     au8MACAddress[6];          // MAC address of the peer Wi-Fi station
    int8_t      s8RSSI;                    // Connection RSSI signal
    uint8_t     padding[3];                // not used
} tstrM2MConnInfo;

// Event data for M2M_WIFI_IP_ADDRESS_ASSIGNED_EVENT.  See m2m_wifi_handle_events().
typedef struct
{
    uint32_t     u32StaticIp;       // Static IP address assigned to device (big-endian)
    uint32_t     u32Gateway;        // IP address of the default gateway (big-endian)
    uint32_t     u32DNS;            // IP address for the DNS server (big-endian)
    uint32_t     u32SubnetMask;     // Subnet mask for the local area network (big-endian)
    uint32_t     u32DhcpLeaseTime;  // DHCP lease time, in seconds
} tstrM2MIPConfig;

// Event data for M2M_WIFI_WPS_EVENT.  See m2m_wifi_handle_events().
typedef struct
{
    uint8_t     u8AuthType;                  // Network authentication type; if 0 WPS session failed (see tenuM2mSecType)
    uint8_t     u8Ch;                        // RF Channel for the AP
    uint8_t     au8SSID[M2M_MAX_SSID_LEN];   // SSID obtained from WPS
    uint8_t     au8PSK[M2M_MAX_PSK_LEN];     // PSK for the network obtained from WPS
} tstrM2MWPSInfo;

// Event data for M2M_WIFI_PROVISION_INFO_EVENT.  See m2m_wifi_handle_events().
// This is the provisioning Information obtained from the HTTP Provisioning server.
typedef struct
{
    uint8_t    au8SSID[M2M_MAX_SSID_LEN];      // Provisioned SSID
    uint8_t    au8Password[M2M_MAX_PSK_LEN];   // Provisioned Password
    uint8_t    u8SecType;                      // Wi-Fi Security type
    uint8_t    u8Status;                       // Provisioning status. It must be checked before reading the provisioning information.
                                               // Values are:
                                               //   M2M_SUCCESS: Provision successful.
                                               //   M2M_WIFI_FAIL: Provision Failed.
} tstrM2MProvisionInfo;

// Event data for M2M_WIFI_DEFAULT_CONNNECT_EVENT.  See m2m_wifi_handle_events().
typedef struct
{
    int8_t      s8ErrorCode;                // See tenuM2mDefaultConnErrcode
                                            //
                                            //
    uint8_t     padding[3];
} tstrM2MDefaultConnResp;

#if defined(M2M_ENABLE_PRNG)
// Event data for M2M_WIFI_PRNG_EVENT.  See m2m_wifi_handle_events().
typedef struct
{
    uint8_t     buf[M2M_MAX_PRNG_BYTES];    // return buffer
    uint16_t    size;                       // PRNG size requested
} tstrM2MPrng;
#endif // M2M_ENABLE_PRNG

// Pointer to this union is passed to m2m_wifi_handle_events()
typedef union t_wifiEventData
{
    uint32_t                    conflictedIpAddress;
    int8_t                      rssi;
    tstrM2mScanDone             scanDone;
    tstrM2mWifiscanResult       scanResult;
    tstrM2mWifiStateChanged     connState;
    tstrM2MIPConfig             ipConfig;
    tstrSystemTime              sysTime;
    tstrM2MConnInfo             connInfo;
#if defined(M2M_ENABLE_WPS)
    tstrM2MWPSInfo              wpsInfo;
#endif
    tstrM2MProvisionInfo        provisionInfo;
    tstrM2MDefaultConnResp      defaultConnInfo;
#if defined(M2M_ENABLE_PRNG)
    tstrM2MPrng              prng;
#endif // M2M_ENABLE_PRNG
} t_wifiEventData;

//==============================================================================
// FUNCTION PROTOTYPES
//==============================================================================
/*******************************************************************************
  Function:
    void  m2m_wifi_init(void)

  Summary:
    Initializes WINC1500 Host driver.

  Description:
    Initializes WINC1500 Host driver.  This function must be called before any other
    WINC1500 API function.  No further API calls should be made until the
    M2M_WIFI_DRIVER_INIT_EVENT has been generated.

  Parameters:
    None

  Returns:
    None
 *****************************************************************************/
void m2m_wifi_init(void);

/*******************************************************************************
  Function:
    void m2m_wifi_task(void)

  Summary:
    WINC1500 main task.

  Description:
    This function must be called periodically in the main loop.  It handles
    all WINC1500 driver tasks.

  Parameters:
    None

  Returns:
    None
 *****************************************************************************/
void m2m_wifi_task(void);

/*******************************************************************************
  Function:
    void nm_get_firmware_info(tstrM2mRev *p_revision)

  Summary:
    Gets the WINC1500 firmware version.

  Description:
    Returns the WINC1500 firmware version.

  Parameters:
    p_revision -- pointer to where version information is written.  See tstrM2mRev.

  Returns:
    None
 *****************************************************************************/
void nm_get_firmware_info(tstrM2mRev *p_revision);

/*******************************************************************************
  Function:
    void nm_get_ota_firmware_info(tstrM2mRev *p_rev)

  Summary:
    Gets the firmware version of the OTA update.

  Description:
    Gets the firmware version of the OTA update.

 Parameters:
    p_rev -- See tstrM2mRev

  Returns:
    None
 *****************************************************************************/
void nm_get_ota_firmware_info(tstrM2mRev *p_rev);


/*******************************************************************************
  Function:
    uint32_t nmi_get_rfrevid(void)

  Summary:
    Gets the WINC1500 RF revision.

  Description:
    Returns the WINC1500 RF revision.

  Parameters:
    None

  Returns:
    RF Revision ID
 *****************************************************************************/
uint32_t nmi_get_rfrevid(void);


/*******************************************************************************
  Function:
    void m2m_wifi_connect(char *pcSsid, uint8_t u8SsidLen, uint8_t u8SecType, void *pvAuthInfo, uint16_t u16Ch)

  Summary:
    Initiates a Wi-Fi connection.

  Description:
    Initiates a  Wi-Fi connection using the input parameters.  Upon the connection
    succeeding (or failing), the M2M_WIFI_CONN_STATE_CHANGED_EVENT is generated.

  Parameters:
    pcSsid       -- SSID of AP to connect to (a null-terminated string)
    u8SecType    -- see tenuM2mSecType
    pvAuthInfo   -- see tuniM2MWifiAuth
    channel      -- RF channels to search (see tenuM2mScanCh)

  Returns:
    None
 *****************************************************************************/
void m2m_wifi_connect(char *pcSsid, uint8_t u8SsidLen, uint8_t u8SecType, void *pvAuthInfo, uint16_t u16Ch);

/*******************************************************************************
  Function:
    void m2m_wifi_connect_sc(char *pcSsid, uint8_t u8SsidLen, uint8_t u8SecType, void *pvAuthInfo, uint16_t channel)

  Summary:
    Initiates a Wi-Fi connection and saves connection config in FLASH.

  Description:
    Identical to m2m_wifi_connect() except input connection parameters are saved to WINC1500 FLASH.
    A future call m2m_wifi_default_connect() will use these parameters.  Upon the connection succeeding
    (or failing), the M2M_WIFI_CONN_STATE_CHANGED_EVENT is generated.

  Parameters:
    pcSsid       -- pointer to SSID string of AP (null-terminated string)
    u8SsidLen    -- Number of characters in the SSID (must be less than or equal to M2M_WIFI_MAX_SSID_LEN
    u8SecType    -- see tenuM2mSecType
    channel      -- RF channels to search (see tenuM2mScanCh)

  Returns:
    None
 *****************************************************************************/
void m2m_wifi_connect_sc(char *pcSsid, uint8_t u8SsidLen, uint8_t u8SecType, void *pvAuthInfo, uint16_t channel);

/*******************************************************************************
  Function:
    void m2m_wifi_default_connect(void)

  Summary:
    Connects using the most recently saved connection profile from the previous
    call to m2m_wifi_connect_sc().

  Description:
    Default connection relies on the connection profiles provisioned into the WINC1500
    serial FLASH.  The WINC1500 will connect to the most recent successful connection
    whose configuration was saved to WINC1500 FLASH.  After this call the
    M2M_WIFI_DEFAULT_CONNNECT_EVENT is generated.

    Upon success, m2m_wifi_get_connection_info() can be called to retrieve data about the
    connection.  See m2m_wifi_connect_sc().

  Parameters:
    None

  Returns:
    None
 *****************************************************************************/
void m2m_wifi_default_connect(void);

 /*******************************************************************************
  Function:
    void m2m_wifi_get_connection_info(void)

  Summary:
    Requests the current connection information.

  Description:
    Requests the current connection information.  The M2M_WIFI_CONN_INFO_RESPONSE_EVENT is
    generated when the information is ready.

  Parameters:
    None

  Returns:
    None
 *****************************************************************************/
void m2m_wifi_get_connection_info(void);

/*******************************************************************************
  Function:
    void m2m_wifi_disconnect(void)

  Summary:
    Disconnects the WINC1500 from the AP it is currently connected to.

  Description:
    Disconnects from the currently connected AP.  If not connected this function
    has no effect. Only valid in station mode; should not be called when in
    SoftAP mode.  When the disconnect is complete the M2M_WIFI_CONN_STATE_CHANGED_EVENT
    is generated.

  Parameters:
    None

  Returns:
    None
 *****************************************************************************/
void m2m_wifi_disconnect(void);

#if defined(M2M_ENABLE_WPS)
/*******************************************************************************
  Function:
    void m2m_wifi_wps(uint8_t u8TriggerType, const char  *pcPinNumber)

  Summary:
    Connects to an AP using WPS (Wi-Fi Protected Setup).

  Description:
    If connecting to an AP using WPS then this function should be used to
    connect (as opposed to m2m_wifi_connect() or m2m_wifi_connect_sc()).  The WINC1500
    must be in Idle or STA mode when this function is called.  Upon success, or
    failure, the M2M_WIFI_WPS_EVENT is generated.

  Parameters:
    u8TriggerType  -- WPS_PIN_TRIGGER or WPS_PBC_TRIGGER

    pcPinNumber    -- Pointer to the pin number; only used if u8TriggerType is WPS_PIN_TRIGGER.
                      Must be an ASCII decimal null-terminated string of 7 digits
                      (e.g. "1234567").

  Returns:
    None
 *****************************************************************************/
void m2m_wifi_wps(uint8_t u8TriggerType, const char  *pcPinNumber);

/*******************************************************************************
  Function:
    void m2m_wifi_wps_disable(void)

  Summary:
    Disables WPS mode on the WINC1500.

  Description:
    Disables WPS mode if m2m_wifi_wps called previously.

  Parameters:
    None

  Returns:
    None
 *****************************************************************************/
void m2m_wifi_wps_disable(void);
#endif // M2M_ENABLE_WPS

#if defined(M2M_WIFI_ENABLE_P2P)
/*******************************************************************************
  Function:
    void m2m_wifi_p2p(uint8_t u8Channel)

  Summary:
    Enable Wi-Fi Direct mode (also known as P2P).

  Description:
    The WINC supports P2P in device listening mode only (intent of 0).
    The WINC P2P implementation does not support P2P GO (Group Owner) mode.
    Active P2P devices (e.g. phones) can find the WINC1500 in the search list.

    When a device connects to the WINC1500 the M2M_WIFI_CONN_STATE_CHANGED_EVENT
    is generated.  Shortly after, the M2M_WIFI_IP_ADDRESS_ASSIGNED_EVENT is generated.

  Parameters:
    u8Channel -- P2P Listen channel.  It must be either channel 1, 6 or 11.

  Returns:
    None
 *****************************************************************************/
void m2m_wifi_p2p(uint8_t u8Channel);

 /*******************************************************************************
  Function:
    void m2m_wifi_p2p_disconnect(void)

  Summary:
    Removes the WINC1500 from P2P mode.

  Description:
    Removes the WINC1500 from P2P mode.  This should only be called if m2m_wifi_p2p()
    was called previously.

  Parameters:
    None

  Returns:
    None
 *****************************************************************************/
void m2m_wifi_p2p_disconnect(void);
#endif // M2M_WIFI_ENABLE_P2P

#if defined(M2M_ENABLE_SOFT_AP_MODE)
/*******************************************************************************
  Function:
    void m2m_wifi_enable_ap(const tstrM2MAPConfig *pstrM2MAPConfig)

  Summary:
    Directs the WIN1500 to go into SoftAP mode.

  Description:
    The WINC1500 starts a SoftAP network.  Only a single client is supported.  Once
    a client connects to the WINC1500 other clients attempting to connect will be
    rejected.  The M2M_WIFI_IP_ADDRESS_ASSIGNED_EVENT is generated when a client connects.

    This function must not be called when in in P2P or STA modes.

  Parameters:
    pstrM2MAPConfig -- AP configuration parameters.  See tstrM2MAPConfig.

  Returns:
    None
 *****************************************************************************/
void m2m_wifi_enable_ap(const tstrM2MAPConfig *pstrM2MAPConfig);

/*******************************************************************************
  Function:
    void m2m_wifi_disable_ap(void)

  Summary:
    Disables SoftAP mode.

  Description:
    Called after a previous call to m2m_wifi_enable_ap() to disable SoftAP mode.
    rejected.

  Parameters:
    None

  Returns:
    None
 *****************************************************************************/
void m2m_wifi_disable_ap(void);

/*******************************************************************************
  Function:
    void m2m_wifi_set_cust_InfoElement(uint8_t* pau8M2mCustInfoElement)

  Summary:
    Add/Remove user-defined Information Element to the Wi-Fi Beacon and Probe Response

  Description:
    This function is only applicable in SoftAP mode.  It allows adding a custom
    information element to the beacon or probe response.  The function can be
    called more than once if an IE needs to be appended to previously defined
    IE's.

  Parameters:
    pau8M2mCustInfoElement -- The format of the array is as follows:
                            [0]   -- total number of bytes in the array
                            [1]   -- Element ID for IE #1
                            [2]   -- Length of data in IE #1
                            [3:N] -- Data for IE #1
                            [N+1] -- Element ID for IE #2

    Maximum size of array cannot exceed 255 bytes.

    Example 1: Presume the application needs to add two information elements, the first
    has an ID of 160, length 3, data of 'A', 'B', 'C'.  The second element has an
    ID of 37, length 4, data of 'H', '2', '0', '0'.  The array would be set up as:
                [0] = 11       // 12 total bytes in array
                [1] = 160      // ID #1
                [2] = 3        // length of data for ID #1
                [3] = 'A'      // data[0] for ID #1
                [4] = 'B'      // data[1] for ID #1
                [5] = 'C'      // data[2] for ID #1
                [6] = 37       // ID #2
                [7] = 3        // length of data for ID #2
                [8] = 'H'      // data[0] for ID #2
                [9] = '2'      // data[1] for ID #2
               [10] = '0'      // data[2] for ID #2

    Example 2: Presume the IE's in Example 1 have been created.  The application wants to
               append a new IE with ID of 50, length 2, data of '2', 'X':
                [0] = 15       // cumulative total, 11 + 4
                [1:10]         // Same values as from Example 1
                [11] = 50      // ID #3
                [12] = 2       // length of ID #3
                [13] = '2'     // data[0] for ID #3
                [14] = 'X'     // data[1] for ID #3

     Example 3: To clear all information elements, the array has a single zero byte:
                [0] = 0

  Returns:
    None
 *****************************************************************************/
void m2m_wifi_set_cust_InfoElement(uint8_t* pau8M2mCustInfoElement);
#endif // M2M_ENABLE_SOFT_AP_MODE


/*******************************************************************************
  Function:
    void m2m_wifi_get_mac_address(uint8_t *pu8MacAddr)

  Summary:
    Reads the current MAC address on the WINC1500.

  Description:
    Reads the current MAC address on the WINC1500.

  Parameters:
    macAddress -- pointer to where the 6-byte MAC address will be written.

  Returns:
    None
 *****************************************************************************/
void m2m_wifi_get_mac_address(uint8_t *pu8MacAddr);

/*******************************************************************************
  Function:
    void m2m_wifi_get_otp_mac_address(uint8_t *pu8MacAddr, uint8_t *pu8IsValid)

  Summary:
    Reads the hardware (OTP) MAC address on the WINC1500.

  Description:
    If the OTP has been programmed with a valid MAC address this function returns
    that address.  If the OTP has not been programmed with a MAC address this
    functions returns a MAC address of all zeros.

  Parameters:
    macAddress -- pointer to where the 6-byte MAC address will be written.

  Returns:
    None
 *****************************************************************************/
void m2m_wifi_get_otp_mac_address(uint8_t *pu8MacAddr, uint8_t *pu8IsValid);

/*******************************************************************************
  Function:
    void m2m_wifi_set_mac_address(uint8_t macAddress[6])

  Summary:
    Sets the WINC1500 MAC address.

  Description:
    Performs a software overrides of the WINC1500 MAC address.  Typically only needed
    when testing.  In production code the WINC1500 MAC address should be used.

  Parameters:
    macAddress -- MAC address

  Returns:
    None
 *****************************************************************************/
void m2m_wifi_set_mac_address(uint8_t *au8MacAddress);

/*******************************************************************************
  Function:
    void m2m_wifi_set_static_ip(tstrM2MIPConfig * pstrStaticIPConf)

  Summary:
    Assign a static IP address to the WINC1500.

  Description:
    Assign a static IP address (and related parameters) to the WINC1500.  This function
    is needed if the AP does not have a DHCP server, or, a known, static IP address is
    needed.  Assigning a static IP address must be done with care to avoid a network conflict.
    If the WINC1500 detects a conflict the M2M_WIFI_IP_CONFLICT_EVENT will be generated.

  Parameters:
    pstrStaticIPConf -- Configuration data for static IP address.  See tstrM2MIPConfig.
                        All IP addresses in the structure must be in big-endian format.

  Returns:
    None
 *****************************************************************************/
void m2m_wifi_set_static_ip(tstrM2MIPConfig * pstrStaticIPConf);

#if defined(M2M_ENABLE_HTTP_PROVISION_MODE)
 /*******************************************************************************
  Function:
    void m2m_wifi_start_provision_mode(tstrM2MAPConfig *pstrM2MAPConfig, char *pcHttpServerDomainName, uint8_t bEnableHttpRedirect)

  Summary:
    Requests the WINC1500 to serve a provisioning web page.

  Description:
    The WINC1500 will start in SoftAP mode and start the HTTP Provision WEB Server.
    When the provisioning is complete the M2M_WIFI_PROVISION_INFO_EVENT is generated.

  Parameters:
    pstrM2MAPConfig         -- Configuration for the SoftAP (see tstrM2MAPConfig)

    pcHttpServerDomainName   -- Domain name of the HTTP Provision WEB server which will be used
                          to load the provisioning home page from a browser.
                          The domain name can have one of the following 3 forms as
                          shown in the examples below:
                            1: "wincprov.com"
                            2: "http://wincprov.com"
                            3: "https://wincprov.com"

                          Examples 1 and 2 above are equivalent and both will start an
                          unsecured http server.  Example 3 above will will start a
                          secure HTTP provisioning Session (HTTP over SSL connection).

                          Do not use ".local" in the HTTP server name

    bEnableHttpRedirect -- Enable or disable the HTTP redirect feature. This parameter is
                          ignored for a secure provisioning session (e.g. using "https"
                          in the prefix).

                          Possible values are:
                            0: Do not use HTTP Redirect. The associated device
                               can open the provisioning page only when the
                               HTTP Provision URL of the WINC HTTP Server is
                               correctly written on the browser.

                            1: Use HTTP Redirect. All HTTP traffic (http://URL)
                               from the associated device (Phone, PC, etc)
                               will be redirected to the WINC HTTP Provisioning
                               home page.

  Returns:
    None
 *****************************************************************************/
void m2m_wifi_start_provision_mode(tstrM2MAPConfig *pstrM2MAPConfig, char *pcHttpServerDomainName, uint8_t bEnableHttpRedirect);

/*******************************************************************************
  Function:
    void m2m_wifi_stop_provision_mode(void)

  Summary:
    Stops the HTPP provisioning mode.

  Description:
    Stops the HTPP provisioning mode that was started with m2m_wifi_start_provision_mode().

  Parameters:
    None

  Returns:
    None
 *****************************************************************************/
void m2m_wifi_stop_provision_mode(void);
#endif // M2M_ENABLE_HTTP_PROVISION_MODE

#if defined(M2M_ENABLE_SCAN_MODE)
/*******************************************************************************
  Function:
    void m2m_wifi_set_scan_options(tstrM2MScanOption* ptstrM2MScanOption)

  Summary:
    Sets the options for a Wi-Fi scan.

  Description:
    Normally this function will not be needed as the default scan options are
    sufficient.

  Parameters:
    ptstrM2MScanOption -- pointer to scan options (see tstrM2MScanOption).

  Returns:
    None
 *****************************************************************************/
void m2m_wifi_set_scan_options(tstrM2MScanOption *ptstrM2MScanOption);

/*******************************************************************************
  Function:
    void m2m_wifi_set_scan_region(uint16_t  scanRegion)

  Summary:
    Sets the scan region.

  Description:
    Setting the scan region affects the range of channels that can be scanned.  The
    default scan region is M2M_WIFI_NORTH_AMERICA_REGION.

  Parameters:
    scanRegion -- M2M_WIFI_NORTH_AMERICA_REGION or M2M_WIFI_NORTH_ASIA_REGION

  Returns:
    None
 *****************************************************************************/
void m2m_wifi_set_scan_region(uint16_t  scanRegion);

/*******************************************************************************
  Function:
    void m2m_wifi_request_scan(uint8_t ch)

  Summary:
    Requests an active scan of the specified Wi-Fi channel(s)

  Description:
    This function requests a active Wi-Fi scan operation (the WINC1500 sends out
    probe requests).  When the scan operation completes the M2M_WIFI_SCAN_DONE_EVENT
    is generated; after this event the m2m_wifi_req_scan_result() as many times as needed
    to retrieve the scan results.

    A scan cannot be requested when in P2P or SoftAP modes.  A scan can be requested
    in STA mode whether connected or disconnected.

  Parameters:
    channel -- Desired channel, or all channels.  See tenuM2mScanCh.

  Returns:
    None
 *****************************************************************************/
void m2m_wifi_request_scan(uint8_t ch);

/*******************************************************************************
  Function:
    void m2m_wifi_request_scan_passive(uint8_t channel, uint16_t scanTime)

  Summary:
    Requests an passive scan of the specified Wi-Fi channel(s)

  Description:
    This function requests a passive Wi-Fi scan operation.  The WINC1500 does not
    send out probe requests, but passively listens.  When the scan operation
    completes the M2M_WIFI_SCAN_DONE_EVENT is generated; after this event the
    m2m_wifi_req_scan_result() as many times as needed to retrieve the scan results.

    A scan cannot be requested when in P2P or SoftAP modes.  A scan can be requested
    in STA mode whether connected or disconnected.

  Parameters:
    ch        -- Desired channel, or all channels.  See tenuM2mScanCh.

    scan_time -- The time (in milliseconds) that passive scan is listening to beacons
                on each channel per one slot.  A value of 0 uses the default settings.

  Returns:
    None
 *****************************************************************************/
void m2m_wifi_request_scan_passive(uint8_t ch, uint16_t scan_time);

/*******************************************************************************
  Function:
    void m2m_wifi_req_hidden_ssid_scan(uint8_t channel, uint8_t *p_ssidList)

  Summary:
    Requests an active scan that looks for AP's not broadcasting their SSID (hidden
    SSID) in their beacons.

  Description:
    A 'normal' scan will find an AP with a hidden SSID but cannot report the actual
    SSID.  This function allows an application to specifically scan for one or more
    AP's with a hidden SSID.  When the scan completes the M2M_WIFI_SCAN_DONE_EVENT
    is generated.

  Parameters:
    channel    -- Desired channel, or all channels.  See tenuM2mScanCh.

    p_ssidList -- Pointer to an array containing the list SSID's to look for.  The
                  format of the list is:
                    list[0]   -- Total number of SSID's in the list
                    list[1]   -- Length of first SSID
                    list[2]   -- First character of first SSID
                    list[2:N] -- Remaining characters of first ID (do not include a '\0' terminator)
                    list[N+1] -- Length of second SSID
                    list[N+2] -- First character of second SSID
                    ...
    Restrictions to the list:
        * The maximum number of SSID's that can be in the list is M2M_WIFI_MAX_HIDDEN_SITES (4).
        * The maximum size of the buffer cannot exceed 133 bytes.
        * The maximum size of an SSID cannot exceed 32 bytes.
        * String terminators are NOT included in the list

    For example, presume a list of two SSID's, "AP_1", and "AP_20".  To create the list:
        uint8_t ssidList[12];

        ssidList[0] = 2;                                // Total number of SSID's in list is 2
        ssidList[1] = strlen("AP_1");                   // Length of "AP_1" in list is 4 (do not count string terminator)
        memcpy(&ssidList[2], "AP_1", strlen("AP_1"));   // Bytes index 2-5 containing the string AP_1
        ssidList[6] = strlen("AP_20");                  // Length of "AP_20" in list is 5 (do not count string terminator)
        memcpy(&ssidList[7], "AP_20", strlen("AP_20")); // Bytes index 7-11 containing the string "AP_20"

        ssidList[] now looks like:
                             Index:  0  1   2    3    4    5   6   7    8    9   10   11
                                     -  -   -    -    -    -   -   -    -    -   --   --
                             Value:  2, 4, 'A', 'P', '_', '1', 5, 'A', 'P', '_', '2', '0'

  Returns:
    None
 *****************************************************************************/
void m2m_wifi_req_hidden_ssid_scan(uint8_t channel, uint8_t *p_ssidList);

/*******************************************************************************
  Function:
    void m2m_wifi_req_scan_result(uint8_t index)

  Summary:
    Requests one result from a previous scan operation.

  Description:
    After a scan has been initiated the M2M_WIFI_SCAN_DONE_EVENT is generated when the
    scan is complete; the number of scan results are reported with this event.
    At that point this function is called to retrieve each scan result.  When the
    scan result has been retrieved the M2M_WIFI_SCAN_RESULT_EVENT is generated.

    This function must not be called prior to calling either m2m_wifi_request_scan(),
    m2m_wifi_request_scan_passive(), or m2m_wifi_req_hidden_ssid_scan() with the result of at
    least one AP being found.

  Parameters:
    index -- Index of scan result to retrieve

  Returns:
    None
 *****************************************************************************/
void m2m_wifi_req_scan_result(uint8_t index);
#endif // M2M_ENABLE_SCAN_MODE

/*******************************************************************************
  Function:
    void m2m_wifi_req_curr_rssi(void)

  Summary:
    Requests the RSSI value for the current connection.

  Description:
    Requests the RSSI value for the current connection.  When the RSSI is ready
    the M2M_WIFI_RSSI_EVENT is generated.

  Parameters:
    None

  Returns:
    None
 *****************************************************************************/
void m2m_wifi_req_curr_rssi(void);

/*******************************************************************************
  Function:
    void m2m_wifi_set_sleep_mode(uint8_t PsTyp, uint8_t BcastEn)

  Summary:
    Sets the power-save mode for WINC1500.

  Description:
    Sets the power-save mode for WINC1500.  This is one of the two power-save
    setting functions that allow an application to adjust WINC1500 power
    consumption (the other function is m2m_wifi_set_lsn_int()).

    Most of the power-save modes are performed automatically by the WINC1500.
    If PsTyp is set to M2M_WIFI_PS_MANUAL then m2m_wifi_request_sleep() is used to control
    the sleep times from the application.

    Note: This function should only be called once after initialization.

  Parameters:
    PsTyp   -- see tenuPowerSaveModes.
    BcastEn -- Range:
                0: The WINC1500 will not wake up at the DTIM interval
                   and will not receive broadcast traffic. It will still
                   wake up at the listen interval; see m2m_wifi_set_lsn_int().
                1: The WINC1500 will wake up at each DTIM interval to listen
                   for broadcast traffic.
  Returns:
    None
 *****************************************************************************/
void m2m_wifi_set_sleep_mode(uint8_t PsTyp, uint8_t BcastEn);

/*******************************************************************************
  Function:
    void m2m_wifi_set_lsn_int(tstrM2mLsnInt *pstrM2mLsnInt)

  Summary:
    Sets the Wi-Fi listen interval.

  Description:
    Sets the Wi-Fi listen interval in AP beacon intervals.  This is one of the
    two power-save setting functions that allow an application to adjust WINC1500
    power consumption (the other function is  m2m_wifi_set_sleep_mode()).

    Typically a beacon interval on an AP is 100ms, but it can vary.  The listen
    interval is the number of beacon periods the station sleeps before waking up
    to receive data the AP has buffered for it.

    Note: This function should only be called once after initialization.

  Parameters:
    listenInterval -- sleep interval, in beacon periods

  Returns:
    None
 *****************************************************************************/
void m2m_wifi_set_lsn_int(tstrM2mLsnInt *pstrM2mLsnInt);

/*******************************************************************************
  Function:
    tenuPowerSaveModes m2m_wifi_get_sleep_mode(void)

  Summary:
    Returns the current sleep mode.

  Description:
    Returns the current sleep mode.  See tenuPowerSaveModes.

  Parameters:
    None

  Returns:
    Current sleep mode.
 *****************************************************************************/
uint8_t m2m_wifi_get_sleep_mode(void);

/*******************************************************************************
  Function:
    void m2m_wifi_request_sleep(uint32_t u32SlpReqTime)

  Summary:
    Puts the WINC1500 into the current sleep mode (when mode is M2M_WIFI_PS_MANUAL).

  Description:
    Puts the WINC1500 into the current sleep mode.  This function should only be
    called if the previous call to m2m_wifi_set_sleep_mode() set the PsTyp to M2M_WIFI_PS_MANUAL.
    Essentially, this function is used for those applications that wish to control
    the WINC1500 sleep times manually as opposed to the other modes where the WINC1500
    controls the sleep times automatically.

    Note: The host driver will automatically wake up the WINC1500 when any host
          driver API function (e.g. Wi-Fi or socket) is called which requires
          communication with the WINC1500.

  Parameters:
    u32SlpReqTime -- Number of milliseconds that the sleep mode should be active.

 Returns:
    None
 *****************************************************************************/
void m2m_wifi_request_sleep(uint32_t u32SlpReqTime);

/*******************************************************************************
  Function:
    void m2m_wifi_set_tx_power(uint8_t u8TxPwrLevel)

  Summary:
    Sets the WINC1500 power profile.

  Description:
        Sets the Tx power level.  If this function is to be used, then
        it must be called after initialization and before any connection request.
        This function can only be called once after initialization.

  Parameters:
    txPowerLevel -- See tenuM2mTxPwrLevel.

 Returns:
    None
 *****************************************************************************/
void m2m_wifi_set_tx_power(uint8_t u8TxPwrLevel);

/*******************************************************************************
  Function:
    void m2m_wifi_set_power_profile(uint8_t u8PwrMode)

  Summary:
    Sets the WINC1500 power profile.

  Description:
        Sets the WINC1500 power profile.  If this function is to be used, then
        it must be called after initialization and before any connection request.
        This function can only be called once after initialization.

  Parameters:
    u8PwrMode -- See tenuM2mPwrMode.

 Returns:
    None
 *****************************************************************************/
void m2m_wifi_set_power_profile(uint8_t u8PwrMode);

/*******************************************************************************
  Function:
    void m2m_wifi_set_device_name(char *pu8DeviceName, uint8_t u8DeviceNameLength)

  Summary:
    Sets the WINC1500 device name.

  Description:
     The device name is used in (P2P) WiFi-Direct mode as well as DHCP hostname (option 12).
     For P2P devices to communicate a device name must be present. If it is not set
     through this function a default name is assigned.  The default name is
     "WINC-XX-YY", where XX and YY are the last 2 bytes of the OTP MAC address.
     If OTP (eFuse) is not programmed then zeros will be used (e.g. "WINC-00-00").
     See RFC 952 and 1123 for valid device name rules.

    Note: This function should only be called once after initialization.

  Parameters:
    pu8DeviceName -- pointer to null-terminated device name string.  Max size is
                     48 characters, including terminator.

    u8DeviceNameLength -- device name length, in bytes

  Returns:
    None
 *****************************************************************************/
void m2m_wifi_set_device_name(char *pu8DeviceName, uint8_t u8DeviceNameLength);

/*******************************************************************************
  Function:
    void m2m_wifi_enable_sntp(uint8_t bEnable)

  Summary:
    Enables or disables the Simple Network Time Protocol(SNTP) client.

  Description:
    The WINC1500 SNTP client is enabled by default and is used to set the WINC1500
    system clock to UTC time from one of the 'well-known' timer servers (e.g.
    time-c.nist.gov).  The default SNTP client updates the time once every 24 hours.
    The UTC (Coordinated Universal Time) is needed for checking the expiration
    date of X509 certificates when establishing TLS connections.  If the host has
    a real-time clock (RTC) then the SNTP could be disabled and the application
    could set the system time via m2m_wifi_set_sytem_time().

  Parameters:
    bEnable -- Set true to enable SNTP client, false to disable SNTP client.

  Returns:
    None
 *****************************************************************************/
void m2m_wifi_enable_sntp(uint8_t bEnable);

/*******************************************************************************
  Function:
    void m2m_wifi_set_sytem_time(uint32_t u32UTCSeconds)

  Summary:
    Sets the system time in UTC seconds.

  Description:
    Sets the system time in UTC seconds.  If the host MCU has a real-time clock
    then the SNTP client can be disabled (see m2m_wifi_enable_sntp()) and this function
    can be used to set the WINC1500 system time.

  Parameters:
    u32UTCSeconds -- UTC seconds (number of seconds elapsed since 00:00:00, January
                  1, 1970).

  Returns:
    None
 *****************************************************************************/
void m2m_wifi_set_sytem_time(uint32_t u32UTCSeconds);

/*******************************************************************************
  Function:
    void m2m_wifi_get_sytem_time(void)

  Summary:
    Requests the UTC time from the WINC1500

  Description:
    Issues a request to the WINC1500 for the current system time.  When the time
    is available the M2M_WIFI_SYS_TIME_EVENT is generated.

  Parameters:
    None

  Returns:
    None
 *****************************************************************************/
void m2m_wifi_get_sytem_time(void);

/*******************************************************************************
  Function:
    void m2m_wifi_enable_firmware_log(uint8_t enable)

  Summary:
    Enables/Disables WINC1500 UART debug output.

  Description:
    The WINC1500 UART debug output is on by default.  The M2M_DISABLE_FIRMWARE_LOG
    define is used to disable the debug output during host initialization.
    This function can be used to dynamically enable or disable WINC1500 debug output
    during runtime.

  Parameters:
    u8Enable -- 0 to disable, 1 to enable

  Returns:
    None
 *****************************************************************************/
void m2m_wifi_enable_firmware_log(uint8_t u8Enable);

/*******************************************************************************
  Function:
    void m2m_wifi_set_battery_voltage(uint16_t batVoltx100)

  Summary:
    Sets battery voltage to update the firmware calculations.

  Description:
    Sets battery voltage to update the firmware calculations.

  Parameters:
    batVoltx100 -- Battery voltage * 100

  Returns:
    None
 *****************************************************************************/
void m2m_wifi_set_battery_voltage(uint16_t batVoltx100);


void  m2m_wifi_deinit(void * arg);

/*******************************************************************************
  Function:
    void m2m_wifi_send_crl(tstrTlsCrlInfo *p_crl)

  Summary:
    Notifies the WINC1500 with the Certificate Revocation List.  Used with TLS.

  Description:
    Notifies the WINC1500 with the Certificate Revocation List.  Used with TLS.

  Parameters:
    p_crl -- see tstrTlsCrlInfo.

  Returns:
    None
 *****************************************************************************/
void m2m_wifi_send_crl(tstrTlsCrlInfo *pCRL);


//==============================================================================
//                      EVENT FUNCTIONS
//==============================================================================
t_wifiEventData *       m2m_wifi_get_wifi_event_data(void);
t_socketEventData *     m2m_wifi_get_socket_event_data(void);
t_m2mOtaEventData *        m2m_wifi_get_ota_event_data(void);

//==============================================================================
//                      UTILITY FUNCTIONS
//==============================================================================
void inet_ntop4(uint32_t src, char *dest);
int  inet_pton4(const char *src, uint32_t *dest);
uint32_t m2m_get_elapsed_time(uint32_t startTime);

#if defined(M2M_ENABLE_PRNG)
/*******************************************************************************
  Function:
    void m2m_wifi_prng_get_random_bytes(uint16_t size)

  Summary:
    Requests the WINC1500 to generate psuedo-random bytes values.

  Description:
    Issues a request to the WINC1500 to generate one or more psuedo-random byte
    values.  When the bytes are ready the M2M_WIFI_PRNG_EVENT is generated.

  Parameters:
    size -- Number of psuedo-random bytes to generate.  Must be between 1 and
            M2M_MAX_PRNG_BYTES (16).

  Returns:
    None
 *****************************************************************************/
void m2m_wifi_prng_get_random_bytes(uint16_t size);
#endif // M2M_ENABLE_PRNG

#if defined(M2M_ENABLE_SPI_FLASH)
void nm_drv_init_download_mode(void);
#endif

//==============================================================================
//                      WINC1500 STUB FUNCTIONS
//==============================================================================
/*******************************************************************************
  Function:
    void m2mStub_PinSet_CE(t_m2mWifiPinAction action)

  Summary:
    Sets WINC1500 CHIP_EN pin high or low.

  Description:
    The WINC1500 driver will call this function to set the WINC1500 CHIP_EN pin
    high or low.  This is a host output GPIO.

  Parameters:
    action -- M2M_WIFI_PIN_LOW or M2M_WIFI_PIN_HIGH

  Returns:
    None
 *****************************************************************************/
void m2mStub_PinSet_CE(t_m2mWifiPinAction action);

/*******************************************************************************
  Function:
    void m2mStub_PinSet_RESET(t_m2mWifiPinAction action)

  Summary:
    Sets WINC1500 RESET_N pin high or low.

  Description:
    The WINC1500 driver will call this function to set the WINC1500 RESET_N pin
    high or low.  This is a host output GPIO.

  Parameters:
    action -- M2M_WIFI_PIN_LOW or M2M_WIFI_PIN_HIGH

  Returns:
    None
 *****************************************************************************/
void m2mStub_PinSet_RESET(t_m2mWifiPinAction action);

/*******************************************************************************
  Function:
    void m2mStub_PinSet_SPI_SS(t_m2mWifiPinAction action)

  Summary:
    Sets WINC1500 SPI_SSN pin high or low.

  Description:
    The WINC1500 driver will call this function to set the WINC1500 SPI_SSN pin
    high or low.  This is a host output GPIO.

  Parameters:
    action -- M2M_WIFI_PIN_LOW or M2M_WIFI_PIN_HIGH

  Returns:
    None
 *****************************************************************************/
void m2mStub_PinSet_SPI_SS(t_m2mWifiPinAction action);

/*******************************************************************************
  Function:
    uint32_t m2mStub_GetOneMsTimer(void)

  Summary:
    Reads 1 millisecond counter value

  Description:
    The WINC1500 driver will call this function to read the 1ms counter.

  Parameters:
    None

  Returns:
    One-millisecond counter value
 *****************************************************************************/
uint32_t m2mStub_GetOneMsTimer(void);

/*******************************************************************************
  Function:
    void m2mStub_EintEnable(void)

  Summary:
    Enables the WINC1500 interrupt

  Description:
    The WINC1500 driver will call this function to enable the WINC1500 interrupt.
    When the interrupt is initially configured it should be in a disabled state.
    The interrupt handler should call m2m_EintHandler() and clear the interrupt.

  Parameters:
    None

  Returns:
    None
 *****************************************************************************/
void m2mStub_EintEnable(void);

/*******************************************************************************
  Function:
    void m2mStub_EintDisable(void)

  Summary:
    Disables the WINC1500 interrupt

  Description:
    The WINC1500 driver will call this function to disable the WINC1500 interrupt.

  Parameters:
    None

  Returns:
    None
 *****************************************************************************/
void m2mStub_EintDisable(void);

/*******************************************************************************
  Function:
    void m2m_EintHandler(void)

  Summary:
    WINC1500 interrupt handler

  Description:
    This function must be called by the WINC1500 interrupt handler.  The interrupt
    performs no other action other than clearing the interrupt.  This function should
    never be called directly by the application -- it should only be called from
    the interrupt handler.

  Parameters:
    None

  Returns:
    None
 *****************************************************************************/
void m2m_EintHandler(void);

/*******************************************************************************
  Function:
    void m2mStub_SpiTxRx(uint8_t *p_txBuf, uint16_t txLen, uint8_t *p_rxBuf, uint16_t rxLen)

  Summary:
    Writes and reads bytes from the WINC1500 via the SPI interface

  Description:
    If txLen > rxLen then:
        Throw away the extra read bytes.  Do NOT write the garbage read bytes to p_rxBuf

    If rxLen is > txLen then:
        Write out filler bytes of 0x00 in order to get all the read bytes

  Parameters:
    p_txBuf -- Pointer to tx data (data being clocked out to the WINC1500).
               This will be NULL if txLen is 0.
    txLen   -- Number of Tx bytes to clock out.  This will be 0 if only a read is
               occurring.
    p_rxBuf -- Pointer to rx data (data being clocked in from the WINC1500).
               This will be NULL if rxLen is 0.
    rxLen   -- Number of bytes to read.  This will be 0 if only a write is occurring.

  Returns:
    None
 *****************************************************************************/
void m2mStub_SpiTxRx(uint8_t *p_txBuf, uint16_t txLen, uint8_t *p_rxBuf, uint16_t rxLen);

/*******************************************************************************
  Function:
    void m2m_wifi_handle_events(t_m2mWifiEventType eventCode, t_wifiEventData *p_eventData)

  Summary:
    Called by the WINC1500 driver to notify the application of a Wi-Fi event.

  Description:
    Called by the WINC1500 driver to notify the application of a Wi-Fi event.

  Parameters:
    eventCode    -- type of Wi-Fi event.  See t_m2mWifiEventType
    p_eventData  -- pointer to a union of all Wi-Fi events.  See t_wifiEventData.

  Returns:
    None
 *****************************************************************************/
void m2m_wifi_handle_events(t_m2mWifiEventType eventCode, t_wifiEventData *p_eventData);

/*******************************************************************************
  Function:
    void m2m_socket_handle_events(SOCKET sock, t_m2mSocketEventType eventCode, t_socketEventData *p_eventData)

  Summary:
    Called by the WINC1500 driver to notify the application of a socket event.

  Description:
    Called by the WINC1500 driver to notify the application of a socket event.

  Parameters:
    sock         -- socket ID the event is associated with
    eventCode    -- type of socket event.  See t_m2mSocketEventType
    p_eventData  -- pointer to a union of all socket events.  See t_socketEventData.

  Returns:
    None
 *****************************************************************************/
void m2m_socket_handle_events(SOCKET sock, t_m2mSocketEventType eventCode, t_socketEventData *p_eventData);

/*******************************************************************************
  Function:
    void m2m_ota_handle_events(t_m2mOtaEventType eventCode, t_m2mOtaEventData *p_eventData)

  Summary:
    Called by the WINC1500 driver to notify the application of an OTA event.

  Description:
    Called by the WINC1500 driver to notify the application of a OTA event.

  Parameters:
    eventCode    -- type of OTA event.  See t_m2mOtaEventType
    p_eventData  -- pointer to a union of all socket events.  See t_m2mOtaEventData.

  Returns:
    None
 *****************************************************************************/
void m2m_ota_handle_events(t_m2mOtaEventType eventCode, t_m2mOtaEventData *p_eventData);

/*******************************************************************************
  Function:
    void m2m_error_handle_events(uint32_t errorCode)

  Summary:
    Called by the WINC1500 driver to notify the application of an error event.

  Description:
    Called by the WINC1500 driver to notify the application of an error event.

  Parameters:
    errorCode  -- type of OTA event.  See t_m2mWifiErrorCodes.

  Returns:
    None
 *****************************************************************************/
void m2m_error_handle_events(uint32_t errorCode);
//
// The following method should be called from a 1mS timer ISR
//
void m2m_TMR_ISR(void);
	     
#if defined(M2M_ENABLE_SPI_FLASH)
// Functions for firmware update utility
void    m2m_wifi_console_write_data(uint16_t length, uint8_t *p_buf);
uint8_t m2m_wifi_console_read_data(void);
bool    m2m_wifi_console_is_read_data(void);
#endif // M2M_ENABLE_SPI_FLASH

#ifdef __cplusplus
}
#endif

#endif // __WINC1500_API_H
