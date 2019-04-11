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

#ifndef __WF_DRV_H
#define __WF_DRV_H

#ifdef __cplusplus
     extern "C" {
 #endif

/*DOM-IGNORE-BEGIN*/  
         
//============================================================================
// CONSTANTS
//============================================================================

 // Base values for Wi-Fi Command groups
#define CONFIG_CMD_BASE             1       // Base value of all the host configuration commands opcodes
#define STA_CMD_BASE                40      // Base value of all the station mode host commands opcodes
#define AP_CMD_BASE                 70      // Base value of all the Access Point mode host commands opcodes
#define P2P_CMD_BASE                90      // Base value of all the P2P mode host commands opcodes
#define SERVER_CMD_BASE             100     // [not currently supported] Base value of all the power save mode host commands codes
         
// OTA Definitions
#define OTA_STATUS_VALID            (0x12526285)        // Magic value updated in the Control structure in case of ROLLACK image valid
#define OTA_STATUS_INVALID          (0x23987718)        // Magic value updated in the Control structure in case of ROLLACK image invalid
#define OTA_MAGIC_VALUE             (0x1ABCDEF9)        // Magic value set at the beginning of the OTA image header
#define OTA_FORMAT_VER_0            (0)                 // Until 19.2.2 format
#define OTA_FORMAT_VER_1            (1)                 // Starting from 19.3.0 CRC is used and sequence number is used
#define OTA_SHA256_DIGEST_SIZE      (32)                // Sha256 digest size in the OTA image, the sha256 
                                                        //  digest is set at the beginning of image before the OTA header 
#define M2M_MAGIC_APP               (0xef522f61UL)      // Magic value set at the beginning of the Cortus OTA image header
         
// Base value for OTA Command group
//  The OTA have a special group so can extended from 1-MAX_GRP_NUM_REQ         
#define M2M_OTA_CMD_BASE            100         

// Base value for Crypto Command group
//  The crypto have a special group so can extended from 1-MAX_GRP_NUM_REQ         
#define M2M_CRYPTO_CMD_BASE                                    1
     
         
#define MAX_GRP_NUM_REQ             (127)   // max number of request in one group equal to 127 
                                            //  as the last bit reserved for config or data pkt

#define M2M_ETHERNET_HDR_OFFSET         34      // Offset of the Ethernet header within the WLAN Tx Buffer
#define M2M_ETHERNET_HDR_LEN            14      // Length of the Etherenet header in bytes

#define M2M_BUFFER_MAX_SIZE      (1600UL - 4)    // Maximum size for the shared packet buffer
#define M2M_MAC_ADDRES_LEN             6

#define MAJOR_SHIFT                 (8)
#define MINOR_SHIFT                 (4)
#define PATCH_SHIFT                 (0)

#define DRIVER_VERSION_SHIFT        (16)
#define FIRMWARE_VERSION_SHIFT      (0)


#define REL_19_5_2_VER            MAKE_VERSION_INFO(19,5,2,19,3,0)
#define REL_19_5_1_VER            MAKE_VERSION_INFO(19,5,1,19,3,0)
#define REL_19_5_0_VER            MAKE_VERSION_INFO(19,5,0,19,3,0)
#define REL_19_4_6_VER            MAKE_VERSION_INFO(19,4,6,19,3,0)
#define REL_19_4_5_VER            MAKE_VERSION_INFO(19,4,5,19,3,0)
#define REL_19_4_4_VER            MAKE_VERSION_INFO(19,4,4,19,3,0)
#define REL_19_4_3_VER            MAKE_VERSION_INFO(19,4,3,19,3,0)
#define REL_19_4_2_VER            MAKE_VERSION_INFO(19,4,2,19,3,0)
#define REL_19_4_1_VER            MAKE_VERSION_INFO(19,4,1,19,3,0)
#define REL_19_4_0_VER            MAKE_VERSION_INFO(19,4,0,19,3,0)
#define REL_19_3_1_VER            MAKE_VERSION_INFO(19,3,1,19,3,0)
#define REL_19_3_0_VER            MAKE_VERSION_INFO(19,3,0,19,3,0)
#define REL_19_2_2_VER            MAKE_VERSION_INFO(19,2,2,19,2,0)
#define REL_19_2_1_VER            MAKE_VERSION_INFO(19,2,1,19,2,0)
#define REL_19_2_0_VER            MAKE_VERSION_INFO(19,2,0,19,2,0)
#define REL_19_1_0_VER            MAKE_VERSION_INFO(19,1,0,18,2,0)
#define REL_19_0_0_VER            MAKE_VERSION_INFO(19,0,0,18,1,1)

//------------------------
// Firmware version number
//------------------------         
#define FIRMWARE_RELEASE_VERSION_MAJOR_NO   (19)
#define FIRMWARE_RELEASE_VERSION_MINOR_NO   (5)
#define FIRMWARE_RELEASE_VERSION_PATCH_NO   (2)

         
#if !defined(FIRMWARE_RELEASE_VERSION_MAJOR_NO) || !defined(FIRMWARE_RELEASE_VERSION_MINOR_NO)
#error Undefined version number
#endif

//==============================================================================
// MACROS
//==============================================================================
#define GET_MAJOR(ver_info_hword)           ((uint8_t)((ver_info_hword) >> MAJOR_SHIFT) & 0xff)
#define GET_MINOR(ver_info_hword)           ((uint8_t)((ver_info_hword) >> MINOR_SHIFT) & 0x0f)
#define GET_PATCH(ver_info_hword)           ((uint8_t)((ver_info_hword) >> PATCH_SHIFT) & 0x0f)

#define GET_FIRMWARE_VERSION(ver_info_word) ((uint16_t) ((ver_info_word) >> FIRMWARE_VERSION_SHIFT))
#define GET_DRIVER_VERSION(ver_info_word)   ((uint16_t) ((ver_info_word) >> DRIVER_VERSION_SHIFT))

#define GET_DRIVER_MAJOR(ver_info_word)     GET_MAJOR(GET_DRIVER_VERSION(ver_info_word))
#define GET_DRIVER_MINOR(ver_info_word)     GET_MINOR(GET_DRIVER_VERSION(ver_info_word))
#define GET_DRIVER_PATCH(ver_info_word)     GET_PATCH(GET_DRIVER_VERSION(ver_info_word))

#define GET_FIRMWARE_MAJOR(ver_info_word)   GET_MAJOR(GET_FIRMWARE_VERSION(ver_info_word))
#define GET_FIRMWARE_MINOR(ver_info_word)   GET_MINOR(GET_FIRMWARE_VERSION(ver_info_word))
#define GET_FIRMWARE_PATCH(ver_info_word)   GET_PATCH(GET_FIRMWARE_VERSION(ver_info_word))

#define MAKE_VERSION(major, minor, patch) ( \
    ((uint16_t)((major)  & 0xff)  << MAJOR_SHIFT) | \
    ((uint16_t)((minor)  & 0x0f)  << MINOR_SHIFT) | \
    ((uint16_t)((patch)  & 0x0f)  << PATCH_SHIFT))

