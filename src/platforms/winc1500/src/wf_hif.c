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

#include "winc1500_api.h"
#include "wf_common.h"
#include "wf_hif.h"
#include "wf_types.h"
#include "wf_asic.h"
#include "wf_drv.h"
#include "wf_spi.h"

#define NM_EDGE_INTERRUPT

#if (defined NM_EDGE_INTERRUPT)&&(defined NM_LEVEL_INTERRUPT)
#error "only one type of interrupt NM_EDGE_INTERRUPT,NM_LEVEL_INTERRUPT"
#endif

#if !((defined NM_EDGE_INTERRUPT)||(defined NM_LEVEL_INTERRUPT))
#error "define interrupt type NM_EDGE_INTERRUPT,NM_LEVEL_INTERRUPT"
#endif


#define NMI_AHB_DATA_MEM_BASE  0x30000
#define NMI_AHB_SHARE_MEM_BASE 0xd0000

#define WIFI_HOST_RCV_CTRL_0    (0x1070)
#define WIFI_HOST_RCV_CTRL_1    (0x1084)
#define WIFI_HOST_RCV_CTRL_2    (0x1078)
#define WIFI_HOST_RCV_CTRL_3    (0x106c)
#define WIFI_HOST_RCV_CTRL_4    (0x150400)
#define WIFI_HOST_RCV_CTRL_5    (0x1088)


#define WAKE_VALUE              (0x5678)
#define SLEEP_VALUE             (0x4321)
#define WAKE_REG                (0x1074)

typedef struct 
{
     uint8_t chipMode;
     uint8_t chipSleep;
     uint8_t hifRxDone;
     uint8_t interruptCount;
     uint32_t rxAddr;
     uint32_t rxSize;
} t_hifContext;

t_hifContext g_hifContext;


tpfHifCallBack pfSigmaCb = NULL;
tpfHifCallBack pfHifCb = NULL;
tpfHifCallBack pfCryptoCb = NULL;

static uint32_t GetInterruptCount(void);
static void DecrementInterruptCount(void);

void m2m_EintHandler(void)
{
    g_hifContext.interruptCount++;
#ifdef NM_LEVEL_INTERRUPT
    m2mStub_EintDisable();
#endif
}

static void hif_set_rx_done(void)
{
    uint32_t reg;

    g_hifContext.hifRxDone = 0;
    
#ifdef NM_EDGE_INTERRUPT
    m2mStub_EintEnable();
#endif
            
    reg = nm_read_reg(WIFI_HOST_RCV_CTRL_0);
    reg |= NBIT1;
    nm_write_reg(WIFI_HOST_RCV_CTRL_0,reg);
    
#ifdef NM_EDGE_INTERRUPT
    m2mStub_EintEnable();
#endif

#ifdef NM_LEVEL_INTERRUPT
    m2mStub_EintEnable();
#endif
}

/**
*    @fn        int8_t hif_chip_wake(void);
*    @brief    To Wakeup the chip.
*    @return        The function shall return ZERO for successful operation and a negative value otherwise.
*/

void hif_chip_wake(void)
{
    if(g_hifContext.hifRxDone)
    {
        // chip already wake for the rx not done no need to send wake request
        return;
    }
    
    if (g_hifContext.chipSleep == 0)
    {
        if (g_hifContext.chipMode != M2M_NO_PS)
        {
            ChipWake();
        }
    }
    
    g_hifContext.chipSleep++;
}

/*!
@fn    \
    void hif_set_sleep_mode(uint8_t type);

@brief
    Set the sleep mode of the HIF layer.

@param [in]    type
                Sleep mode.

@return
    The function SHALL return 0 for success and a negative value otherwise.
*/

void hif_set_sleep_mode(uint8_t type)
{
    g_hifContext.chipMode = type;
}
/*!
@fn    \
    uint8_t hif_get_sleep_mode(void);

@brief
    Get the sleep mode of the HIF layer.

@return
    The function SHALL return the sleep mode of the HIF layer.
*/



