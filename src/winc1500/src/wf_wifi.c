/*******************************************************************************
   File Name:
    m2m_wifi.c

  Summary:
    Wi-Fi related messages and responses/events

  Description:
    This module contains functions to configure WINC1500 Wi-Fi parameters and to
    process Wi-Fi events from the WIN1500.
*******************************************************************************/

//DOM-IGNORE-BEGIN
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


//============================================================================
// INCLUDES
//============================================================================
#include "winc1500_api.h"
#include "wf_hif.h"
#include "wf_asic.h"
#include "wf_drv.h"
#include "wf_utils.h"
#include "wf_common.h"
#include "wf_spi.h"
#include "wf_spi_flash.h"

//============================================================================
// LOCAL GLOBALS
//============================================================================
static volatile bool g_scanInProgress = false;
static t_wifiEventData g_eventData;

//============================================================================
// LOCAL GLOBALS
//============================================================================
#if defined(M2M_ENABLE_PRNG)
static uint8_t g_prngSize;
#endif 

//============================================================================
// LOCAL FUNCTION PROTOTYPES
//============================================================================
#if 0
static void ChipHardwareReset(void);
static void nm_drv_init(void);
#endif
static void ConnectInternal(const char *pcSsid, uint8_t u8SsidLen, uint8_t u8SecType, void *pvAuthInfo, uint16_t channel, uint8_t noSaveCred);
#if defined(M2M_ENABLE_ERROR_CHECKING)  
static bool isValidApParameters(const tstrM2MAPConfig* apConfig);
static bool isChannelValid(uint8_t ch);
static bool isConnectionParamsValid(const char *pcSsid, uint8_t u8SsidLen, uint8_t u8SecType, void *pvAuthInfo, uint16_t channel);
    #if defined(M2M_ENABLE_SCAN_MODE)
    static bool isValidScanChannel(int channel);
    static int8_t ValidateScanOptions(tstrM2MScanOption* ptstrM2MScanOption);
    #endif // M2M_ENABLE_SCAN_MODE
#endif // M2M_ENABLE_ERROR_CHECKING

    
#if 1
#define SetInitState(state)                 g_initState = state
#define GetInitState()                      g_initState

#define SetChipHardwareResetState(state)    g_subState = state
#define GetChipHardwareResetState()         g_subState

#define SetBootRomState(state)              g_subState = state
#define GetBootRomState()                   g_subState

#define SetFirmwareStartState(state)        g_subState = state
#define GetFirmwareStartState()             g_subState

    
static uint8_t  g_initState;
static uint8_t  g_subState;


static bool InitStateMachine(void);
static bool ChipHardwareResetStateMachine(void);
static bool BootRomStateMachine(void);
static bool FirmwareStartStateMachine(void);

typedef enum
{
    INIT_START_STATE,
    INIT_WAIT_FOR_CHIP_RESET_STATE,
    INIT_WAIT_FOR_BOOT_ROM_STATE,
    INIT_WAIT_FOR_FIRMWARE_START_STATE,
    INIT_COMPLETE_STATE
} t_initState;

typedef enum
{
    CHIP_HARDWARE_RESET_START,
    CHIP_HARDWARE_RESET_FIRST_DELAY_1_MS,
    CHIP_HARDWARE_RESET_SECOND_DELAY_5_MS,
    CHIP_HARDWARE_RESET_FINAL_DELAY,            
    CHIP_HARDWARE_RESET_COMPLETE            
} t_chipHardwareResetState;

typedef enum
{
    BOOT_ROM_START,
    BOOT_ROM_WAIT_LOAD,
    BOOT_ROM_CHECK_REV,
    BOOT_ROM_FAIL
} t_bootRomState;

typedef enum
{
    FIRMWARE_START,
    FIRMWARE_START_WAIT,
    FIRMWARE_START_ERROR            
} t_firmwareStart;

static bool InitStateMachine(void)
{
    bool retVal = false;
    switch (GetInitState())
    {
        case INIT_START_STATE:
            SetInitState(INIT_WAIT_FOR_CHIP_RESET_STATE);
            SetChipHardwareResetState(CHIP_HARDWARE_RESET_START);
            break;
            
        case INIT_WAIT_FOR_CHIP_RESET_STATE:
            // if reset state machine done
            if (ChipHardwareResetStateMachine() == true)
            {
                g_scanInProgress = false;
                nm_spi_init();
                SetBootRomState(BOOT_ROM_START);
                SetInitState(INIT_WAIT_FOR_BOOT_ROM_STATE);
            }
            break;
            
        case INIT_WAIT_FOR_BOOT_ROM_STATE:
            // if Boot ROM state machine done
            if (BootRomStateMachine() == true)
            {
                SetFirmwareStartState(FIRMWARE_START);
                SetInitState(INIT_WAIT_FOR_FIRMWARE_START_STATE);
            }
            break;
            
        case INIT_WAIT_FOR_FIRMWARE_START_STATE:
            if (FirmwareStartStateMachine() == true)
            {
                m2mStub_EintEnable();
                EnableInterrupts();
                hif_init();
                SocketInit();     
                SetInitState(INIT_COMPLETE_STATE);
                m2m_wifi_handle_events(M2M_WIFI_DRIVER_INIT_EVENT, NULL);

                tstrM2mRev rev;
                nm_get_firmware_info(&rev);
                dprintf("\nWINC1500 Host Driver:\r\n");
                dprintf("  Chip ID:                     %3lx\r\n", rev.u32Chipid);
                dprintf("  Firmware Version:            %d.%d.%d\r\n", rev.u8FirmwareMajor, rev.u8FirmwareMinor, rev.u8FirmwarePatch);
                dprintf("  Firmware Build Date/Time:    %s, %s\r\n", rev.BuildDate, rev.BuildTime);
                dprintf("  Firmware Min Driver Version: %d.%d.%d\r\n", rev.u8DriverMajor, rev.u8DriverMinor, rev.u8DriverPatch);
                dprintf("  Host Driver Version:         %d.%d.%d\r\n", M2M_FIRMWARE_VERSION_MAJOR_NO, M2M_FIRMWARE_VERSION_MINOR_NO, M2M_FIRMWARE_VERSION_PATCH_NO);
                dprintf("  Host Driver Build Date/Time: %s, %s\r\n\r\n", __DATE__, __TIME__);
                
                retVal = true;  // init is complete
            }
            break;
            
        case INIT_COMPLETE_STATE:
            break;
    }
    
    return retVal;
}