#define MAKE_VERSION_INFO(fw_major, fw_minor, fw_patch, drv_major, drv_minor, drv_patch)          \
    (                                                                                              \
    ( ((uint32_t)MAKE_VERSION((fw_major),  (fw_minor),  (fw_patch)))  << FIRMWARE_VERSION_SHIFT) | \
    ( ((uint32_t)MAKE_VERSION((drv_major), (drv_minor), (drv_patch))) << DRIVER_VERSION_SHIFT))
         
typedef enum
{
    REQ_GROUP_MAIN = 0,
    M2M_REQ_GROUP_WIFI,
    REQ_GROUP_IP,
    REQ_GROUP_HIF,
    REQ_GROUP_OTA,
    REQ_GROUP_SSL,
    REQ_GROUP_CRYPTO,
    REQ_GROUP_SIGMA
} t_group;
         
// Soft AP mode commands
typedef enum 
{
    AP_REQ_ENABLE_AP = AP_CMD_BASE,         // Enable AP mode
    AP_REQ_DISABLE_AP,                      // Disable AP mode
    AP_REQ_RESTART_AP,
    AP_MAX_AP_ALL
} t_softApCmd;
 
// Host commands to configure WINC module
typedef enum 
{
    CFG_REQ_RESTART = CONFIG_CMD_BASE,      // Restart the WINC MAC layer, it's doesn't restart the IP layer
    CFG_REQ_SET_MAC_ADDRESS,                // Set the WINC mac address (not possible for production effused boards).
    CFG_REQ_CURRENT_RSSI,                   // Request the current connected AP RSSI
    CFG_RSSI_EVENT,                         // Response to CFG_REQ_CURRENT_RSSI with the RSSI value
    CFG_REQ_GET_CONN_INFO,                  // Request connection information command
    CFG_CONN_INFO_RESPONSE_EVENT,           // Response to CFG_REQ_GET_CONN_INFO
    M2M_WIFI_REQ_SET_DEVICE_NAME,                // Set the WINC device name 
    CFG_REQ_START_PROVISION_MODE,           // Start the provisioning mode for the M2M Device
    CFG_PROVISION_INFO_EVENT,               // Response to CFG_REQ_START_PROVISION_MODE 
    CFG_REQ_STOP_PROVISION_MODE,            // Stop the current running provision mode
    CFG_REQ_SET_SYS_TIME,                   // Set time/date
    CFG_REQ_ENABLE_SNTP_CLIENT,             // Enable SNTP client to get the timer from SNTP server.
    CFG_REQ_DISABLE_SNTP_CLIENT,            // Disable SNTP client
    CFG_RESP_MEMORY_RECOVER,                // Reserved for debugging
    CFG_REQ_CUST_INFO_ELEMENT,              // Add Custom ELement to Beacon Managament Frame
    CFG_REQ_SCAN,                           // Request scan command
    CFG_SCAN_DONE_EVENT,                    // Scan complete notification response
    CFG_REQ_SCAN_RESULT,                    // Request Scan results command
    CFG_SCAN_RESULT_EVENT,                  // Response to CFG_REQ_SCAN_RESULT 
    CFG_REQ_SET_SCAN_OPTION,                // Set Scan options "slot time, slot number .. etc"
    CFG_REQ_SET_SCAN_REGION,                // Set scan region
    CFG_REQ_SET_POWER_PROFILE,              // Set WINC1500 power mode
    CFG_REQ_SET_TX_POWER,                   // Set WINC1500 Tx power level
    CFG_REQ_SET_BATTERY_VOLTAGE,            // Set battery voltage
    CFG_REQ_SET_ENABLE_LOGS,
    CFG_REQ_GET_SYS_TIME,                   // Request time from WINC1500
    CFG_SYS_TIME_EVENT,                     // Response to CFG_REQ_GET_SYS_TIME
    CFG_REQ_SEND_ETHERNET_PACKET,           // Send Ethernet packet in bypass mode
    CFG_EVENT_ETHERNET_RX_PACKET,           // Ethernet packet received in bypass mode
    CFG_REQ_SET_MAC_MCAST,                  // Set the WINC multicast filters in bypass mode
    CFG_REQ_GET_PRNG,                       // Request random numbers
    CFG_PRNG_EVENT,                         // Response to CFG_REQ_GET_PRNG
    CFG_REQ_SCAN_SSID_LIST,                 // Request scan with list of hidden SSID plus the broadcast scan          
    CFG_REQ_SET_GAINS,                      // Request set the PPA gain
    CFG_REQ_PASSIVE_SCAN,                   // Request a passive scan      
    CFG_MAX_CONFIG_ALL
} t_configCmd;

