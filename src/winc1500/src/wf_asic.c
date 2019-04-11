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
#include "wf_asic.h"
#include "wf_types.h"
#include "wf_utils.h"
#include "wf_spi.h"
#include "wf_drv.h"

#define NMI_GLB_RESET_0                 (NMI_PERIPH_REG_BASE + 0x400)
#define NMI_INTR_REG_BASE               (NMI_PERIPH_REG_BASE + 0xa00)
#define NMI_PIN_MUX_0                   (NMI_PERIPH_REG_BASE + 0x408)
#define NMI_INTR_ENABLE                 (NMI_INTR_REG_BASE)
#define GET_UINT32(X,Y)                 (X[0+Y] + ((uint32_t)X[1+Y]<<8) + ((uint32_t)X[2+Y]<<16) +((uint32_t)X[3+Y]<<24))

#define TIMEOUT                         (0xfffffffful)
#define M2M_DISABLE_PS                  (0xd0ul)
#define WAKUP_TRAILS_TIMEOUT            (4)

// SPI and I2C only
#define CORT_HOST_COMM                  (0x10)
#define HOST_CORT_COMM                  (0x0b)
#define WAKE_CLK_REG                    (0x1)
#define CLOCKS_EN_REG                   (0xf)


void ChipApplyConfig(uint32_t conf)
{
    uint32_t val32 = conf;
    
#if (defined __ENABLE_PMU__) || (defined CONF_WINC_INT_PMU)
    val32 |= rHAVE_USE_PMU_BIT;
#endif
#ifdef __ENABLE_SLEEP_CLK_SRC_RTC__
    val32 |= rHAVE_SLEEP_CLK_SRC_RTC_BIT;
#elif defined __ENABLE_SLEEP_CLK_SRC_XO__
    val32 |= rHAVE_SLEEP_CLK_SRC_XO_BIT;
#endif
#ifdef __ENABLE_EXT_PA_INV_TX_RX__
    val32 |= rHAVE_EXT_PA_INV_TX_RX;
#endif
#ifdef __ENABLE_LEGACY_RF_SETTINGS__
    val32 |= rHAVE_LEGACY_RF_SETTINGS;
#endif
#ifdef M2M_DISnmi_get_chipidABLE_FIRMWARE_LOG
    val32 |= rHAVE_LOGS_DISABLED_BIT;
#endif
    val32 |= rHAVE_RESERVED1_BIT;
    do  {
        nm_write_reg(rNMI_GP_REG_1, val32);
        if(val32 != 0) 
        {        
            uint32_t reg = 0;
            reg = nm_read_reg(rNMI_GP_REG_1);
            if(reg == val32)
            {
                break;
            }
        } 
        else 
        {
            break;
        }
    } while(1);
}


void EnableInterrupts(void)
{
    uint32_t reg;

    // interrupt pin mux select
    reg = nm_read_reg(NMI_PIN_MUX_0);
    reg |= ((uint32_t) 1 << 8);
    nm_write_reg(NMI_PIN_MUX_0, reg);

    // interrupt enable
    reg = nm_read_reg(NMI_INTR_ENABLE);
    reg |= ((uint32_t) 1 << 16);
    nm_write_reg(NMI_INTR_ENABLE, reg);
}


uint32_t GetChipId(void)
{
    static uint32_t chipId = 0;

    if (chipId == 0) 
    {
        uint32_t rfrevid;

        chipId = nm_read_reg(CHIP_ID_REG);

        rfrevid = nm_read_reg(RF_REV_ID_REG);

        if (chipId == 0x1002a0)  
        {
            if (rfrevid == 0x1) 
            { 
                /* 1002A0 */
            } 
            else /* if (rfrevid == 0x2) */ /* 1002A1 */
            { 
                chipId = 0x1002a1;
            }
        } 
        else if(chipId == 0x1002b0) 
        {
            if(rfrevid == 3) 
            { 
                /* 1002B0 */
            } 
            else if(rfrevid == 4) 
            { 
                chipId = 0x1002b1;
            } 
            else /* else (rfrevid == 5) */ 
            { 
                chipId = 0x1002b2;
            }
        } 
        else if(chipId == 0x1000F0) 
        { 
            chipId = nm_read_reg(0x3B0000);
        } 
        else 
        {
            
        }

        /*M2M is by default have SPI flash*/
        chipId &= ~(0x0f0000);
        chipId |= 0x050000;
    }
    return chipId;
}