static bool ChipHardwareResetStateMachine(void)
{
    static uint32_t startTime;
    bool retVal = false;
    
    switch (GetChipHardwareResetState())
    {
        case CHIP_HARDWARE_RESET_START:
            m2mStub_PinSet_CE(M2M_WIFI_PIN_LOW);
            m2mStub_PinSet_RESET(M2M_WIFI_PIN_LOW);
            startTime = m2mStub_GetOneMsTimer();
            SetChipHardwareResetState(CHIP_HARDWARE_RESET_FIRST_DELAY_1_MS);
            break;
            
        case CHIP_HARDWARE_RESET_FIRST_DELAY_1_MS:
            if (m2m_get_elapsed_time(startTime) >= 2)
            {
                m2mStub_PinSet_CE(M2M_WIFI_PIN_HIGH); 
                startTime = m2mStub_GetOneMsTimer();
                SetChipHardwareResetState(CHIP_HARDWARE_RESET_SECOND_DELAY_5_MS);
            }
            break;
            
        case CHIP_HARDWARE_RESET_SECOND_DELAY_5_MS:
            if (m2m_get_elapsed_time(startTime) >= 6)            
            {
                m2mStub_PinSet_RESET(M2M_WIFI_PIN_HIGH);
                startTime = m2mStub_GetOneMsTimer();
                SetChipHardwareResetState(CHIP_HARDWARE_RESET_FINAL_DELAY);
            }
            break;
            
        case CHIP_HARDWARE_RESET_FINAL_DELAY:
            if (m2m_get_elapsed_time(startTime) >= 10)
            {                        
                SetChipHardwareResetState(CHIP_HARDWARE_RESET_COMPLETE);
                retVal= true; // state machine has completed successfully                
            }
            break;
            
            
        case CHIP_HARDWARE_RESET_COMPLETE:
            break;
    }
    
    return retVal;
}

static bool BootRomStateMachine(void)
{
    bool retVal = false;
    uint32_t reg;
    uint32_t driverVersion;
    static uint32_t startTime;
    
    switch (GetBootRomState())
    {
        case BOOT_ROM_START:
            // if efuse loading done
            reg = nm_read_reg(0x1014);
            if (reg & 0x80000000)
            {
                reg = nm_read_reg(M2M_WAIT_FOR_HOST_REG) & 0x01;
                if (reg == 0)
                {
                    startTime = m2mStub_GetOneMsTimer();                    
                    SetBootRomState(BOOT_ROM_WAIT_LOAD);
                }
                else
                {
                    SetBootRomState(BOOT_ROM_CHECK_REV);
                }
            }
            break;
            
        case BOOT_ROM_WAIT_LOAD:
            reg = nm_read_reg(BOOTROM_REG);
            if (reg == M2M_FINISH_BOOT_ROM)
            {
                SetBootRomState(BOOT_ROM_CHECK_REV);
            }
            // else if timed out waiting (100ms)
            else if (m2m_get_elapsed_time(startTime) > 100)
            {
                dprintf("failed to load firmware from flash.\r\n");
                GenerateErrorEvent(M2M_WIFI_BOOTROM_LOAD_FAIL_ERROR); 
                SetBootRomState(BOOT_ROM_FAIL);
            }
            break;
            
        case BOOT_ROM_CHECK_REV:
            driverVersion = MAKE_VERSION_INFO(FIRMWARE_RELEASE_VERSION_MAJOR_NO, 
                                               FIRMWARE_RELEASE_VERSION_MINOR_NO,
                                               FIRMWARE_RELEASE_VERSION_PATCH_NO,
                                               M2M_FIRMWARE_VERSION_MAJOR_NO,  
                                               M2M_FIRMWARE_VERSION_MINOR_NO,  
                                               M2M_FIRMWARE_VERSION_PATCH_NO);
            nm_write_reg(NMI_STATE_REG, driverVersion);
            if(REV(GetChipId()) >= REV_3A0)
            {
                ChipApplyConfig((uint32_t)rHAVE_USE_PMU_BIT);
                nm_write_reg(BOOTROM_REG, M2M_START_FIRMWARE);   
                retVal = true;  // state machine has completed successfully
            }
            else
            {
                dprintf("failed to load firmware from flash.\r\n");
                GenerateErrorEvent(M2M_WIFI_CHIP_REV_ERROR);
                SetBootRomState(BOOT_ROM_FAIL);
            }
            break;
            
        case BOOT_ROM_FAIL:
            break;
    }
    
    return retVal;
}

static bool FirmwareStartStateMachine(void)
{
    static uint32_t startTime;
    uint32_t reg;
    bool     retVal = false;
    
    switch (GetFirmwareStartState())
    {
        case FIRMWARE_START:
            startTime = m2mStub_GetOneMsTimer();
            SetFirmwareStartState(FIRMWARE_START_WAIT);
            break;
            
        case FIRMWARE_START_WAIT:
            reg = nm_read_reg(NMI_STATE_REG);
            if (reg == M2M_FINISH_INIT_STATE)
            {
                nm_write_reg(NMI_STATE_REG, 0);
                retVal = true;  // state machine has completed successfully
            }
            else if (m2m_get_elapsed_time(startTime) > 200)
            {
                dprintf("Time out for waiting for WINC1500 firmware to run\n");
                GenerateErrorEvent(M2M_WIFI_FIRMWARE_START_ERROR);                
                SetFirmwareStartState(FIRMWARE_START_ERROR);
            }
            break;
            
        case FIRMWARE_START_ERROR:
            break;
    }
    
    return retVal;
}
#endif

    
    
