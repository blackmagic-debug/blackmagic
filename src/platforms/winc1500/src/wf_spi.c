//==============================================================================
// INCLUDES
//==============================================================================         
#include "winc1500_api.h"
#include "wf_common.h"
#include "wf_spi.h"

//==============================================================================
// CONSTANTS
//==============================================================================      
#define SUPPORTED_WINC1500_CHIP_REVISION      ((uint32_t)0x001003a0)

#define NMI_PERIPH_REG_BASE 0x1000
#define NMI_INTR_REG_BASE (NMI_PERIPH_REG_BASE+0xa00)
#define CHIP_ID_REG    (NMI_PERIPH_REG_BASE)
#define NMI_PIN_MUX_0 (NMI_PERIPH_REG_BASE + 0x408)
#define NMI_INTR_ENABLE (NMI_INTR_REG_BASE)
#define NMI_SPI_REG_BASE 0xe800
#define NMI_SPI_CTL (NMI_SPI_REG_BASE)
#define NMI_SPI_MASTER_DMA_ADDR (NMI_SPI_REG_BASE+0x4)
#define NMI_SPI_MASTER_DMA_COUNT (NMI_SPI_REG_BASE+0x8)
#define NMI_SPI_SLAVE_DMA_ADDR (NMI_SPI_REG_BASE+0xc)
#define NMI_SPI_SLAVE_DMA_COUNT (NMI_SPI_REG_BASE+0x10)
#define NMI_SPI_TX_MODE (NMI_SPI_REG_BASE+0x20)
#define NMI_SPI_PROTOCOL_CONFIG (NMI_SPI_REG_BASE+0x24)
#define NMI_SPI_INTR_CTL (NMI_SPI_REG_BASE+0x2c)
#define NMI_SPI_PROTOCOL_OFFSET (NMI_SPI_PROTOCOL_CONFIG-NMI_SPI_REG_BASE)
#define NMI_SPI_INTR_CTL (NMI_SPI_REG_BASE+0x2c)
#define NMI_SPI_MISC_CTRL (NMI_SPI_REG_BASE+0x48)


#define SPI_BASE                NMI_SPI_REG_BASE

#define CMD_DMA_WRITE           0xc1
#define CMD_DMA_READ            0xc2
#define CMD_INTERNAL_WRITE      0xc3
#define CMD_INTERNAL_READ       0xc4
#define CMD_TERMINATE           0xc5
#define CMD_REPEAT              0xc6
#define CMD_DMA_EXT_WRITE       0xc7
#define CMD_DMA_EXT_READ        0xc8
#define CMD_SINGLE_WRITE        0xc9
#define CMD_SINGLE_READ         0xca
#define CMD_RESET               0xcf

#define N_OK                    1
#define N_FAIL                  0
#define N_RESET                 -1
#define N_RETRY                 -2

#define SPI_RESP_RETRY_COUNT    (10)
#define SPI_RETRY_COUNT         (10)

#define DATA_PKT_SZ_256         256
#define DATA_PKT_SZ_512         512
#define DATA_PKT_SZ_1K          1024
#define DATA_PKT_SZ_4K          (4 * 1024)
#define DATA_PKT_SZ_8K          (8 * 1024)
#define DATA_PKT_SZ             DATA_PKT_SZ_8K

#define NM_BUS_MAX_TRX_SZ       256
#define MAX_TRX_CFG_SZ          8

//==============================================================================
// LOCAL GLOBALS
//==============================================================================         
static uint8_t g_crcOff = 0;

