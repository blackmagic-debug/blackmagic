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

#ifndef __WF_HIF_H
#define __WF_HIF_H

#ifdef __cplusplus
     extern "C" {
#endif

/*DOM-IGNORE-BEGIN*/  
         
//============================================================================
// CONSTANTS
//============================================================================
#define M2M_HIF_MAX_PACKET_SIZE      (1600 - 4)
/*!< Maximum size of the buffer could be transferred between Host and Firmware.
*/

#define M2M_HIF_HDR_OFFSET (sizeof(t_hifHdr) + 4)

/**
*    @struct        t_hifHdr
*    @brief        Structure to hold HIF header
*/
typedef struct
{
    uint8_t   groupId; // group ID
    uint8_t   opCode;  // op code
    uint16_t  length;  // payload length
} t_hifHdr;


// This structure needs to be 8 bytes.  If a pointer is less then 4 bytes then
// need some extra padding.
typedef struct 
{
    uint8_t     *p_buf;         // return buffer
#if (M2M_POINTER_SIZE_IN_BYTES == 2)
    uint8_t     extraPadding[2]; 
#elif (M2M_POINTER_SIZE_IN_BYTES == 3)    // PIC18
    uint8_t     extraPadding[1]; 
#endif
    uint16_t    size;           // PRNG size requested 
    uint8_t     padding[2];     // not used
} t_prng;


/*!
@typedef typedef void (*tpfHifCallBack)(uint8_t opCode, uint16 dataSize, uint32 address);
@brief    used to point to Wi-Fi call back function depend on Arduino project or other projects.
@param [in]    opCode
                HIF Opcode type.
@param [in]    dataSize
                HIF data length.
@param [in]    address
                HIF address.
@param [in]    grp
                HIF group type.
*/
typedef void (*tpfHifCallBack)(uint8_t opCode, uint16_t dataSize, uint32_t address);
/**
*   @fn            int8_t hif_init(void * arg);
*   @brief
                To initialize HIF layer.
*   @param [in]    arg
*                Pointer to the arguments.
*   @return
                The function shall return ZERO for successful operation and a negative value otherwise.
*/
void hif_init(void);
void hif_deinit(void);
/**
*    @fn        int8_t hif_send(uint8_t groupId,uint8_t opCode,uint8_t *p_ctrlBuf,uint16 ctrlBufSize,
                       uint8_t *p_dataBuf,uint16 dataSize, uint16 dataOffset)
*    @brief    Send packet using host interface.

*    @param [in]    groupId
*                Group ID.
*    @param [in]    opCode
*                Operation ID.
*    @param [in]    p_ctrlBuf
*                Pointer to the Control buffer.
*    @param [in]    ctrlBufSize
                Control buffer size.
*    @param [in]    dataOffset
                Packet Data offset.
*    @param [in]    p_dataBuf
*                Packet buffer Allocated by the caller.
*    @param [in]    dataSize
                Packet buffer size (including the HIF header).
*    @return    The function shall return ZERO for successful operation and a negative value otherwise.
*/
int8_t hif_send(uint8_t groupId,uint8_t opCode,uint8_t *p_ctrlBuf,uint16_t ctrlBufSize,
                       uint8_t *p_dataBuf,uint16_t dataSize, uint16_t dataOffset);
/*
*    @fn        hif_receive
*    @brief    Host interface interrupt serviece routine
*    @param [in]    address
*                Receive start address
*    @param [out] p_buf
*                Pointer to receive buffer. Allocated by the caller
*    @param [in]     size
*                Receive buffer size
*    @param [in]    isDone
*                If you don't need any more packets send True otherwise send false
*   @return
                The function shall return ZERO for successful operation and a negative value otherwise.
*/

void hif_receive(uint32_t address, uint8_t *p_buf, uint16_t size, uint8_t isDone);
void hif_register_cb(uint8_t group, tpfHifCallBack fn);
void hif_chip_sleep(void);
void hif_chip_sleep_sc(void);
void hif_chip_wake(void);
void hif_set_sleep_mode(uint8_t type);
uint8_t hif_get_sleep_mode(void);
void hif_handle_isr(void);


/*DOM-IGNORE-END*/  

#ifdef __cplusplus
}
#endif
     
#endif // __WF_HIF_H
