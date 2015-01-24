/* @file stm32lx-nvm.h
 *
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2014 Woollysoft
 * Written by Marc Singer <elf@woollysoft.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#if !defined (STM32Lx_NVM_H_INCLUDED)
#    define   STM32Lx_NVM_H_INCLUDED

/* ----- Includes */

#include <stdint.h>

/* ----- Macros */

/* ----- Types */

enum {
  STM32Lx_STUB_PHYS            = 0x20000000ul,
  STM32Lx_STUB_INFO_PHYS       = 0x20000004ul,
  STM32Lx_STUB_DATA_PHYS       = (0x20000000ul + 1024),
  STM32Lx_STUB_DATA_MAX        = 1024,

  STM32Lx_NVM_OPT_PHYS         = 0x1ff80000ul,
  STM32Lx_NVM_EEPROM_PHYS      = 0x08080000ul,

  STM32L0_NVM_PHYS             = 0x40022000ul,
  STM32L0_NVM_PROG_PAGE_SIZE   = 128,
  STM32L0_NVM_DATA_PAGE_SIZE   = 4,
  STM32L0_NVM_OPT_SIZE         = 12,
  STM32L0_NVM_EEPROM_SIZE      = 2*1024,

  STM32L1_NVM_PHYS             = 0x40023c00ul,
  STM32L1_NVM_PROG_PAGE_SIZE   = 256,
  STM32L1_NVM_DATA_PAGE_SIZE   = 4,
  STM32L1_NVM_OPT_SIZE         = 32,
  STM32L1_NVM_EEPROM_SIZE      = 16*1024,

  STM32Lx_NVM_PEKEY1           = 0x89abcdeful,
  STM32Lx_NVM_PEKEY2           = 0x02030405ul,
  STM32Lx_NVM_PRGKEY1          = 0x8c9daebful,
  STM32Lx_NVM_PRGKEY2          = 0x13141516ul,
  STM32Lx_NVM_OPTKEY1          = 0xfbead9c8ul,
  STM32Lx_NVM_OPTKEY2          = 0x24252627ul,

  STM32Lx_NVM_PECR_OBL_LAUNCH  = (1<<18),
  STM32Lx_NVM_PECR_ERRIE       = (1<<17),
  STM32Lx_NVM_PECR_EOPIE       = (1<<16),
  STM32Lx_NVM_PECR_FPRG        = (1<<10),
  STM32Lx_NVM_PECR_ERASE       = (1<< 9),
  STM32Lx_NVM_PECR_FIX         = (1<< 8), /* FTDW */
  STM32Lx_NVM_PECR_DATA        = (1<< 4),
  STM32Lx_NVM_PECR_PROG        = (1<< 3),
  STM32Lx_NVM_PECR_OPTLOCK     = (1<< 2),
  STM32Lx_NVM_PECR_PRGLOCK     = (1<< 1),
  STM32Lx_NVM_PECR_PELOCK      = (1<< 0),

  STM32Lx_NVM_SR_FWWERR        = (1<<17),
  STM32Lx_NVM_SR_NOTZEROERR    = (1<<16),
  STM32Lx_NVM_SR_RDERR         = (1<<13),
  STM32Lx_NVM_SR_OPTVER        = (1<<11),
  STM32Lx_NVM_SR_SIZERR        = (1<<10),
  STM32Lx_NVM_SR_PGAERR        = (1<<9),
  STM32Lx_NVM_SR_WRPERR        = (1<<8),
  STM32Lx_NVM_SR_READY         = (1<<3),
  STM32Lx_NVM_SR_HWOFF         = (1<<2),
  STM32Lx_NVM_SR_EOP           = (1<<1),
  STM32Lx_NVM_SR_BSY           = (1<<0),
  STM32Lx_NVM_SR_ERR_M         = (  STM32Lx_NVM_SR_WRPERR
                                  | STM32Lx_NVM_SR_PGAERR
                                  | STM32Lx_NVM_SR_SIZERR
                                  | STM32Lx_NVM_SR_NOTZEROERR),

  STM32L0_NVM_OPTR_BOOT1       = (1<<31),
  STM32L0_NVM_OPTR_WDG_SW      = (1<<20),
  STM32L0_NVM_OPTR_WPRMOD      = (1<<8),
  STM32L0_NVM_OPTR_RDPROT_S    = (0),
  STM32L0_NVM_OPTR_RDPROT_M    = (0xff),
  STM32L0_NVM_OPTR_RDPROT_0    = (0xaa),
  STM32L0_NVM_OPTR_RDPROT_2    = (0xcc),

  STM32L1_NVM_OPTR_nBFB2       = (1<<23),
  STM32L1_NVM_OPTR_nRST_STDBY  = (1<<22),
  STM32L1_NVM_OPTR_nRST_STOP   = (1<<21),
  STM32L1_NVM_OPTR_WDG_SW      = (1<<20),
  STM32L1_NVM_OPTR_BOR_LEV_S   = (16),
  STM32L1_NVM_OPTR_BOR_LEV_M   = (0xf),
  STM32L1_NVM_OPTR_SPRMOD      = (1<<8),
  STM32L1_NVM_OPTR_RDPROT_S    = (0),
  STM32L1_NVM_OPTR_RDPROT_M    = (0xff),
  STM32L1_NVM_OPTR_RDPROT_0    = (0xaa),
  STM32L1_NVM_OPTR_RDPROT_2    = (0xcc),

};

#if defined (__cplusplus)