// called from hif_isr() when a Wi-Fi event occurs.
void WifiInternalEventHandler(uint8_t opCode, uint16_t dataSize, uint32_t address)
{
    t_m2mWifiEventType appEvent;
    
    dataSize = dataSize; // avoid warning

    switch (opCode)
    {
        case STA_CONN_STATE_CHANGED_EVENT:
            appEvent = M2M_WIFI_CONN_STATE_CHANGED_EVENT;
            hif_receive(address, (uint8_t*)&g_eventData, sizeof(tstrM2mWifiStateChanged), 0);
            break;
            
        case CFG_SYS_TIME_EVENT:
            appEvent = M2M_WIFI_SYS_TIME_EVENT;
            hif_receive(address, (uint8_t*)&g_eventData, sizeof(tstrSystemTime), 0);
            g_eventData.sysTime.u16Year = FIX_ENDIAN_16(g_eventData.sysTime.u16Year);
            break;
            
        case CFG_CONN_INFO_RESPONSE_EVENT:
            appEvent = M2M_WIFI_CONN_INFO_RESPONSE_EVENT;
            hif_receive(address, (uint8_t*)&g_eventData, sizeof(tstrM2MConnInfo), 1);       
            break;
            
        case STA_IP_ADDRESS_ASSIGNED_EVENT:
            appEvent = M2M_WIFI_IP_ADDRESS_ASSIGNED_EVENT;
            hif_receive(address, (uint8_t*)&g_eventData, sizeof(tstrM2MIPConfig), 1);                   
            g_eventData.ipConfig.u32DhcpLeaseTime = FIX_ENDIAN_32(g_eventData.ipConfig.u32DhcpLeaseTime);
            break;
            
        case STA_REQ_WPS:
            appEvent = M2M_WIFI_WPS_EVENT;
            hif_receive(address, (uint8_t*)&g_eventData, sizeof(tstrM2MWPSInfo), 0);
            break;
            
        case STA_IP_CONFLICT_EVENT:
            appEvent = M2M_WIFI_IP_CONFLICT_EVENT;
            hif_receive(address, (uint8_t*)&g_eventData, sizeof(g_eventData.conflictedIpAddress), 0);
            break;
            
        case CFG_SCAN_DONE_EVENT:
            appEvent = M2M_WIFI_SCAN_DONE_EVENT;
            g_scanInProgress = false;
            hif_receive(address, (uint8_t*)&g_eventData, sizeof(tstrM2mScanDone), 0);            
            break;
            
        case CFG_SCAN_RESULT_EVENT:
            appEvent = M2M_WIFI_SCAN_RESULT_EVENT;
            hif_receive(address, (uint8_t*)&g_eventData, sizeof(tstrM2mWifiscanResult), 0);
            break;
            
        case CFG_RSSI_EVENT:
            appEvent = M2M_WIFI_RSSI_EVENT;
            hif_receive(address, (uint8_t *)&g_eventData, 4, 0);
            break;
            
        case CFG_PROVISION_INFO_EVENT:
            appEvent = M2M_WIFI_PROVISION_INFO_EVENT;
            hif_receive(address, (uint8_t*)&g_eventData, sizeof(tstrM2MProvisionInfo), 1);            
            break;
            
        case STA_DEFAULT_CONNNECT_EVENT:
            appEvent = M2M_WIFI_DEFAULT_CONNNECT_EVENT;
            break;
            
#if defined(M2M_ENABLE_PRNG)
        case CFG_PRNG_EVENT:
            appEvent = M2M_WIFI_PRNG_EVENT;
            hif_receive(address, (uint8_t*)&g_eventData, sizeof(t_prng), 0);
            // do second read to get the psuedo-random bytes
            hif_receive(address + sizeof(t_prng), g_eventData.prng.buf, g_prngSize, 1);
            g_eventData.prng.size = g_prngSize; 
            break;
#endif
            
        default:
            dprintf("ERROR: invalid wifi op code\r\n");
            GenerateErrorEvent(M2M_WIFI_INVALID_WIFI_EVENT_ERROR);
            return;
    }
    
    // notify application of event
    m2m_wifi_handle_events(appEvent, &g_eventData);
}

void m2m_wifi_init(void)
{
    SetInitState(INIT_START_STATE);
}

void  m2m_wifi_deinit(void * arg)
{
    hif_deinit();
    nm_drv_deinit();
}

void m2m_wifi_task(void)
{
    // if driver not yet initialized
    if (GetInitState() != INIT_COMPLETE_STATE)
    {
        InitStateMachine();
    }
    hif_handle_isr();
}

void m2m_wifi_default_connect(void)
{
    hif_send(M2M_REQ_GROUP_WIFI, STA_REQ_DEFAULT_CONNECT, NULL, 0,NULL, 0,0);
}

void m2m_wifi_connect(char *pcSsid, uint8_t u8SsidLen, uint8_t u8SecType, void *pvAuthInfo, uint16_t channel)
{
    ConnectInternal(pcSsid, u8SsidLen, u8SecType, pvAuthInfo,  channel, 1);
}

void m2m_wifi_connect_sc(char *pcSsid, uint8_t u8SsidLen, uint8_t u8SecType, void *pvAuthInfo, uint16_t channel)
{
    ConnectInternal(pcSsid, u8SsidLen, u8SecType, pvAuthInfo,  channel, 0);    
}

void nm_get_firmware_full_info(tstrM2mRev* p_revision, t_versionNumberType versionType)
{
    uint16_t    curr_drv_ver, min_req_drv_ver, curr_firm_ver;
    uint32_t    reg;
    t_gpRegs    gpReg;
    
    if (p_revision != NULL)
    {
        memset((uint8_t*)p_revision,0,sizeof(tstrM2mRev));
        reg = nm_read_reg(rNMI_GP_REG_2);
        if(reg != 0)
        {
            nm_read_block(reg | 0x30000,(uint8_t*)&gpReg,sizeof(t_gpRegs));
            gpReg.firmwareOtaRev = FIX_ENDIAN_32(gpReg.firmwareOtaRev);
            gpReg.macEfuseMib    = FIX_ENDIAN_32(gpReg.macEfuseMib);
            reg = gpReg.firmwareOtaRev;
            
            // if reading active firmware version
            if (versionType == FIRMWARE_VERSION_ACTIVE)
            {
                reg &= 0x0000ffff;  // get currently running version
            }
            // else reading OTA version number
            else
            {
                reg >>= 16;         // get OTA version
            }
            
            if(reg != 0)
            {
                nm_read_block(reg|0x30000,(uint8_t*)p_revision,sizeof(tstrM2mRev));
                p_revision->u32Chipid = FIX_ENDIAN_32(p_revision->u32Chipid); 

                curr_firm_ver   = MAKE_VERSION(p_revision->u8FirmwareMajor, p_revision->u8FirmwareMinor,p_revision->u8FirmwarePatch);
                curr_drv_ver    = MAKE_VERSION(M2M_FIRMWARE_VERSION_MAJOR_NO, M2M_FIRMWARE_VERSION_MINOR_NO, M2M_FIRMWARE_VERSION_PATCH_NO);
                min_req_drv_ver = MAKE_VERSION(p_revision->u8DriverMajor, p_revision->u8DriverMinor,p_revision->u8DriverPatch);
                if((curr_firm_ver == 0)||(min_req_drv_ver == 0)||(min_req_drv_ver == 0))
                {
                    GenerateErrorEvent(M2M_WIFI_FIRMWARE_READ_ERROR);
                    return;
                }
                if(curr_drv_ver <  min_req_drv_ver) 
                {
                    /*The current driver version should be larger or equal 
                    than the min driver that the current firmware support  */
                    GenerateErrorEvent(M2M_WIFI_FIRMWARE_MISMATCH_ERROR);
                    return;
                }
                if(curr_drv_ver >  curr_firm_ver) 
                {
                    /*The current driver should be equal or less than the firmware version*/
                    GenerateErrorEvent(M2M_WIFI_FIRMWARE_MISMATCH_ERROR);
                    return;
                }
            }
            else 
            {
                // not valid for current firmware to have a version of 0.  If OTA
                // has not yet been programmed it could have a version of 0.
                if (versionType == FIRMWARE_VERSION_ACTIVE)
                {
                    GenerateErrorEvent(M2M_WIFI_FIRMWARE_VERS_ZERO_ERROR); 
                }
                return;
            }
        }
        else
        {
            GenerateErrorEvent(M2M_WIFI_FIRMWARE_REG_READ_2_ERROR);
            return;
        }
    }
    
}

