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


// NOTE: DO NOT DO printf() during FLASH update.  If the PC utility is being used
//       it uses the serial port for commands and data.  A prinf() in the middle
//       of that is fatal to the update.

#ifdef PROFILING
#include "windows.h"
#endif

#include "winc1500_api.h"
#include "wf_spi_flash.h"
#include "wf_spi_flash_map.h"
#include "wf_asic.h"
#include "wf_common.h"
#include "wf_spi.h"




#define DUMMY_REGISTER    (0x1084)

#define TIMEOUT (-1) /*MS*/

//#define DISABLE_UNUSED_FLASH_FUNCTIONS

#define HOST_SHARE_MEM_BASE        (0xd0000UL)
#define CORTUS_SHARE_MEM_BASE    (0x60000000UL)
#define NMI_SPI_FLASH_ADDR        (0x111c)
/***********************************************************
SPI Flash DMA 
***********************************************************/
#define GET_UINT32(X,Y)            (X[0+Y] + ((uint32_t)X[1+Y]<<8) + ((uint32_t)X[2+Y]<<16) +((uint32_t)X[3+Y]<<24))
#define SPI_FLASH_BASE            (0x10200)
#define SPI_FLASH_MODE            (SPI_FLASH_BASE + 0x00)
#define SPI_FLASH_CMD_CNT        (SPI_FLASH_BASE + 0x04)
#define SPI_FLASH_DATA_CNT        (SPI_FLASH_BASE + 0x08)
#define SPI_FLASH_BUF1            (SPI_FLASH_BASE + 0x0c)
#define SPI_FLASH_BUF2            (SPI_FLASH_BASE + 0x10)
#define SPI_FLASH_BUF_DIR        (SPI_FLASH_BASE + 0x14)
#define SPI_FLASH_TR_DONE        (SPI_FLASH_BASE + 0x18)
#define SPI_FLASH_DMA_ADDR        (SPI_FLASH_BASE + 0x1c)
#define SPI_FLASH_MSB_CTL        (SPI_FLASH_BASE + 0x20)
#define SPI_FLASH_TX_CTL        (SPI_FLASH_BASE + 0x24)

//==============================================================================
// LOCAL FUNCTION PROTOTYPES
//==============================================================================
static void spi_flash_enter_low_power_mode(void);
static void spi_flash_leave_low_power_mode(void);

// To enable all SPI FLASH functions, define M2M_ENABLE_SPI_FLASH
#if defined(M2M_ENABLE_SPI_FLASH)
#if 0
static void nm_drv_init_download_mode(void);
#endif
static int8_t spi_flash_read_status_reg(uint8_t * val);
static int8_t spi_flash_load_to_cortus_mem(uint32_t u32MemAdr, uint32_t u32FlashAdr, uint32_t u32Sz);
static int8_t spi_flash_sector_erase(uint32_t u32FlashAdr);
static int8_t spi_flash_write_enable(void);
static int8_t spi_flash_write_disable(void);
static int8_t spi_flash_page_program(uint32_t u32MemAdr, uint32_t u32FlashAdr, uint32_t u32Sz);
static int8_t spi_flash_read_internal(uint8_t *p_buf, uint32_t address, uint32_t u32Sz);
static int8_t spi_flash_pp(uint32_t u32Offset, uint8_t *p_buf, uint16_t size);
static uint32_t spi_flash_rdid(void);
#endif // M2M_ENABLE_SPI_FLASH 

/**
 *    @fn        spi_flash_enable
 *    @brief    Enable spi flash operations
 */
int8_t spi_flash_enable(uint8_t enable)
{
    int8_t s8Ret = M2M_SUCCESS;
    uint32_t reg;
    if(REV(GetChipId()) >= REV_3A0) {        
        
        
        /* Enable pinmux to SPI flash. */
        reg = nm_read_reg(0x1410);
        
        /* GPIO15/16/17/18 */
        reg &= ~((0x7777ul) << 12);
        reg |= ((0x1111ul) << 12);
        nm_write_reg(0x1410, reg);
        if(enable) 
        {
            spi_flash_leave_low_power_mode();
        } else 
        {
            spi_flash_enter_low_power_mode();
        }
        /* Disable pinmux to SPI flash to minimize leakage. */
        reg &= ~((0x7777ul) << 12);
        reg |= ((0x0010ul) << 12);
        nm_write_reg(0x1410, reg);
    }

    return s8Ret;
}

