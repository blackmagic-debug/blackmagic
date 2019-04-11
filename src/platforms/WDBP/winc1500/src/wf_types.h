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

#ifndef __M2M_WIFI_TYPES_H__
#define __M2M_WIFI_TYPES_H__

#ifdef __cplusplus
     extern "C" {
#endif

//==============================================================================
// CONSTANTS
//==============================================================================
#define M2M_MAX_SSID_LEN            33      // Maximum size for the Wi-Fi SSID including the NULL termination
#define M2M_WIFI_MIN_PSK_LEN        9       // Minimum size for the WPA PSK including the NULL termination  
#define M2M_MAX_PSK_LEN             65      // Maximum size for the WPA PSK including the NULL termination
#define M2M_DEVICE_NAME_MAX         48      // Maximum Size for the device name including the NULL termination
#define M2M_WIFI_MAX_HIDDEN_SITES   4       // Max number of hidden SSID's supported by scan request
#define M2M_1X_USR_NAME_MAX         21      // The maximum size of the user name including the NULL termination.
                                            //  It is used for RADIUS authentication in case of connecting the device to
                                            //  an AP secured with WPA-Enterprise.
#define M2M_WIFI_PASSWORD_1X_MAX    41      // The maximum size of the password including the NULL termination.
                                            //  It is used for RADIUS authentication in case of connecting the device to
                                            //  an AP secured with WPA-Enterprise.
#define M2M_WIFI_CUST_IE_LEN_MAX    252     // Maximum size of IE (Information Element)

#define M2M_WIFI_PWR_DEFAULT        PWR_HIGH 

#define M2M_WIFI_WEP_40_KEY_STRING_SIZE      ((uint8_t)10)   // WEP key size in bytes for 40 bit string passphrase
#define WEP_104_KEY_STRING_SIZE     ((uint8_t)26)   // WEP key size in bytes for 104 bit string passphrase
#define M2M_WIFI_WEP_KEY_MAX_INDEX           ((uint8_t)4)    // Max key index value for WEP authentication
#define M2M_WIFI_SHA256_CONTEXT_BUF_LEN      (128)           // sha256 context size
#define M2M_WIFI_SCAN_DEFAULT_NUM_SLOTS      (2)             // Default number of scan slots performed by the WINC board
#define M2M_WIFI_SCAN_DEFAULT_SLOT_TIME      (30)            // Default duration in miliseconds of a scan slots performed by the WINC board
#define M2M_WIFI_SCAN_DEFAULT_NUM_PROBE      (2)             // Default number of scan slots performed by the WINC board

//----------------         
// SSL Definitions
//----------------         

#define M2M_WIFI_TLS_CRL_DATA_MAX_LEN      64  // Maximum data length in a CRL entry (= Hash length for SHA512)
#define M2M_WIFI_TLS_CRL_MAX_ENTRIES       10  // Maximum number of entries in a CRL

typedef enum
{
    M2M_TLS_CRL_TYPE_NONE            = 0,  // No CRL check
    M2M_TLS_CRL_TYPE_CERT_HASH       = 1   // CRL contains certificate hashes
} t_m2mCrlType;
         
        
// Default Connection Error Definitions
typedef enum 
{         
    M2M_WIFI_DEFAULT_CONN_IN_PROGRESS = ((int8_t)-23),   // default connection or forced connection is in progress        
    M2M_DEFAULT_CONN_FAIL,                          // WINC1500 failed to connect to the cached network
    M2M_DEFAULT_CONN_SCAN_MISMATCH,                 // none of cached networks found in scan result                                        
    M2M_DEFAULT_CONN_EMPTY_LIST                     // connection list is empty    
} tenuM2mDefaultConnErrcode;

//----------------
// TLS Definitions
//----------------
// Maximum length for each TLS certificate file name including null terminator.
#define TLS_FILE_NAME_MAX								48

// Maximum number of certificates allowed in TLS_SRV section.
#define TLS_SRV_SEC_MAX_FILES							8

// Length of certificate struct start pattern.
#define TLS_SRV_SEC_START_PATTERN_LEN					8


#if 0
typedef enum
{
    OTA_SUCCESS                     = 0,            // OTA successful
    OTA_ERR_WORKING_IMAGE_LOAD_FAIL = ((int8_t)-1), // Failure to load the firmware image
    OTA_ERR_INVAILD_CONTROL_SEC     = ((int8_t)-2), // Control structure is being corrupted  
    OTA_ERR_SWITCH_FAIL             = ((int8_t)-3), // Switching to the updated image failed as may be the image is invalid  
    OTA_ERR_START_UPDATE_FAIL       = ((int8_t)-4), // OTA update fail due to multiple reasons 
                                                    //   - Connection failure
                                                    //   - Image integrity fail  
    OTA_ERR_ROLLBACK_FAIL           = ((int8_t)-5), // Roll-back failed due to Roll-back image is not valid 
    OTA_ERR_INVAILD_FLASH_SIZE      = ((int8_t)-6), // Current FLASH is less than 4MB
    OTA_ERR_INVAILD_ARG             = ((int8_t)-7), // Invalid argument in any OTA Function  
    OTA_ERR_OTA_IN_PROGRESS         = ((int8_t)-8)  // OTA still in progress       
} t_otaErrorCode;
#endif

// see tstrM2mWifiStateChanged
typedef enum 
{
    M2M_ERR_SCAN_FAIL = ((uint8_t)1), // Failed to perform the scan operation.
    M2M_ERR_JOIN_FAIL,                // Failed to join the BSS
    M2M_ERR_AUTH_FAIL,                // Failed to authenticate with the AP
    M2M_WIFI_ERR_ASSOC_FAIL,          // Failed to associate with the AP
    M2M_ERR_CONN_INPROGRESS           // Another connection request in progress
} tenuM2mConnChangedErrcode;

typedef enum 
{
    M2M_WIFI_WEP_KEY_INDEX_1 = ((uint8_t) 1),
    M2M_WIFI_WEP_KEY_INDEX_2,
    M2M_WIFI_WEP_KEY_INDEX_3,
    M2M_WEP_KEY_INDEX_4
} tenuM2mWepKeyIndex;

typedef enum 
{
    PWR_AUTO = ((uint8_t) 1),   // FW will decide the best power mode to use internally
    PWR_LOW1,                   // low power mode #1         
    PWR_LOW2,                   // low power mode #2
    PWR_HIGH                    // high power mode
} tenuM2mPwrMode;

typedef enum 
{
    TX_PWR_HIGH = ((uint8_t) 1),     // PPA Gain 6dbm    PA Gain 18dbm
    TX_PWR_MED,                      // PPA Gain 6dbm    PA Gain 12dbm
    TX_PWR_LOW                       // PPA Gain 6dbm    PA Gain 6dbm
} tenuM2mTxPwrLevel;

typedef struct 
{
    //Note: on SAMD D21 the size of double is 8 Bytes
    uint16_t   u16BattVolt; 
    uint8_t    padding[2];
} tstrM2mBatteryVoltage;

typedef enum 
{
    M2M_WIFI_DISCONNECTED = 0,       // Wi-Fi state is disconnected
    M2M_WIFI_CONNECTED,              // Wi-Fi state is connected
    M2M_WIFI_UNDEF = 0xff            // Undefined Wi-Fi State
} tenuM2mConnState;

typedef enum 
{
    M2M_WIFI_SEC_INVALID = 0,        // Invalid security type
    M2M_WIFI_SEC_OPEN,               // open security
    M2M_WIFI_SEC_WPA_PSK,            // WPA/WPA2 personal(PSK)
    M2M_WIFI_SEC_WEP,                // WEP (40 or 104) OPEN OR SHARED
    M2M_WIFI_SEC_802_1X              // WPA/WPA2 Enterprise.IEEE802.1x user-name/password authentication
} tenuM2mSecType;

typedef enum 
{
    SSID_MODE_VISIBLE = 0,       // SSID is visible to others
    SSID_MODE_HIDDEN             // SSID is hidden
} tenuM2mSsidMode;

// Wi-Fi channels
typedef enum 
{
    M2M_WIFI_CH_1 = ((uint8_t) 1),
    M2M_WIFI_CH_2,
    M2M_WIFI_CH_3,
    M2M_WIFI_CH_4,
    M2M_WIFI_CH_5,
    M2M_WIFI_CH_6,
    M2M_WIFI_CH_7,
    M2M_WIFI_CH_8,
    M2M_WIFI_CH_9,
    M2M_WIFI_CH_10,
    M2M_WIFI_CH_11,
    M2M_WIFI_CH_12,
    M2M_WIFI_CH_13,
    M2M_WIFI_CH_14,
    M2M_WIFI_CH_ALL = ((uint8_t) 255)
} tenuM2mScanCh;

// see m2m_wifi_set_scan_region()
typedef enum 
{
    M2M_WIFI_NORTH_AMERICA_REGION = ((uint16_t) 0x7FF),     // 11 channels
    M2M_WIFI_EUROPE_REGION        = ((uint16_t) 0x1FFF),    // 13 channels
    M2M_WIFI_NORTH_ASIA_REGION    = ((uint16_t) 0x3FFF)     // 14 channels
} tenuM2mScanRegion;

// Power Save Modes
typedef enum 
{
    M2M_NO_PS,   // Power save is disabled.
            
    M2M_WIFI_PS_AUTOMATIC,  // Power save is done automatically by the WINC1500.
                            // This mode doesn't disable all of the WINC modules and 
                            //  use higher levels of power than the M2M_WIFI_PS_H_AUTOMATIC and 
                            //  the M2M_WIFI_PS_DEEP_AUTOMATIC modes

    M2M_WIFI_PS_H_AUTOMATIC,// Power save is done automatically by the WINC1500.
                            //  Achieves higher power save than the M2M_WIFI_PS_AUTOMATIC mode
                            //  by shutting down more parts of the WINC board
            
    M2M_WIFI_PS_DEEP_AUTOMATIC,   // Power save is done automatically by the WINC1500.
                                  //  Achieves the highest possible power save.
            
    M2M_WIFI_PS_MANUAL            // Power save is done manually by host application
} tenuPowerSaveModes;

typedef enum 
{
    M2M_WIFI_MODE_NORMAL = ((uint8_t) 1),   // run customer firmware version
    M2M_WIFI_MODE_ATE_HIGH,                 // Config Mode in HIGH POWER means to run production test firmware version which is known as ATE (Burst) firmware
    M2M_WIFI_MODE_ATE_LOW,                  // Config Mode in LOW POWER means to run production test firmware version which is known as ATE (Burst) firmware
    M2M_WIFI_MODE_ETHERNET,                 // Ethernet mode
    M2M_WIFI_MODE_MAX
} tenuM2mWifiMode;

typedef enum
{
    WPS_PIN_TRIGGER = 0,                // WPS is triggered in PIN method
    WPS_PBC_TRIGGER = 4                 // WPS is triggered via push button
} tenuWPSTrigger;

// WEP security parameters
typedef struct
{
    uint8_t    u8KeyIndx;        // WEP key Index
    uint8_t    u8KeySz;         // WEP key size
    uint8_t    au8WepKey[WEP_104_KEY_STRING_SIZE + 1]; // WEP Key represented as a NULL terminated ASCII string
    uint8_t    padding[3];
} tstrM2mWifiWepParams;


// Credentials for the user to authenticate with the AAA server (WPA-Enterprise Mode IEEE802.1x).
typedef struct
{
    uint8_t    au8UserName[M2M_1X_USR_NAME_MAX];      //User Name. It must be Null terminated string
    uint8_t    au8Passwd[M2M_WIFI_PASSWORD_1X_MAX];   // Password corresponding to the user name. It must be Null terminated string
} tstr1xAuthCredentials;

//  Wi-Fi Security Parameters for all supported security modes.
typedef union
{
    uint8_t                 au8PSK[M2M_MAX_PSK_LEN];  // Pre-Shared Key in case of WPA-Personal security
    tstr1xAuthCredentials   strCred1x;                // Credentials for RADIUS server authentication in case of WPA-Enterprise security
    tstrM2mWifiWepParams    strWepInfo;               // WEP key parameters in case of WEP security
} tuniM2MWifiAuth;


// Authentication credentials to connect to a Wi-Fi network
#define SECURITY_INFO_PAD_SIZE      (4 - ((sizeof(tuniM2MWifiAuth) + 1) % 4))
typedef struct
{
    tuniM2MWifiAuth    uniAuth;           // Union holding all possible authentication parameters corresponding the current security types
    uint8_t            u8SecType;         // Wi-Fi network security type.  See tenuM2mSecType.
    uint8_t            padding[SECURITY_INFO_PAD_SIZE];
} tstrM2MWifiSecInfo;


// WPS Configuration parameters
typedef struct 
{
    uint8_t    u8TriggerType;         // WPS_PBC_TRIGGER or WPS_PIN_TRIGGER
    char       acPinNumber[8];       // WPS Pin
    uint8_t    padding[3];
} tstrM2MWPSConnect;


// Scan config
typedef struct 
{
    uint8_t   u8NumOfSlot;  // The min number of slots is 2 for every channel.  Every 
                            //  slot the WINC will send a probe request and then wait/listen 
                            //  for a probe response/beacon u8SlotTime

    uint8_t   u8SlotTime;   // The time (in ms) that the WINC will wait on every channel listening 
                            // for AP's.  The min time is 10ms; the max time is 250ms.
    
    uint8_t   u8ProbesPerSlot; // Number of probe requests to be sent per channel scan slot

    int8_t    s8RssiThresh;  // RSSI threshold of the AP
} tstrM2MScanOption;

typedef struct 
{
    uint16_t u16ScanRegion;           // see tstrM2MScanRegion 
    uint8_t padding[2];
} tstrM2MScanRegion;

typedef struct 
{
    uint8_t    u8ChNum;         // see tenuM2mScanCh
    uint8_t    padding[1];    
    uint16_t   passiveScanTime; 

} tstrM2MScan;


typedef struct 
{
    uint8_t    u8Index;      // Index of the desired scan result
    uint8_t    padding[3];
} tstrM2mReqScanResult;

typedef struct
{
    uint8_t     u8PsType;         // see tenuPowerSaveModes
    uint8_t     u8BcastEn;       // 1: WINC1500 must be awake each DTIM Beacon for receiving Broadcast traffic
                                 // 0: WINC1500 will not wakeup at the DTIM Beacon, but its wakeup depends only 
                                 //     on the the configured Listen Interval
    uint8_t     padding[2];
} tstrM2mPsType;


//  Manual power save request sleep time
typedef struct 
{
    uint32_t u32SleepTime;  // sleep time in ms
} tstrM2mSlpReqTime;


// Configuration parameters for the Soft AP mode.
typedef struct 
{
    uint8_t     au8SSID[M2M_MAX_SSID_LEN];      // SSID of SoftAP network
    uint8_t     u8ListenChannel;                // channel to use for SoftAP
    uint8_t     u8KeyIndx;                      // WEP key index
    uint8_t     u8KeySz;                        // M2M_WIFI_WEP_40_KEY_STRING_SIZE, M2M_WIFI_WEP_104_KEY_STRING_SIZE, or WPA key size
    uint8_t     au8WepKey[WEP_104_KEY_STRING_SIZE + 1];
    uint8_t     u8SecType;                      // M2M_WIFI_SEC_OPEN, M2M_WIFI_SEC_WEP, or M2M_WIFI_SEC_WPA_PSK 
    uint8_t     u8SsidHide;                     // SSID_MODE_VISIBLE or SSID_MODE_HIDDEN
    uint8_t     au8DHCPServerIP[4];             // AP IP server address
    uint8_t     au8Key[M2M_MAX_PSK_LEN];        // WPA key, if used
    uint8_t     __PAD24__[2];
} tstrM2MAPConfig;

typedef struct 
{
	uint16_t 	u16LsnInt;      // Listen interval in Beacon period count.
	uint8_t     padding[2];     // not used
} tstrM2mLsnInt;

// This struct contains a TLS certificate.
typedef struct
{
	char        acFileName[TLS_FILE_NAME_MAX];  // Name of the certificate
	uint32_t    u32FileSize;                    // Size of the certificate
	uint32_t    u32FileAddr;                    // Error Code
} tstrTlsSrvSecFileEntry;

// This struct contains a set of TLS certificates.
typedef struct
{
	uint8_t     au8SecStartPattern[TLS_SRV_SEC_START_PATTERN_LEN];   // Start pattern
	uint32_t    u32nEntries;                                         // Number of certificates stored in the struct
	uint32_t    u32NextWriteAddr;                                    // TLS Certificates
	tstrTlsSrvSecFileEntry astrEntries[TLS_SRV_SEC_MAX_FILES];
} tstrTlsSrvSecHdr;






#ifdef __cplusplus
     }
#endif

#endif // __M2M_WIFI_TYPES_H__
