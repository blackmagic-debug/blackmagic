/* @file stm32l05x-nvm-prog-erase.cc
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

   NVM program flash erase stub for STM32L05x, a Cortex-M0+ core.  The
   stub uses SRAM to host the code fragment to perform the erase.

   This stub works with the STM32L1xx given a few options.

   If you plan to modify this routine and emit a new stub, make sure
   to audit the code.  We don't have a stack so we cannot make calls
   that save the link pointer.  IOW, the inline functions should be be
   inlined.

*/

#include <stdint.h>
#include <string.h>
#include "../src/include/stm32lx-nvm.h"

/* Erase a region of flash.  In the event that the erase is misaligned
   with respect to pages, it will erase the pages that contain the
   requested range of bytes. */
extern "C" void __attribute((naked)) stm32l05x_nvm_prog_erase() {
        // Leave room for INFO at second word of routine
        __asm volatile ("b 0f\n\t"
                        ".align 2\n\t"
                        ".word 0\n\t"
                        ".word 0\n\t"
                        ".word 0\n\t"
                        ".word 0\n\t"
                        ".word 0\n\t"
                        "0:");

        auto& nvm = Nvm (Info.nvm);

        // Align to the start of the first page so that we make sure to erase
        // all of the target pages.
        auto remainder    = reinterpret_cast<uint32_t> (Info.destination)
                & (Info.page_size - 1);
        Info.size        += remainder;
        Info.destination -= remainder/sizeof (*Info.destination);

        if (!unlock(nvm))
                goto quit;

        nvm.sr = STM32Lx_NVM_SR_ERR_M; // Clear errors

        // Enable erasing
        nvm.pecr = STM32Lx_NVM_PECR_PROG | STM32Lx_NVM_PECR_ERASE;
        if ((nvm.pecr & (STM32Lx_NVM_PECR_PROG | STM32Lx_NVM_PECR_ERASE))
            != (STM32Lx_NVM_PECR_PROG | STM32Lx_NVM_PECR_ERASE))
                goto quit;

        while (Info.size > 0) {
                *Info.destination = 0;      // Initiate erase

                Info.destination += Info.page_size/sizeof (*Info.destination);
                Info.size -= Info.page_size;
        }

quit:
        lock(nvm);
        __asm volatile ("bkpt");
}

/*
   Local Variables:
   compile-command: "/opt/arm/arm-none-eabi-g++ -mcpu=cortex-m0plus -g -c -std=c++11 -mthumb -o stm32l05x-nvm-prog-erase.o -Os -Wa,-ahndl=stm32l05x-nvm-prog-erase.lst stm32l05x-nvm-prog-erase.cc ; /opt/arm/arm-none-eabi-objdump -d -z stm32l05x-nvm-prog-erase.o | ./dump-to-array.sh > stm32l05x-nvm-prog-erase.stub"
   End:

*/
