/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2022 1BitSquared <info@1bitsquared.com>
 * Written by Rafael Silva <perigoso@riseup.net>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* Support for Renesas RA family of microcontrollers (Arm Core) */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"
#include "adiv5.h"
#include "renesas_ra.h"

const uint32_t host_system_clock_frequency=100000000; // 100Mhz

#define RENESAS_PARTID_RA2A1 0x01b0U
#define RENESAS_PARTID_RA4M2 0x0340U
#define RENESAS_PARTID_RA4M3 0x0310U
#define RENESAS_PARTID_RA6M2 0x0150U

/*
 * Part numbering scheme
 *
 *  R7   F   A   xx   x   x   x   x   x   xx
 * \__/ \_/ \_/ \__/ \_/ \_/ \_/ \_/ \_/ \__/
 *  |    |   |   |    |   |   |   |   |   |
 *  |    |   |   |    |   |   |   |   |   \_ Package type
 *  |    |   |   |    |   |   |   |   \_____ Quality Grade
 *  |    |   |   |    |   |   |   \_________ Operating temperature
 *  |    |   |   |    |   |   \_____________ Code flash memory size
 *  |    |   |   |    |   \_________________ Feature set
 *  |    |   |   |    \_____________________ Group number
 *  |    |   |   \__________________________ Series name
 *  |    |   \______________________________ family (A: RA)
 *  |    \__________________________________ Flash memory
 *  \_______________________________________ Renesas microcontroller (always 'R7')
 *
 * Renesas Flash MCUs have an internal 16 byte read only register that stores
 * the part number, the code is stored ascii encoded, starting from the lowest memory address
 * except for pnrs stored in 'FIXED_PNR1', where the code is stored in reverse order (but the last 3 bytes are still 0x20 aka ' ')
 */


/* For future reference, if we want to add an info command
 *
 * Package type
 * FP: LQFP 100 pins 0.5 mm pitch
 * FN: LQFP 80 pins 0.5 mm pitch
 * FM: LQFP 64 pins 0.5 mm pitch
 * FL: LQFP 48 pins 0.5 mm pitch
 * NE: HWQFN 48 pins 0.5 mm pitch
 * FK: LQFP 64 pins 0.8 mm pitch
 * BU: BGA 64 pins 0.4 mm pitch
 * LM: LGA 36 pins 0.5 mm pitch
 * FJ: LQFP 32 pins 0.8 mm pitch
 * NH: HWQFN 32 pins 0.5 mm pitch
 * BV: WLCSP 25 pins 0.4 mm pitch
 * BT: BGA 36 pins
 * NK: HWQFN 24 pins 0.5 mm pitch
 * NJ: HWQFN 20 pins 0.5 mm pitch
 * BY: WLCSP 16 pins 0.4 mm pitch
 * NF: QFN 40 pins
 * LJ: LGA 100 pins
 * NB: QFN 64 pins
 * FB: LQFP 144 pins
 * NG: QFN 56 pins
 * LK: LGA 145 pins
 * BG: BGA 176 pins
 * FC: LQFP 176 pins
 *
 * Quality ID
 * C: Industrial applications
 * D: Consumer applications
 *
 * Operating temperature
 * 2: -40°C to +85°C
 * 3: -40°C to +105°C
 * 4: -40°C to +125°C
 */

/* PNR/UID location by series
 * newer series have a 'Flash Root Table'
 * older series have a fixed location in the flash memory
 *
 * ra2l1 - Fixed location 1
 * ra2e1 - Fixed location 1
 * ra2e2 - Fixed location 1
 * ra2a1 - Flash Root Table *undocumented
 * ra4m1 - Flash Root Table *undocumented
 * ra4m2 - Fixed location 2 *undocumented
 * ra4m3 - Fixed location 2 *undocumented
 * ra4e1 - Fixed location 2
 * ra4e2 - Fixed location 2
 * ra4w1 - Flash Root Table *undocumented
 * ra6m1 - Flash Root Table
 * ra6m2 - Flash Root Table
 * ra6m3 - Flash Root Table
 * ra6m4 - Fixed location 2
 * ra6m5 - Fixed location 2
 * ra6e1 - Fixed location 2
 * ra6e2 - Fixed location 2
 * ra6t1 - Flash Root Table
 * ra6t2 - Fixed location 2
 */
#define RENESAS_FIXED1_UID    UINT32_C(0x01001c00) /* Unique ID Register */
#define RENESAS_FIXED1_PNR    UINT32_C(0x01001c10) /* Part Numbering Register */
#define RENESAS_FIXED1_MCUVER UINT32_C(0x01001c20) /* MCU Version Register */

#define RENESAS_FIXED2_UID    UINT32_C(0x01008190) /* Unique ID Register */
#define RENESAS_FIXED2_PNR    UINT32_C(0x010080f0) /* Part Numbering Register */
#define RENESAS_FIXED2_MCUVER UINT32_C(0x010081b0) /* MCU Version Register */