uint8_t hif_get_sleep_mode(void)
{
    return g_hifContext.chipMode;
}

uint8_t m2m_wifi_get_sleep_mode(void)
{
    return g_hifContext.chipMode;
}


/**
*    @fn        int8_t hif_chip_sleep(void);
*    @brief    To make the chip sleep.
*    @return        The function shall return ZERO for successful operation and a negative value otherwise.
*/

/**
*    @fn        NMI_API sint8 hif_chip_sleep_sc(void);
*    @brief    To clear the chip sleep but keep the chip sleep
*    @return        The function shall return ZERO for successful operation and a negative value otherwise.
*/

void hif_chip_sleep_sc(void)
{
    if(g_hifContext.chipSleep >= 1)
    {
        g_hifContext.chipSleep--;
    }
}

void hif_chip_sleep(void)
{
    if(g_hifContext.chipSleep >= 1)
    {
        g_hifContext.chipSleep--;
    }
    
    if(g_hifContext.chipSleep == 0)
    {
        if(g_hifContext.chipMode != M2M_NO_PS)
        {
            ChipSleep();
        }
    }
}

void hif_init(void)
{
    memset(&g_hifContext, 0x00, sizeof(t_hifContext));
    m2mStub_EintEnable();
}

void hif_deinit(void)
{
    hif_chip_wake();
    memset(&g_hifContext, 0x00, sizeof(t_hifContext));
}
/**
*    @fn        int8_t hif_send(uint8_t groupId,uint8_t opCode,uint8_t *p_ctrlBuf,uint16_t ctrlBufSize,
                       uint8_t *p_dataBuf,uint16_t dataSize, uint16_t dataOffset)
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
*    @return        The function shall return ZERO for successful operation and a negative value otherwise.
*/

int8_t hif_send(uint8_t  groupId, 
                uint8_t  opCode, 
                uint8_t  *p_ctrlBuf, 
                uint16_t ctrlBufSize,
                uint8_t  *p_dataBuf,
                uint16_t dataSize, 
                uint16_t dataOffset)
{
   int8_t      ret;
   t_hifHdr   hif;
   uint32_t reg, dma_addr = 0;
   uint16_t cnt = 0;

   hif.opCode  = opCode & (~NBIT7);
   hif.groupId = groupId;
   hif.length   = M2M_HIF_HDR_OFFSET;
   if(p_dataBuf != NULL)
   {
      hif.length += dataOffset + dataSize;
   }
   else
   {
      hif.length += ctrlBufSize;
   }
   ChipWake();


//#define OPTIMIZE_BUS 
/*please define in firmware also*/
#ifndef OPTIMIZE_BUS
    reg = 0UL;
    reg |= (uint32_t)groupId;
    reg |= ((uint32_t)opCode << 8);
    reg |= ((uint32_t)hif.length << 16);
    nm_write_reg(NMI_STATE_REG, reg);

    reg = 0UL;
    reg |= NBIT1;
    nm_write_reg(WIFI_HOST_RCV_CTRL_2, reg);
#else
    reg = 0UL;
    reg |= NBIT1;
    reg |= ((opCode & NBIT7) ? (NBIT2):(0));          // Data = 1 or config
    reg |= (groupId == REQ_GROUP_IP) ? (NBIT3):(0);   // IP = 1 or non IP
    reg |= ((uint32_t)hif.length << 4);               // length of pkt max = 4096*/
    nm_write_reg(WIFI_HOST_RCV_CTRL_2, reg);
#endif
    dma_addr = 0;

    for(cnt = 0; cnt < 1000; ++cnt)
    {
        reg = nm_read_reg(WIFI_HOST_RCV_CTRL_2);
        if (!(reg & NBIT1))
        {
            dma_addr = nm_read_reg(WIFI_HOST_RCV_CTRL_4);
            break;
        }
        // If it takes too long to get a response, the slow down to 
        // avoid back-to-back register read operations.
        if(cnt >= 500) 
        {
            if(cnt < 501) 
            {
                //printf("Slowing down...\n");
            }
            DelayMs(2);
        }         
    }
    
    if (dma_addr != 0)
    {
        volatile uint32_t   currAddr;
        currAddr = dma_addr;
        hif.length=FIX_ENDIAN_16(hif.length);
        nm_write_block(currAddr, (uint8_t*)&hif, M2M_HIF_HDR_OFFSET);
        currAddr += M2M_HIF_HDR_OFFSET;
        if(p_ctrlBuf != NULL)
        {
            nm_write_block(currAddr, p_ctrlBuf, ctrlBufSize);
            currAddr += ctrlBufSize;
        }
        if(p_dataBuf != NULL)
        {
            currAddr += (dataOffset - ctrlBufSize);
            nm_write_block(currAddr, p_dataBuf, dataSize);
            currAddr += dataSize;
        }

        reg = dma_addr << 2;
        reg |= NBIT1;
        nm_write_reg(WIFI_HOST_RCV_CTRL_3, reg);
        ret = M2M_SUCCESS;
    }
    else
    {
        ChipSleep();
        ret = M2M_ERR_MEM_ALLOC;
        //dprintf("Failed to alloc rx size %d\r\n",ret);
        goto ERR2;
    }

   /*actual sleep ret = M2M_SUCCESS*/
   ChipSleep();
   return ret;

ERR2:
   return ret;
}
/**
*    @fn        hif_isr
*    @brief    Host interface interrupt service routine
*    @author    M. Abdelmawla
*    @date    15 July 2012
*    @return    1 in case of interrupt received else 0 will be returned
*    @version    1.0
*/

