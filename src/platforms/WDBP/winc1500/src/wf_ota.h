/*******************************************************************************
   File Name:
    wf_ota.h

  Summary:
    Support for Over-The-Air firmware update.

  Description:
    Support for Over-The-Air firmware update.
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

#ifndef __WF_OTA_H
#define __WF_OTA_H

#ifdef __cplusplus
     extern "C" {
#endif

//==============================================================================
// CONSTANTS
//==============================================================================
// OTA return status
typedef enum 
{
    OTA_STATUS_SUCCESS              = 0,        // OTA Success with not errors
    OTA_STATUS_FAIL                 = 1,        // OTA generic fail
    OTA_STATUS_INVALID_ARG          = 2,        // Invalid or malformed download URL
    OTA_STATUS_INVALID_RB_IMAGE     = 3,        // Invalid rollback image
    OTA_STATUS_INVALID_FLASH_SIZE   = 4,        // Flash size on device is not enough for OTA
    OTA_STATUS_AlREADY_ENABLED      = 5,        // An OTA operation is already enabled
    OTA_STATUS_UPDATE_IN_PROGRESS   = 6,        // An OTA operation update is in progress
    OTA_STATUS_IMAGE_VERIFY_FAILED  = 7,        // OTA Verification failed
    OTA_STATUS_CONNECTION_ERROR     = 8,        // OTA connection error
    OTA_STATUS_SERVER_ERROR         = 9,        // OTA server Error (file not found or else ...)
    OTA_STATUS_ABORTED              = 10            
} t_m2mOtaUpdateStatusCode;


// OTA update Status type
typedef enum 
{
    DL_STATUS = 1,            // OTA download file status
    SW_STATUS = 2,            // Switching to the upgrade firmware status
    RB_STATUS = 3,            // Roll-back status
    AB_STATUS         = 4             // Abort status
} tenuOtaUpdateStatusType;
         
// OTA Events.  See m2m_ota_handle_events().
typedef enum
{
    M2M_OTA_STATUS_EVENT = 1,
} t_m2mOtaEventType;
    

//==============================================================================
// DATA TYPES
//==============================================================================

// Update Information
typedef struct 
{
    uint8_t     u8OtaUpdateStatusType;  // see tenuOtaUpdateStatusType
    uint8_t     u8OtaUpdateStatus;      // see t_m2mOtaUpdateStatusCode
    uint8_t     padding[2];
} tstrOtaUpdateStatusResp;


// Pointer to this union is passed to m2m_ota_handle_events()
typedef struct t_m2mOtaEventData
{
    tstrOtaUpdateStatusResp  otaUpdateStatus;
} t_m2mOtaEventData;


         
//==============================================================================
// FUNCTION PROTOTYPES
//==============================================================================

/*******************************************************************************
  Function:
    void m2m_ota_start_update(char *downloadUrl)

  Summary:
    Request OTA update using the downloaded URL.
 
  Description:
    The WINC1500 will download the OTA image and ensure integrity of the image
    Switching to the new image is not automatic; the application must call 
    m2m_ota_switch_firmware().  Upon success of the download (or failure), 
    the M2M_OTA_STATUS_EVENT is generated with the status type of 
    DL_STATUS.
 
    A Wi-Fi connection is required prior to calling this function.
 
  Parameters:
    downloadUrl -- URL to retrieve the download from
 
  Returns:
    None
 *****************************************************************************/
void m2m_ota_start_update(char *p_downloadUrl);

/*******************************************************************************
  Function:
    void m2m_ota_switch_firmware(void)

  Summary:
    Switches to the OTA firmware image.
 
  Description:
    After a successful OTA update the application must call this function to
    have the WIN1500 switch to the new (OTA) image. Upon success (or failure), 
    the M2M_OTA_STATUS_EVENT is generated with the status type of 
    SW_STATUS.
    
    If successful, a system restart is required.

 Parameters:
    None
 
  Returns:
    None
 *****************************************************************************/
void m2m_ota_switch_firmware(void);


/*******************************************************************************
  Function:
    void m2m_ota_rollback(void)

  Summary:
    Request OTA Roll-back to the older (other) WINC1500 image.
 
  Description:
    The WINC1500 will check the validity of the Roll-back image before switching 
    to it.  Upon success (or failure) of the roll-back the M2M_OTA_STATUS_EVENT is 
    generated with the status type of RB_STATUS.  If successful, 
    a system restart is required.

 Parameters:
    None
 
  Returns:
    None
 *****************************************************************************/
void m2m_ota_rollback(void);

/*******************************************************************************
  Function:
    void m2m_ota_abort(void)

  Summary:
    Request an abort of the current OTA download.
 
  Description:
    The WINC1500 will terminate the OTA download if one is in progress.  It will 
    then check the validity of the Roll-back image before switching to it.  
 
    Upon success (or failure) of the abort the M2M_OTA_STATUS_EVENT is generated 
    with the status type of AB_STATUS.

 Parameters:
    None
 
  Returns:
    None
 *****************************************************************************/
void m2m_ota_abort(void);



#ifdef __cplusplus
}
#endif

#endif // __WF_OTA_H