void m2m_wifi_send_crl(tstrTlsCrlInfo* pCRL)
{
    hif_send(REQ_GROUP_SSL, M2M_SSL_IND_CRL | REQ_DATA_PKT, NULL, 0, (uint8_t*)pCRL, sizeof(tstrTlsCrlInfo), 0);
}

int8_t nm_drv_deinit(void)
{
    int8_t ret;

    ChipDeinit();
    
    /* Disable SPI flash to save power when the chip is off */
    ret = spi_flash_enable(0);
    if (M2M_SUCCESS != ret) 
    {
        dprintf("[nmi stop]: SPI flash disable fail\r\n");
        goto ERR1;
    }

    /* Must do this after global reset to set SPI data packet size. */
    nm_spi_deinit();

ERR1:
    return ret;
}

static void ConnectInternal(const char *pcSsid, uint8_t u8SsidLen, uint8_t u8SecType, void *pvAuthInfo, uint16_t channel, uint8_t noSaveCred)
{
    t_connectConfig    connectInfo;
    tstrM2MWifiSecInfo *p_securityInfo;

#if defined(M2M_ENABLE_ERROR_CHECKING)    
    if (!isConnectionParamsValid(pcSsid, u8SsidLen, u8SecType, pvAuthInfo, channel))
    {
        GenerateErrorEvent(M2M_WIFI_CONNECT_ERROR);
        return;
    }
#endif
    
    memcpy(connectInfo.ssid, (uint8_t*)pcSsid, u8SsidLen);
    connectInfo.ssid[u8SsidLen]  = 0;
    connectInfo.channel        = FIX_ENDIAN_16(channel);
    connectInfo.noSaveCred      = noSaveCred;
    
    p_securityInfo = &connectInfo.securityInfo;     // shortcut
    
    p_securityInfo->u8SecType = u8SecType;

    if(u8SecType == M2M_WIFI_SEC_WEP)
    {
        tstrM2mWifiWepParams    *p_wepParams = (tstrM2mWifiWepParams*)pvAuthInfo;
        tstrM2mWifiWepParams    *pstrWep = &p_securityInfo->uniAuth.strWepInfo;
        pstrWep->u8KeyIndx =p_wepParams->u8KeyIndx-1;
        pstrWep->u8KeySz = p_wepParams->u8KeySz-1;        
        memcpy((uint8_t*)pstrWep->au8WepKey,(uint8_t*)p_wepParams->au8WepKey, p_wepParams->u8KeySz);
        pstrWep->au8WepKey[p_wepParams->u8KeySz] = 0;
    }
    else if(u8SecType == M2M_WIFI_SEC_WPA_PSK)
    {
        uint16_t    keyLength = strlen((char *)pvAuthInfo);
        memcpy(p_securityInfo->uniAuth.au8PSK, (uint8_t*)pvAuthInfo, keyLength + 1);
    }
    else if(u8SecType == M2M_WIFI_SEC_802_1X)
    {
        memcpy((uint8_t*)&p_securityInfo->uniAuth.strCred1x, (uint8_t*)pvAuthInfo, sizeof(tstr1xAuthCredentials));
    }
    else if(u8SecType == M2M_WIFI_SEC_OPEN)
    {
        // nothing to do
    }

    hif_send(M2M_REQ_GROUP_WIFI, STA_REQ_CONNECT, (uint8_t*)&connectInfo, sizeof(t_connectConfig),NULL, 0,0);
}


void m2m_wifi_disconnect(void)
{
    hif_send(M2M_REQ_GROUP_WIFI, STA_REQ_DISCONNECT, NULL, 0, NULL, 0,0);
}

void m2m_wifi_set_mac_address(uint8_t *au8MacAddress)
{
    tstrM2mSetMacAddress tmp;
    memcpy((uint8_t*) tmp.au8Mac, au8MacAddress, 6);
    hif_send(M2M_REQ_GROUP_WIFI, CFG_REQ_SET_MAC_ADDRESS, (uint8_t*) &tmp, sizeof(tstrM2mSetMacAddress), NULL, 0,0);
}

// all addresses must be in big-endian format
void m2m_wifi_set_static_ip(tstrM2MIPConfig * pstrStaticIPConf)
{
    hif_send(REQ_GROUP_IP, IP_REQ_STATIC_IP_CONF, (uint8_t*) pstrStaticIPConf, sizeof(tstrM2MIPConfig), NULL, 0,0);
}

void m2m_wifi_set_lsn_int(tstrM2mLsnInt *pstrM2mLsnInt)
{
    pstrM2mLsnInt->u16LsnInt = FIX_ENDIAN_16(pstrM2mLsnInt->u16LsnInt);
    
    hif_send(M2M_REQ_GROUP_WIFI, STA_REQ_LSN_INTERVAL, (uint8_t*)pstrM2mLsnInt, sizeof(tstrM2mLsnInt), NULL, 0, 0);
}

#if defined(M2M_ENABLE_SOFT_AP_MODE)
void m2m_wifi_set_cust_InfoElement(uint8_t *pau8M2mCustInfoElement)
{
#if defined(M2M_ENABLE_ERROR_CHECKING)
    if(pau8M2mCustInfoElement == NULL)
    {
        GenerateErrorEvent(M2M_WIFI_SET_CUST_INFO_ERROR);
        return;
    }
    if((pau8M2mCustInfoElement[0] + 1) > M2M_WIFI_CUST_IE_LEN_MAX)
    {
        GenerateErrorEvent(M2M_WIFI_SET_CUST_INFO_LEN_ERROR);
        return;
    }
#endif
    hif_send(M2M_REQ_GROUP_WIFI, CFG_REQ_CUST_INFO_ELEMENT|REQ_DATA_PKT, (uint8_t*)pau8M2mCustInfoElement, pau8M2mCustInfoElement[0]+1, NULL, 0, 0);
}
#endif // M2M_ENABLE_SOFT_AP_MODE

#if defined(M2M_ENABLE_SCAN_MODE)
void m2m_wifi_set_scan_options(tstrM2MScanOption *ptstrM2MScanOption)
{
#if defined(M2M_ENABLE_ERROR_CHECKING)
    if (ValidateScanOptions(ptstrM2MScanOption) != M2M_SUCCESS)    
    {
        GenerateErrorEvent(M2M_WIFI_SCAN_OPTIONS_ERROR);
        return;
    }
#endif    
    hif_send(M2M_REQ_GROUP_WIFI, CFG_REQ_SET_SCAN_OPTION, (uint8_t*)ptstrM2MScanOption, sizeof(tstrM2MScanOption),NULL, 0,0);
}

