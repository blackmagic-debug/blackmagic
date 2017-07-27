/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2015  Richard Meadows
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
#include "stub.h"
#include <stdint.h>

#define EFM32_MSC ((volatile uint32_t *)0x400e0000)
#define EFM32_MSC_WRITECTRL EFM32_MSC[2]
#define EFM32_MSC_WRITECMD EFM32_MSC[3]
#define EFM32_MSC_ADDRB EFM32_MSC[4]
#define EFM32_MSC_WDATA EFM32_MSC[6]
#define EFM32_MSC_STATUS EFM32_MSC[7]

#define EFM32_MSC_LOCK EFM32_MSC[15]

#define EFR32_MSC_LOCK EFM32_MSC[16]

#define EFM32_MSC_LOCK_LOCKKEY 0x1b71

#define EFM32_MSC_WRITECMD_LADDRIM (1 << 0)
#define EFM32_MSC_WRITECMD_ERASEPAGE (1 << 1)
#define EFM32_MSC_WRITECMD_WRITEEND (1 << 2)
#define EFM32_MSC_WRITECMD_WRITEONCE (1 << 3)
#define EFM32_MSC_WRITECMD_WRITETRIG (1 << 4)
#define EFM32_MSC_WRITECMD_ERASEABORT (1 << 5)

#define EFM32_MSC_STATUS_BUSY (1 << 0)
#define EFM32_MSC_STATUS_LOCKED (1 << 1)
#define EFM32_MSC_STATUS_INVADDR (1 << 2)
#define EFM32_MSC_STATUS_WDATAREADY (1 << 3)
#define EFM32_MSC_STATUS_WORDTIMEOUT (1 << 4)

typedef struct {
  volatile uint32_t CTRL;      /**< Memory System Control Register  */
  volatile uint32_t READCTRL;  /**< Read Control Register  */
  volatile uint32_t WRITECTRL; /**< Write Control Register  */
  volatile uint32_t WRITECMD;  /**< Write Command Register  */
  volatile uint32_t ADDRB;     /**< Page Erase/Write Address Buffer  */
  uint32_t RESERVED0[1];       /**< Reserved for future use **/
  volatile uint32_t WDATA;     /**< Write Data Register  */
  volatile uint32_t STATUS;    /**< Status Register  */

  uint32_t RESERVED1[4];         /**< Reserved for future use **/
  volatile uint32_t IF;          /**< Interrupt Flag Register  */
  volatile uint32_t IFS;         /**< Interrupt Flag Set Register  */
  volatile uint32_t IFC;         /**< Interrupt Flag Clear Register  */
  volatile uint32_t IEN;         /**< Interrupt Enable Register  */
  volatile uint32_t LOCK;        /**< Configuration Lock Register  */
  volatile uint32_t CACHECMD;    /**< Flash Cache Command Register  */
  volatile uint32_t CACHEHITS;   /**< Cache Hits Performance Counter  */
  volatile uint32_t CACHEMISSES; /**< Cache Misses Performance Counter  */

  uint32_t RESERVED2[1];      /**< Reserved for future use **/
  volatile uint32_t MASSLOCK; /**< Mass Erase Lock Register  */

  uint32_t RESERVED3[1];     /**< Reserved for future use **/
  volatile uint32_t STARTUP; /**< Startup Control  */

  uint32_t RESERVED4[5]; /**< Reserved for future use **/
  volatile uint32_t CMD; /**< Command Register  */
} MSC_TypeDef;           /** @} */

#define MSC ((MSC_TypeDef *)(0x400E0000UL))

/*
void MSC_LoadWriteData(uint32_t *data, uint32_t numWords,
                       uint8_t writeStrategy);
void MSC_LoadVerifyAddress(uint32_t *address);
*/