/* The FMIFRT is a read-only register that stores the Flash Root Table address */
#define RENESAS_FMIFRT             UINT32_C(0x407fb19c)
#define RENESAS_FMIFRT_UID(frt)    ((frt) + 0x14U) /* UID Register offset from Flash Root Table */
#define RENESAS_FMIFRT_PNR(frt)    ((frt) + 0x24U) /* PNR Register offset from Flash Root Table */
#define RENESAS_FMIFRT_MCUVER(frt) ((frt) + 0x44U) /* MCUVER Register offset from Flash Root Table */

/* System Control OCD Control */
#define SYSC_BASE UINT32_C(0x4001e000)

#define SYSC_SYOCDCR  (SYSC_BASE + 0x40eU) /* System Control OCD Control Register */
#define SYOCDCR_DBGEN (1U << 7U)           /* Debug Enable */

#define SYSC_FWEPROR          (SYSC_BASE + 0x416U) /* Flash P/E Protect Register */
#define SYSC_FWEPROR_PERMIT   (0x01U)
#define SYSC_FWEPROR_PROHIBIT (0x10U)

/* Flash Memory Control */
#define FENTRYR_KEY_OFFSET 8U
#define FENTRYR_KEY        (0xaaU << FENTRYR_KEY_OFFSET)
#define FENTRYR_PE_CF      (1U)
#define FENTRYR_PE_DF      (1U << 7U)

/* Renesas RA MCUs can have one of two kinds of flash memory, MF3/4 and RV40 */

#define RENESAS_CF_END UINT32_C(0x00300000) /* End of Flash (maximum possible across families) */

/* MF3/4 Flash */
/*
 * MF3/4 Flash Memory Specifications
 * Block Size: Code area: 2 KB (except RA2A1 is 1KB), Data area: 1 KB
 * Program/Erase unit Program: Code area: 64 bits, Data area: 8 bits
 *					  Erase:  1 block
 */
#define FLASH_LP_FENTRYR_CF_PE_MODE                   (0x0001)
#define FLASH_LP_DATAFLASH_PE_MODE                    (0x10U)
#define FLASH_LP_READ_MODE                            (0x08U)
#define FLASH_LP_LVPE_MODE                            (0x40U)
#define FLASH_LP_DISCHARGE_1                          (0x12U)
#define FLASH_LP_DISCHARGE_2                          (0x92U)
#define FLASH_LP_CODEFLASH_PE_MODE                    (0x82U)
#define FLASH_LP_CODEFLASH_PE_MODE_MF4                (0x82U)
#define FLASH_LP_6BIT_MASK                            (0x3FU)
#define FLASH_LP_5BIT_MASK                            (0x1FU)
#define FLASH_LP_FISR_INCREASE_PCKA_EVERY_2MHZ        (32)
#define FLASH_LP_HZ_IN_MHZ                            (1000000U)
/*  operation definition (FCR Register setting)*/
#define FLASH_LP_FCR_WRITE                            (0x81U)
#define FLASH_LP_FCR_ERASE                            (0x84U)
#define FLASH_LP_FCR_BLANKCHECK                       (0x83U)
#define FLASH_LP_FCR_CLEAR                            (0x00U)

#define FLASH_FRDY_MSK (0x42)

#define FLASH_LP_FCR_PROCESSING_MASK                  (0x80U)
#define FLASH_LP_DATAFLASH_READ_BASE_ADDR             (0x40100000U)
#define FLASH_LP_DATAFLASH_WRITE_BASE_ADDR            (0xFE000000U)
#define FLASH_LP_DATAFLASH_ADDR_OFFSET                (FLASH_LP_DATAFLASH_WRITE_BASE_ADDR - \
                                                       FLASH_LP_DATAFLASH_READ_BASE_ADDR)

#define FLASH_LP_REGISTER_WAIT_TIMEOUT(val, reg, timeout, err) \
    while (val != reg)                                         \
    {                                                          \
        if (0 == timeout)                                      \
        {                                                      \
            return err;                                        \
        }                                                      \
        timeout--;                                             \
    }


#define FLASH_LP_FSTATR2_ILLEGAL_ERROR_BITS           (0x10)
#define FLASH_LP_FSTATR2_ERASE_ERROR_BITS             (0x11)
#define FLASH_LP_FSTATR2_WRITE_ERROR_BITS             0x12

#define BSP_FEATURE_BSP_FLASH_PREFETCH_BUFFER 1