// CRC7
static const uint8_t crc7_syndrome_table[256] = {
    0x00, 0x09, 0x12, 0x1b, 0x24, 0x2d, 0x36, 0x3f,
    0x48, 0x41, 0x5a, 0x53, 0x6c, 0x65, 0x7e, 0x77,
    0x19, 0x10, 0x0b, 0x02, 0x3d, 0x34, 0x2f, 0x26,
    0x51, 0x58, 0x43, 0x4a, 0x75, 0x7c, 0x67, 0x6e,
    0x32, 0x3b, 0x20, 0x29, 0x16, 0x1f, 0x04, 0x0d,
    0x7a, 0x73, 0x68, 0x61, 0x5e, 0x57, 0x4c, 0x45,
    0x2b, 0x22, 0x39, 0x30, 0x0f, 0x06, 0x1d, 0x14,
    0x63, 0x6a, 0x71, 0x78, 0x47, 0x4e, 0x55, 0x5c,
    0x64, 0x6d, 0x76, 0x7f, 0x40, 0x49, 0x52, 0x5b,
    0x2c, 0x25, 0x3e, 0x37, 0x08, 0x01, 0x1a, 0x13,
    0x7d, 0x74, 0x6f, 0x66, 0x59, 0x50, 0x4b, 0x42,
    0x35, 0x3c, 0x27, 0x2e, 0x11, 0x18, 0x03, 0x0a,
    0x56, 0x5f, 0x44, 0x4d, 0x72, 0x7b, 0x60, 0x69,
    0x1e, 0x17, 0x0c, 0x05, 0x3a, 0x33, 0x28, 0x21,
    0x4f, 0x46, 0x5d, 0x54, 0x6b, 0x62, 0x79, 0x70,
    0x07, 0x0e, 0x15, 0x1c, 0x23, 0x2a, 0x31, 0x38,
    0x41, 0x48, 0x53, 0x5a, 0x65, 0x6c, 0x77, 0x7e,
    0x09, 0x00, 0x1b, 0x12, 0x2d, 0x24, 0x3f, 0x36,
    0x58, 0x51, 0x4a, 0x43, 0x7c, 0x75, 0x6e, 0x67,
    0x10, 0x19, 0x02, 0x0b, 0x34, 0x3d, 0x26, 0x2f,
    0x73, 0x7a, 0x61, 0x68, 0x57, 0x5e, 0x45, 0x4c,
    0x3b, 0x32, 0x29, 0x20, 0x1f, 0x16, 0x0d, 0x04,
    0x6a, 0x63, 0x78, 0x71, 0x4e, 0x47, 0x5c, 0x55,
    0x22, 0x2b, 0x30, 0x39, 0x06, 0x0f, 0x14, 0x1d,
    0x25, 0x2c, 0x37, 0x3e, 0x01, 0x08, 0x13, 0x1a,
    0x6d, 0x64, 0x7f, 0x76, 0x49, 0x40, 0x5b, 0x52,
    0x3c, 0x35, 0x2e, 0x27, 0x18, 0x11, 0x0a, 0x03,
    0x74, 0x7d, 0x66, 0x6f, 0x50, 0x59, 0x42, 0x4b,
    0x17, 0x1e, 0x05, 0x0c, 0x33, 0x3a, 0x21, 0x28,
    0x5f, 0x56, 0x4d, 0x44, 0x7b, 0x72, 0x69, 0x60,
    0x0e, 0x07, 0x1c, 0x15, 0x2a, 0x23, 0x38, 0x31,
    0x46, 0x4f, 0x54, 0x5d, 0x62, 0x6b, 0x70, 0x79
};

//==============================================================================
// LOCAL FUNCTION PROTOTYPES
//==============================================================================         
static void SpiInitPktSize(void);
static void nmi_spi_read(uint8_t* b, uint16_t sz);
static void nmi_spi_write(uint8_t* b, uint16_t sz);
static uint8_t crc7_byte(uint8_t crc, uint8_t data);
static uint8_t crc7(uint8_t crc, const uint8_t *buffer, uint32_t len);
static int8_t spi_cmd(uint8_t cmd, uint32_t adr, uint32_t data, uint32_t sz,uint8_t clockless);
static int8_t spi_data_rsp(void);
static int8_t spi_cmd_rsp(uint8_t cmd);
static int8_t spi_data_read(uint8_t *b, uint16_t sz,uint8_t clockless);
static int8_t spi_data_write(uint8_t *b, uint16_t sz);
static int8_t nm_spi_write(uint32_t addr, uint8_t *buf, uint16_t size);

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

void nm_spi_init(void)
{
    uint32_t reg;

    // configure protocol
    g_crcOff = 0;

    // TODO: We can remove the CRC trials if there is a definite way to reset
    // the SPI to it's initial value.
    reg = spi_read_reg(NMI_SPI_PROTOCOL_CONFIG);

    if(g_crcOff == 0)
    {
        reg &= ~0xc;    /* disable crc checking */
        reg &= ~0x70;
        reg |= (0x5 << 4);
        spi_write_reg(NMI_SPI_PROTOCOL_CONFIG, reg);
        g_crcOff = 1;
    }

    // make sure can read back chip id correctly
    uint32_t chipId = spi_read_reg(CHIP_ID_REG);
    if (chipId != SUPPORTED_WINC1500_CHIP_REVISION)
    {
        dprintf("Invalid chip rev, expected %lx, read %lx\n", SUPPORTED_WINC1500_CHIP_REVISION, chipId);
        GenerateErrorEvent(M2M_WIFI_INVALID_CHIP_REV_ERROR);
        return;
    }

    SpiInitPktSize();
}

