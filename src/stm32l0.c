/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2014,2015 Marc Singer <elf@woollysoft.com>
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

/* Description
   -----------

   This is an implementation of the target-specific functions for the
   STM32L0x[1] and STM32L1x[2] families of ST Microelectronics MCUs,
   Cortex M0+ SOCs.  The NVM interface is substantially similar to the
   STM32L1x parts.  This module is written to better generalize the
   NVM interface and to provide more features.

   [1] ST Microelectronics Document RM0377 (DocID025942), "Reference
       manual for Ultra-low-power STM32L0x1 advanced ARM-based 32-bit
       MCUs," April 2014.

   [2] ST Microelectronics Document RM0038 (DocID15965, "..."Reference
       manual for STM32L100xx, STM32L151xx, STM32L152xx and STM32L162xx
       advanced ARM®-based 32-bit MCUs, " July 2014


   NOTES
   =====

   o Stubbed and non-stubbed NVM operation functions.  The STM32L0xx
     appears to behave differently from other STM32 cores.  When it
     enters a fault state it will not exit this state without a
     reset.  However, the reset will immediately enter a fault state
     if program flash is erased.  When in this state, it will not
     permit execution of code in RAM in the way that other cores
     will.  Changing the PC to the start of RAM and single stepping
     will immediately HardFault.

     The stub functions can be both faster and simpler because they
     have direct access to the MCU.  So, to permit stub operation in
     the best circumstances, the NVM operation functions will check
     the MCU state and either execute the stub or non-stub version
     accordingly. The user can override stubs as well with a command
     in case the detection feature fails.

   o Erase would be more efficient if we checked for non-blank-ness
     before initiating an erase.  This would have to be done in a stub
     for efficiency.

   o Mass erase broken.  The method for performing a mass erase is to
     set the options for read protection, reload the option bytes, set
     options for no protection, and then reload the option bytes
     again.  The command fails because we lose contact with the target
     when we perform the option byte reload.  For the time being, the
     command is disabled.

   o Errors.  We probably should clear SR errors immediately after
     detecting them.  If we don't then we always must wait for the NVM
     module to complete the last operation before we can start another.

   o There are minor inconsistencies between the stm32l0 and the
     stm32l1 in when handling NVM operations.

     o When we erase or write individual words (not half-pages) on the
       stm32l0, we set the PROG bit.  On the stm32l1 the PROG bit is
       only set when erasing.  This  is not documented in the register
       summaries, but in the functional quick reference.

     o On the STM32L1xx, PECR can only be changed when the NVM
       hardware is idle.  The STM32L0xx allows the PECR to be updated
       while an operation is in progress.

  0x1ff80000: 0x00aa 0xff55 OK
  0x1ff80004: 0x8070 0x7f8f OK
  0x1ff80008: 0x0000 0xffff OK
  OPTR: 0x807000aa, RDPROT 0, WPRMOD 0, WDG_SW 1, BOOT1 1


    Options code
p *(int*)0x40022004 = 1
p *(int*)0x4002200c = 0x89abcdef
p *(int*)0x4002200c = 0x02030405
p *(int*)0x40022014 = 0xfbead9c8
p *(int*)0x40022014 = 0x24252627
p *(int*)0x40022004 = 0x10
x/4wx 0x40022000
p *(int*)0x1ff80000 = 0xff5500aa

  l1 program
p *(int*)0x40023c04 = 1
p *(int*)0x40023c0c = 0x89abcdef
p *(int*)0x40023c0c = 0x02030405
p *(int*)0x40023c10 = 0x8c9daebf
p *(int*)0x40023c10 = 0x13141516


p *(int*)0x40023c14 = 0xfbead9c8
p *(int*)0x40023c14 = 0x24252627

*/

#define CONFIG_STM32L1          /* Include support for STM32L1 */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "general.h"
#include "adiv5.h"
#include "target.h"
#include "command.h"

#include "stm32lx-nvm.h"

static int inhibit_stubs;

static int stm32lx_nvm_erase (struct target_s* target,
                              uint32_t addr, int len);
static int stm32lx_nvm_write (struct target_s* target,
                              uint32_t destination,
                              const uint8_t* source,
                              int size);

static int stm32lx_nvm_prog_erase (struct target_s* target,
                                   uint32_t addr, int len);
static int stm32lx_nvm_prog_write (struct target_s* target,
                                   uint32_t destination,
                                   const uint8_t* source,
                                   int size);

static int stm32lx_nvm_prog_erase_stubbed (struct target_s* target,
                                           uint32_t addr, int len);
static int stm32lx_nvm_prog_write_stubbed (struct target_s* target,
                                           uint32_t destination,
                                           const uint8_t* source,
                                           int size);

static int stm32lx_nvm_data_erase (struct target_s* target,
                                   uint32_t addr, int len);
static int stm32lx_nvm_data_write (struct target_s* target,
                                   uint32_t destination,
                                   const uint8_t* source,
                                   int size);

//static bool stm32l0_cmd_erase_mass (target* t, int argc, char** argv);
static bool stm32lx_cmd_option     (target* t, int argc, char** argv);
static bool stm32lx_cmd_eeprom     (target* t, int argc, char** argv);
//static bool stm32l0_cmd_reset      (target* t, int argc, char** argv);
static bool stm32lx_cmd_stubs      (target* t, int argc, char** argv);