#define MF4_BASE (0x407EC000)
#define MF4_FSADDRL (MF4_BASE+0x108)
#define MF4_FSADDRH (MF4_BASE+0x110)
#define MF4_FEADDRL (MF4_BASE+0x118)
#define MF4_FEADDRH (MF4_BASE+0x120)
#define MF4_FENTRYR (MF4_BASE+0x3FB0)
#define MF4_FPR (MF4_BASE+0x180)
#define MF4_FPMCR (MF4_BASE+0x100)
#define MF4_FISR (MF4_BASE+0x1d8)
#define MF4_FLWAITR (MF4_BASE+0x3fC0)
#define MF4_DFLCTL (MF4_BASE + 0x90)
#define MF4_FASR (MF4_BASE+0x0104)
#define MF4_FCR (MF4_BASE + 0x114)
#define MF4_FWBL0 (MF4_BASE + 0x130)
#define MF4_FWBH0 (MF4_BASE + 0x138)
#define MF4_FWBL1 (MF4_BASE+0x140)
#define MF4_FWBH1 (MF4_BASE + 0x144)
#define MF4_FSTAT1 (MF4_BASE + 0x12c)
#define MF4_FSTAT2 (MF4_BASE + 0x1F0)
#define MF4_FRESETR  (MF4_BASE + 0x124)
#define MF4_PFBER (MF4_BASE+0x3FC8)
#define MF4_SYS_BASE (0x4001E000)
#define MF4_OPCCR (MF4_SYS_BASE+0x0A0)
#define MF4_SYS_PRCR (MF4_SYS_BASE+0x3FE)

/* Wait Process definition */
#define FLASH_LP_WAIT_TDIS                            (3U)
#define FLASH_LP_WAIT_TMS_MID                         (4U)
#define FLASH_LP_WAIT_TMS_HIGH                        (6U)
#define FLASH_LP_WAIT_TDSTOP                          (6U)

#define MF3_FCACHEE (0x4001C100U)
//#define MF4_
#define OFS1_WORD_ADDR (0x0404)
#define HOCOFREQ_BIT (12)
#define HOCOFREQ_MSK (0x7 << HOCOFREQ_BIT)
#define SCKDIVCR_ADDR (0x4001e000+0x20)
#define SCKSCR_ADDR (0x4001e000+0x26)
#define ICLK_DIV_BIT (24)
#define ICLK_MSK (0x7 << ICLK_DIV_BIT)

#define FLASH_LP_FENTRYR_DATAFLASH_PE_MODE            (0xAA80U)
#define FLASH_LP_FENTRYR_CODEFLASH_PE_MODE            (0xAA01U)
#define FLASH_LP_FENTRYR_READ_MODE                    (0xAA00U)

typedef enum pe_mode_ {
	PE_MODE_READ,
	PE_MODE_CF,
	PE_MODE_DF,
} pe_mode_e;


typedef const struct hocoFrqtable_ {
    uint16_t bits;
    uint16_t clock;
} hocoFrqtable_s;

hocoFrqtable_s hocoFrqtable[]=
{
        {0,24},
        {2,32},
        {4,48},
        {5,64},
};

typedef enum e_fsp_priv_clock
{
    FSP_PRIV_CLOCK_PCLKD = 0,
    FSP_PRIV_CLOCK_PCLKC = 4,
    FSP_PRIV_CLOCK_PCLKB = 8,
    FSP_PRIV_CLOCK_PCLKA = 12,
    FSP_PRIV_CLOCK_BCLK  = 16,
    FSP_PRIV_CLOCK_ICLK  = 24,
    FSP_PRIV_CLOCK_FCLK  = 28,
} fsp_priv_clock_t;


static bool r_flash_lp_wait_for_ready (target_s *const t,
                                            uint32_t                         timeout_ms,
                                            uint32_t                         error_bits,
                                            bool                              return_code);