void spi_write_reg(uint32_t addr, uint32_t data)
{
    uint8_t retry = SPI_RETRY_COUNT;
    uint8_t cmd = CMD_SINGLE_WRITE;
    int8_t result = N_OK;
    uint8_t clockless = 0;
    
_RETRY_:    
    if (addr <= 0x30)
    {
        /**
        NMC1000 clockless registers.
        **/
        cmd = CMD_INTERNAL_WRITE;
        clockless = 1;
    }
    else
    {
        cmd = CMD_SINGLE_WRITE;
        clockless = 0;
    }

    spi_cmd(cmd, addr, data, 4, clockless);
    result = spi_cmd_rsp(cmd);
    if (result != N_OK)
    {
        goto _FAIL_;
    }
    
_FAIL_:
    if(result != N_OK)
    {
        DelayMs(1);
        nm_spi_reset();
        dprintf("Reset and retry %d %lx %lx\n",retry, addr, data);
        DelayMs(10);
        retry--;
        if(retry) goto _RETRY_;
    }      
}

void nm_spi_reset(void)
{
    spi_cmd(CMD_RESET, 0, 0, 0, 0);
    spi_cmd_rsp(CMD_RESET);
}


void spi_write_block(uint32_t startAddress, uint8_t *buf, uint16_t size)
{
    nm_spi_write(startAddress, buf, size);
}

uint32_t spi_read_reg(uint32_t addr)
{
    uint8_t retry = SPI_RETRY_COUNT;
    int8_t  result = N_OK;
    uint8_t cmd = CMD_SINGLE_READ;
    uint8_t tmp[4];
    uint8_t clockless = 0;
    uint32_t reg = 0;
    
_RETRY_:
    if (addr <= 0xff)
    {
        /**
        NMC1000 clockless registers.
        **/
        cmd = CMD_INTERNAL_READ;
        clockless = 1;
    }
    else
    {
        cmd = CMD_SINGLE_READ;
        clockless = 0;
    }

    spi_cmd(cmd, addr, 0, 4, clockless);
    result = spi_cmd_rsp(cmd);
    if (result != N_OK)
    {
        goto _FAIL_;
    }

    /* to avoid endianess issues */
    result = spi_data_read(&tmp[0], 4, clockless);
    if (result != N_OK)
    {
        goto _FAIL_;
    }

    reg = tmp[0] |
          ((uint32_t)tmp[1] << 8) |
          ((uint32_t)tmp[2] << 16) |
          ((uint32_t)tmp[3] << 24);

_FAIL_:
    if(result != N_OK)
    {
        DelayMs(1);
        nm_spi_reset();
        dprintf("Reset and retry %d\n",retry);
        DelayMs(1);
        retry--;
        if(retry) goto _RETRY_;
    }    
    return reg;
}

static int8_t nm_spi_read(uint32_t addr, uint8_t *buf, uint16_t size)
{
    uint8_t cmd = CMD_DMA_EXT_READ;
    int8_t result;
    uint8_t retry = SPI_RETRY_COUNT;
    uint8_t tmp[2];
    uint8_t single_byte_workaround = 0;
    
_RETRY_:
    /**
        Command
    **/

    if (size == 1)
    {
        //Workaround hardware problem with single byte transfers over SPI bus
        size = 2;
        single_byte_workaround = 1;
    }        
    result = spi_cmd(cmd, addr, 0, size,0);
    if (result != N_OK) {
        dprintf("[nmi spi]: Failed cmd, read block (%08x)...\r\n", (unsigned int)addr);
        goto _FAIL_;
    }

    result = spi_cmd_rsp(cmd);
    if (result != N_OK) {
        dprintf("[nmi spi]: Failed cmd response, read block (%08x)...\r\n", (unsigned int)addr);
        spi_cmd(CMD_RESET, 0, 0, 0, 0);
        goto _FAIL_;
    }

    /**
        Data
    **/
    if (single_byte_workaround)
    {
        result = spi_data_read(tmp, size,0);
        buf[0] = tmp[0];
    }
    else
    {
        result = spi_data_read(buf, size,0);
    }
    
    if (result != N_OK) {
        dprintf("[nmi spi]: Failed block data read...\r\n");
        goto _FAIL_;
    }

_FAIL_:
    if(result != N_OK)
    {
        DelayMs(1);
        nm_spi_reset();
        dprintf("Reset and retry %d %lx, %x\n",retry, addr, size);
        DelayMs(1);
        retry--;
        if(retry) goto _RETRY_;
    }

    return N_OK;
}