static void spi_flash_enter_low_power_mode(void) 
{
    volatile unsigned long tmp;
    unsigned char* cmd = (unsigned char*) &tmp;

    cmd[0] = 0xb9;

    nm_write_reg(SPI_FLASH_DATA_CNT, 0);
    nm_write_reg(SPI_FLASH_BUF1, cmd[0]);
    nm_write_reg(SPI_FLASH_BUF_DIR, 0x1);
    nm_write_reg(SPI_FLASH_DMA_ADDR, 0);
    nm_write_reg(SPI_FLASH_CMD_CNT, 1 | (1 << 7));
    while(nm_read_reg(SPI_FLASH_TR_DONE) != 1);
}


static void spi_flash_leave_low_power_mode(void) 
{
    volatile unsigned long tmp;
    unsigned char* cmd = (unsigned char*) &tmp;

    cmd[0] = 0xab;

    nm_write_reg(SPI_FLASH_DATA_CNT, 0);
    nm_write_reg(SPI_FLASH_BUF1, cmd[0]);
    nm_write_reg(SPI_FLASH_BUF_DIR, 0x1);
    nm_write_reg(SPI_FLASH_DMA_ADDR, 0);
    nm_write_reg(SPI_FLASH_CMD_CNT,  1 | (1 << 7));
    while(nm_read_reg(SPI_FLASH_TR_DONE) != 1);
}

#if defined(M2M_ENABLE_SPI_FLASH)

#if 0
/*******************************************************************************
  Function:
    void  DownloadMode(void)

  Summary:
    Prepares the WINC1500 for download of firmware or certificates.

  Description:
    Prepares the WINC1500 for download of firmware or certificates.  Firmware 
    can be downloaded through a number of interfaces (UART, I2C and SPI) on
    the module.
 
  Parameters:
    None
 
  Returns:
    None
 *****************************************************************************/
void  DownloadMode(void)
{
    /* Apply device specific initialization. */
    nm_drv_init_download_mode();
    EnableInterrupts();
}
#endif

/**
*    @fn            spi_flash_read
*    @brief        Read from data from SPI flash
*    @param[OUT]    p_buf
*                    Pointer to data buffer
*    @param[IN]    u32offset
*                    Address to read from at the SPI flash
*    @param[IN]    u32Sz
*                    Data size
*    @return        Status of execution
*    @note        Data size is limited by the SPI flash size only
*/ 
int8_t spi_flash_read(uint8_t *p_buf, uint32_t u32offset, uint32_t u32Sz)
{
    int8_t ret = M2M_SUCCESS;
    if(u32Sz > FLASH_BLOCK_SIZE)
    {
        do
        {
            ret = spi_flash_read_internal(p_buf, u32offset, FLASH_BLOCK_SIZE);
            if(M2M_SUCCESS != ret) goto ERR;
            u32Sz -= FLASH_BLOCK_SIZE;
            u32offset += FLASH_BLOCK_SIZE;
            p_buf += FLASH_BLOCK_SIZE;
        } while(u32Sz > FLASH_BLOCK_SIZE);
    }
    
    ret = spi_flash_read_internal(p_buf, u32offset, u32Sz);

ERR:
    return ret;
}