static void r_flash_lp_delay_us(uint32_t us, uint32_t mhz)
{
    volatile uint32_t loop_cnt;

	// From Renasas documentation @12 MHz, one loop is 332 ns. A delay of 5 us would require 15 loops. 15 * 332 = 4980 ns or ~ 5us
    /* Calculation of a loop count */
    loop_cnt = ((us * mhz) / 1); // Arm M4 is superscalar so devide by 1 is prabably suitable here.

    while (loop_cnt > 0U)
    {
		loop_cnt--;
    }
}
static uint32_t get_iclk_clock(target_flash_s *const f)
{
    target_s *const t = f->t;
    uint32_t ofs1 = target_mem_read32(t,OFS1_WORD_ADDR);
    uint32_t sckdivcr = target_mem_read32(t,SCKDIVCR_ADDR);
    uint32_t hocofreqbits=(ofs1>>HOCOFREQ_BIT) & HOCOFREQ_MSK;
    uint8_t clock_source=target_mem_read8(t,SCKSCR_ADDR);
    int32_t oscilator_freq=0;
    if (clock_source==0) {
		for (int i=0; i<4; i++) {
			if (hocoFrqtable[i].bits==hocofreqbits) {
				oscilator_freq=hocoFrqtable[i].clock*1000000UL;
				break;
			}
		}
    } else if (clock_source==1) {
    	oscilator_freq=8000000;
    } else {
    	// Not applicable for Flash writing.
    	// Should recover here.
    	// Nobody would try to program the device after setting clocksource to
    	// LOCOCR or SOSCCR
    	return 0;
    }
    uint32_t iclk_div = 1<<((sckdivcr >> FSP_PRIV_CLOCK_ICLK) & 0x07);
    uint32_t cpu_freq=oscilator_freq/iclk_div;
    if(cpu_freq<1000000) { // Fix too low speed. This will happen first time the RA2L1 is started up.
    	// Recover from low ICLK
    	target_mem_write16(t, MF4_SYS_PRCR, 0xA501);
    	if (clock_source==1)
    	{
    		iclk_div=0;
    		cpu_freq=8000000;
    	}
    	else
    	{
    		iclk_div=1; //Highest HOCO frequency is 24Mhz-64Mhz. If we divide by 2 max clock frequency is 32Mhz and min is 12Mhz.
    		cpu_freq=oscilator_freq/2;
    	}
    	sckdivcr=(sckdivcr & ~ICLK_MSK) | ((iclk_div << ICLK_DIV_BIT) & ICLK_MSK);
    	target_mem_write32(t, SCKDIVCR_ADDR, sckdivcr);
    }
    return cpu_freq;
}

#define FLASH_LP_FPR_UNLOCK 0xA5U
static void flash_lp_write_fpmcr (target_flash_s *const f,uint8_t value)
{
    target_s *const t = f->t;

    /* The procedure for writing to FPMCR is documented in Section 37.3.4 of the RA2L1 manual r01uh0853ej0100-ra2l1 */
	target_mem_write8(t, MF4_FPR, FLASH_LP_FPR_UNLOCK);
	target_mem_write8(t, MF4_FPMCR, value);
	target_mem_write8(t, MF4_FPMCR, ~value);
	target_mem_write8(t, MF4_FPMCR, value);
}

static void R_BSP_FlashCacheDisable(target_flash_s *const f)
{
    target_s *const t = f->t;
 	renesas_priv_s *priv_storage = (renesas_priv_s *)t->target_storage;
if (priv_storage->flash_cache)
	target_mem_write16(t,MF3_FCACHEE,0);

#if BSP_FEATURE_BSP_HAS_CODE_SYSTEM_CACHE // None of the supported processors have this option. RA8 series
    /* Disable the C-Cache. */
    R_CACHE->CCACTL = 0U;
#endif
}