static const struct command_s stm32lx_cmd_list[] = {
  { "stubs",		(cmd_handler) stm32lx_cmd_stubs,
    "Enable/disable NVM operation stubs" },
//  { "erase_mass",	(cmd_handler) stm32l0_cmd_erase_mass,
//    "Erase NVM flash and data" },
//  { "reset",		(cmd_handler) stm32l0_cmd_reset, "Reset target" },
  { "option",		(cmd_handler) stm32lx_cmd_option,
    "Manipulate option bytes"},
  { "eeprom",		(cmd_handler) stm32lx_cmd_eeprom,
    "Manipulate EEPROM (NVM data) memory"},
  { 0 },
};

enum {
  STM32L0_DBGMCU_IDCODE_PHYS = 0x40015800,
  STM32L1_DBGMCU_IDCODE_PHYS = 0xe0042000,
};

static const char stm32l0_driver_str[] = "STM32L0xx";

static const char stm32l0_xml_memory_map[] = "<?xml version=\"1.0\"?>"
/* "<!DOCTYPE memory-map "
   "             PUBLIC \"+//IDN gnu.org//DTD GDB Memory Map V1.0//EN\""
   "                    \"http://sourceware.org/gdb/gdb-memory-map.dtd\">"*/
	"<memory-map>"
  /* Program flash; ranges up to 64KiB (0x10000). */
	"  <memory type=\"flash\" start=\"0x08000000\" length=\"0x10000\">"
	"    <property name=\"blocksize\">0x80</property>"
	"  </memory>"
  /* Data (EEPROM) NVRAM; ranges up to 2KiB (0x800). */
	"  <memory type=\"flash\" start=\"0x08080000\" length=\"0x800\">"
	"    <property name=\"blocksize\">0x4</property>"
	"  </memory>"
  /* SRAM; ranges up to 8KiB (0x2000). */
	"  <memory type=\"ram\" start=\"0x20000000\" length=\"0x2000\"/>"
	"</memory-map>";

#if defined (CONFIG_STM32L1)
static const char stm32l1_driver_str[] = "STM32L1xx+";

static const char stm32l1_xml_memory_map[] = "<?xml version=\"1.0\"?>"
/* "<!DOCTYPE memory-map "
   "             PUBLIC \"+//IDN gnu.org//DTD GDB Memory Map V1.0//EN\""
   "                    \"http://sourceware.org/gdb/gdb-memory-map.dtd\">"*/
	"<memory-map>"
  /* Program flash; ranges from 32KiB to 512KiB (0x80000). */
	"  <memory type=\"flash\" start=\"0x08000000\" length=\"0x80000\">"
	"    <property name=\"blocksize\">0x100</property>"
	"  </memory>"
  /* Data (EEPROM) NVRAM; ranges from 2K to 16KiB (0x4000). */
	"  <memory type=\"flash\" start=\"0x08080000\" length=\"0x4000\">"
	"    <property name=\"blocksize\">0x4</property>"
	"  </memory>"
  /* SRAM; ranges from 4KiB to 80KiB (0x14000). */
	"  <memory type=\"ram\" start=\"0x20000000\" length=\"0x14000\"/>"
	"</memory-map>";
#endif

static const uint16_t stm32l0_nvm_prog_write_stub [] = {
#include "../flashstub/stm32l05x-nvm-prog-write.stub"
};

static const uint16_t stm32l0_nvm_prog_erase_stub [] = {
#include "../flashstub/stm32l05x-nvm-prog-erase.stub"
};

static uint32_t stm32lx_nvm_prog_page_size (struct target_s* target)
{
  switch (target->idcode) {
  case 0x417:                   /* STM32L0xx */
    return STM32L0_NVM_PROG_PAGE_SIZE;
  default:                      /* STM32L1xx */
    return STM32L1_NVM_PROG_PAGE_SIZE;
  }
}

static bool stm32lx_is_stm32l1 (struct target_s* target)
{
  switch (target->idcode) {
  case 0x417:                   /* STM32L0xx */
    return false;
  default:                      /* STM32L1xx */
    return true;
  }
}

static uint32_t stm32lx_nvm_eeprom_size (struct target_s* target)
{
  switch (target->idcode) {
  case 0x417:                   /* STM32L0xx */
    return STM32L0_NVM_EEPROM_SIZE;
  default:                      /* STM32L1xx */
    return STM32L1_NVM_EEPROM_SIZE;
  }
}

static uint32_t stm32lx_nvm_phys (struct target_s* target)
{
  switch (target->idcode) {
  case 0x417:                   /* STM32L0xx */
    return STM32L0_NVM_PHYS;
  default:                      /* STM32L1xx */
    return STM32L1_NVM_PHYS;
  }
}

static uint32_t stm32lx_nvm_data_page_size (struct target_s* target)
{
  switch (target->idcode) {
  case 0x417:                   /* STM32L0xx */
    return STM32L0_NVM_DATA_PAGE_SIZE;
  default:                      /* STM32L1xx */
    return STM32L1_NVM_DATA_PAGE_SIZE;
  }
}

static uint32_t stm32lx_nvm_option_size (struct target_s* target)
{
  switch (target->idcode) {
  case 0x417:                   /* STM32L0xx */
    return STM32L0_NVM_OPT_SIZE;
  default:                      /* STM32L1xx */
    return STM32L1_NVM_OPT_SIZE;
  }
}