// Host commands while in Station mode
typedef enum 
{
    STA_REQ_CONNECT = STA_CMD_BASE,         // Connect with AP
    STA_REQ_DEFAULT_CONNECT,                // Connect with default AP
    STA_DEFAULT_CONNNECT_EVENT,             // Response to connection
    STA_REQ_DISCONNECT,                     // Request to disconnect from AP
    STA_CONN_STATE_CHANGED_EVENT,           // Connection changed response
    STA_REQ_SLEEP,                          // Set PS mode                      
    STA_REQ_WPS_SCAN,                       // Request WPS scan
    STA_REQ_WPS,                            // Request WPS start
    STA_REQ_START_WPS,                      // his command is for internal use by the WINC and
                                            //  should not be used by the host driver
    STA_REQ_DISABLE_WPS,                    // Disable WPS
    STA_IP_ADDRESS_ASSIGNED_EVENT,          // Response indicating that IP address was obtained
    STA_WIFI_IP_CONFIGURED_EVENT,           // This command is for internal use by the WINC and
                                            //  should not be used by the host driver
    STA_IP_CONFLICT_EVENT,                  // Response indicating a conflict in obtained IP address;
                                            //  the user should re-attempt the DHCP request
    STA_REQ_ENABLE_MONITORING,              // (not used)Enable monitor mode
    STA_REQ_DISABLE_MONITORING,             // (not used) Disable monitor mode
    STA_WIFI_WIFI_RX_PACKET_EVENT,          // (not used) Indicates a packet was received in monitor mode
    STA_REQ_SEND_WIFI_PACKET,               // (not used)Send packet in monitor mode
    STA_REQ_LSN_INTERVAL,                   // Set Wi-Fi listen interval
    STA_REQ_DOZE,                           // Force the WINC to sleep in manual PS mode
    STA_MAX_STA_ALL                        
} t_stationModeCmd;