/**
*    @fn            spi_flash_write
*    @brief        Proram SPI flash
*    @param[IN]    p_buf
*                    Pointer to data buffer
*    @param[IN]    u32Offset
*                    Address to write to at the SPI flash
*    @param[IN]    u32Sz
*                    Data size
*    @return        Status of execution
*/ 
int8_t spi_flash_write(uint8_t* p_buf, uint32_t u32Offset, uint32_t u32Sz)
{
#ifdef PROFILING
    uint32_t t1 = 0;
    uint32_t percent =0;
    uint32_t tpercent =0;
#endif
    int8_t ret = M2M_SUCCESS;
    uint32_t u32wsz;
    uint32_t u32off;
    uint32_t u32Blksz;
    u32Blksz = FLASH_PAGE_SZ;
    u32off = u32Offset % u32Blksz;
#ifdef PROFILING
    tpercent = (u32Sz/u32Blksz)+((u32Sz%u32Blksz)>0);
    t1 = GetTickCount();
    M2M_PRINT(">Start programming...\r\n");
#endif
    if(u32Sz<=0)
    {
        dprintf("Data size = %d",(int)u32Sz);
        GenerateErrorEvent(M2M_WIFI_FLASH_WRITE_2_ERROR);
        return -1;
    }

    if (u32off)/*first part of data in the address page*/
    {
        u32wsz = u32Blksz - u32off;
        if(spi_flash_pp(u32Offset, p_buf, (uint16_t)BSP_MIN(u32Sz, u32wsz))!=M2M_SUCCESS)
        {
            GenerateErrorEvent(M2M_WIFI_FLASH_WRITE_1_ERROR);
            return -1;
        }
        
        if (u32Sz < u32wsz) goto EXIT;
        p_buf += u32wsz;
        u32Offset += u32wsz;
        u32Sz -= u32wsz;
    }
    while (u32Sz > 0)
    {
        u32wsz = BSP_MIN(u32Sz, u32Blksz);

        /*write complete page or the remaining data*/
        if(spi_flash_pp(u32Offset, p_buf, (uint16_t)u32wsz)!=M2M_SUCCESS)
        {
            GenerateErrorEvent(M2M_WIFI_FLASH_WRITE_3_ERROR);
            return -1;
        }
        p_buf += u32wsz;
        u32Offset += u32wsz;
        u32Sz -= u32wsz;
#ifdef PROFILING
        percent++;
        printf("\r>Complete Percentage = %d%%.\r\n",((percent*100)/tpercent));
#endif
    }
EXIT:
#ifdef PROFILING
    M2M_PRINT("\rDone\t\t\t\t\t\t");
    M2M_PRINT("\n#Programming time = %f sec\r\n",(GetTickCount() - t1)/1000.0);
#endif

    return ret;
}

/**
*    @fn            spi_flash_erase
*    @brief        Erase from data from SPI flash
*    @param[IN]    u32Offset
*                    Address to write to at the SPI flash
*    @param[IN]    u32Sz
*                    Data size
*    @return        Status of execution
*    @note        Data size is limited by the SPI flash size only
*/ 
// NOTE: DO NOT DO printf() during FLASH update.  If the PC utility is being used
//       it uses the serial port for commands and data.  A prinf() in the middle
//       of that is fatal to the update.
int8_t spi_flash_erase(uint32_t u32Offset, uint32_t u32Sz)
{
    uint32_t i = 0;
    int8_t ret = M2M_SUCCESS;
    uint8_t  tmp = 0;
#ifdef PROFILING
    uint32_t t;
    t = GetTickCount();
#endif
    for(i = u32Offset; i < (u32Sz +u32Offset); i += (16*FLASH_PAGE_SZ))
    {
        ret += spi_flash_write_enable();
        ret += spi_flash_read_status_reg(&tmp);
        ret += spi_flash_sector_erase(i + 10);
        ret += spi_flash_read_status_reg(&tmp);
        do
        {
            if(ret != M2M_SUCCESS) goto ERR;
            ret += spi_flash_read_status_reg(&tmp);
        }while(tmp & 0x01);
        
    }
#ifdef PROFILING
    M2M_PRINT("#Erase time = %f sec\n", (GetTickCount()-t)/1000.0);
#endif
ERR:
    return ret;
}

