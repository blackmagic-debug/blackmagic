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

/* This file is derived from the Silicon Labs SDK:
 *
 *******************************************************************************
 * @section License
 * <b>Copyright 2016 Silicon Laboratories, Inc. http://www.silabs.com</b>
 *******************************************************************************
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 * DISCLAIMER OF WARRANTY/LIMITATION OF REMEDIES: Silicon Labs has no
 * obligation to support this Software. Silicon Labs is providing the
 * Software "AS IS", with no express or implied warranties of any kind,
 * including, but not limited to, any implied warranties of merchantability
 * or fitness for any particular purpose or warranties against infringement
 * of any proprietary rights of a third party.
 *
 * Silicon Labs will not be liable for any consequential, incidental, or
 * special damages, or any other relief, or for any claim by any third party,
 * arising from your use of this Software.
 *
 ******************************************************************************/

#include "stub.h"
#include "efr32.h"

#define MSC ((MSC_TypeDef *)(0x400E0000UL))

void _efm32_flash_write_stub(uint32_t *dest, uint32_t *src, uint32_t size);

void __attribute__((naked))
efm32_flash_write_stub(uint32_t *dest, uint32_t *src, uint32_t size) {
  asm("ldr r0, =_estack; mov   sp, r0;" ::: "r0");
  _efm32_flash_write_stub(dest, src, size);
};

void _efm32_flash_write_stub(uint32_t *dest, uint32_t *src, uint32_t size) {
  uint32_t wordCount;
  uint32_t numWords;
  uint32_t pageWords;
  uint32_t *pData;

  // Unlock the MSC
  MSC->LOCK = EFM32_MSC_LOCK_LOCKKEY;

  // Enable writes
  MSC->WRITECTRL |= (0x1UL << 0);

  // MSC->BOOTLOADERCTRL |= (0x1UL << 1);  // we're in like Flynn

  // According to the manual you can brick the device by erasing 'reserved'
  // pages while BOOTLOADERCTRL is unlocked.
  // Managed to write to 0x0 anyway without setting this bit...

  numWords = size >> 2;

  for (wordCount = 0, pData = (uint32_t *)src; wordCount < numWords;) {

    { // MSC_LoadVerifyAddress(dest + wordCount);
      uint32_t status;
      uint32_t timeOut;

      timeOut = EFM32_FLASH_WRITE_TIMEOUT;
      while ((MSC->STATUS & (0x1UL << 0)) && (timeOut != 0)) {
        timeOut--;
      }

      if (timeOut == 0) {
        stub_exit(1);
      }

      MSC->ADDRB = (uint32_t)(dest + wordCount);

      // gdb issues a separate 'erase page' command
      MSC->WRITECMD = (0x1UL << 0); // erase page
    }

    // for parts with 2048b pages
    pageWords = (2048 - (((uint32_t)(dest + wordCount)) & (2048 - 1))) /
                sizeof(uint32_t);
    if (pageWords > numWords - wordCount) {
      pageWords = numWords - wordCount;
    }

    // MSC_LoadWriteData(pData, pageWords, 0);
    {
      uint32_t *data;
      data = pData;
      uint32_t numWords = pageWords;
      uint8_t writeStrategy = 0;
      uint32_t timeOut;
      uint32_t wordIndex;
      int useWDouble = 0;

      uint32_t irqState;

      if (numWords > 0) {
        wordIndex = 0;
        while (wordIndex < numWords) {
          if (!useWDouble) {
            MSC->WDATA = *data++;
            wordIndex++;
            MSC->WRITECMD = (0x1UL << 3);
          }

          timeOut = EFM32_FLASH_WRITE_TIMEOUT;
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