static void R_BSP_FlashCacheEnable(target_flash_s *const f)
{
    target_s *const t = f->t;
 	renesas_priv_s *priv_storage = (renesas_priv_s *)t->target_storage;
if (priv_storage->flash_cache)
	target_mem_write16(t,MF3_FCACHEE,1);

#if BSP_FEATURE_BSP_HAS_CODE_SYSTEM_CACHE // None of the supported processors have this option. RA8 series
    /* Disable the C-Cache. */
    R_CACHE->CCACTL = 0U;
#endif
}
int renesas_mf_pe_mode(target_flash_s *const f, pe_mode_e mode)
{
    target_s *const t = f->t;
 	renesas_priv_s *priv_storage = (renesas_priv_s *)t->target_storage;
   /* While the Flash API is in use we will disable the Flash Cache. */
 	if (priv_storage->pre_fetch_buffer)
    	target_mem_write8(t, MF4_PFBER, 0);
	if (priv_storage->flash_cache)
	    R_BSP_FlashCacheDisable(f);
	switch (mode) {
		case PE_MODE_CF: 
		    target_mem_write16(t, MF4_FENTRYR, FLASH_LP_FENTRYR_CODEFLASH_PE_MODE);
			break;
		case PE_MODE_DF:
    		target_mem_write16(t, MF4_FENTRYR, FLASH_LP_FENTRYR_DATAFLASH_PE_MODE);
			break;
		case PE_MODE_READ:
		break;			
	}

	if (priv_storage->flash_version == FLASH_VERSION_MF3) {
		switch (mode)
		{
			case PE_MODE_CF: 
			{
				flash_lp_write_fpmcr(f,FLASH_LP_DISCHARGE_1);

				/* Wait for 2us over (tDIS) */
				r_flash_lp_delay_us(FLASH_LP_WAIT_TDIS, host_system_clock_frequency);

				uint32_t fpmcr_command1;
				uint32_t fpmcr_command2;
				uint32_t fpmcr_mode_setup_time;

				/* If the device is not in high speed mode enable LVPE mode as per the flash documentation. */
				uint8_t opccr=target_mem_read8(t,0x4001E000+0xA0);
				if ((opccr & 0x03) == 0U)
				{
					fpmcr_command1        = FLASH_LP_DISCHARGE_2;
					fpmcr_command2        = FLASH_LP_CODEFLASH_PE_MODE;
					fpmcr_mode_setup_time = FLASH_LP_WAIT_TMS_HIGH;
				}
				else
				{
					fpmcr_command1        = FLASH_LP_DISCHARGE_2 | FLASH_LP_LVPE_MODE;
					fpmcr_command2        = FLASH_LP_CODEFLASH_PE_MODE | FLASH_LP_LVPE_MODE;
					fpmcr_mode_setup_time = FLASH_LP_WAIT_TMS_MID;
				}

				flash_lp_write_fpmcr(f,(uint8_t) fpmcr_command1);
				flash_lp_write_fpmcr(f,(uint8_t) fpmcr_command2);

				/* Wait for 5us or 3us depending on current operating mode. (tMS) */
				r_flash_lp_delay_us(fpmcr_mode_setup_time, host_system_clock_frequency);
				break;
			}
			case PE_MODE_DF:
			{
				target_mem_write16(t, MF4_FENTRYR, FLASH_LP_FENTRYR_DATAFLASH_PE_MODE);

				r_flash_lp_delay_us(FLASH_LP_WAIT_TDSTOP, host_system_clock_frequency);

				/* See "Procedure for changing from the read mode to the data flash P/E mode": Figure 37.16 in Section 37.13.3
				* of the RA2L1 manual r01uh0853ej0100-ra2l1 */

				/* If the device is not in high speed mode enable LVPE mode as per the flash documentation. */
				if ((target_mem_read8(t,MF4_OPCCR) & 0x3) == 0U)
				{
					flash_lp_write_fpmcr(f,FLASH_LP_DATAFLASH_PE_MODE);
				}
				else
				{
					flash_lp_write_fpmcr(f,(uint8_t) FLASH_LP_DATAFLASH_PE_MODE | (uint8_t) FLASH_LP_LVPE_MODE);
				}

				break;
			}
			case PE_MODE_READ:
			{
			    uint32_t flash_pe_mode = target_mem_read16(t,MF4_FENTRYR);

				if (flash_pe_mode == FLASH_LP_FENTRYR_CF_PE_MODE)
				{
					flash_lp_write_fpmcr(f,FLASH_LP_DISCHARGE_2);

					/* Wait for 2us over (tDIS) */
					r_flash_lp_delay_us(FLASH_LP_WAIT_TDIS, host_system_clock_frequency);

					flash_lp_write_fpmcr(f,FLASH_LP_DISCHARGE_1);
				}
				flash_lp_write_fpmcr(f,FLASH_LP_READ_MODE);

				/* Wait for 5us over (tMS) */
				r_flash_lp_delay_us(FLASH_LP_WAIT_TMS_HIGH, host_system_clock_frequency);

				/* Clear the P/E mode register */
				target_mem_write16(t, MF4_FENTRYR, FLASH_LP_FENTRYR_READ_MODE);

				/* Loop until the Flash P/E mode entry register is cleared or a timeout occurs. If timeout occurs return error. */
				uint32_t wait_count=20000;
				FLASH_LP_REGISTER_WAIT_TIMEOUT(0, (target_mem_read16(t,MF4_FENTRYR)), wait_count, -1);

				if (flash_pe_mode == FLASH_LP_FENTRYR_CF_PE_MODE)
				{
					if (priv_storage->flash_cache)
							R_BSP_FlashCacheEnable(f);
					if (priv_storage->pre_fetch_buffer)
							target_mem_write8(t,MF4_PFBER,1);
				}
				break;
			}
		}
	} else if (priv_storage->flash_version == FLASH_VERSION_MF4) {

    /* See "Procedure for changing from read mode to code flash P/E mode": See Figure 37.15 in Section 37.13.3 of the
     * RA2L1 manual r01uh0853ej0100-ra2l1 */
		switch (mode)
		{
		case PE_MODE_DF:
			flash_lp_write_fpmcr(f,0x10);
			break;
		case PE_MODE_CF:
			flash_lp_write_fpmcr(f,0x02);//FLASH_LP_CODEFLASH_PE_MODE_MF4);
			break;
		case PE_MODE_READ: {
			flash_lp_write_fpmcr(f,FLASH_LP_READ_MODE);

			/* Wait for 5us over (tMS) */
			for (volatile int i;i<1000; i++);

			/* Clear the P/E mode register */
			target_mem_write16(t, MF4_FENTRYR, FLASH_LP_FENTRYR_READ_MODE);
			volatile int wait_count=19200;
			/* Loop until the Flash P/E mode entry register is cleared or a timeout occurs. If timeout occurs return error. */
			FLASH_LP_REGISTER_WAIT_TIMEOUT(0, (target_mem_read16(t,MF4_FENTRYR)), wait_count, -1);
			flash_lp_write_fpmcr(f,FLASH_LP_READ_MODE);
		}
		break;
		default:
		  break;
		}
	}
    /* Wait for 2us over (tDIS) // platform dependent*/
	r_flash_lp_delay_us(2, 1000000);
    return 1;
}