void m2m_wifi_set_scan_region(uint16_t scanRegion)
{
#if defined(M2M_ENABLE_ERROR_CHECKING)  
    if ((scanRegion != M2M_WIFI_NORTH_AMERICA_REGION) && (scanRegion != M2M_WIFI_EUROPE_REGION) && (scanRegion != M2M_WIFI_NORTH_ASIA_REGION))
    {
        GenerateErrorEvent(M2M_WIFI_SCAN_REGION_ERROR);
        return;
    }
#endif     
    
    tstrM2MScanRegion strScanRegion;
    strScanRegion.u16ScanRegion = scanRegion;
    hif_send(M2M_REQ_GROUP_WIFI, CFG_REQ_SET_SCAN_REGION, (uint8_t*)&strScanRegion, sizeof(tstrM2MScanRegion),NULL, 0,0);
}

// request an active scan
void m2m_wifi_request_scan(uint8_t ch)
{
#if defined(M2M_ENABLE_ERROR_CHECKING)  
     if(g_scanInProgress)
     {
         GenerateErrorEvent(M2M_WIFI_SCAN_IN_PROGRESS_ERROR);
         return;
     }
     if (!isValidScanChannel(ch))
     {
        GenerateErrorEvent(M2M_WIFI_SCAN_CHANNEL_ERROR);
        return;
     }
#endif
    tstrM2MScan scanConfig;
    scanConfig.u8ChNum = ch;
    hif_send(M2M_REQ_GROUP_WIFI, CFG_REQ_SCAN, (uint8_t*)&scanConfig, sizeof(tstrM2MScan),NULL, 0,0);
    g_scanInProgress = true;
}

void m2m_wifi_request_scan_passive(uint8_t ch, uint16_t scan_time)
{
#if defined(M2M_ENABLE_ERROR_CHECKING)  
     if(g_scanInProgress)
     {
         GenerateErrorEvent(M2M_WIFI_SCAN_IN_PROGRESS_ERROR);
         return;
     }
     if (!isValidScanChannel(ch))
     {
        GenerateErrorEvent(M2M_WIFI_SCAN_CHANNEL_ERROR);
        return;
     }
#endif
   tstrM2MScan scanConfig;

   scanConfig.u8ChNum = ch;
   scanConfig.passiveScanTime = FIX_ENDIAN_16(scan_time);
   hif_send(M2M_REQ_GROUP_WIFI, CFG_REQ_PASSIVE_SCAN, (uint8_t*)&scanConfig, sizeof(tstrM2MScan),NULL, 0,0);
   g_scanInProgress = true;
}

void m2m_wifi_req_hidden_ssid_scan(uint8_t ch, uint8_t *p_ssidList)
{
#if defined(M2M_ENABLE_ERROR_CHECKING)  
     if(g_scanInProgress)
     {
         GenerateErrorEvent(M2M_WIFI_SCAN_IN_PROGRESS_ERROR);
         return;
     }
     else if(((ch < M2M_WIFI_CH_1) && (ch > M2M_WIFI_CH_14)) || (ch != M2M_WIFI_CH_ALL))
     {
        GenerateErrorEvent(M2M_WIFI_SCAN_CHANNEL_ERROR);
        return;
     }
#endif
    tstrM2MScan tmp;
    uint16_t listSize = 0;
    uint8_t apNum = p_ssidList[listSize];
    
    if(apNum <= M2M_WIFI_MAX_HIDDEN_SITES)
    {
        listSize++;
        while(apNum)
        {
            if(p_ssidList[listSize] >= M2M_MAX_SSID_LEN) // TBD: ???
            { 
                dprintf("request_scan_ssid_list: failed\r\n");
                return;
            }
            else 
            {
                listSize += p_ssidList[listSize] + 1;
                apNum--;
            }
        }
        tmp.u8ChNum = ch;
        hif_send(M2M_REQ_GROUP_WIFI,CFG_REQ_SCAN_SSID_LIST | REQ_DATA_PKT, (uint8_t*)&tmp, sizeof(tstrM2MScan),p_ssidList, listSize,sizeof(tstrM2MScan));
        g_scanInProgress = 1;
    }
}

void m2m_wifi_req_scan_result(uint8_t index)
{
    tstrM2mReqScanResult reqScan;
    reqScan.u8Index = index;
    hif_send(M2M_REQ_GROUP_WIFI, CFG_REQ_SCAN_RESULT, (uint8_t*)&reqScan, sizeof(tstrM2mReqScanResult), NULL, 0, 0);
}

#endif // M2M_ENABLE_SCAN_MODE

#if defined(M2M_ENABLE_WPS)
void m2m_wifi_wps(uint8_t u8TriggerType, const char  *pcPinNumber)
{
    tstrM2MWPSConnect wps;

    // stop scan if it is ongoing.
    g_scanInProgress = false;

    wps.u8TriggerType = u8TriggerType;
    
    // if WPS is using PIN METHOD
    if (u8TriggerType == WPS_PIN_TRIGGER)
    {
        memcpy ((uint8_t*)wps.acPinNumber,(uint8_t*) pcPinNumber,8);
    }
    hif_send(M2M_REQ_GROUP_WIFI, STA_REQ_WPS, (uint8_t*)&wps, sizeof(tstrM2MWPSConnect), NULL, 0,0);
}

void m2m_wifi_wps_disable(void)
{
    hif_send(M2M_REQ_GROUP_WIFI, STA_REQ_DISABLE_WPS, NULL,0, NULL, 0, 0);
}
#endif // M2M_ENABLE_WPS

#if defined(M2M_WIFI_ENABLE_P2P)
void m2m_wifi_p2p(uint8_t u8Channel)
{
#if defined(M2M_ENABLE_ERROR_CHECKING)  
    if((u8Channel != M2M_WIFI_CH_1) && (u8Channel != M2M_WIFI_CH_6) && (u8Channel != M2M_WIFI_CH_11))
    {
        GenerateErrorEvent(M2M_WIFI_P2P_CHANNEL_ERROR);
        return;
    }
#endif    
        tstrM2MP2PConnect tmp;
        tmp.u8ListenChannel = u8Channel;
        hif_send(M2M_REQ_GROUP_WIFI, P2P_REQ_ENABLE, (uint8_t*)&tmp, sizeof(tstrM2MP2PConnect), NULL, 0,0);
}