static void hif_isr(void)
{
    volatile uint32_t reg;
    volatile t_hifHdr hifHdr;

    hif_chip_wake();
    reg = nm_read_reg(WIFI_HOST_RCV_CTRL_0);
    if(reg & 0x1)    /* New interrupt has been received */
    {
        uint16_t size;

        m2mStub_EintDisable();
        
        // clear RX interrupt
        reg &= ~NBIT0;
        nm_write_reg(WIFI_HOST_RCV_CTRL_0,reg);
        g_hifContext.hifRxDone = 1;
        size = (uint16_t)((reg >> 2) & 0xfff);
        if (size > 0) 
        {
            uint32_t address = 0;

            // start bus transfer
            address = nm_read_reg(WIFI_HOST_RCV_CTRL_1);
            g_hifContext.rxAddr = address;
            g_hifContext.rxSize = size;
            nm_read_block(address, (uint8_t*)&hifHdr, sizeof(t_hifHdr));
            hifHdr.length = FIX_ENDIAN_16(hifHdr.length);
            if(hifHdr.length != size)
            {
                if((size - hifHdr.length) > 4)
                {
                    dprintf("(hif) Corrupted packet Size = %u <L = %u, G = %u, OP = %02X>\r\n",
                        size, hifHdr.length, hifHdr.groupId, hifHdr.opCode);
                    m2mStub_EintEnable();
                    hif_chip_sleep_sc();
                    GenerateErrorEvent(M2M_WIFI_INVALID_PACKET_SIZE_ERROR);
                    return;
                }
            }

            if(M2M_REQ_GROUP_WIFI == hifHdr.groupId)
            {
                WifiInternalEventHandler(hifHdr.opCode, hifHdr.length - M2M_HIF_HDR_OFFSET, address + M2M_HIF_HDR_OFFSET);
            }
            else if(REQ_GROUP_IP == hifHdr.groupId)
            {
                SocketInternalEventHandler(hifHdr.opCode, hifHdr.length - M2M_HIF_HDR_OFFSET, address + M2M_HIF_HDR_OFFSET);
            }
            else if(REQ_GROUP_OTA == hifHdr.groupId)
            {
                OtaInternalEventHandler(hifHdr.opCode, hifHdr.length - M2M_HIF_HDR_OFFSET, address + M2M_HIF_HDR_OFFSET);
            }
            else if(REQ_GROUP_CRYPTO == hifHdr.groupId)
            {
                if(pfCryptoCb)
                    pfCryptoCb(hifHdr.opCode, hifHdr.length - M2M_HIF_HDR_OFFSET, address + M2M_HIF_HDR_OFFSET);
            }
            else if(REQ_GROUP_SIGMA == hifHdr.groupId)
            {
                if(pfSigmaCb)
                    pfSigmaCb(hifHdr.opCode, hifHdr.length - M2M_HIF_HDR_OFFSET, address + M2M_HIF_HDR_OFFSET);
            }
            else
            {
                dprintf("(hif) invalid group ID\n");
                hif_chip_sleep_sc();
                GenerateErrorEvent(M2M_WIFI_INVALID_GROUP_ERROR);
                return;
            }

            if(g_hifContext.hifRxDone)
            {
                dprintf("(hif) host app didn't set RX Done\n");
                hif_set_rx_done();
            }
        }
        else
        {
            dprintf("(hif) Wrong Size\n");
            hif_chip_sleep_sc();            
            GenerateErrorEvent(M2M_WIFI_INVALID_SIZE_ERROR);
            return;
        }
    }
    else
    {
        dprintf("(hif) False interrupt %lx",reg);
        GenerateErrorEvent(M2M_WIFI_FALSE_INTERRUPT_ERROR);
    }

    hif_chip_sleep();
    m2mStub_EintEnable();
}