void __attribute__((naked))
efm32_flash_write_stub(uint32_t *dest, uint32_t *src, uint32_t size) {
  uint32_t wordCount;
  uint32_t numWords;
  uint32_t pageWords;
  uint32_t *pData;

  /* Unlock the MSC */
  MSC->LOCK = EFM32_MSC_LOCK_LOCKKEY;

  MSC->WRITECTRL |= (0x1UL << 0);

  numWords = size >> 2;

  for (wordCount = 0, pData = (uint32_t *)src; wordCount < numWords;) {

    { // MSC_LoadVerifyAddress(dest + wordCount);
      uint32_t *address = dest + wordCount;
      uint32_t status;
      uint32_t timeOut;

      timeOut = 10000000ul;
      while ((MSC->STATUS & (0x1UL << 0)) && (timeOut != 0)) {
        timeOut--;
      }

      if (timeOut == 0) {
        stub_exit(1);
      }

      MSC->ADDRB = (uint32_t)address;
      MSC->WRITECMD = (0x1UL << 0);
    }

    // for parts with 2048b pages
    pageWords = (2048 - (((uint32_t)(dest + wordCount)) & (2048 - 1))) /
                sizeof(uint32_t);
    if (pageWords > numWords - wordCount) {
      pageWords = numWords - wordCount;
    }

    // MSC_LoadWriteData(pData, pageWords, 0);
    {
      uint32_t *data = pData;
      uint32_t numWords = pageWords;
      uint8_t writeStrategy = 0;
      uint32_t timeOut;
      uint32_t wordIndex;
      _Bool useWDouble = 0;

      uint32_t irqState;

      if (numWords > 0) {

        if (writeStrategy == 0) { // writeintsafe

          wordIndex = 0;
          while (wordIndex < numWords) {
            if (!useWDouble) {
              MSC->WDATA = *data++;
              wordIndex++;
              MSC->WRITECMD = (0x1UL << 3);
            }

            timeOut = 10000000ul;
            while ((MSC->STATUS & (0x1UL << 0)) && (timeOut != 0)) {
              timeOut--;
            }

            if (timeOut == 0) {
              stub_exit(1);
            }
          }
        }

        else {

          wordIndex = 0;
          while (wordIndex < numWords) {

            while (!(MSC->STATUS & (0x1UL << 3))) {

              if ((MSC->STATUS & ((0x1UL << 4) | (0x1UL << 0) |
                                  (0x1UL << 3))) == (0x1UL << 4)) {
                MSC->WRITECMD = (0x1UL << 4);
              }
            }

            if (!useWDouble) {
              MSC->WDATA = *data;
              MSC->WRITECMD = (0x1UL << 4);
              data++;
              wordIndex++;
            }

            else // useWDouble == 1
            {
            }
          }

          timeOut = 10000000ul;
          while ((MSC->STATUS & (0x1UL << 0)) && (timeOut != 0)) {
            timeOut--;
          }

          if (timeOut == 0) {
            stub_exit(1);
          }
        }
      }
    }

    wordCount += pageWords;
    pData += pageWords;
  }

  // turning writes off
  MSC->WRITECTRL &= ~(0x1UL << 0);
  // lock it up
  MSC->LOCK = 0;

  stub_exit(0);
}

/*
void MSC_LoadVerifyAddress(uint32_t *address) {
  uint32_t status;
  uint32_t timeOut;

  timeOut = 10000000ul;
  while ((MSC->STATUS & (0x1UL << 0)) && (timeOut != 0)) {
    timeOut--;
  }

  if (timeOut == 0) {
    stub_exit(1);
  }

  MSC->ADDRB = (uint32_t)address;
  MSC->WRITECMD = (0x1UL << 0);
}

void MSC_LoadWriteData(uint32_t *data, uint32_t numWords,
                       uint8_t writeStrategy) {
  uint32_t timeOut;
  uint32_t wordIndex;
  _Bool useWDouble = 0;

  uint32_t irqState;

  if (numWords > 0) {

    if (writeStrategy == 0) { // writeintsafe

      wordIndex = 0;
      while (wordIndex < numWords) {
        if (!useWDouble) {
          MSC->WDATA = *data++;
          wordIndex++;
          MSC->WRITECMD = (0x1UL << 3);
        }

        timeOut = 10000000ul;
        while ((MSC->STATUS & (0x1UL << 0)) && (timeOut != 0)) {
          timeOut--;
        }

        if (timeOut == 0) {
          stub_exit(1);
        }
      }
    }

    else {

      wordIndex = 0;
      while (wordIndex < numWords) {

        while (!(MSC->STATUS & (0x1UL << 3))) {

          if ((MSC->STATUS & ((0x1UL << 4) | (0x1UL << 0) | (0x1UL << 3))) ==
              (0x1UL << 4)) {
            MSC->WRITECMD = (0x1UL << 4);
          }
        }

        if (!useWDouble) {
          MSC->WDATA = *data;
          MSC->WRITECMD = (0x1UL << 4);
          data++;
          wordIndex++;
        }

        else // useWDouble == 1
        {
        }
      }

      timeOut = 10000000ul;
      while ((MSC->STATUS & (0x1UL << 0)) && (timeOut != 0)) {
        timeOut--;
      }

      if (timeOut == 0) {
        stub_exit(1);
      }
    }
  }
}
*/