static void SpiInitPktSize(void)
{
    uint32_t val32;

    /* Make sure SPI max. packet size fits the defined DATA_PKT_SZ.  */
    val32 = nm_spi_read_reg(SPI_BASE+0x24);
    val32 &= ~(0x7 << 4);
    switch(DATA_PKT_SZ)
    {
    case 256:  val32 |= (0 << 4); break;
    case 512:  val32 |= (1 << 4); break;
    case 1024: val32 |= (2 << 4); break;
    case 2048: val32 |= (3 << 4); break;
    case 4096: val32 |= (4 << 4); break;
    case 8192: val32 |= (5 << 4); break;

    }
    spi_write_reg(SPI_BASE+0x24, val32);
}



void nm_spi_deinit(void)
{
    g_crcOff = 0;
}

uint32_t nm_spi_read_reg(uint32_t address)
{
    uint32_t reg;

    reg = spi_read_reg(address);

    return reg;
}

void nm_spi_write_reg(uint32_t address, uint32_t value)
{
    spi_write_reg(address, value);
}

void spi_read_block(uint32_t startAddress, uint8_t *p_buf, uint16_t size)
{
    nm_spi_read(startAddress, p_buf, size);
}

static void nmi_spi_read(uint8_t* b, uint16_t sz)
{
    m2mStub_PinSet_SPI_SS(M2M_WIFI_PIN_LOW);
    m2mStub_SpiTxRx(NULL, 0, b, sz);   
    m2mStub_PinSet_SPI_SS(M2M_WIFI_PIN_HIGH);
}

static void nmi_spi_write(uint8_t* b, uint16_t sz)
{
    m2mStub_PinSet_SPI_SS(M2M_WIFI_PIN_LOW);
    m2mStub_SpiTxRx(b, sz, NULL, 0);
    m2mStub_PinSet_SPI_SS(M2M_WIFI_PIN_HIGH);    
}

static uint8_t crc7_byte(uint8_t crc, uint8_t data)
{
    return crc7_syndrome_table[(crc << 1) ^ data];
}

static uint8_t crc7(uint8_t crc, const uint8_t *buffer, uint32_t len)
{
    while (len--)
        crc = crc7_byte(crc, *buffer++);
    return crc;
}

static int8_t spi_cmd(uint8_t cmd, uint32_t adr, uint32_t data, uint32_t sz,uint8_t clockless)
{
    uint8_t bc[9];
    uint8_t len = 5;
    int8_t result = N_OK;

    bc[0] = cmd;
    switch (cmd) {
    case CMD_SINGLE_READ:                /* single word (4 bytes) read */
        bc[1] = (uint8_t)(adr >> 16);
        bc[2] = (uint8_t)(adr >> 8);
        bc[3] = (uint8_t)adr;
        len = 5;
        break;
    case CMD_INTERNAL_READ:            /* internal register read */
        bc[1] = (uint8_t)(adr >> 8);
        if(clockless)  bc[1] |= (1 << 7);
        bc[2] = (uint8_t)adr;
        bc[3] = 0x00;
        len = 5;
        break;
    case CMD_TERMINATE:                    /* termination */
        bc[1] = 0x00;
        bc[2] = 0x00;
        bc[3] = 0x00;
        len = 5;
        break;
    case CMD_REPEAT:                        /* repeat */
        bc[1] = 0x00;
        bc[2] = 0x00;
        bc[3] = 0x00;
        len = 5;
        break;
    case CMD_RESET:                            /* reset */
        bc[1] = 0xff;
        bc[2] = 0xff;
        bc[3] = 0xff;
        len = 5;
        break;
    case CMD_DMA_WRITE:                    /* dma write */
    case CMD_DMA_READ:                    /* dma read */
        bc[1] = (uint8_t)(adr >> 16);
        bc[2] = (uint8_t)(adr >> 8);
        bc[3] = (uint8_t)adr;
        bc[4] = (uint8_t)(sz >> 8);
        bc[5] = (uint8_t)(sz);
        len = 7;
        break;
    case CMD_DMA_EXT_WRITE:        /* dma extended write */
    case CMD_DMA_EXT_READ:            /* dma extended read */
        bc[1] = (uint8_t)(adr >> 16);
        bc[2] = (uint8_t)(adr >> 8);
        bc[3] = (uint8_t)adr;
        bc[4] = (uint8_t)(sz >> 16);
        bc[5] = (uint8_t)(sz >> 8);
        bc[6] = (uint8_t)(sz);
        len = 8;
        break;
    case CMD_INTERNAL_WRITE:        /* internal register write */
        bc[1] = (uint8_t)(adr >> 8);
        if(clockless)  bc[1] |= (1 << 7);
        bc[2] = (uint8_t)(adr);
        bc[3] = (uint8_t)(data >> 24);
        bc[4] = (uint8_t)(data >> 16);
        bc[5] = (uint8_t)(data >> 8);
        bc[6] = (uint8_t)(data);
        len = 8;
        break;
    case CMD_SINGLE_WRITE:            /* single word write */
        bc[1] = (uint8_t)(adr >> 16);
        bc[2] = (uint8_t)(adr >> 8);
        bc[3] = (uint8_t)(adr);
        bc[4] = (uint8_t)(data >> 24);
        bc[5] = (uint8_t)(data >> 16);
        bc[6] = (uint8_t)(data >> 8);
        bc[7] = (uint8_t)(data);
        len = 9;
        break;
        
    default:
        result = N_FAIL;
        break;
    }

    if (result) 
    {
        if (!g_crcOff)
        {
            bc[len-1] = (crc7(0x7f, (const uint8_t *)&bc[0], len-1)) << 1;
        }
        else
        {
            len-=1;
        }
        
        nmi_spi_write(bc, len);
    }

    return result;
}