int r_flash_lp_set_fisr_mf(target_flash_s *const f)
{

    target_s *const t = f->t;
	renesas_priv_s *priv_storage = (renesas_priv_s *)t->target_storage;
    /* Enter data flash P/E mode to enable writing to FISR. */
    uint32_t iclk=get_iclk_clock(f);
    uint8_t FISR_value;
	renesas_mf_pe_mode(f,PE_MODE_DF);
    iclk = (iclk + (FLASH_LP_HZ_IN_MHZ - 1)) /
                                    FLASH_LP_HZ_IN_MHZ;
    /* If the flash clock is larger than 32 increment FISR_b.PCKA by 1 for every 2MHZ. (See Section 37.3.7 "Flash
     * Internal Setting Register" of the RA2L1 manual r01uh0853ej0100-ra2l1 */

    /* If the frequency is over 32MHz round up to an even number. */
    if (iclk >= FLASH_LP_FISR_INCREASE_PCKA_EVERY_2MHZ && priv_storage->flash_version==FLASH_VERSION_MF4)
    {
    	if ((iclk & 1) == 1)
    		iclk++;
    	FISR_value =
            (0x1F + ((iclk - FLASH_LP_FISR_INCREASE_PCKA_EVERY_2MHZ) >> 1)) &
            FLASH_LP_6BIT_MASK;
    }
    else
    {
    	FISR_value = (iclk - 1U) & FLASH_LP_5BIT_MASK;
    }
    target_mem_write8(t, MF4_FISR, FISR_value);

	return renesas_mf_pe_mode(f,PE_MODE_READ);
}

static bool renesas_mf_prepare(target_flash_s *const f)
{
	if (f->start >= 0x1010000 && f->start<=0x1010100)
		return true;
    target_s *const t = f->t;
    /* FLWAITR should be set to 0 when the FCLK/ICLK is within the acceptable range. */
    target_mem_write8(t, MF4_FLWAITR, 0); // wait state?? Not in the manual, but present in fsp library.

    target_mem_write8(t,MF4_DFLCTL,1U);
    for (volatile int i=0; i<1000; i++);

    /* Set the FlashIF peripheral clock frequency. */
    r_flash_lp_set_fisr_mf(f);
    /* Code flash or data flash operation */
    const bool code_flash = f->start < FLASH_LP_DATAFLASH_READ_BASE_ADDR;

    /* Transition to PE mode */
    const pe_mode_e pe_mode = code_flash ? PE_MODE_CF : PE_MODE_DF;

    return renesas_mf_pe_mode(f, pe_mode);
}

static bool renesas_mf_done(target_flash_s *const f)
{
	if (f->start >= 0x1010000 && f->start<=0x1010100)
		return true;
    /* Return to read mode */
    return renesas_mf_pe_mode(f, PE_MODE_READ);
}

static void r_flash_lp_process_command (target_s *const t, const uint32_t start_addr, uint32_t num_bytes, uint32_t command)
{
    uint32_t end_addr_idx = start_addr + (num_bytes - 1U);

    /* Select User Area */
    target_mem_write8(t, MF4_FASR, 0);

    /* BlankCheck start address setting */
    target_mem_write16(t,MF4_FSADDRH,start_addr>>16);
    target_mem_write16(t,MF4_FSADDRL,start_addr);

    /* BlankCheck end address setting */
    target_mem_write16(t,MF4_FEADDRL,end_addr_idx);
    target_mem_write16(t,MF4_FEADDRH, (end_addr_idx >> 16));

    /* Execute BlankCheck command */
    target_mem_write8(t, MF4_FCR, command);
}

/*******************************************************************************************************************//**
 * This function erases a specified number of Code or Data Flash blocks
 *
 * @param[in]  p_ctrl                Pointer to the Flash control block
 * @param[in]  block_address         The starting address of the first block to erase.
 * @param[in]  num_blocks            The number of blocks to erase.
 * @param[in]  block_size            The Flash block size.
 *
 * @retval     FSP_SUCCESS           Successfully erased (non-BGO) mode or operation successfully started (BGO).
 * @retval     FSP_ERR_ERASE_FAILED  Erase failed. Flash could be locked or address could be under access window
 *                                   control.
 * @retval     FSP_ERR_TIMEOUT       Timed out waiting for the FCU to become ready.
 **********************************************************************************************************************/