/** Query MCU memory for an indication as to whether or not the
    currently attached target is served by this module.  We detect the
    STM32L0xx parts as well as the STM32L1xx's. */
bool stm32l0_probe (struct target_s* target)
{
  uint32_t idcode;

#if defined (CONFIG_STM32L1)

  idcode = adiv5_ap_mem_read (adiv5_target_ap (target),
                              STM32L1_DBGMCU_IDCODE_PHYS) & 0xfff;
  switch (idcode) {
  case 0x416:                   /* CAT. 1 device */
  case 0x429:                   /* CAT. 2 device */
  case 0x427:                   /* CAT. 3 device */
  case 0x436:                   /* CAT. 4 device */
  case 0x437:                   /* CAT. 5 device  */
    target->idcode      = idcode;
    target->driver      = stm32l1_driver_str;
    target->xml_mem_map = stm32l1_xml_memory_map;
    target->flash_erase = stm32lx_nvm_erase;
    target->flash_write = stm32lx_nvm_write;
    target_add_commands (target, stm32lx_cmd_list, "STM32L1x");
    return true;
  }
#endif

  idcode = adiv5_ap_mem_read (adiv5_target_ap (target),
                              STM32L0_DBGMCU_IDCODE_PHYS) & 0xfff;
  switch (idcode) {
  default:
    break;

  case 0x417:                   /* STM32L0x[123] & probably others */
    target->idcode      = idcode;
    target->driver	= stm32l0_driver_str;
    target->xml_mem_map = stm32l0_xml_memory_map;
    target->flash_erase = stm32lx_nvm_erase;
    target->flash_write = stm32lx_nvm_write;
    target_add_commands (target, stm32lx_cmd_list, "STM32L0x");
    return true;
  }

  return false;
}

/** Lock the NVM control registers preventing writes or erases. */
static void stm32lx_nvm_lock (ADIv5_AP_t* ap, uint32_t nvm)
{
  adiv5_ap_mem_write (ap, STM32Lx_NVM_PECR(nvm), STM32Lx_NVM_PECR_PELOCK);
}

/** Unlock the NVM control registers for modifying program or
    data flash.  Returns true if the unlock succeeds. */
static bool stm32lx_nvm_prog_data_unlock (ADIv5_AP_t* ap, uint32_t nvm)
{
  /* Always lock first because that's the only way to know that the
     unlock can succeed. */
  adiv5_ap_mem_write (ap, STM32Lx_NVM_PECR(nvm),    STM32Lx_NVM_PECR_PELOCK);
  adiv5_ap_mem_write (ap, STM32Lx_NVM_PEKEYR(nvm),  STM32Lx_NVM_PEKEY1);
  adiv5_ap_mem_write (ap, STM32Lx_NVM_PEKEYR(nvm),  STM32Lx_NVM_PEKEY2);
  adiv5_ap_mem_write (ap, STM32Lx_NVM_PRGKEYR(nvm), STM32Lx_NVM_PRGKEY1);
  adiv5_ap_mem_write (ap, STM32Lx_NVM_PRGKEYR(nvm), STM32Lx_NVM_PRGKEY2);

  return !(adiv5_ap_mem_read (ap, STM32Lx_NVM_PECR(nvm))
           & STM32Lx_NVM_PECR_PRGLOCK);
}


/** Unlock the NVM control registers for modifying option bytes.
    Returns true if the unlock succeeds. */
static bool stm32lx_nvm_opt_unlock (ADIv5_AP_t* ap, uint32_t nvm)
{
  /* Always lock first because that's the only way to know that the
     unlock can succeed. */
  adiv5_ap_mem_write (ap, STM32Lx_NVM_PECR(nvm),    STM32Lx_NVM_PECR_PELOCK);
  adiv5_ap_mem_write (ap, STM32Lx_NVM_PEKEYR(nvm),  STM32Lx_NVM_PEKEY1);
  adiv5_ap_mem_write (ap, STM32Lx_NVM_PEKEYR(nvm),  STM32Lx_NVM_PEKEY2);
  adiv5_ap_mem_write (ap, STM32Lx_NVM_OPTKEYR(nvm), STM32Lx_NVM_OPTKEY1);
  adiv5_ap_mem_write (ap, STM32Lx_NVM_OPTKEYR(nvm), STM32Lx_NVM_OPTKEY2);

  return !(adiv5_ap_mem_read (ap, STM32Lx_NVM_PECR(nvm))
           & STM32Lx_NVM_PECR_OPTLOCK);
}


/** Erase a region of flash using a stub function.  This only works
    when the MCU hasn't entered a fault state (see NOTES).  The flash
    array is erased for all pages from addr to addr+len inclusive.
    Only stm32l0xx is supported. */
static int stm32lx_nvm_prog_erase_stubbed (struct target_s* target,
                                           uint32_t addr, int size)
{
  struct stm32lx_nvm_stub_info info;

  info.page_size = stm32lx_nvm_prog_page_size (target);

  /* Load the stub */
  target_mem_write_words (target, STM32Lx_STUB_PHYS,
                          (void*) &stm32l0_nvm_prog_erase_stub[0],
                          sizeof (stm32l0_nvm_prog_erase_stub));

  /* Setup parameters */
  info.destination = addr;
  info.size        = size;

  /* Copy parameters */
  target_mem_write_words (target, STM32Lx_STUB_INFO_PHYS,
                          (void*) &info, sizeof (info));

  /* Execute stub */
  target_pc_write (target, STM32Lx_STUB_PHYS);
  if (target_check_error (target))
    return -1;
  target_halt_resume (target, 0);
  while (!target_halt_wait (target))
    ;
  {
    ADIv5_AP_t* ap     = adiv5_target_ap(target);
    const uint32_t nvm = stm32lx_nvm_phys (target);
    if (adiv5_ap_mem_read (ap, STM32Lx_NVM_SR(nvm)) & STM32Lx_NVM_SR_ERR_M)
      return -1;
  }

  return 0;
}