static int8_t spi_data_rsp(void)
{
    uint8_t len;
    uint8_t rsp[3];
    int8_t result = N_OK;

    if (!g_crcOff)
        len = 2;
    else
        len = 3;

    nmi_spi_read(&rsp[0], len);
        
    if((rsp[len-1] != 0)||(rsp[len-2] != 0xC3))
    {
        dprintf("[nmi spi]: Failed data response read, %x %x %x\n",rsp[0],rsp[1],rsp[2]);
        result = N_FAIL;
        goto _fail_;
    }
_fail_:

    return result;
}

static int8_t spi_cmd_rsp(uint8_t cmd)
{
    uint8_t rsp;
    int8_t result = N_OK;
    int8_t s8RetryCnt;

    /**
        Command/Control response
    **/
    if ((cmd == CMD_RESET)      ||
        (cmd == CMD_TERMINATE)  ||
        (cmd == CMD_REPEAT)) 
    {
        nmi_spi_read(&rsp, 1);
    }

    /* wait for response */
    s8RetryCnt = SPI_RESP_RETRY_COUNT;
    do
    {
        nmi_spi_read(&rsp, 1);  
    } while((rsp != cmd) && (s8RetryCnt-- > 0));
    
    if (rsp != cmd)
    {
        result = N_FAIL;
        goto _fail_;
    }

    /**
        State response
    **/
    /* wait for response */
    s8RetryCnt = SPI_RESP_RETRY_COUNT;
    do
    {
        nmi_spi_read(&rsp, 1);
    } while((rsp != 0x00) && (s8RetryCnt-- > 0));
    
    if (rsp != 0x00)
    {
        result = N_FAIL;
        goto _fail_;
    }
_fail_:
    return result;
}

static int8_t spi_data_read(uint8_t *b, uint16_t sz,uint8_t clockless)
{
    int16_t retry, ix, nbytes;
    int8_t result = N_OK;
    uint8_t crc[2];
    uint8_t rsp;

    /**
        Data
    **/
    ix = 0;
    do {
        if (sz <= DATA_PKT_SZ)
            nbytes = sz;
        else
            nbytes = DATA_PKT_SZ;

        /**
            Data Respnose header
        **/
        retry = SPI_RESP_RETRY_COUNT;
        do {
            nmi_spi_read(&rsp, 1);
            if (((rsp >> 4) & 0xf) == 0xf)
            {
                break;
            }
        } while (retry--);

        if (retry <= 0) {
            dprintf("[nmi spi]: Failed data response read...(%02x)\r\n", rsp);
            result = N_FAIL;
            break;
        }

        /**
            Read bytes
        **/
        nmi_spi_read(&b[ix], nbytes);
        if(!clockless)
        {
            /**
            Read Crc
            **/
            if (!g_crcOff) 
            {
                nmi_spi_read(crc, 2);
            }
        }
        ix += nbytes;
        sz -= nbytes;

    } while (sz);

    return result;
}