/**
*    @fn            spi_flash_get_size
*    @brief        Get size of SPI Flash
*    @return        Size of Flash
*/
uint32_t spi_flash_get_size(void)
{
    uint32_t u32FlashId = 0, u32FlashPwr = 0;
    static uint32_t gu32InernalFlashSize= 0;
    
    if(!gu32InernalFlashSize)
    {
        u32FlashId = spi_flash_rdid();//spi_flash_probe();
        if(u32FlashId != 0xffffffff)
        {
            /*flash size is the third byte from the FLASH RDID*/
            u32FlashPwr = ((u32FlashId>>16)&0xff) - 0x11; /*2MBIT is the min*/
            /*That number power 2 to get the flash size*/
            gu32InernalFlashSize = 1<<u32FlashPwr;
            //M2M_INFO("Flash Size %lu Mb\n",gu32InernalFlashSize);
        }
        else
        {
            dprintf("Can't Detect Flash size\n");
        }
    }

    return gu32InernalFlashSize;
}

#if 0
static void nm_drv_init_download_mode(void)
{
    /**
        TODO:reset the chip and halt the cpu in case of no wait efuse is set (add the no wait effuse check)
    */
    if(!ISNMC3000(GET_CHIPID()))
    {
        /*Execute that function only for 1500A/B, no room in 3000, but it may be needed in 3400 no wait*/
        ChipResetAndCpuHalt();
    }

    /* Must do this after global reset to set SPI data packet size. */
    nm_spi_init();
    
    /*disable all interrupt in ROM (to disable uart) in 2b0 chip*/
    nm_write_reg(0x20300,0);
}
#endif




/**
*    @fn            spi_flash_read_status_reg
*    @brief        Read status register
*    @param[OUT]    val
                    value of status reg
*    @return        Status of execution
*/ 
static int8_t spi_flash_read_status_reg(uint8_t * val)
{
    int8_t ret = M2M_SUCCESS;
    uint8_t cmd[1];
    uint32_t reg;

    cmd[0] = 0x05;

    nm_write_reg(SPI_FLASH_DATA_CNT, 4);
    nm_write_reg(SPI_FLASH_BUF1, cmd[0]);
    nm_write_reg(SPI_FLASH_BUF_DIR, 0x01);
    nm_write_reg(SPI_FLASH_DMA_ADDR, DUMMY_REGISTER);
    nm_write_reg(SPI_FLASH_CMD_CNT, 1 | (1<<7));
    do
    {
        reg = nm_read_reg(SPI_FLASH_TR_DONE);
    }
    while(reg != 1);

    reg = (M2M_SUCCESS == ret)?(nm_read_reg(DUMMY_REGISTER)):(0);
    *val = (uint8_t)(reg & 0xff);
    return ret;
}

/**
*    @fn            spi_flash_load_to_cortus_mem
*    @brief        Load data from SPI flash into cortus memory
*    @param[IN]    u32MemAdr
*                    Cortus load address. It must be set to its AHB access address
*    @param[IN]    u32FlashAdr
*                    Address to read from at the SPI flash
*    @param[IN]    u32Sz
*                    Data size
*    @return        Status of execution
*/ 
static int8_t spi_flash_load_to_cortus_mem(uint32_t u32MemAdr, uint32_t u32FlashAdr, uint32_t u32Sz)
{
    uint8_t cmd[5];
    uint32_t    reg    = 0;
    int8_t    ret = M2M_SUCCESS;

    cmd[0] = 0x0b;
    cmd[1] = (uint8_t)(u32FlashAdr >> 16);
    cmd[2] = (uint8_t)(u32FlashAdr >> 8);
    cmd[3] = (uint8_t)(u32FlashAdr);
    cmd[4] = 0xA5;

    nm_write_reg(SPI_FLASH_DATA_CNT, u32Sz);
    nm_write_reg(SPI_FLASH_BUF1, (uint32_t)cmd[0]            | 
                                       ((uint32_t)cmd[1] << 8)      | 
                                       ((uint32_t)cmd[2] << 16)     |
                                       ((uint32_t)cmd[3] << 24));
    nm_write_reg(SPI_FLASH_BUF2, cmd[4]);
    nm_write_reg(SPI_FLASH_BUF_DIR, 0x1f);
    nm_write_reg(SPI_FLASH_DMA_ADDR, u32MemAdr);
    nm_write_reg(SPI_FLASH_CMD_CNT, 5 | (1<<7));
    do
    {
        reg = nm_read_reg(SPI_FLASH_TR_DONE);
    }
    while(reg != 1);

    return ret;
}