/** Write to program flash using a stub function.  This only works
    when the MCU hasn't entered a fault state.  Once the MCU faults,
    this function will not succeed because the MCU will fault before
    executing a single instruction in the stub. */
static int stm32lx_nvm_prog_write_stubbed (struct target_s* target,
                                           uint32_t destination,
                                           const uint8_t* source,
                                           int size)
{
  struct stm32lx_nvm_stub_info info;
  const uint32_t nvm = stm32lx_nvm_phys (target);
  const size_t page_size = stm32lx_nvm_prog_page_size (target);

//  static int attempts;

  /* We can't handle unaligned destination or non-word writes. */
  /* *** FIXME: we should handle misaligned writes by padding with
     *** zeros.  Probably, the only real time we'd see something
     *** misaligned would be on a write to a final half-word.  Perhaps
     *** this could be handled with the stub?  In fact, aligning the
     *** start is going to be mandatory.  We will change the code to
     *** cope with a trailing half-word. */
  if ((destination & 3) || (size & 3))
    return -1;

//  if (attempts++)
//    return 0;

//  if (!stm32l0_nvm_prog_data_unlock (adiv5_target_ap (target)))
//    return -1;

  info.page_size = page_size;

  /* Load the stub */
  target_mem_write_words (target, STM32Lx_STUB_PHYS,
                          (void*) &stm32l0_nvm_prog_write_stub[0],
                          sizeof (stm32l0_nvm_prog_write_stub));

  while (size > 0) {

    /* Max transfer size is adjusted in the event that the
       destination isn't half-page aligned.  This allows the
       sub to write the first partial half-page and then
       as many half-pages as will fit in the buffer. */
    size_t max = STM32Lx_STUB_DATA_MAX
      - (destination - (destination & ~(info.page_size/2 - 1)));
    size_t cb = size;
    if (cb > max)
      cb = max;

    /* Setup parameters */
    info.source      = STM32Lx_STUB_DATA_PHYS;
    info.destination = destination;
    info.size        = cb;

    /* Copy data to write to flash */
    target_mem_write_words (target, info.source, (void*) source, info.size);

    /* Move pointers early */
    destination += cb;
    source += cb;
    size -= cb;

    /* Copy parameters */
    target_mem_write_words (target, STM32Lx_STUB_INFO_PHYS,
                            (void*) &info, sizeof (info));

    /* Execute stub */
    target_pc_write (target, STM32Lx_STUB_PHYS);
    if (target_check_error (target))
      return -1;
    target_halt_resume (target, 0);
    while (!target_halt_wait (target))
      ;

    if (adiv5_ap_mem_read (adiv5_target_ap (target), STM32Lx_NVM_SR(nvm))
        & STM32Lx_NVM_SR_ERR_M)
      return -1;
  }

  return 0;
}


/** Erase a region of NVM for STM32Lx.  This is the lead function and
    it will invoke an implementation, stubbed or not depending on the
    options and the range of addresses. */
static int stm32lx_nvm_erase (struct target_s* target, uint32_t addr, int size)
{
  if (addr >= STM32Lx_NVM_EEPROM_PHYS)
    return stm32lx_nvm_data_erase (target, addr, size);

  /* Use stub if not inhibited, the MCU is in a non-exceptonal state
     and there is stub. */
  volatile uint32_t regs[20];
  target_regs_read (target, &regs);
  if (inhibit_stubs || (regs[16] & 0xf) || stm32lx_is_stm32l1 (target))
    return stm32lx_nvm_prog_erase (target, addr, size);

  return stm32lx_nvm_prog_erase_stubbed (target, addr, size);
}


/** Write to a region on NVM for STM32L1xx.  This is the lead function
    and it will ibvoke an implementation, stubbed or not depending on
    the options and the range of addresses. */
static int stm32lx_nvm_write (struct target_s* target,
                              uint32_t destination,
                              const uint8_t* source,
                              int size)
{
  if (destination >= STM32Lx_NVM_EEPROM_PHYS)
    return stm32lx_nvm_data_write (target, destination, source, size);

  /* Skip stub if the MCU is in a questionable state or if the user
     asks us to avoid stubs. */
  volatile uint32_t regs[20];
  target_regs_read (target, &regs);
  if (inhibit_stubs || (regs[16] & 0xf) || stm32lx_is_stm32l1 (target))
    return stm32lx_nvm_prog_write (target, destination, source, size);

  return stm32lx_nvm_prog_write_stubbed (target, destination, source, size);
}


/** Erase a region of program flash using operations through the debug
    interface.  This is slower than stubbed versions (see NOTES).  The
    flash array is erased for all pages from addr to addr+len
    inclusive.  NVM register file address chosen from target. */