static int8_t spi_data_write(uint8_t *b, uint16_t sz)
{
    int16_t ix;
    uint16_t nbytes;
    int8_t result = 1;
    uint8_t cmd, order, crc[2] = {0};
    //uint8_t rsp;

    /**
        Data
    **/
    ix = 0;
    do {
        if (sz <= DATA_PKT_SZ)
            nbytes = sz;
        else
            nbytes = DATA_PKT_SZ;

        /**
            Write command
        **/
        cmd = 0xf0;
        if (ix == 0)  {
            if (sz <= DATA_PKT_SZ)
                order = 0x3;
            else
                order = 0x1;
        } else {
            if (sz <= DATA_PKT_SZ)
                order = 0x3;
            else
                order = 0x2;
        }
        cmd |= order;
        nmi_spi_write(&cmd, 1);

        /**
            Write data
        **/
        nmi_spi_write(&b[ix], nbytes);

        /**
            Write Crc
        **/
        if (!g_crcOff) 
        {
            nmi_spi_write(crc, 2);
        }

        ix += nbytes;
        sz -= nbytes;
    } while (sz);


    return result;
}

static int8_t nm_spi_write(uint32_t addr, uint8_t *buf, uint16_t size)
{
    int8_t result;
    uint8_t retry = SPI_RETRY_COUNT;
    uint8_t cmd = CMD_DMA_EXT_WRITE;

_RETRY_:
    /**
        Command
    **/
    //Workaround hardware problem with single byte transfers over SPI bus
    if (size == 1)
        size = 2;

    result = spi_cmd(cmd, addr, 0, size,0);
    if (result != N_OK) {
        dprintf("[nmi spi]: Failed cmd, write block (%08x)...\r\n", (unsigned int)addr);
        goto _FAIL_;
    }

    result = spi_cmd_rsp(cmd);
    if (result != N_OK) {
        dprintf("[nmi spi ]: Failed cmd response, write block (%08x)...\r\n", (unsigned int)addr);
        goto _FAIL_;
    }

    /**
        Data
    **/
    result = spi_data_write(buf, size);
    if (result != N_OK) {
        dprintf("[nmi spi]: Failed block data write...\r\n");
        goto _FAIL_;
    }
    /**
        Data RESP
    **/
    result = spi_data_rsp();
    if (result != N_OK) {
        dprintf("[nmi spi]: Failed block data write...\r\n");
        goto _FAIL_;
    }
    
_FAIL_:
    if(result != N_OK)
    {
        DelayMs(1);
        nm_spi_reset();
        dprintf("Reset and retry %d\n",retry);
        DelayMs(1);
        retry--;
        if(retry) goto _RETRY_;
    }

    return result;
}


uint32_t nm_read_reg(uint32_t regAddress)
{
    uint32_t reg;

    reg = spi_read_reg(regAddress);
    reg = FIX_ENDIAN_32(reg);
    return reg;
}

void nm_write_reg(uint32_t regAddress, uint32_t value)
{
    value = FIX_ENDIAN_32(value);
    spi_write_reg(regAddress, value);
}


void nm_read_block(uint32_t startAddress, uint8_t *p_buf, uint32_t size)
{
    uint16_t maxTransferSize = NM_BUS_MAX_TRX_SZ - MAX_TRX_CFG_SZ;
    uint32_t offset = 0;

    for(;;)
    {
        if(size <= maxTransferSize)
        {
            spi_read_block(startAddress, &p_buf[offset], (uint16_t)size);
            break;
        }
        else
        {
            spi_read_block(startAddress, &p_buf[offset], maxTransferSize);
            size -= maxTransferSize;
            offset += maxTransferSize;
            startAddress += maxTransferSize;
        }
    }
}

void nm_write_block(uint32_t startAddress, uint8_t *p_buf, uint32_t size)
{
    uint16_t maxTransferSize = NM_BUS_MAX_TRX_SZ - MAX_TRX_CFG_SZ;
    uint32_t offset = 0;

    for(;;)
    {
        if(size <= maxTransferSize)
        {
            spi_write_block(startAddress, &p_buf[offset], (uint16_t)size);
            break;
        }
        else
        {
            spi_write_block(startAddress, &p_buf[offset], maxTransferSize);
            size -= maxTransferSize;
            offset += maxTransferSize;
            startAddress += maxTransferSize;
        }
    }
}