/**
*    @fn            spi_flash_sector_erase
*    @brief        Erase sector (4KB)
*    @param[IN]    u32FlashAdr
*                    Any memory address within the sector
*    @return        Status of execution
*/ 
static int8_t spi_flash_sector_erase(uint32_t u32FlashAdr)
{
    uint8_t cmd[4];
    uint32_t    reg    = 0;
    int8_t    ret = M2M_SUCCESS;

    cmd[0] = 0x20;
    cmd[1] = (uint8_t)(u32FlashAdr >> 16);
    cmd[2] = (uint8_t)(u32FlashAdr >> 8);
    cmd[3] = (uint8_t)(u32FlashAdr);

    nm_write_reg(SPI_FLASH_DATA_CNT, 0);
    nm_write_reg(SPI_FLASH_BUF1,  (uint32_t)cmd[0]           | 
                                        ((uint32_t)cmd[1] << 8)     | 
                                        ((uint32_t)cmd[2] << 16)    |
                                        ((uint32_t)cmd[3] << 24));
    nm_write_reg(SPI_FLASH_BUF_DIR, 0x0f);
    nm_write_reg(SPI_FLASH_DMA_ADDR, 0);
    nm_write_reg(SPI_FLASH_CMD_CNT, 4 | (1<<7));
    do
    {
        reg = nm_read_reg(SPI_FLASH_TR_DONE);
    }
    while(reg != 1);

    return ret;
}

/**
*    @fn            spi_flash_write_enable
*    @brief        Send write enable command to SPI flash
*    @return        Status of execution
*/ 
static int8_t spi_flash_write_enable(void)
{
    uint8_t cmd[1];
    uint32_t    reg    = 0;
    int8_t    ret = M2M_SUCCESS;

    cmd[0] = 0x06;

    nm_write_reg(SPI_FLASH_DATA_CNT, 0);
    nm_write_reg(SPI_FLASH_BUF1, cmd[0]);
    nm_write_reg(SPI_FLASH_BUF_DIR, 0x01);
    nm_write_reg(SPI_FLASH_DMA_ADDR, 0);
    nm_write_reg(SPI_FLASH_CMD_CNT, 1 | (1<<7));
    do
    {
        reg += nm_read_reg(SPI_FLASH_TR_DONE);
    }
    while(reg != 1);

    return ret;
}

/**
*    @fn            spi_flash_write_disable
*    @brief        Send write disable command to SPI flash
*/
static int8_t spi_flash_write_disable(void)
{
    uint8_t cmd[1];
    uint32_t    reg    = 0;
    int8_t    ret = M2M_SUCCESS;
    cmd[0] = 0x04;

    nm_write_reg(SPI_FLASH_DATA_CNT, 0);
    nm_write_reg(SPI_FLASH_BUF1, cmd[0]);
    nm_write_reg(SPI_FLASH_BUF_DIR, 0x01);
    nm_write_reg(SPI_FLASH_DMA_ADDR, 0);
    nm_write_reg(SPI_FLASH_CMD_CNT, 1 | (1<<7));
    do
    {
        reg = nm_read_reg(SPI_FLASH_TR_DONE);
    }
    while(reg != 1);

    return ret;
}