static int stm32lx_nvm_prog_erase (struct target_s* target,
                                   uint32_t addr, int len)
{
  ADIv5_AP_t* ap = adiv5_target_ap (target);
  const size_t page_size = stm32lx_nvm_prog_page_size (target);
  const uint32_t nvm = stm32lx_nvm_phys (target);

  /* Word align */
  len += (addr & 3);
  addr &= ~3;

  if (!stm32lx_nvm_prog_data_unlock (ap, nvm))
    return -1;

  /* Flash page erase instruction */
  adiv5_ap_mem_write (ap, STM32Lx_NVM_PECR(nvm),
                      STM32Lx_NVM_PECR_ERASE | STM32Lx_NVM_PECR_PROG);

  {
    uint32_t pecr = adiv5_ap_mem_read (ap, STM32Lx_NVM_PECR (nvm));
    if ((pecr & (STM32Lx_NVM_PECR_PROG | STM32Lx_NVM_PECR_ERASE))
        != (STM32Lx_NVM_PECR_PROG | STM32Lx_NVM_PECR_ERASE))
      return -1;
  }

  /* Busy wait? *** FIXME: remove? */
  while (adiv5_ap_mem_read (ap, STM32Lx_NVM_SR(nvm))
         & STM32Lx_NVM_SR_BSY)
    if (target_check_error(target))
      return -1;

  /* Clear errors.  *** FIXME: this only works when we wait for the
     NVM block to complete the last operation. */
  adiv5_ap_mem_write (ap, STM32Lx_NVM_SR (nvm), STM32Lx_NVM_SR_ERR_M);

  while (len > 0) {
    /* Write first word of page to 0 */
    adiv5_ap_mem_write (ap, addr, 0);

    len  -= page_size;
    addr += page_size;
  }

  /* Disable further programming by locking PECR */
  stm32lx_nvm_lock (ap, nvm);

  /* Wait for completion or an error */
  while (1) {
    uint32_t sr = adiv5_ap_mem_read (ap, STM32Lx_NVM_SR(nvm));
    if (target_check_error (target))
      return -1;
    if (sr & STM32Lx_NVM_SR_BSY)
      continue;
    if ((sr & STM32Lx_NVM_SR_ERR_M) || !(sr & STM32Lx_NVM_SR_EOP))
      return -1;
    break;
  }

  return 0;
}


/** Write to program flash using operations through the debug
    interface.  This is slower than the stubbed write (see NOTES).
    NVM register file address chosen from target. */
static int stm32lx_nvm_prog_write (struct target_s* target,
                                   uint32_t destination,
                                   const uint8_t* source_8,
                                   int size)
{
  ADIv5_AP_t* ap = adiv5_target_ap (target);
  const uint32_t nvm = stm32lx_nvm_phys (target);
  const bool is_stm32l1 = stm32lx_is_stm32l1 (target);

  /* We can only handle word aligned writes and even word-multiple
     ranges.  The stm32l0 cannot perform anything smaller than a word
     write due to the ECC bits. */
  if ((destination & 3) || (size & 3))
    return -1;

  if (!stm32lx_nvm_prog_data_unlock (ap, nvm))
    return -1;

  const size_t half_page_size = stm32lx_nvm_prog_page_size (target)/2;
  uint32_t* source = (uint32_t*) source_8;

  while (size > 0) {

    /* Wait for BSY to clear because we cannot write the PECR until
       the previous operation completes on STM32L1xx. */
    while (adiv5_ap_mem_read (ap, STM32Lx_NVM_SR(nvm)) & STM32Lx_NVM_SR_BSY)
      if (target_check_error(target)) {
        return -1;
      }

    // Either we're not half-page aligned or we have less than a half
    // page to write
    if (size < half_page_size || (destination & (half_page_size - 1))) {
      adiv5_ap_mem_write (ap, STM32Lx_NVM_PECR(nvm),
                          is_stm32l1 ? 0 : STM32Lx_NVM_PECR_PROG);
      size_t c = half_page_size - (destination & (half_page_size - 1));

      if (c > size)
        c = size;
      size -= c;

      target_mem_write_words (target, destination, source, c);
      source += c/4;
      destination += c;
    }
    // Or we are writing a half-page(s)
    else {
      adiv5_ap_mem_write (ap, STM32Lx_NVM_PECR (nvm),
                          STM32Lx_NVM_PECR_PROG | STM32Lx_NVM_PECR_FPRG);

      size_t c = size & ~(half_page_size - 1);
      size -= c;
      target_mem_write_words (target, destination, source, c);
      source += c/4;
      destination += c;
    }
  }

  /* Disable further programming by locking PECR */
  stm32lx_nvm_lock (ap, nvm);

  /* Wait for completion or an error */
  while (1) {
    uint32_t sr = adiv5_ap_mem_read (ap, STM32Lx_NVM_SR (nvm));
    if (target_check_error (target)) {
      return -1;
    }
    if (sr & STM32Lx_NVM_SR_BSY)
      continue;
    if ((sr & STM32Lx_NVM_SR_ERR_M) || !(sr & STM32Lx_NVM_SR_EOP)) {
      return -1;
    }
    break;
  }

  return 0;
}


/** Erase a region of data flash using operations through the debug
    interface .  The flash is erased for all pages from addr to
    addr+len, inclusive, on a word boundary.  NVM register file
    address chosen from target. */