// P2P commands
typedef enum 
{
    P2P_REQ_P2P_INTERNAL_CONNECT = P2P_CMD_BASE,    // This command is for internal use by the WINC and
                                                    //  should not be used by the host driver
    P2P_REQ_ENABLE,                                 // Enable P2P mode
    P2P_REQ_DISABLE,                                // Disable P2P mode
    P2P_REQ_REPOST,                                 // This command is for internal use by the WINC and
                                                    //  should not be used by the host driver.
    P2P_MAX_P2P_ALL
} t_p2pCmd;

// Commands while in PS mode, not current supported.
typedef enum 
{
    SERVER_REQ_CLIENT_CTRL = SERVER_CMD_BASE,
    SERVER_RESP_CLIENT_INFO,
    SERVER_REQ_SERVER_INIT,
    SERVER_MAX_SERVER_ALL
} t_serverCmd;

// OTA Commands
typedef enum 
{
    OTA_REQ_NOTIF_SET_URL = M2M_OTA_CMD_BASE,
    OTA_REQ_NOTIF_CHECK_FOR_UPDATE,
    OTA_REQ_NOTIF_SCHED,
    OTA_REQ_START_FW_UPDATE,
    OTA_REQ_SWITCH_FIRMWARE,
    OTA_REQ_ROLLBACK_FW,
    OTA_RESP_NOTIF_UPDATE_INFO,
    OTA_RESP_UPDATE_STATUS,
    OTA_REQ_TEST,
    OTA_REQ_START_CRT_UPDATE,
    OTA_REQ_SWITCH_CRT_IMG,
    OTA_REQ_ROLLBACK_CRT,
    OTA_REQ_ABORT,            
    OTA_MAX_ALL
} t_otaCmd;

// Crypto Commands
typedef enum 
{
    CRYPTO_REQ_SHA256_INIT = M2M_CRYPTO_CMD_BASE,
    CRYPTO_RESP_SHA256_INIT,
    CRYPTO_REQ_SHA256_UPDATE,
    CRYPTO_RESP_SHA256_UPDATE,
    CRYPTO_REQ_SHA256_FINISH,
    CRYPTO_RESP_SHA256_FINISH,
    CRYPTO_REQ_RSA_SIGN_GEN,
    CRYPTO_RESP_RSA_SIGN_GEN,
    CRYPTO_REQ_RSA_SIGN_VERIFY,
    CRYPTO_RESP_RSA_SIGN_VERIFY,
    CRYPTO_MAX_ALL
} t_cryptoCmd;

typedef enum 
{
    /* Request IDs corresponding to the IP GROUP. */
    IP_REQ_STATIC_IP_CONF = ((uint8_t) 10),
    IP_REQ_ENABLE_DHCP,
    IP_REQ_DISABLE_DHCP
} t_ipCmd;

         
typedef enum
{
    FIRMWARE_VERSION_ACTIVE = 0,    // get the firmware version currently running
    FIRMWARE_VERSION_OTA     = 1    // get the firmware version of the OTA update
} t_versionNumberType;
   
typedef enum
{
    REQ_CONFIG_PKT,
    REQ_DATA_PKT = 0x80 // BIT7
} t_packetType;

//============================================================================
// DATA TYPES
//============================================================================
#if 0
// OTA Image Header 
typedef struct
{
    uint32_t u32OtaMagicValue;     // Magic value kept in the OTA image after the 
                                //  sha256 Digest buffer to define the Start of OTA Header 
    uint32_t otaPayloadSzie;    // Total OTA image payload size, include the sha256 key size
} t_otaInitHeader;
#endif

