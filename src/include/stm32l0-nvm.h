/* @file stm32l0-nvm.h
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

#if !defined (STM32L0_NVM_H_INCLUDED)
#    define   STM32L0_NVM_H_INCLUDED

/* ----- Includes */

#include <stdint.h>

/* ----- Macros */

/* ----- Types */

enum {
  STM32L0_NVM_PHYS             = 0x40022000ul,
  STM32L0_NVM_PROG_PAGE_SIZE   = 128,
  STM32L0_NVM_DATA_PAGE_SIZE   = 4,
  STM32L0_NVM_OPT_PHYS         = 0x1ff80000ul,
  STM32L0_NVM_OPT_SIZE         = 12,
  STM32L0_NVM_EEPROM_PHYS      = 0x08080000ul,
  STM32L0_NVM_EEPROM_SIZE      = 2048,
  STM32L0_STUB_PHYS            = 0x20000000ul,
  STM32L0_STUB_INFO_PHYS       = 0x20000004ul,
  STM32L0_STUB_DATA_PHYS       = (0x20000000ul + 1024),
  STM32L0_STUB_DATA_MAX        = 1024,
  STM32L0_NVM_PKEY1            = 0x89abcdeful,
  STM32L0_NVM_PKEY2            = 0x02030405ul,
  STM32L0_NVM_PRGKEY1          = 0x8c9daebful,
  STM32L0_NVM_PRGKEY2          = 0x13141516ul,
  STM32L0_NVM_OPTKEY1          = 0xfbead9c8ul,
  STM32L0_NVM_OPTKEY2          = 0x24252627ul,

  STM32L0_NVM_PECR_OBL_LAUNCH  = (1<<18),
  STM32L0_NVM_PECR_ERRIE       = (1<<17),
  STM32L0_NVM_PECR_EOPIE       = (1<<16),
  STM32L0_NVM_PECR_FPRG        = (1<<10),
  STM32L0_NVM_PECR_ERASE       = (1<< 9),
  STM32L0_NVM_PECR_FIX         = (1<< 8),
  STM32L0_NVM_PECR_DATA        = (1<< 4),
  STM32L0_NVM_PECR_PROG        = (1<< 3),
  STM32L0_NVM_PECR_OPTLOCK     = (1<< 2),
  STM32L0_NVM_PECR_PRGLOCK     = (1<< 1),
  STM32L0_NVM_PECR_PELOCK      = (1<< 0),

  STM32L0_NVM_SR_FWWERR        = (1<<17),
  STM32L0_NVM_SR_NOTZEROERR    = (1<<16),
  STM32L0_NVM_SR_RDERR         = (1<<13),
  STM32L0_NVM_SR_OPTVER        = (1<<11),
  STM32L0_NVM_SR_SIZERR        = (1<<10),
  STM32L0_NVM_SR_PGAERR        = (1<<9),
  STM32L0_NVM_SR_WRPERR        = (1<<8),
  STM32L0_NVM_SR_READY         = (1<<3),
  STM32L0_NVM_SR_HWOFF         = (1<<2),
  STM32L0_NVM_SR_EOP           = (1<<1),
  STM32L0_NVM_SR_BSY           = (1<<0),
  STM32L0_NVM_SR_ERR_M         = (STM32L0_NVM_SR_WRPERR
                                  | STM32L0_NVM_SR_PGAERR
                                  | STM32L0_NVM_SR_SIZERR
                                  | STM32L0_NVM_SR_NOTZEROERR),

  STM32L0_NVM_OPTR_BOOT1       = (1<<31),
  STM32L0_NVM_OPTR_WDG_SW      = (1<<20),
  STM32L0_NVM_OPTR_WPRMOD      = (1<<8),
  STM32L0_NVM_OPTR_RDPROT_S    = (0),
  STM32L0_NVM_OPTR_RDPROT_M    = (0xff),
  STM32L0_NVM_OPTR_RDPROT_0    = (0xaa),
  STM32L0_NVM_OPTR_RDPROT_2    = (0xcc),
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
}
using   stm32l0_stub_pointer_t = uint32_t*;

#define Nvm  (*reinterpret_cast<STM32::NVM*>(STM32L0_NVM_PHYS))
#define Info (*reinterpret_cast<stm32l0_nvm_stub_info*>(STM32L0_STUB_INFO_PHYS))

namespace {
  inline __attribute((always_inline)) bool unlock () {
    Nvm.pecr    = STM32L0_NVM_PECR_PELOCK; // Lock to guarantee unlock
    Nvm.pkeyr   = STM32::NVM::PKEY1;
    Nvm.pkeyr   = STM32::NVM::PKEY2;
    Nvm.prgkeyr = STM32::NVM::PRGKEY1;
    Nvm.prgkeyr = STM32::NVM::PRGKEY2;
    return !(Nvm.pecr & STM32L0_NVM_PECR_PRGLOCK);
  }
  inline __attribute((always_inline)) void lock () {
    Nvm.pecr = STM32L0_NVM_PECR_PELOCK; }
}

#else

struct stm32l0_nvm {
  volatile uint32_t acr;
  volatile uint32_t pecr;
  volatile uint32_t pdkeyr;
  volatile uint32_t pkeyr;
  volatile uint32_t prgkeyr;
  volatile uint32_t optkeyr;
  volatile uint32_t sr;
  volatile uint32_t optr;
  volatile uint32_t wrprot;
};

#define STM32L0_NVM		(*(struct stm32l0_nvm*) (STM32L0_NVM_PHYS))
#define STM32L0_NVM_PECR	((uint32_t) &STM32L0_NVM.pecr)
#define STM32L0_NVM_PKEYR	((uint32_t) &STM32L0_NVM.pkeyr)
#define STM32L0_NVM_PRGKEYR	((uint32_t) &STM32L0_NVM.prgkeyr)
#define STM32L0_NVM_OPTKEYR	((uint32_t) &STM32L0_NVM.optkeyr)
#define STM32L0_NVM_SR		((uint32_t) &STM32L0_NVM.sr)
#define STM32L0_NVM_OPTR	((uint32_t) &STM32L0_NVM.optr)

typedef uint32_t stm32l0_stub_pointer_t;
#endif

struct stm32l0_nvm_stub_info {
  stm32l0_stub_pointer_t  destination;
  int32_t		  size;
  stm32l0_stub_pointer_t  source;
  uint32_t                page_size;
};

/* ----- Globals */

/* ----- Prototypes */



#endif  /* STM32L0_NVM_H_INCLUDED */