void m2m_wifi_p2p_disconnect(void)
{
    hif_send(M2M_REQ_GROUP_WIFI, P2P_REQ_DISABLE, NULL, 0, NULL, 0, 0);
}
#endif // M2M_WIFI_ENABLE_P2P

#if defined(M2M_ENABLE_SOFT_AP_MODE)
void m2m_wifi_enable_ap(const tstrM2MAPConfig* pstrM2MAPConfig)
{
#if defined(M2M_ENABLE_ERROR_CHECKING)  
    if (!isValidApParameters(pstrM2MAPConfig))
    {
        GenerateErrorEvent(M2M_WIFI_AP_CONFIG_ERROR);
        return;
    }
#endif    
    hif_send(M2M_REQ_GROUP_WIFI, AP_REQ_ENABLE_AP, (uint8_t *)pstrM2MAPConfig, sizeof(tstrM2MAPConfig), NULL, 0, 0);    
}

void m2m_wifi_disable_ap(void)
{
    hif_send(M2M_REQ_GROUP_WIFI, AP_REQ_DISABLE_AP, NULL, 0, NULL, 0, 0);
}
#endif // M2M_ENABLE_SOFT_AP_MODE

void m2m_wifi_req_curr_rssi(void)
{
    hif_send(M2M_REQ_GROUP_WIFI, CFG_REQ_CURRENT_RSSI, NULL, 0, NULL,0, 0);
}

// Reads MAC address from OTP on WINC1500.  If the OTP has been programmed, function
// returns true with mac address, else returns false, and mac address set to 0's.
void m2m_wifi_get_otp_mac_address(uint8_t *pu8MacAddr, uint8_t *pu8IsValid)
{
    uint32_t    reg;
    uint8_t     mac[6];
    t_gpRegs    gpReg = {0};
    
    *pu8IsValid = true;
    
    hif_chip_wake(); 

    reg = nm_read_reg(rNMI_GP_REG_2);
    nm_read_block(reg | 0x30000,(uint8_t*)&gpReg, sizeof(t_gpRegs));
    reg = FIX_ENDIAN_32(gpReg.macEfuseMib);
         
    if(!EFUSED_MAC(reg)) 
    {
        memset(pu8MacAddr, 0, 6);
        *pu8IsValid = false;
    }
    else
    {
        reg >>=16;
        nm_read_block(reg | 0x30000, mac, 6);
        memcpy(pu8MacAddr,mac,6);

        hif_chip_sleep();
    }
}

void m2m_wifi_get_mac_address(uint8_t *pu8MacAddr)
{
    hif_chip_wake();
    GetMacAddress(pu8MacAddr);
    hif_chip_sleep();
}



void m2m_wifi_set_sleep_mode(uint8_t PsTyp, uint8_t BcastEn)
{
    tstrM2mPsType ps;
    
    ps.u8PsType = PsTyp;
    ps.u8BcastEn = BcastEn;
    hif_send(M2M_REQ_GROUP_WIFI, STA_REQ_SLEEP, (uint8_t*) &ps ,sizeof(tstrM2mPsType), NULL, 0, 0);
    hif_set_sleep_mode(PsTyp);
}

void m2m_wifi_request_sleep(uint32_t u32SlpReqTime)
{
#if defined(M2M_ENABLE_ERROR_CHECKING)  
    if (hif_get_sleep_mode() != M2M_WIFI_PS_MANUAL)
    {
        GenerateErrorEvent(M2M_WIFI_REQ_SLEEP_ERROR);
        return;
    }
#endif    
    if(hif_get_sleep_mode() == M2M_WIFI_PS_MANUAL)
    {
        tstrM2mSlpReqTime ps;
        ps.u32SleepTime = FIX_ENDIAN_32(u32SlpReqTime);
        hif_send(M2M_REQ_GROUP_WIFI, STA_REQ_DOZE, (uint8_t*) &ps,sizeof(tstrM2mSlpReqTime), NULL, 0, 0);
    }
}

void m2m_wifi_set_device_name(char *pu8DeviceName, uint8_t u8DeviceNameLength)
{
    tstrM2MDeviceNameConfig deviceName;
     
#if defined(M2M_ENABLE_ERROR_CHECKING) 
    if (u8DeviceNameLength >= M2M_DEVICE_NAME_MAX)
    {
        GenerateErrorEvent(M2M_WIFI_DEVICE_NAME_TO_LONG_ERROR);
        return;
    }
#endif
    u8DeviceNameLength++;
    
    memcpy(deviceName.au8DeviceName, pu8DeviceName, u8DeviceNameLength);
    hif_send(M2M_REQ_GROUP_WIFI, M2M_WIFI_REQ_SET_DEVICE_NAME, (uint8_t*)&deviceName, sizeof(tstrM2MDeviceNameConfig), NULL, 0,0);
}
void nm_get_firmware_info(tstrM2mRev *p_revision)
{
    hif_chip_wake();
    nm_get_firmware_full_info(p_revision, FIRMWARE_VERSION_ACTIVE);

#if defined(HOST_MCU_BIG_ENDIAN)
    p_revision->u32Chipid = FIX_ENDIAN_32(p_revision->u32Chipid);
#endif    
    
    p_revision->u32Chipid  &= 0xfff;
    hif_chip_sleep();
}

#if defined(M2M_ENABLE_HTTP_PROVISION_MODE)
void m2m_wifi_start_provision_mode(tstrM2MAPConfig *pstrM2MAPConfig, char *p_httpServerDomainName, uint8_t bEnableHttpRedirect)
{
    tstrM2MProvisionModeConfig    strProvConfig;

#if defined(M2M_ENABLE_ERROR_CHECKING) 
    if ((pstrM2MAPConfig == NULL) || (p_httpServerDomainName == NULL))
    {
        GenerateErrorEvent(M2M_WIFI_PROVISION_MODE_ERROR);
        return;
    }
    else if(!isValidApParameters(pstrM2MAPConfig))
    {
        GenerateErrorEvent(M2M_WIFI_AP_CONFIG_ERROR);
        return;
    }
    else if (bEnableHttpRedirect > 1)
    {
        GenerateErrorEvent(M2M_WIFI_PROVISION_MODE_ERROR);
    }
#endif     

    memcpy((uint8_t*)&strProvConfig.strApConfig, (uint8_t*)pstrM2MAPConfig, sizeof(tstrM2MAPConfig));
    memcpy((uint8_t*)strProvConfig.acHttpServerDomainName, (uint8_t*)p_httpServerDomainName, 64);
    strProvConfig.u8EnableRedirect = bEnableHttpRedirect;

    //  Stop Scan if it is ongoing.
    g_scanInProgress = false;
    hif_send(M2M_REQ_GROUP_WIFI, CFG_REQ_START_PROVISION_MODE | REQ_DATA_PKT, (uint8_t*)&strProvConfig, sizeof(tstrM2MProvisionModeConfig), NULL, 0, 0);
}