static int stm32lx_nvm_data_erase (struct target_s* target,
                                   uint32_t addr, int len)
{
  ADIv5_AP_t* ap = adiv5_target_ap (target);
  const size_t page_size = stm32lx_nvm_data_page_size (target);
  const uint32_t nvm = stm32lx_nvm_phys (target);

  /* Word align */
  len += (addr & 3);
  addr &= ~3;

  if (!stm32lx_nvm_prog_data_unlock (ap, nvm))
    return -1;

  /* Flash data erase instruction */
  adiv5_ap_mem_write (ap, STM32Lx_NVM_PECR(nvm),
                      STM32Lx_NVM_PECR_ERASE | STM32Lx_NVM_PECR_DATA);

  {
    uint32_t pecr = adiv5_ap_mem_read (ap, STM32Lx_NVM_PECR(nvm));
    if ((pecr & (STM32Lx_NVM_PECR_ERASE | STM32Lx_NVM_PECR_DATA))
        != (STM32Lx_NVM_PECR_ERASE | STM32Lx_NVM_PECR_DATA))
      return -1;
  }

  while (len > 0) {
    /* Write first word of page to 0 */
    adiv5_ap_mem_write (ap, addr, 0);

    len  -= page_size;
    addr += page_size;
  }

  /* Disable further programming by locking PECR */
  stm32lx_nvm_lock (ap, nvm);

  /* Wait for completion or an error */
  while (1) {
    uint32_t sr = adiv5_ap_mem_read (ap, STM32Lx_NVM_SR (nvm));
    if (target_check_error (target))
      return -1;
    if (sr & STM32Lx_NVM_SR_BSY)
      continue;
    if ((sr & STM32Lx_NVM_SR_ERR_M) || !(sr & STM32Lx_NVM_SR_EOP))
      return -1;
    break;
  }

  return 0;
}


/** Write to data flash using operations through the debug interface.
    NVM register file address chosen from target.

    *** FIXME: need to make this work with writing a single byte as
    well as words. */
static int stm32lx_nvm_data_write (struct target_s* target,
                                   uint32_t destination,
                                   const uint8_t* source_8,
                                   int size)
{
  ADIv5_AP_t* ap = adiv5_target_ap (target);
  const uint32_t nvm = stm32lx_nvm_phys (target);
  const bool is_stm32l1 = stm32lx_is_stm32l1 (target);

  /* *** FIXME: need to make this work with writing a single byte. */
  if ((destination & 3) || (size & 3))
    return -1;

  if (!stm32lx_nvm_prog_data_unlock (ap, nvm))
    return -1;

  uint32_t* source = (uint32_t*) source_8;

  adiv5_ap_mem_write (ap, STM32Lx_NVM_PECR(nvm),
                      is_stm32l1 ? 0 : STM32Lx_NVM_PECR_DATA);

  while (size) {
    size -= 4;
    uint32_t v = *source++;
    adiv5_ap_mem_write (ap, destination, v);
    destination += 4;

    if (target_check_error (target))
      return -1;
  }

  /* Disable further programming by locking PECR */
  stm32lx_nvm_lock (ap, nvm);

  /* Wait for completion or an error */
  while (1) {
    uint32_t sr = adiv5_ap_mem_read (ap, STM32Lx_NVM_SR(nvm));
    if (target_check_error (target))
      return -1;
    if (sr & STM32Lx_NVM_SR_BSY)
      continue;
    if ((sr & STM32Lx_NVM_SR_ERR_M) || !(sr & STM32Lx_NVM_SR_EOP))
      return -1;
    break;
  }

  return 0;
}


/** Write one option word.  The address is the physical address of the
    word and the value is a complete word value.  The caller is
    responsible for making sure that the value satisfies the proper
    format where the upper 16 bits are the 1s complement of the lower
    16 bits.  The funtion returns when the operation is complete.
    The return value is true if the write succeeded. */
static bool stm32lx_option_write (target *t, uint32_t address, uint32_t value)
{
  ADIv5_AP_t*    ap  = adiv5_target_ap(t);
  const uint32_t nvm = stm32lx_nvm_phys (t);

  /* Erase and program option in one go. */
  adiv5_ap_mem_write (ap, STM32Lx_NVM_PECR(nvm), STM32Lx_NVM_PECR_FIX);
  adiv5_ap_mem_write (ap, address, value);

  uint32_t sr;
  do {
    sr = adiv5_ap_mem_read (ap, STM32Lx_NVM_SR(nvm));
  } while (sr & STM32Lx_NVM_SR_BSY);

  return !(sr & STM32Lx_NVM_SR_ERR_M);
}

/** Write one eeprom value.  This version is more flexible than that
    bulk version used for writing data from the executable file.  The
    address is the physical address of the word and the value is a
    complete word value.  The funtion returns when the operation is
    complete.  The return value is true if the write succeeded.
    FWIW, byte writing isn't supported because the adiv5 layer
    doesn't support byte-level operations. */