void hif_handle_isr(void)
{
    while (GetInterruptCount() > 0)
    {
        /*must be at that place because of the race of interrupt increment and that decrement*/
        /*when the interrupt enabled*/
        DecrementInterruptCount();
        hif_isr();
    } 
}

/*
*    @fn        hif_receive
*    @brief    Host interface interrupt service routine
*    @param [in]    address
*                Receive start address
*    @param [out]    p_buf
*                Pointer to receive buffer. Allocated by the caller
*    @param [in]    size
*                Receive buffer size
*    @param [in]    isDone
*                If you don't need any more packets send True otherwise send false
*    @return        The function shall return ZERO for successful operation and a negative value otherwise.
*/
void hif_receive(uint32_t address, uint8_t *p_buf, uint16_t size, uint8_t isDone)
{
    if(address == 0 || p_buf == NULL || size == 0)
    {
        if(isDone)
        {
            hif_set_rx_done();
        }
        else
        {
            dprintf(" hif_receive: Invalid argument\n");            
            GenerateErrorEvent(M2M_WIFI_HIF_RECEIVE_1_ERROR);
            return;
        }
        GenerateErrorEvent(M2M_WIFI_HIF_RECEIVE_2_ERROR);
        return;
    }

    if(size > g_hifContext.rxSize)
    {
        dprintf("APP Requested Size is larger than the receive buffer size <%d><%d>\r\n",size, (int)g_hifContext.rxSize);
        GenerateErrorEvent(M2M_WIFI_HIF_RECEIVE_3_ERROR);
        return;
    }
    if((address < g_hifContext.rxAddr) || ((address + size) > (g_hifContext.rxAddr + g_hifContext.rxSize)))
    {
        dprintf("APP Requested Address beyond the receive buffer address and length\n");
        GenerateErrorEvent(M2M_WIFI_HIF_RECEIVE_4_ERROR);
        return;
    }
    
    /* Receive the payload */
    nm_read_block(address, p_buf, size);

    /* check if this is the last packet */
    if((( (g_hifContext.rxAddr + g_hifContext.rxSize) - (address + size)) <= 0) || isDone)
    {
        hif_set_rx_done();
    }
}

/**
*    @fn        hif_register_cb
*    @brief    To set Callback function for every compantent Component
*    @param [in]    group
*                Group to which the Callback function should be set.
*    @param [in]    fn
*                function to be set
*    @return        The function shall return ZERO for successful operation and a negative value otherwise.
*/