static bool r_flash_lp_df_erase (target_flash_s *f,
								uint32_t                         block_address,
								uint32_t                         num_blocks,
								uint32_t                         block_size)
{
	if (f->start >= 0x1010000 && f->start<=0x1010100)
		return true;
	target_s * t=f->t;
    bool err = true;

//    /* Enter data flash P/E mode. */
	bool code_flash=block_address<FLASH_LP_DATAFLASH_READ_BASE_ADDR?true:false;

    /* Select user area. */
    target_mem_write8(t, MF4_FASR, 0);

    /* Save the current operation parameters. */
    uint32_t source_start_address = block_address + FLASH_LP_DATAFLASH_ADDR_OFFSET;
	if (code_flash)
		source_start_address=block_address;
    /* Start the code flash erase operation. */
    r_flash_lp_process_command(t,source_start_address, num_blocks * block_size, FLASH_LP_FCR_ERASE);

	/* Waits for the erase commands to be completed and verifies the result of the command execution. */
	err = r_flash_lp_wait_for_ready(t,
									500*num_blocks,//Erase block should finish in 355 ms pr block, so 500ms should do the trick,
									FLASH_LP_FSTATR2_ERASE_ERROR_BITS,
									false);

    return err;
}

static bool renesas_mf_flash_erase(target_flash_s *f, target_addr_t addr, size_t len)
{
	bool err=r_flash_lp_df_erase(f,addr,len/f->blocksize,f->blocksize);
	return err;
}

/*******************************************************************************************************************//**
 * Execute a single Write operation on the Low Power Data Flash data.
 * See Figure 37.21 in Section 37.13.3 of the RA2L1 manual r01uh0853ej0100-ra2l1
 *
 * @param[in]  data       	data to write
 * @param[in]  dest_addr    End address (read form) for writing.
 **********************************************************************************************************************/

void r_flash_lp_write_operation (target_flash_s *const f, uint8_t* dataptr, uint32_t dest_addr)
{
	if (dest_addr==0x1010010)
		return;
    target_s* t = f->t;
	bool code_flash = dest_addr < FLASH_LP_DATAFLASH_READ_BASE_ADDR;
	uint32_t dest_addr_actual=dest_addr+ FLASH_LP_DATAFLASH_ADDR_OFFSET;
	if (code_flash) 
	{
		dest_addr_actual = dest_addr;
	}
	if (dest_addr_actual==0x1010010)
		return;
    /* Write flash address setting */
    target_mem_write16(t,MF4_FSADDRH,dest_addr_actual>>16);
    target_mem_write16(t,MF4_FSADDRL,dest_addr_actual);

    /* Write data buffer setting */
    target_mem_write16(t, MF4_FWBL0, ((uint16_t*)dataptr)[0]);   // For data flash there are only 8 bits used of the 16 in the reg
    target_mem_write16(t, MF4_FWBH0, ((uint16_t*)dataptr)[1]);   // for data flash this is not used.
	if (f->writesize>4) 
	{
		target_mem_write16(t, MF4_FWBL1, ((uint16_t*)dataptr)[2]);
		target_mem_write16(t, MF4_FWBH1, ((uint16_t*)dataptr)[3]);
	}
    /* Execute Write command */
    target_mem_write8(t, MF4_FCR, FLASH_LP_FCR_WRITE);
}

static int r_flash_lp_wait_FRDY(target_s *const t, uint32_t timeout_ms, uint8_t bit_state)
{
	uint32_t start_time=platform_time_ms();
	while ((platform_time_ms()-start_time)<timeout_ms)
	{
		volatile uint8_t fstat1 = target_mem_read8(t, MF4_FSTAT1);
		if (((fstat1 & 0x40)!= 0) == bit_state)
			return 1;
	}
	return -1;
}

/*******************************************************************************************************************//**
 * Wait for the current command to finish processing and clear the FCR register. If MF4 is used clear the processing
 * bit before clearing the rest of FCR.
 * See Figure 37.19 in Section 37.13.3 of the RA2L1 manual r01uh0853ej0100-ra2l1
 *
 * @param[in]  timeout         The timeout
 * @retval     FSP_SUCCESS     The command completed successfully.
 * @retval     FSP_ERR_TIMEOUT The command timed out.
 **********************************************************************************************************************/
static bool r_flash_lp_command_finish_mf(target_s *const t,uint32_t timeout_ms)
{
	renesas_priv_s *priv_storage = (renesas_priv_s *)t->target_storage;
    /* Check the Flash Ready Flag bit*/
    if (r_flash_lp_wait_FRDY(t,timeout_ms,1) < 0)
		return false;

	if (priv_storage->flash_version == FLASH_VERSION_MF4)
	{
        uint8_t fcr=target_mem_read8(t, MF4_FCR);
    	/* Stop Processing */
    	target_mem_write8(t, MF4_FCR, ( fcr & (~FLASH_LP_FCR_PROCESSING_MASK)));
	}

    /* Clear FCR register */
    target_mem_write8(t, MF4_FCR, FLASH_LP_FCR_CLEAR);

    /* Wait for the Flash Ready Flag bit to indicate ready or a timeout to occur. If timeout return error. */
    if (r_flash_lp_wait_FRDY(t,timeout_ms,0) < 0)
		return false;

    return true;
}