// Control section structure is used to define the working image and 
// the validity of the roll-back image and its offset, also both firmware versions is 
// kept in that structure.
typedef struct 
{
    uint32_t u32OtaMagicValue;         // Magic value used to ensure the structure is valid or not 
    uint32_t u32OtaFormatVersion;      // NA   NA   NA   Flash version   cs struct version
                                    // 00   00   00   00              00 
                                    // Control structure format version, the value will be incremented in 
                                    // case of structure changed or updated
    uint32_t u32OtaSequenceNumber;     // Sequence number is used while update the control structure to keep track of how many times that section updated 
    uint32_t u32OtaLastCheckTime;      // Last time OTA check for update
    uint32_t u32OtaCurrentworkingImagOffset;       // Current working offset in flash 
    uint32_t u32OtaCurrentworkingImagFirmwareVer;  // current working image version ex 18.0.1
    uint32_t u32OtaRollbackImageOffset;            // Roll-back image offset in flash 
    uint32_t u32OtaRollbackImageValidStatus;       // roll-back image valid status 
    uint32_t u32OtaRollbackImagFirmwareVer;        // Roll-back image version (ex 18.0.3)
    uint32_t u32OtaCortusAppWorkingOffset;         // cortus app working offset in flash 
    uint32_t u32OtaCortusAppWorkingValidSts;       // Working Cortus app valid status 
    uint32_t u32OtaCortusAppWorkingVer;            // Working cortus app version (ex 18.0.3)
    uint32_t u32OtaCortusAppRollbackOffset;        //cortus app rollback offset in flash 
    uint32_t u32OtaCortusAppRollbackValidSts;      // roll-back cortus app valid status 
    uint32_t u32OtaCortusAppRollbackVer;           // Roll-back cortus app version (ex 18.0.3)
    uint32_t u32OtaControlSecCrc;                  // CRC for the control structure to ensure validity 
} tstrOtaControlSec;

typedef struct 
{
    uint8_t    u8PwrMode;  // power Save Mode (see tenuM2mPwrMode)
    uint8_t    padding[3];
} tstrM2mPwrMode;

typedef struct 
{
    uint8_t    u8TxPwrLevel; // see tenuM2mTxPwrLevel
    uint8_t    padding[3];
} tstrM2mTxPwrLevel;

typedef struct 
{
    uint8_t    u8Enable;      // Enable/Disable firmware logs
    uint8_t   padding[3];
} tstrM2mEnableLogs;

#define CONNECTION_INFO_PAD_SIZE    (4 - ((sizeof(tstrM2MWifiSecInfo) + M2M_MAX_SSID_LEN + 3) % 4))
typedef struct
{
    tstrM2MWifiSecInfo    securityInfo;           // Security parameters for authenticating with the AP
    uint16_t              channel;                // RF Channel for the target SSID
    uint8_t               ssid[M2M_MAX_SSID_LEN];  // SSID of the desired AP. It must be NULL terminated string
    uint8_t               noSaveCred;
    uint8_t               padding[CONNECTION_INFO_PAD_SIZE];
} t_connectConfig;

typedef struct 
{
    uint8_t     u8ListenChannel;          // P2P Listen Channel (1, 6 or 11)
    uint8_t     padding[3];
} tstrM2MP2PConnect;

// Sets the MAC address from application. The WINC load the mac address from the effuse by default to the WINC configuration memory, 
// but that function is used to let the application overwrite the configuration memory with the mac address from the host.
// It's recommended to call this only once before calling connect request and after the m2m_wifi_init
typedef struct 
{
    uint8_t  au8Mac[6];          // MAC address
    uint8_t  padding[2];
} tstrM2mSetMacAddress;

// It is assigned by the application. It is used mainly for Wi-Fi Direct device
// discovery and WPS device information.
typedef struct 
{
    char  au8DeviceName[M2M_DEVICE_NAME_MAX]; // NULL-terminated device name
} tstrM2MDeviceNameConfig;

// Provisioning Mode Configuration
typedef struct 
{
    tstrM2MAPConfig    strApConfig;                  // Configuration parameters for the WiFi AP
    char               acHttpServerDomainName[64];   // The device domain name for HTTP provisioning
    uint8_t            u8EnableRedirect;             // A flag to enable/disable HTTP redirect feature for 
                                                     // the HTTP Provisioning server. If the Redirect is enabled,
                                                     // all HTTP traffic (http://URL) from the device associated 
                                                     // with WINC AP will be redirected to the HTTP Provisioning 
                                                     // Web page.
                                                     //   - 0 : Disable HTTP Redirect        
                                                     //   - 1 : Enable HTTP Redirect.
    uint8_t         padding[3];
} tstrM2MProvisionModeConfig;


//============================================================================
// FUNCTION PROTOTYPES
//============================================================================
void nm_get_firmware_full_info(tstrM2mRev* p_rev, t_versionNumberType versionType);
void nm_drv_init_download_mode(void);
int8_t nm_drv_deinit(void);
void WifiInternalEventHandler(uint8_t opCode, uint16_t dataSize, uint32_t address);
void SocketInternalEventHandler(uint8_t opCode, uint16_t bufferSize,uint32_t address);
void OtaInternalEventHandler(uint8_t opCode, uint16_t dataSize, uint32_t address);
void SocketInit(void);


/*DOM-IGNORE-END*/  

#ifdef __cplusplus
    }
 #endif

#endif // __WF_DRV_H