static bool stm32lx_eeprom_write (target *t, uint32_t address,
                                  size_t cb, uint32_t value)
{
  ADIv5_AP_t*    ap         = adiv5_target_ap(t);
  const uint32_t nvm        = stm32lx_nvm_phys (t);
  const bool     is_stm32l1 = stm32lx_is_stm32l1 (t);

  /* Clear errors. */
  adiv5_ap_mem_write (ap, STM32Lx_NVM_SR (nvm), STM32Lx_NVM_SR_ERR_M);

  /* Erase and program option in one go. */
  adiv5_ap_mem_write (ap, STM32Lx_NVM_PECR(nvm),
                      (is_stm32l1 ? 0 : STM32Lx_NVM_PECR_DATA)
                      | STM32Lx_NVM_PECR_FIX);
  if (cb == 4)
    adiv5_ap_mem_write (ap, address, value);
  else if (cb == 2)
    adiv5_ap_mem_write_halfword (ap, address, value);
  else
    return false;

  uint32_t sr;
  do {
    sr = adiv5_ap_mem_read (ap, STM32Lx_NVM_SR(nvm));
  } while (sr & STM32Lx_NVM_SR_BSY);

  return !(sr & STM32Lx_NVM_SR_ERR_M);
}

static bool stm32lx_cmd_stubs (target* t,
                               int argc, char** argv)
{
  if (argc == 1) {
    gdb_out ("usage: mon stubs [enable/disable]\n");
  }
  else if (argc == 2) {
    size_t cb = strlen (argv[1]);
    if (!strncasecmp (argv[1], "enable", cb))
      inhibit_stubs = 0;
    if (!strncasecmp (argv[1], "disable", cb))
      inhibit_stubs = 1;
  }
  gdb_outf ("stubs: %sabled\n", inhibit_stubs ? "dis" : "en");

  return true;
}

#if 0
static bool stm32l0_cmd_erase_mass (target* t, int argc , char** argv)
{
  ADIv5_AP_t* ap = adiv5_target_ap (t);

  stm32lx_nvm_opt_unlock (ap);

  stm32l0_option_write (t, 0x1ff80000, 0xffff0000);
  adiv5_ap_mem_write (ap, STM32L0_NVM_PECR, STM32L0_NVM_PECR_OBL_LAUNCH);
  stm32l0_option_write (t, 0x1ff80000, 0xff5500aa);
  adiv5_ap_mem_write (ap, STM32L0_NVM_PECR, STM32L0_NVM_PECR_OBL_LAUNCH);

  uint32_t sr;
  do {
    sr = adiv5_ap_mem_read (ap, STM32L0_NVM_SR);
  } while (sr & STM32L0_NVM_SR_BSY);

  stm32l0_nvm_lock (ap);
  return true;
}
#endif

#if 0
static bool stm32l0_cmd_reset (target* t, int argc, char** argv)
{
  gdb_out ("Resetting target\n");
  target_reset (t);

  return true;
}
#endif

static bool stm32lx_cmd_option (target* t, int argc, char** argv)
{
  ADIv5_AP_t*    ap       = adiv5_target_ap (t);
  const uint32_t nvm      = stm32lx_nvm_phys (t);
  const size_t   opt_size = stm32lx_nvm_option_size (t);

  if (!stm32lx_nvm_opt_unlock (ap, nvm)) {
    gdb_out ("unable to unlock NVM option bytes\n");
    return true;
  }

  size_t cb = strlen (argv[1]);

  if (argc == 2 && !strncasecmp (argv[1], "obl_launch", cb)) {
    adiv5_ap_mem_write (ap, STM32Lx_NVM_PECR(nvm), STM32Lx_NVM_PECR_OBL_LAUNCH);
  }
  else if (argc == 4 && !strncasecmp (argv[1], "raw", cb)) {
    uint32_t addr = strtoul (argv[2], NULL, 0);
    uint32_t val  = strtoul (argv[3], NULL, 0);
    gdb_outf ("raw %08x <- %08x\n", addr, val);
    if (   addr <  STM32Lx_NVM_OPT_PHYS
        || addr >= STM32Lx_NVM_OPT_PHYS + opt_size
        || (addr & 3))
      goto usage;
    if (!stm32lx_option_write (t, addr, val))
      gdb_out ("option write failed\n");
  }
  else if (argc == 4 && !strncasecmp (argv[1], "write", cb)) {
    uint32_t addr = strtoul (argv[2], NULL, 0);
    uint32_t val  = strtoul (argv[3], NULL, 0);
    val = (val & 0xffff) | ((~val & 0xffff) << 16);
    gdb_outf ("write %08x <- %08x\n", addr, val);
    if (   addr <  STM32Lx_NVM_OPT_PHYS
        || addr >= STM32Lx_NVM_OPT_PHYS + opt_size
        || (addr & 3))
      goto usage;
    if (!stm32lx_option_write (t, addr, val))
      gdb_out ("option write failed\n");
  }
  else if (argc == 2 && !strncasecmp (argv[1], "show", cb))
    ;
  else
    goto usage;

  /* Report the current option values */
  for (int i = 0; i < opt_size; i += sizeof (uint32_t)) {
    uint32_t addr = STM32Lx_NVM_OPT_PHYS + i;
    uint32_t val = adiv5_ap_mem_read (ap, addr);
    gdb_outf ("0x%08x: 0x%04x 0x%04x %s\n",
              addr, val & 0xffff, (val >> 16) & 0xffff,
              ((val & 0xffff) == ((~val >> 16) & 0xffff)) ? "OK" : "ERR");
  }
  if (stm32lx_is_stm32l1 (t)) {
    uint32_t optr   = adiv5_ap_mem_read (ap, STM32Lx_NVM_OPTR(nvm));
    uint8_t  rdprot = (optr >> STM32L1_NVM_OPTR_RDPROT_S)
      & STM32L1_NVM_OPTR_RDPROT_M;
    if (rdprot == STM32L1_NVM_OPTR_RDPROT_0)
      rdprot = 0;
    else if (rdprot == STM32L1_NVM_OPTR_RDPROT_2)
      rdprot = 2;
    else
      rdprot = 1;
    gdb_outf ("OPTR: 0x%08x, RDPRT %d, SPRMD %d, "
              "BOR %d, WDG_SW %d, nRST_STP %d, nRST_STBY %d, nBFB2 %d\n",
              optr, rdprot,
              (optr &  STM32L1_NVM_OPTR_SPRMOD)     ? 1 : 0,
              (optr >> STM32L1_NVM_OPTR_BOR_LEV_S) & STM32L1_NVM_OPTR_BOR_LEV_M,
              (optr &  STM32L1_NVM_OPTR_WDG_SW)     ? 1 : 0,
              (optr &  STM32L1_NVM_OPTR_nRST_STOP)  ? 1 : 0,
              (optr &  STM32L1_NVM_OPTR_nRST_STDBY) ? 1 : 0,
              (optr &  STM32L1_NVM_OPTR_nBFB2)      ? 1 : 0);
  }
  else {
    uint32_t optr   = adiv5_ap_mem_read (ap, STM32Lx_NVM_OPTR(nvm));
    uint8_t  rdprot = (optr >> STM32L0_NVM_OPTR_RDPROT_S)
      & STM32L0_NVM_OPTR_RDPROT_M;
    if (rdprot == STM32L0_NVM_OPTR_RDPROT_0)
      rdprot = 0;
    else if (rdprot == STM32L0_NVM_OPTR_RDPROT_2)
      rdprot = 2;
    else
      rdprot = 1;
    gdb_outf ("OPTR: 0x%08x, RDPROT %d, WPRMOD %d, WDG_SW %d, BOOT1 %d\n",
              optr, rdprot,
              (optr & STM32L0_NVM_OPTR_WPRMOD) ? 1 : 0,
              (optr & STM32L0_NVM_OPTR_WDG_SW) ? 1 : 0,
              (optr & STM32L0_NVM_OPTR_BOOT1)  ? 1 : 0);
  }

  goto done;

 usage:
  gdb_out ("usage: monitor option [ARGS]\n");
  gdb_out ("  show                   - Show options in NVM and as loaded\n");
  gdb_out ("  obl_launch             - Reload options from NVM\n");
  gdb_out ("  write <addr> <value16> - Set option half-word; "
           "complement computed\n");
  gdb_out ("  raw <addr> <value32>   - Set option word\n");
  gdb_outf ("The value of <addr> must be word aligned and from 0x%08x "
            "to +0x%x\n",
            STM32Lx_NVM_OPT_PHYS,
            STM32Lx_NVM_OPT_PHYS + opt_size - sizeof (uint32_t));

 done:
  stm32lx_nvm_lock (ap, nvm);
  return true;
}