void hif_register_cb(uint8_t group,tpfHifCallBack fn)
{
    switch(group)
    {
        case M2M_REQ_GROUP_WIFI:
            break;

        case REQ_GROUP_HIF:
            pfHifCb = fn;
            break;
            
        case REQ_GROUP_CRYPTO:
            pfCryptoCb = fn;
            break;
            
        case REQ_GROUP_SIGMA:
            pfSigmaCb = fn;
            break;
            
        default:
            dprintf("GRp ? %d\n",group);
            break;
    }
}

static uint32_t GetInterruptCount(void)
{
    uint32_t tmp;
    
    m2mStub_EintDisable();
    tmp = g_hifContext.interruptCount;
    m2mStub_EintEnable();
    
    return tmp;
}

static void DecrementInterruptCount(void)
{
    m2mStub_EintDisable();
    --g_hifContext.interruptCount;
    m2mStub_EintEnable();
}

#if defined(__XC8)
extern void SocketInternalEventHandler_Pic18WaiteHttpSend(uint8_t opCode, uint16_t bufferSize,uint32_t address);
static void hif_isr_Pic18WaiteHttpSend(void)
{
    volatile uint32_t reg;
    volatile t_hifHdr hifHdr;

    hif_chip_wake();
    reg = nm_read_reg(WIFI_HOST_RCV_CTRL_0);
    if(reg & 0x1)    /* New interrupt has been received */
    {
        uint16_t size;

        m2mStub_EintDisable();
        
        // clear RX interrupt
        reg &= ~NBIT0;
        nm_write_reg(WIFI_HOST_RCV_CTRL_0,reg);
        g_hifContext.hifRxDone = 1;
        size = (uint16_t)((reg >> 2) & 0xfff);
        if (size > 0) 
        {
            uint32_t address = 0;

            // start bus transfer
            address = nm_read_reg(WIFI_HOST_RCV_CTRL_1);
            g_hifContext.rxAddr = address;
            g_hifContext.rxSize = size;
            nm_read_block(address, (uint8_t*)&hifHdr, sizeof(t_hifHdr));
            hifHdr.length = FIX_ENDIAN_16(hifHdr.length);
            if(hifHdr.length != size)
            {
                if((size - hifHdr.length) > 4)
                {
                    dprintf("(hif) Corrupted packet Size = %u <L = %u, G = %u, OP = %02X>\r\n",
                        size, hifHdr.length, hifHdr.groupId, hifHdr.opCode);
                    m2mStub_EintEnable();
                    hif_chip_sleep_sc();
                    GenerateErrorEvent(M2M_WIFI_INVALID_PACKET_SIZE_ERROR);
                    return;
                }
            }

            if(REQ_GROUP_IP == hifHdr.groupId)
            {
                SocketInternalEventHandler_Pic18WaiteHttpSend(hifHdr.opCode, hifHdr.length - M2M_HIF_HDR_OFFSET, address + M2M_HIF_HDR_OFFSET);
            }
           
            if(g_hifContext.hifRxDone)
            {
                dprintf("(hif) host app didn't set RX Done\n");
                hif_set_rx_done();
            }
        }
        else
        {
            dprintf("(hif) Wrong Size\n");
            hif_chip_sleep_sc();            
            GenerateErrorEvent(M2M_WIFI_INVALID_PACKET_SIZE_ERROR);
            return;
        }
    }
    else
    {
        dprintf("(hif) False interrupt %lx",reg);
        GenerateErrorEvent(M2M_WIFI_FALSE_INTERRUPT_ERROR);
    }

    hif_chip_sleep();
    m2mStub_EintEnable();
}

void hif_handle_isr_Pic18WaiteHttpSend(void)
{
    while (GetInterruptCount() > 0)
    {
        /*must be at that place because of the race of interrupt increment and that decrement*/
        /*when the interrupt enabled*/
        DecrementInterruptCount();
        hif_isr_Pic18WaiteHttpSend();
    } 
}
#endif