void m2m_wifi_stop_provision_mode(void)
{
    hif_send(M2M_REQ_GROUP_WIFI, CFG_REQ_STOP_PROVISION_MODE, NULL, 0, NULL, 0, 0);
}
#endif // M2M_ENABLE_HTTP_PROVISION_MODE

void m2m_wifi_get_connection_info(void)
{
    hif_send(M2M_REQ_GROUP_WIFI, CFG_REQ_GET_CONN_INFO, NULL, 0, NULL, 0, 0);
}

void m2m_wifi_set_sytem_time(uint32_t u32UTCSeconds)
{
#if defined(HOST_MCU_BIG_ENDIAN)    
    u32UTCSeconds = FIX_ENDIAN_32(u32UTCSeconds);
#endif
    // The firmware accepts timestamps relative to 1900 like NTP Timestamp.
    hif_send(M2M_REQ_GROUP_WIFI, CFG_REQ_SET_SYS_TIME, (uint8_t*)&u32UTCSeconds, sizeof(u32UTCSeconds), NULL, 0, 0);
}

void m2m_wifi_get_sytem_time(void)
{
    hif_send(M2M_REQ_GROUP_WIFI, CFG_REQ_GET_SYS_TIME, NULL,0, NULL, 0, 0);
}

void m2m_wifi_enable_sntp(uint8_t bEnable)
{
    uint8_t    req;

    req = bEnable ? CFG_REQ_ENABLE_SNTP_CLIENT : CFG_REQ_DISABLE_SNTP_CLIENT;
    hif_send(M2M_REQ_GROUP_WIFI, req, NULL, 0, NULL, 0, 0);
}

void m2m_wifi_set_power_profile(uint8_t u8PwrMode)
{
    tstrM2mPwrMode wincPowerMode;
    wincPowerMode.u8PwrMode = u8PwrMode;
    hif_send(M2M_REQ_GROUP_WIFI, CFG_REQ_SET_POWER_PROFILE, (uint8_t*)&wincPowerMode,sizeof(tstrM2mPwrMode), NULL, 0, 0);
}

void m2m_wifi_set_tx_power(uint8_t u8TxPwrLevel)
{
    tstrM2mTxPwrLevel wincPowerLevel;
    wincPowerLevel.u8TxPwrLevel = u8TxPwrLevel;
    hif_send(M2M_REQ_GROUP_WIFI, CFG_REQ_SET_TX_POWER, (uint8_t*)&wincPowerLevel,sizeof(tstrM2mTxPwrLevel), NULL, 0, 0);
}

void m2m_wifi_enable_firmware_log(uint8_t u8Enable)
{
    tstrM2mEnableLogs enableLog;
    enableLog.u8Enable = u8Enable;
    hif_send(M2M_REQ_GROUP_WIFI, CFG_REQ_SET_ENABLE_LOGS, (uint8_t*)&enableLog,sizeof(tstrM2mEnableLogs), NULL, 0, 0);
}

/*!
@fn            int8_t m2m_wifi_set_battery_voltage(uint16_t batVoltx100);
@brief        Enable or Disable logs in run time (Disable Firmware logs will 
            enhance the firmware start-up time and performance)
@param [in]    batVoltx100
            battery voltage multiplied by 100
@return        The function SHALL return M2M_SUCCESS for success and a negative value otherwise.
@sa            M2M_DISABLE_FIRMWARE_LOG (build option to disable logs from initializations)
@pre        m2m_wifi_init
@warning    
*/
void m2m_wifi_set_battery_voltage(uint16_t batVoltx100)
{
    tstrM2mBatteryVoltage batVoltage;
    batVoltage.u16BattVolt = FIX_ENDIAN_16(batVoltx100);
    hif_send(M2M_REQ_GROUP_WIFI, CFG_REQ_SET_BATTERY_VOLTAGE, (uint8_t*)&batVoltage,sizeof(tstrM2mBatteryVoltage), NULL, 0, 0);
}

#if defined(M2M_ENABLE_PRNG)
/*!
@fn                  int8_t m2m_wifi_prng_get_random_bytes(uint8_t * p_prngBuf,uint16_t size)
@brief          Get random bytes using the PRNG bytes.          
@param [in]    size
               Size of the required random bytes to be generated.        
@param [in]    p_prngBuf
                Pointer to user allocated buffer.                      
@return           The function SHALL return M2M_SUCCESS for success and a negative value otherwise.
*/
void m2m_wifi_prng_get_random_bytes(uint16_t size)
{
    t_prng   rng;
    g_prngSize = size;
#if defined(M2M_ENABLE_ERROR_CHECKING)  
    if (size > M2M_MAX_PRNG_BYTES)
    {
        GenerateErrorEvent(M2M_WIFI_PRNG_GET_ERROR);
        return;
    }
#endif    
    rng.size = FIX_ENDIAN_16(size);
    rng.p_buf  = g_eventData.prng.buf;
    hif_send(M2M_REQ_GROUP_WIFI, CFG_REQ_GET_PRNG|REQ_DATA_PKT,(uint8_t *)&rng, sizeof(t_prng),NULL,0, 0);
}
#endif // M2M_ENABLE_PRNG

t_wifiEventData * m2m_wifi_get_wifi_event_data(void)
{
    return &g_eventData;
}

void GenerateErrorEvent(uint32_t errorCode)
{
    m2m_error_handle_events(errorCode);
}

#if defined(M2M_ENABLE_SPI_FLASH)
void nm_drv_init_download_mode(void)
{
    nm_spi_init();
    ChipResetAndCpuHalt();
    
    /* Must do this again after global reset to set SPI data packet size. */
    nm_spi_init();

    /*disable all interrupt in ROM (to disable uart) in 2b0 chip*/
    nm_write_reg(0x20300,0);

    EnableInterrupts();
}
#endif