/*******************************************************************************************************************//**
 * This function resets the Flash sequencer.
 * See Figure 37.19 in Section 37.13.3 of the RA2L1 manual r01uh0853ej0100-ra2l1
 *
 * @param[in]  p_ctrl          Pointer to the Flash control block
 **********************************************************************************************************************/
static void r_flash_lp_reset (target_s *const t )
{
    /* Reset the flash. */
    target_mem_write8(t, MF4_FRESETR, 1);
    target_mem_write8(t, MF4_FRESETR, 1);
}

/*******************************************************************************************************************//**
 * Wait for the current command to finish processing and check for error.
 *
 * @param      p_ctrl                Pointer to the control block
 * @param[in]  timeout               The timeout
 * @param[in]  error_bits            The error bits related to the current command
 * @param[in]  return_code           The operation specific error code
 *
 * @retval     FSP_SUCCESS           Erase command successfully completed.
 * @retval     FSP_ERR_TIMEOUT       Timed out waiting for erase command completion.
 * @return     return_code           The operation specific error code.
 **********************************************************************************************************************/
static bool r_flash_lp_wait_for_ready (target_s *const t,
                                            uint32_t                         timeout_ms,
                                            uint32_t                         error_bits,
                                            bool                              return_code)
{
    bool err = r_flash_lp_command_finish_mf(t,timeout_ms);

    /* If a timeout occurs reset the flash and return error. */
    if ( !err)
    {
        r_flash_lp_reset(t);

        return err;
    }

    /* If an error occurs reset and return error. */
    if (0U != (target_mem_read16(t, MF4_FSTAT2) & error_bits))
    {
        r_flash_lp_reset(t);

        return return_code;
    }

    return true;
}

static bool renesas_mf_flash_write(target_flash_s *const f, target_addr_t dest, const void *src, size_t len)
{
	if (f->start >= 0x1010000 && f->start<=0x1010100)
		return true;
    target_s *const t = f->t;

    bool err = true;


    uint8_t* src_ptr=(uint8_t*)src; // handle write size = 8 bytes.
	uint32_t increment=f->writesize;

    /* Select User Area */
	target_mem_write8(t,MF4_FASR,0);

    while (len && (err))
    {
        /* Initiate the code flash write operation. */
        r_flash_lp_write_operation(f,src_ptr,dest);
        dest+=increment;
		src_ptr+=increment;
        len-=increment;
        err = r_flash_lp_wait_for_ready(t,
                                        3, // write should be finished after 1440uS, so 4 ms should be enough. 
                                        FLASH_LP_FSTATR2_WRITE_ERROR_BITS,
                                        false);
    }

    /* If successful exit P/E mode. */
    if (!err)
    {
        renesas_mf_pe_mode(f,PE_MODE_READ);
    }

    return err;
}

void renesas_add_mf_flash(target_s *t, target_addr_t addr, size_t length)
{
	target_flash_s *f = calloc(1, sizeof(*f));
	renesas_priv_s *priv_storage = (renesas_priv_s *)t->target_storage;
	uint32_t block_size_cf=0x800;
	uint32_t block_writesize_cf=4;
	uint32_t block_size_df=0x400;
	uint32_t block_writesize_df=1;
	if (!f) /* calloc failed: heap exhaustion */
		return;

	const bool code_flash = addr < FLASH_LP_DATAFLASH_READ_BASE_ADDR;
	f->start = addr;
	f->length = length;
	f->erased = 0xffU;
	f->erase = renesas_mf_flash_erase;
	f->write = renesas_mf_flash_write;
	f->prepare = renesas_mf_prepare;
	f->done = renesas_mf_done;

	switch (priv_storage->series) {
		case PNR_SERIES_RA2L1:
		case PNR_SERIES_RA2E2:
		case PNR_SERIES_RA2E1: 
		{
			priv_storage->flash_version = FLASH_VERSION_MF4;
			priv_storage->pre_fetch_buffer = true;
			priv_storage->flash_cache=false;
			block_writesize_cf=4;
			break;
		}
		case PNR_SERIES_RA2A1:
		case PNR_SERIES_RA4M1:
		case PNR_SERIES_RA4W1:
		{ // Actually not supported yet.
			priv_storage->flash_version = FLASH_VERSION_MF3;
			block_writesize_cf=8;
			priv_storage->pre_fetch_buffer = false;
			priv_storage->flash_cache = false;
			break;
		}
		default:
			break;
	}

	if (code_flash) {
		f->blocksize = block_size_cf;
		f->writesize = block_writesize_cf;
	} else {
		f->blocksize = block_size_df;
		f->writesize = block_writesize_df;
	}
	if (addr == 0x1010010) {
		f->blocksize=length;
	}
	target_add_flash(t, f);
}