static bool stm32lx_cmd_eeprom (target* t, int argc, char** argv)
{
  ADIv5_AP_t*    ap  = adiv5_target_ap (t);
  const uint32_t nvm = stm32lx_nvm_phys (t);

  if (!stm32lx_nvm_prog_data_unlock (ap, nvm)) {
    gdb_out ("unable to unlock EEPROM\n");
    return true;
  }

  size_t cb = strlen (argv[1]);

  if (argc == 4) {
    uint32_t addr = strtoul (argv[2], NULL, 0);
    uint32_t val  = strtoul (argv[3], NULL, 0);

    if (   addr <  STM32Lx_NVM_EEPROM_PHYS
        || addr >= STM32Lx_NVM_EEPROM_PHYS + stm32lx_nvm_eeprom_size (t))
      goto usage;

#if 0
    if (!strncasecmp (argv[1], "byte", cb)) {
      gdb_outf ("write byte 0x%08x <- 0x%08x\n", addr, val);
      if (!stm32l0_eeprom_write (t, addr, 1, val))
        gdb_out ("eeprom write failed\n");
    } else
#endif
      if (!strncasecmp (argv[1], "halfword", cb)) {
        val &= 0xffff;
        gdb_outf ("write halfword 0x%08x <- 0x%04x\n", addr, val);
        if (addr & 1)
          goto usage;
        if (!stm32lx_eeprom_write (t, addr, 2, val))
          gdb_out ("eeprom write failed\n");
      } else if (!strncasecmp (argv[1], "word", cb)) {
        gdb_outf ("write word 0x%08x <- 0x%08x\n", addr, val);
        if (addr & 3)
          goto usage;
        if (!stm32lx_eeprom_write (t, addr, 4, val))
          gdb_out ("eeprom write failed\n");
      }
      else
        goto usage;
  }
  else
    goto usage;

  goto done;

 usage:
  gdb_out ("usage: monitor eeprom [ARGS]\n");
//  gdb_out ("  byte     <addr> <value8>  - Write a byte\n");
  gdb_out ("  halfword <addr> <value16> - Write a half-word\n");
  gdb_out ("  word     <addr> <value32> - Write a word\n");
  gdb_outf ("The value of <addr> must in the interval [0x%08x, 0x%x)\n",
            STM32Lx_NVM_EEPROM_PHYS,
            STM32Lx_NVM_EEPROM_PHYS
            + stm32lx_nvm_eeprom_size (t));

 done:
  stm32lx_nvm_lock (ap, nvm);
  return true;
}
