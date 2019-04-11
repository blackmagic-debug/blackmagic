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

//==============================================================================
// INCLUDES
//==============================================================================
#include "winc1500_api.h"
#include "wf_common.h"
#include "wf_types.h"
#include "wf_hif.h"
#include "wf_drv.h"

//==============================================================================
 // LOCAL GLOBALS
//==============================================================================
static t_m2mOtaEventData   g_otaEventData;



t_m2mOtaEventData * m2m_wifi_get_ota_event_data(void)
{
    return (&g_otaEventData);
}

void OtaInternalEventHandler(uint8_t opCode, uint16_t dataSize, uint32_t addr)
{
    if (opCode == OTA_RESP_UPDATE_STATUS)
    {
        memset(&g_otaEventData.otaUpdateStatus, 0, sizeof(tstrOtaUpdateStatusResp));
        hif_receive(addr, (uint8_t*)&g_otaEventData.otaUpdateStatus, sizeof(tstrOtaUpdateStatusResp), 0);
        m2m_ota_handle_events(M2M_OTA_STATUS_EVENT, &g_otaEventData);
    }
    else
    {
        dprintf("Invalid OTA response %d\n",opCode);
        GenerateErrorEvent(M2M_WIFI_INVALID_OTA_RESPONSE_ERROR);
        return;
    }

}

void m2m_ota_start_update(char *downloadUrl)
{
    uint16_t urlSize = strlen(downloadUrl) + 1;
    /*Todo: we may change it to data pkt but we need to give it higher priority
            but the priority is not implemented yet in data pkt
    */
    hif_send(REQ_GROUP_OTA,OTA_REQ_START_FW_UPDATE,(uint8_t *)downloadUrl,urlSize,NULL,0,0);
}

void m2m_ota_rollback(void)
{
    hif_send(REQ_GROUP_OTA,OTA_REQ_ROLLBACK_FW,NULL,0,NULL,0,0);
}

void m2m_ota_abort(void)
{
    hif_send(REQ_GROUP_OTA, OTA_REQ_ABORT,NULL,0,NULL,0,0);
}

void m2m_ota_switch_firmware(void)
{
    hif_send(REQ_GROUP_OTA,OTA_REQ_SWITCH_FIRMWARE,NULL,0,NULL,0,0);
}

void nm_get_ota_firmware_info(tstrM2mRev *p_rev)
{
    hif_chip_wake();
    nm_get_firmware_full_info(p_rev, FIRMWARE_VERSION_OTA);
    hif_chip_sleep();
}



