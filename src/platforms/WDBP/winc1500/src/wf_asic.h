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

#ifndef __WF_ASIC_H
#define __WF_ASIC_H

#ifdef __cplusplus
     extern "C" {
 #endif

/*DOM-IGNORE-BEGIN*/         
         
//==============================================================================
// CONSTANTS
//==============================================================================         
#define NMI_PERIPH_REG_BASE     0x1000
#define CHIP_ID_REG             (NMI_PERIPH_REG_BASE)
#define RF_REV_ID_REG           (0x13f4)
#define rNMI_GP_REG_0           (0x149c)
#define rNMI_GP_REG_1           (0x14A0)
#define rNMI_GP_REG_2           (0xc0008)
#define rNMI_GLB_RESET          (0x1400)
#define rNMI_BOOT_RESET_MUX     (0x1118)
#define NMI_STATE_REG           (0x108c)
#define BOOTROM_REG             (0xc000c)
#define NMI_REV_REG             (0x207ac)    /*Also, Used to load ATE firmware from SPI Flash and to ensure that it is running too*/
#define NMI_REV_REG_ATE         (0x1048)     /*Revision info register in case of ATE FW*/
#define M2M_WAIT_FOR_HOST_REG   (0x207bc)
#define M2M_FINISH_INIT_STATE   0x02532636UL
#define M2M_FINISH_BOOT_ROM     0x10add09eUL
#define M2M_START_FIRMWARE      0xef522f61UL
#define M2M_START_PS_FIRMWARE   0x94992610UL
#define M2M_ATE_FW_START_VALUE  (0x3C1CD57D)    /*Also, Change this value in boot_firmware if it will be changed here*/
#define M2M_ATE_FW_IS_UP_VALUE  (0xD75DC1C3)    /*Also, Change this value in ATE (Burst) firmware if it will be changed here*/

#define REV_2B0                 (0x2B0)
#define REV_B0                  (0x2B0)
#define REV_3A0                 (0x3A0)
#define GET_CHIPID()            GetChipId()
#define ISNMC1000(id)           (((id & 0xfffff000) == 0x100000) ? 1 : 0)
#define ISNMC1500(id)           (((id & 0xfffff000) == 0x150000) ? 1 : 0)
#define ISNMC3000(id)           ((((id) & 0xfff00000) == 0x300000) ? 1 : 0)         
#define REV(id)                 (((id) & 0x00000fff))
#define EFUSED_MAC(value)       (value & 0xffff0000)

#define rHAVE_SDIO_IRQ_GPIO_BIT     (NBIT0)
#define rHAVE_USE_PMU_BIT           (NBIT1)
#define rHAVE_SLEEP_CLK_SRC_RTC_BIT (NBIT2)
#define rHAVE_SLEEP_CLK_SRC_XO_BIT  (NBIT3)
#define rHAVE_EXT_PA_INV_TX_RX      (NBIT4)
#define rHAVE_LEGACY_RF_SETTINGS    (NBIT5)
#define rHAVE_LOGS_DISABLED_BIT     (NBIT6)
#define rHAVE_ETHERNET_MODE_BIT     (NBIT7)
#define rHAVE_RESERVED1_BIT         (NBIT8)         

//==============================================================================
// DATA TYPES
//==============================================================================         
typedef struct
{
    uint32_t macEfuseMib;
    uint32_t firmwareOtaRev;
} t_gpRegs;

//==============================================================================
// FUNCTION PROTOTYPES
//==============================================================================         

void ChipSleep(void);
void ChipWake(void);
void EnableInterrupts(void);
uint32_t GetChipId(void);
#if 0
void CpuHalt(void);
void RestorePmuSettingsAfterGlobalReset(void);
void UpdatePll(void);
#endif

void ChipReset(void);
void ChipResetAndCpuHalt(void);
void ChipHalt(void);
#if 0
void WaitForBootRom(void);
#endif
#if 0
void WaitForFirmwareStart(void);

#endif
void ChipDeinit(void);

#if 0
void set_gpio_dir(uint8_t gpio, uint8_t dir);
void set_gpio_val(uint8_t gpio, uint8_t val);
void get_gpio_val(uint8_t gpio, uint8_t* val);
void pullup_ctrl(uint32_t pinmask, uint8_t enable);
#endif
void GetMacAddress(uint8_t *pu8MacAddr);
void ChipApplyConfig(uint32_t conf);

#ifdef __cplusplus
    }
 #endif

/*DOM-IGNORE-END*/     
     
#endif    // __WF_ASIC_H