namespace STM32 {
  struct NVM {
    volatile uint32_t acr;
    volatile uint32_t pecr;
    volatile uint32_t pdkeyr;
    volatile uint32_t pkeyr;
    volatile uint32_t prgkeyr;
    volatile uint32_t optkeyr;
    volatile uint32_t sr;
    volatile uint32_t optr;
    volatile uint32_t wrprot;

    static constexpr uint32_t PKEY1   = 0x89abcdef;
    static constexpr uint32_t PKEY2   = 0x02030405;
    static constexpr uint32_t PRGKEY1 = 0x8c9daebf;
    static constexpr uint32_t PRGKEY2 = 0x13141516;
    static constexpr uint32_t OPTKEY1 = 0xfbead9c8;
    static constexpr uint32_t OPTKEY2 = 0x24252627;
    static constexpr uint32_t PDKEY1  = 0x04152637;
    static constexpr uint32_t PDKEY2  = 0xfafbfcfd;
  };

  static_assert(sizeof (NVM) == 9*4, "NVM size error");

  struct NVML1 {
    volatile uint32_t acr;
    volatile uint32_t pecr;
    volatile uint32_t pdkeyr;
    volatile uint32_t pekeyr;
    volatile uint32_t prgkeyr;
    volatile uint32_t optkeyr;
    volatile uint32_t sr;
    volatile uint32_t obr;
    volatile uint32_t wrprot1;

    static constexpr uint32_t PKEY1   = 0x89abcdef;
    static constexpr uint32_t PKEY2   = 0x02030405;
    static constexpr uint32_t PRGKEY1 = 0x8c9daebf;
    static constexpr uint32_t PRGKEY2 = 0x13141516;
    static constexpr uint32_t OPTKEY1 = 0xfbead9c8;
    static constexpr uint32_t OPTKEY2 = 0x24252627;
    static constexpr uint32_t PDKEY1  = 0x04152637;
    static constexpr uint32_t PDKEY2  = 0xfafbfcfd;
  };

  static_assert(sizeof (NVML1) == 9*4, "NVML1 size error");
}
using   stm32lx_stub_pointer_t = uint32_t*;

#define Nvm	(*reinterpret_cast<STM32::NVM*>(STM32L0_NVM_PHYS))
#define NvmL1   (*reinterpret_cast<STM32::NVML1*>(STM32L1_NVM_PHYS))
#define Info (*reinterpret_cast<stm32lx_nvm_stub_info*>(STM32Lx_STUB_INFO_PHYS))


namespace {
  inline __attribute((always_inline)) bool unlock () {
    Nvm.pecr      = STM32Lx_NVM_PECR_PELOCK; // Lock to guarantee unlock
    Nvm.pkeyr     = STM32::NVM::PKEY1;
    Nvm.pkeyr     = STM32::NVM::PKEY2;
    Nvm.prgkeyr   = STM32::NVM::PRGKEY1;
    Nvm.prgkeyr   = STM32::NVM::PRGKEY2;
    return !(Nvm.pecr & STM32Lx_NVM_PECR_PRGLOCK);
  }
  inline __attribute((always_inline)) void lock () {
    Nvm.pecr      = STM32Lx_NVM_PECR_PELOCK; }

  inline __attribute((always_inline)) bool unlock_l1 () {
    NvmL1.pecr    = STM32Lx_NVM_PECR_PELOCK; // Lock to guarantee unlock
    NvmL1.pekeyr  = STM32::NVML1::PKEY1;
    NvmL1.pekeyr  = STM32::NVML1::PKEY2;
    NvmL1.prgkeyr = STM32::NVML1::PRGKEY1;
    NvmL1.prgkeyr = STM32::NVML1::PRGKEY2;
    return !(Nvm.pecr & STM32Lx_NVM_PECR_PRGLOCK);
  }
  inline __attribute((always_inline)) void lock_l1 () {
    Nvm.pecr      = STM32Lx_NVM_PECR_PELOCK; }
}

#else

typedef uint32_t stm32lx_stub_pointer_t;

struct stm32lx_nvm {
  volatile uint32_t acr;
  volatile uint32_t pecr;
  volatile uint32_t pdkeyr;
  volatile uint32_t pekeyr;
  volatile uint32_t prgkeyr;
  volatile uint32_t optkeyr;
  volatile uint32_t sr;
  volatile uint32_t optr;       /* or obr */
  volatile uint32_t wrprot;     /* or wprot1 */
};

#define STM32Lx_NVM(p)		(*(struct stm32lx_nvm*) (p))
#define STM32Lx_NVM_PECR(p)	((uint32_t) &STM32Lx_NVM(p).pecr)
#define STM32Lx_NVM_PEKEYR(p)	((uint32_t) &STM32Lx_NVM(p).pekeyr)
#define STM32Lx_NVM_PRGKEYR(p)	((uint32_t) &STM32Lx_NVM(p).prgkeyr)
#define STM32Lx_NVM_OPTKEYR(p)	((uint32_t) &STM32Lx_NVM(p).optkeyr)
#define STM32Lx_NVM_SR(p)	((uint32_t) &STM32Lx_NVM(p).sr)
#define STM32Lx_NVM_OPTR(p)	((uint32_t) &STM32Lx_NVM(p).optr)

#endif

struct stm32lx_nvm_stub_info {
  stm32lx_stub_pointer_t  destination;
  int32_t		  size;
  stm32lx_stub_pointer_t  source;
  uint32_t                page_size;
};

/* ----- Globals */

/* ----- Prototypes */



#endif  /* STM32Lx_NVM_H_INCLUDED */