/**
*    @fn            spi_flash_page_program
*    @brief        Write data (less than page size) from cortus memory to SPI flash
*    @param[IN]    u32MemAdr
*                    Cortus data address. It must be set to its AHB access address
*    @param[IN]    u32FlashAdr
*                    Address to write to at the SPI flash
*    @param[IN]    u32Sz
*                    Data size
*/ 
static int8_t spi_flash_page_program(uint32_t u32MemAdr, uint32_t u32FlashAdr, uint32_t u32Sz)
{
    uint8_t cmd[4];
    uint32_t    reg    = 0;
    int8_t    ret = M2M_SUCCESS;

    cmd[0] = 0x02;
    cmd[1] = (uint8_t)(u32FlashAdr >> 16);
    cmd[2] = (uint8_t)(u32FlashAdr >> 8);
    cmd[3] = (uint8_t)(u32FlashAdr);

    nm_write_reg(SPI_FLASH_DATA_CNT, 0);
    nm_write_reg(SPI_FLASH_BUF1,  (uint32_t)cmd[0]            | 
                                        ((uint32_t)cmd[1] << 8)      |
                                        ((uint32_t)cmd[2] << 16)     | 
                                        ((uint32_t)cmd[3] << 24));
    nm_write_reg(SPI_FLASH_BUF_DIR, 0x0f);
    nm_write_reg(SPI_FLASH_DMA_ADDR, u32MemAdr);
    nm_write_reg(SPI_FLASH_CMD_CNT, 4 | (1<<7) | ((u32Sz & 0xfffff) << 8));
    do
    {
        reg = nm_read_reg(SPI_FLASH_TR_DONE);
        if(M2M_SUCCESS != ret) break;
    }
    while(reg != 1);

    return ret;
}

/**
*    @fn            spi_flash_read_internal
*    @brief        Read from data from SPI flash
*    @param[OUT]    p_buf
*                    Pointer to data buffer
*    @param[IN]    address
*                    Address to read from at the SPI flash
*    @param[IN]    u32Sz
*                    Data size
*/ 
static int8_t spi_flash_read_internal(uint8_t *p_buf, uint32_t address, uint32_t u32Sz)
{
    int8_t ret = M2M_SUCCESS;
    /* read size must be < 64KB */
    ret = spi_flash_load_to_cortus_mem(HOST_SHARE_MEM_BASE, address, u32Sz);
    if(M2M_SUCCESS != ret) goto ERR;
    nm_read_block(HOST_SHARE_MEM_BASE, p_buf, u32Sz);
ERR:
    return ret;
} 

/**
*    @fn            spi_flash_pp
*    @brief        Program data of size less than a page (256 bytes) at the SPI flash
*    @param[IN]    u32Offset
*                    Address to write to at the SPI flash
*    @param[IN]    p_buf
*                    Pointer to data buffer
*    @param[IN]    u32Sz
*                    Data size
*    @return        Status of execution
*/
static int8_t spi_flash_pp(uint32_t u32Offset, uint8_t *p_buf, uint16_t size)
{
    int8_t ret = M2M_SUCCESS;
    uint8_t tmp;
    spi_flash_write_enable();
    /* use shared packet memory as temp mem */
    nm_write_block(HOST_SHARE_MEM_BASE, p_buf, size);
    ret += spi_flash_page_program(HOST_SHARE_MEM_BASE, u32Offset, size);
    ret += spi_flash_read_status_reg(&tmp);
    do
    {
        if(ret != M2M_SUCCESS) goto ERR;
        ret += spi_flash_read_status_reg(&tmp);
    }while(tmp & 0x01);
    ret += spi_flash_write_disable();
ERR:
    return ret;
}

/**
*    @fn            spi_flash_rdid
*    @brief        Read SPI Flash ID
*    @return        SPI FLash ID
*/
static uint32_t spi_flash_rdid(void)
{
    unsigned char cmd[1];
    uint32_t reg = 0;
    uint32_t cnt = 0;
    int8_t    ret = M2M_SUCCESS;

    cmd[0] = 0x9f;

    nm_write_reg(SPI_FLASH_DATA_CNT, 4);
    nm_write_reg(SPI_FLASH_BUF1, cmd[0]);
    nm_write_reg(SPI_FLASH_BUF_DIR, 0x1);
    nm_write_reg(SPI_FLASH_DMA_ADDR, DUMMY_REGISTER);
    nm_write_reg(SPI_FLASH_CMD_CNT, 1 | (1<<7));
    do
    {
        reg = nm_read_reg(SPI_FLASH_TR_DONE);
        if(++cnt > 500)
        {
            //ret = M2M_ERR_INIT;
            GenerateErrorEvent(M2M_WIFI_FLASH_READ_ERROR);  // add error code if code is used
            break;
        }
    }
    while(reg != 1);
    
    reg = (M2M_SUCCESS == ret)?(nm_read_reg(DUMMY_REGISTER)):(0);
    //M2M_PRINT("Flash ID %x \n",(unsigned int)reg);
    return reg;
}