uint32_t nmi_get_rfrevid(void)
{
    return nm_read_reg(RF_REV_ID_REG);
}

void ChipSleep(void)
{
    uint32_t reg;
    
    while(1)
    {
        reg = nm_read_reg(CORT_HOST_COMM);
        if((reg & NBIT0) == 0) 
        {
            break;
        }
    }
    
    /* Clear bit 1 */
    reg = nm_read_reg(WAKE_CLK_REG);
    if(reg & NBIT1)
    {
        reg &= ~NBIT1;
        nm_write_reg(WAKE_CLK_REG, reg);
    }
    
    reg = nm_read_reg(HOST_CORT_COMM);
    if(reg & NBIT0)
    {
        reg &= ~NBIT0;
        nm_write_reg(HOST_CORT_COMM, reg);
    }
}

void ChipWake(void)
{
    volatile uint32_t reg = 0, clk_status_reg = 0, trials = 0;

    reg = nm_read_reg(HOST_CORT_COMM);
    
    if(!(reg & NBIT0))
    {
        // use bit 0 to indicate host wakeup
        nm_write_reg(HOST_CORT_COMM, reg | NBIT0);
    }
        
    reg = nm_read_reg(WAKE_CLK_REG);
    // Set bit 1 
    if(!(reg & NBIT1))
    {
        nm_write_reg(WAKE_CLK_REG, reg | NBIT1);
    }

    do
    {
        clk_status_reg = nm_read_reg(CLOCKS_EN_REG);
        if(clk_status_reg & NBIT2) 
        {
            break;
        }
        DelayMs(2);
        trials++;
        if(trials > WAKUP_TRAILS_TIMEOUT)
        {
            dprintf("Failed to wakup the chip\n");
            GenerateErrorEvent(M2M_WIFI_FAILED_TO_WAKE_CHIP_ERROR);
            return;
        }
    } while(1);
    
    // workaround sometimes spi fail to read clock regs after reading/writing clockless registers
    nm_spi_reset();
    
}

void ChipReset(void)
{
    nm_write_reg(NMI_GLB_RESET_0, 0);
    DelayMs(50);
}

void ChipHalt(void)
{
    uint32_t reg = 0;

    reg = nm_read_reg(0x1118);
    reg |= (1 << 0);
    nm_write_reg(0x1118, reg);
    reg = nm_read_reg(NMI_GLB_RESET_0);
    if ((reg & (1ul << 10)) == (1ul << 10)) 
    {
        reg &= ~(1ul << 10);
        nm_write_reg(NMI_GLB_RESET_0, reg);
        reg = nm_read_reg(NMI_GLB_RESET_0);
    }
}

#if defined(M2M_ENABLE_SPI_FLASH)
void ChipResetAndCpuHalt(void)
{
    ChipWake();
    ChipReset();
    ChipHalt();
}
#endif // M2M_ENABLE_SPI_FLASH

void ChipDeinit(void)
{
    uint32_t reg = 0;

    // stop the firmware, need a re-download
    reg = nm_read_reg(NMI_GLB_RESET_0);
    reg &= ~(1 << 10);
    nm_write_reg(NMI_GLB_RESET_0, reg);
}

void GetMacAddress(uint8_t *pu8MacAddr)
{
    uint32_t    reg;
    uint8_t     mac[6];
    t_gpRegs  gpReg = {0};

    reg = nm_read_reg(rNMI_GP_REG_2);

    nm_read_block(reg | 0x30000, (uint8_t*)&gpReg,sizeof(t_gpRegs));
    reg = FIX_ENDIAN_32(gpReg.macEfuseMib);

    reg &= 0x0000ffff;
    nm_read_block(reg | 0x30000, mac, 6);
    memcpy(pu8MacAddr, mac, 6);
}