#if defined(M2M_ENABLE_ERROR_CHECKING)  
static bool isValidApParameters(const tstrM2MAPConfig* apConfig)
{
    // if pointer invalid
    if(apConfig == NULL)
    {
        dprintf("INVALID POINTER\n");
        return false;
    }
    // else if invalid SSID
    else if((strlen((char *)apConfig->au8SSID) <= 0) || (strlen((char *)apConfig->au8SSID) >= M2M_MAX_SSID_LEN))
    {
        dprintf("INVALID SSID\n");
        return false;
    }
    // else if invalid channel
    else if(apConfig->u8ListenChannel > M2M_WIFI_CH_14 || apConfig->u8ListenChannel < M2M_WIFI_CH_1)
    {
        dprintf("INVALID CH\n");
        return false;
    }
    
    /* Check for DHCP Server IP address */
    if(!(apConfig->au8DHCPServerIP[0] || apConfig->au8DHCPServerIP[1]))
    {
        if(!(apConfig->au8DHCPServerIP[2]))
        {
            dprintf("INVALID DHCP SERVER IP\n");
            return false;
        }
    }
    
    // if not open security then check security parameters
    if(apConfig->u8SecType != M2M_WIFI_SEC_OPEN)
    {
        if (apConfig->u8SecType == M2M_WIFI_SEC_WEP)
        {
            // if invalid wep key index
            if((apConfig->u8KeyIndx <= 0) || (apConfig->u8KeyIndx > M2M_WIFI_WEP_KEY_MAX_INDEX))
            {
                dprintf("INVALID KEY INDEX\n");
                return false;
            }
            // else if invalid wep key size
            else if((apConfig->u8KeySz != M2M_WIFI_WEP_40_KEY_STRING_SIZE) &&
               (apConfig->u8KeySz != WEP_104_KEY_STRING_SIZE))
            {
                dprintf("INVALID KEY STRING SIZE\n");
                return false;
            }
            // else if invalid wep key
            if((apConfig->au8WepKey == NULL) || (strlen((char *)apConfig->au8WepKey) <= 0) || (strlen((char *)apConfig->au8WepKey) > WEP_104_KEY_STRING_SIZE))
            {
                dprintf("INVALID WEP KEY\n");
                return false;            
            }
        }
        else if (apConfig->u8SecType == M2M_WIFI_SEC_WPA_PSK)
        {
            // if invalid wpa key size
            if(((apConfig->u8KeySz + 1) < M2M_WIFI_MIN_PSK_LEN) || ((apConfig->u8KeySz + 1) > M2M_MAX_PSK_LEN))
            {
                dprintf("INVALID WPA KEY SIZE\n");
                return false;
            }   
        }
        else // unknown security type
        {
            dprintf("INVALID AUTHENTICATION MODE\n");
            return false;
        }
    }
    
    return true;
}

#if defined(M2M_ENABLE_SCAN_MODE)
static int8_t ValidateScanOptions(tstrM2MScanOption* ptstrM2MScanOption)
{
    int8_t retVal = M2M_SUCCESS;
    
    /* Check incoming pointer */
    if(ptstrM2MScanOption == NULL)
    {
        dprintf("Invalid pointerR\n");
        retVal = -1;
    }    
    /* Check for valid No of slots */
     if(ptstrM2MScanOption->u8NumOfSlot == 0)
    {
        dprintf("Invalid Number of scan slots!(%d)\r\n", ptstrM2MScanOption->u8NumOfSlot);
        retVal = -1;
    }    
    /* Check for valid time of slots */
     if ((ptstrM2MScanOption->u8SlotTime < 10) || (ptstrM2MScanOption->u8SlotTime > 250))
    {
        dprintf("Invalid scan slot time!\r\n");
        retVal = -1;
    }    
    /* Check for valid No of probe requests per slot */
     if((ptstrM2MScanOption->u8ProbesPerSlot == 0)||(ptstrM2MScanOption->u8ProbesPerSlot > M2M_WIFI_SCAN_DEFAULT_NUM_PROBE))
    {
        dprintf("Invalid No of probe requests per scan slot (%d)\r\n", ptstrM2MScanOption->u8ProbesPerSlot);
        retVal = -1;
    }    
    /* Check for valid RSSI threshold */
     if((ptstrM2MScanOption->s8RssiThresh  < -99) || (ptstrM2MScanOption->s8RssiThresh >= 0))
    {
        dprintf("Invalid RSSI threshold %d \r\n",ptstrM2MScanOption->s8RssiThresh);
        retVal = -1;
    }    
    return retVal;
}

static bool isValidScanChannel(int channel)
{
    return (((channel >= M2M_WIFI_CH_1) && (channel <= M2M_WIFI_CH_14)) 
            || (channel == M2M_WIFI_CH_ALL));
}

#endif // M2M_ENABLE_SCAN_MODE

static bool isChannelValid(uint8_t ch)
{
    if ( ((ch >= M2M_WIFI_CH_1) && (ch <= M2M_WIFI_CH_14)) 
                                    || 
                         (ch == M2M_WIFI_CH_ALL))
    {
        return true;
    }
    else
    {
        return false;
    }
}


static bool isConnectionParamsValid(const char *pcSsid, uint8_t u8SsidLen, uint8_t u8SecType, void *pvAuthInfo, uint16_t channel)
{
    if (u8SecType > M2M_WIFI_SEC_802_1X)
    {
        dprintf("Invalid Security Type\n");
        return false;
    }
    
    if ((u8SecType != M2M_WIFI_SEC_OPEN) && (pvAuthInfo == NULL))
    {
        dprintf("pvAuthInfo cannot be NULL if not using open security\n");
        return false;
    }
    
    
    if((u8SsidLen = 0) || (u8SsidLen >= M2M_MAX_SSID_LEN))
    {
        dprintf("SSID LEN INVALID\n");
        return false;
    }
    
    if (!isChannelValid(channel))
    {
        dprintf("CH INVALID\n");
        return false;
    }
    
    if(u8SecType == M2M_WIFI_SEC_WEP)
    {
        int keyIndex;
        tstrM2mWifiWepParams *p_wep = (tstrM2mWifiWepParams *)pvAuthInfo;
        
        keyIndex = p_wep->u8KeyIndx - 1;
        
        if (keyIndex >= M2M_WIFI_WEP_KEY_MAX_INDEX)
        {
            dprintf("Invalid WEP key index %d\n", p_wep->u8KeyIndx);
            return false;
        }
        else if ((p_wep->u8KeySz != M2M_WIFI_WEP_40_KEY_STRING_SIZE+1) && (p_wep->u8KeySz != WEP_104_KEY_STRING_SIZE+1))
        {
            dprintf("Invalid WEP key length %d\n", p_wep->u8KeySz);
            return false;
        }
        
    }
    else if(u8SecType == M2M_WIFI_SEC_WPA_PSK)
    {
        uint16_t keyLength =  strlen((char *)pvAuthInfo);
        if((keyLength <= 0) || (keyLength >= M2M_MAX_PSK_LEN))
        {
            dprintf("Incorrect PSK key length\n");
            return false;
        }        
    }
    
    return true;
}


#endif // M2M_ENABLE_ERROR_CHECKING

#if defined(__XC8)
extern void hif_handle_isr_Pic18WaiteHttpSend(void);
void m2m_wifi_task_Pic18WaiteHttpSend(void)
{
    hif_handle_isr_Pic18WaiteHttpSend();
}
#endif




//DOM-IGNORE-END