/* @file stm32l05x-nvm-prog-write.cc
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

/* -----------
   DESCRIPTION
   -----------

   NVM program flash writing stub for STM32L05x, a Cortex-M0+ core.
   The stub uses SRAM to host the code fragment and source data to
   perform a write to flash.

   This stub should work with the STM32L1xx as well as long as the
   page_size is set appropriately.  The L0's use a page size of 128
   bytes.  The L1's use a page size of 256 bytes.

   If you plan to modify this routine and emit a new stub, make sure
   to audit the code.  We don't have a stack so we cannot make calls
   that save the link pointer.  IOW, the inline functions should be be
   inlined.

*/

#include <stdint.h>
#include <string.h>
#include "../src/include/stm32l0-nvm.h"

/* Write a block of bytes to flash.  The called is responsible for
   making sure that the address are aligned and that the count is an
   even multiple of words. */
extern "C" void __attribute((naked)) stm32l05x_nvm_prog_write () {
  // Leave room for INFO at second word of routine
  __asm volatile ("b 0f\n\t"
                  ".align 2\n\t"
                  ".word 0\n\t"
                  ".word 0\n\t"
                  ".word 0\n\t"
                  ".word 0\n\t"
                  "0:");

  if (!unlock ())
    goto quit;

  while (Info.size > 0) {

    // Either we're not half-page aligned or we have less than a half
    // page to write
    if (Info.size < Info.page_size/2
        || (reinterpret_cast<uint32_t> (Info.destination)
            & (Info.page_size/2 - 1))) {
      Nvm.pecr = STM32L0_NVM_PECR_PROG; // Word programming
      size_t c = Info.page_size/2
        - (reinterpret_cast<uint32_t> (Info.destination)
           & (Info.page_size/2 - 1));
      if (c > Info.size)
        c = Info.size;
      Info.size -= c;
      c /= 4;
      while (c--) {
        uint32_t v = *Info.source++;
        *Info.destination++ = v;
        if (Nvm.sr & STM32L0_NVM_SR_ERR_M)
          goto quit;
      }
    }
    // Or we are writing a half-page
    else {
      Nvm.pecr = STM32L0_NVM_PECR_PROG | STM32L0_NVM_PECR_FPRG; // Half-page prg
      size_t c = Info.page_size/2;
      Info.size -= c;
      c /= 4;
      while (c--) {
        uint32_t v = *Info.source++;
        *Info.destination++ = v;
      }
      if (Nvm.sr & STM32L0_NVM_SR_ERR_M)
        goto quit;
    }
  }

quit:
  lock ();
  __asm volatile ("bkpt");
}

/*
   Local Variables:
   compile-command: "/opt/arm/arm-none-eabi-g++ -mcpu=cortex-m0plus -g -c -std=c++11 -mthumb -o stm32l05x-nvm-prog-write.o -Os -Wa,-ahndl=stm32l05x-nvm-prog-write.lst stm32l05x-nvm-prog-write.cc ; /opt/arm/arm-none-eabi-objdump -S stm32l05x-nvm-prog-write.o | ./code-to-array.pl > stm32l05x-nvm-prog-write.stub"
   End:

*/