/**
*    @fn            spi_flash_unlock
*    @brief        Unlock SPI Flash
*/
#if 0
static void spi_flash_unlock(void)
{
    uint8_t tmp;
    tmp = spi_flash_read_security_reg();
    spi_flash_clear_security_flags();
    if(tmp & 0x80)
    {
        spi_flash_write_enable();
        spi_flash_gang_unblock();
    }
}
#endif


#ifdef DISABLE_UNUSED_FLASH_FUNCTIONS
/**
*    @fn            spi_flash_read_security_reg
*    @brief        Read security register
*    @return        Security register value
*/ 
static uint8_t spi_flash_read_security_reg(void)
{
    uint8_t    cmd[1];
    uint32_t    reg;
    int8_t    ret = M2M_SUCCESS;

    cmd[0] = 0x2b;

    nm_write_reg(SPI_FLASH_DATA_CNT, 1);
    nm_write_reg(SPI_FLASH_BUF1, cmd[0]);
    nm_write_reg(SPI_FLASH_BUF_DIR, 0x01);
    nm_write_reg(SPI_FLASH_DMA_ADDR, DUMMY_REGISTER);
    nm_write_reg(SPI_FLASH_CMD_CNT, 1 | (1<<7));
    do
    {
        reg = nm_read_reg(SPI_FLASH_TR_DONE);
    }
    while(reg != 1);
    reg = nm_read_reg(DUMMY_REGISTER);

    return (int8_t)reg & 0xff;
}

/**
*    @fn            spi_flash_gang_unblock
*    @brief        Unblock all flash area
*/ 
static int8_t spi_flash_gang_unblock(void)
{
    uint8_t    cmd[1];
    uint32_t    reg    = 0;
    int8_t    ret = M2M_SUCCESS;

    cmd[0] = 0x98;

    nm_write_reg(SPI_FLASH_DATA_CNT, 0);
    nm_write_reg(SPI_FLASH_BUF1, cmd[0]);
    nm_write_reg(SPI_FLASH_BUF_DIR, 0x01);
    nm_write_reg(SPI_FLASH_DMA_ADDR, 0);
    nm_write_reg(SPI_FLASH_CMD_CNT, 1 | (1<<7));
    do
    {
        reg = nm_read_reg(SPI_FLASH_TR_DONE);
    }
    while(reg != 1);

    return ret;
}

/**
*    @fn            spi_flash_clear_security_flags
*    @brief        Clear all security flags
*/ 
static int8_t spi_flash_clear_security_flags(void)
{
    uint8_t cmd[1];
    uint32_t    reg    = 0;
    int8_t    ret = M2M_SUCCESS;

    cmd[0] = 0x30;

    nm_write_reg(SPI_FLASH_DATA_CNT, 0);
    nm_write_reg(SPI_FLASH_BUF1, cmd[0]);
    nm_write_reg(SPI_FLASH_BUF_DIR, 0x01);
    nm_write_reg(SPI_FLASH_DMA_ADDR, 0);
    nm_write_reg(SPI_FLASH_CMD_CNT, 1 | (1<<7));
    do
    {
        reg = nm_read_reg(SPI_FLASH_TR_DONE);
    }
    while(reg != 1);

    return ret;
}
#endif // DISABLE_UNUSED_FLASH_FUNCTIONS

#endif // M2M_ENABLE_SPI_FLASH



