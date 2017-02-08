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

   o Mass erase unimplemented.  The method for performing a mass erase
     is to set the options for read protection, reload the option
     bytes, set options for no protection, and then reload the option
     bytes again.  The command fails because we lose contact with the
     target when we perform the option byte reload.  For the time
     being, the command is disabled.

     The body of the function was the following.  It is left here for
     reference in case someone either discovers what is wrong with
     these lines, or a change is made to the emulator that allows it
     to regain control of the target after the option byte reload.

       stm32l0_option_write(t, 0x1ff80000, 0xffff0000);
       target_mem_write32(target, STM32L0_NVM_PECR, STM32L0_NVM_PECR_OBL_LAUNCH);
       stm32l0_option_write(t, 0x1ff80000, 0xff5500aa);
       target_mem_write32(target, STM32L0_NVM_PECR, STM32L0_NVM_PECR_OBL_LAUNCH);

       uint32_t sr;
       do {
         sr = target_mem_read32(target, STM32L0_NVM_SR);
       } while (sr & STM32L0_NVM_SR_BSY);

   o Errors.  We probably should clear SR errors immediately after
     detecting them.  If we don't then we always must wait for the NVM
     module to complete the last operation before we can start another.

   o There are minor inconsistencies between the stm32l0 and the
     stm32l1 in when handling NVM operations.

   o On the STM32L1xx, PECR can only be changed when the NVM
     hardware is idle.  The STM32L0xx allows the PECR to be updated
     while an operation is in progress.

*/

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"

#define STM32Lx_NVM_PECR(p)     ((p) + 0x04)
#define STM32Lx_NVM_PEKEYR(p)   ((p) + 0x0C)
#define STM32Lx_NVM_PRGKEYR(p)  ((p) + 0x10)
#define STM32Lx_NVM_OPTKEYR(p)  ((p) + 0x14)
#define STM32Lx_NVM_SR(p)       ((p) + 0x18)
#define STM32Lx_NVM_OPTR(p)     ((p) + 0x1C)

#define STM32L0_NVM_PHYS             (0x40022000ul)
#define STM32L0_NVM_OPT_SIZE         (12)
#define STM32L0_NVM_EEPROM_CAT1_SIZE (1*512)
#define STM32L0_NVM_EEPROM_CAT2_SIZE (1*1024)
#define STM32L0_NVM_EEPROM_CAT3_SIZE (2*1024)
#define STM32L0_NVM_EEPROM_CAT5_SIZE (6*1024)

#define STM32L1_NVM_PHYS             (0x40023c00ul)
#define STM32L1_NVM_OPT_SIZE         (32)
#define STM32L1_NVM_EEPROM_SIZE      (16*1024)

#define STM32Lx_NVM_OPT_PHYS         0x1ff80000ul
#define STM32Lx_NVM_EEPROM_PHYS      0x08080000ul

#define STM32Lx_NVM_PEKEY1           (0x89abcdeful)
#define STM32Lx_NVM_PEKEY2           (0x02030405ul)
#define STM32Lx_NVM_PRGKEY1          (0x8c9daebful)
#define STM32Lx_NVM_PRGKEY2          (0x13141516ul)
#define STM32Lx_NVM_OPTKEY1          (0xfbead9c8ul)
#define STM32Lx_NVM_OPTKEY2          (0x24252627ul)

#define STM32Lx_NVM_PECR_OBL_LAUNCH  (1<<18)
#define STM32Lx_NVM_PECR_ERRIE       (1<<17)
#define STM32Lx_NVM_PECR_EOPIE       (1<<16)
#define STM32Lx_NVM_PECR_FPRG        (1<<10)
#define STM32Lx_NVM_PECR_ERASE       (1<< 9)
#define STM32Lx_NVM_PECR_FIX         (1<< 8) /* FTDW */
#define STM32Lx_NVM_PECR_DATA        (1<< 4)
#define STM32Lx_NVM_PECR_PROG        (1<< 3)
#define STM32Lx_NVM_PECR_OPTLOCK     (1<< 2)
#define STM32Lx_NVM_PECR_PRGLOCK     (1<< 1)
#define STM32Lx_NVM_PECR_PELOCK      (1<< 0)

#define STM32Lx_NVM_SR_NOTZEROERR    (1<<16)
#define STM32Lx_NVM_SR_SIZERR        (1<<10)
#define STM32Lx_NVM_SR_PGAERR        (1<<9)
#define STM32Lx_NVM_SR_WRPERR        (1<<8)
#define STM32Lx_NVM_SR_EOP           (1<<1)
#define STM32Lx_NVM_SR_BSY           (1<<0)
#define STM32Lx_NVM_SR_ERR_M         (STM32Lx_NVM_SR_WRPERR | \
                                      STM32Lx_NVM_SR_PGAERR | \
                                      STM32Lx_NVM_SR_SIZERR | \
                                      STM32Lx_NVM_SR_NOTZEROERR)

#define STM32L0_NVM_OPTR_BOOT1       (1<<31)
#define STM32Lx_NVM_OPTR_WDG_SW      (1<<20)
#define STM32L0_NVM_OPTR_WPRMOD      (1<<8)
#define STM32Lx_NVM_OPTR_RDPROT_S    (0)
#define STM32Lx_NVM_OPTR_RDPROT_M    (0xff)
#define STM32Lx_NVM_OPTR_RDPROT_0    (0xaa)
#define STM32Lx_NVM_OPTR_RDPROT_2    (0xcc)

#define STM32L1_NVM_OPTR_nBFB2       (1<<23)
#define STM32L1_NVM_OPTR_nRST_STDBY  (1<<22)
#define STM32L1_NVM_OPTR_nRST_STOP   (1<<21)
#define STM32L1_NVM_OPTR_BOR_LEV_S   (16)
#define STM32L1_NVM_OPTR_BOR_LEV_M   (0xf)
#define STM32L1_NVM_OPTR_SPRMOD      (1<<8)

static int stm32lx_nvm_prog_erase(struct target_flash* f,
                                  target_addr addr, size_t len);
static int stm32lx_nvm_prog_write(struct target_flash* f,
                                  target_addr destination,
                                  const void* src,
                                  size_t size);

static int stm32lx_nvm_data_erase(struct target_flash* f,
                                  target_addr addr, size_t len);
static int stm32lx_nvm_data_write(struct target_flash* f,
                                  target_addr destination,
                                  const void* source,
                                  size_t size);

static bool stm32lx_cmd_option     (target* t, int argc, char** argv);
static bool stm32lx_cmd_eeprom     (target* t, int argc, char** argv);

static const struct command_s stm32lx_cmd_list[] = {
        { "option",		(cmd_handler) stm32lx_cmd_option,
          "Manipulate option bytes"},
        { "eeprom",		(cmd_handler) stm32lx_cmd_eeprom,
          "Manipulate EEPROM(NVM data) memory"},
        { NULL, NULL, NULL },
};

enum {
        STM32L0_DBGMCU_IDCODE_PHYS = 0x40015800,
        STM32L1_DBGMCU_IDCODE_PHYS = 0xe0042000,
};

static bool stm32lx_is_stm32l1(target* t)
{
        switch (t->idcode) {
        case 0x457:                   /* STM32L0xx Cat1 */
        case 0x425:                   /* STM32L0xx Cat2 */
        case 0x417:                   /* STM32L0xx Cat3 */
        case 0x447:                   /* STM32L0xx Cat5 */
               return false;
        default:                      /* STM32L1xx */
                return true;
        }
}

static uint32_t stm32lx_nvm_eeprom_size(target *t)
{
        switch (t->idcode) {
        case 0x457:                   /* STM32L0xx Cat1 */
                return STM32L0_NVM_EEPROM_CAT1_SIZE;
        case 0x425:                   /* STM32L0xx Cat2 */
                return STM32L0_NVM_EEPROM_CAT2_SIZE;
        case 0x417:                   /* STM32L0xx Cat3 */
                return STM32L0_NVM_EEPROM_CAT3_SIZE;
        case 0x447:                   /* STM32L0xx Cat5 */
                return STM32L0_NVM_EEPROM_CAT5_SIZE;
        default:                      /* STM32L1xx */
                return STM32L1_NVM_EEPROM_SIZE;
        }
}

static uint32_t stm32lx_nvm_phys(target *t)
{
        switch (t->idcode) {
        case 0x457:                   /* STM32L0xx Cat1 */
        case 0x425:                   /* STM32L0xx Cat2 */
        case 0x417:                   /* STM32L0xx Cat3 */
        case 0x447:                   /* STM32L0xx Cat5 */
                return STM32L0_NVM_PHYS;
        default:                      /* STM32L1xx */
                return STM32L1_NVM_PHYS;
        }
}

static uint32_t stm32lx_nvm_option_size(target *t)
{
        switch (t->idcode) {
        case 0x457:                   /* STM32L0xx Cat1 */
        case 0x425:                   /* STM32L0xx Cat2 */
        case 0x417:                   /* STM32L0xx Cat3 */
        case 0x447:                   /* STM32L0xx Cat5 */
                return STM32L0_NVM_OPT_SIZE;
        default:                      /* STM32L1xx */
                return STM32L1_NVM_OPT_SIZE;
        }
}

static void stm32l_add_flash(target *t,
                             uint32_t addr, size_t length, size_t erasesize)
{
	struct target_flash *f = calloc(1, sizeof(*f));
	f->start = addr;
	f->length = length;
	f->blocksize = erasesize;
	f->erase = stm32lx_nvm_prog_erase;
	f->write = target_flash_write_buffered;
	f->done = target_flash_done_buffered;
	f->write_buf = stm32lx_nvm_prog_write;
	f->buf_size = erasesize/2;
	target_add_flash(t, f);
}

static void stm32l_add_eeprom(target *t, uint32_t addr, size_t length)
{
	struct target_flash *f = calloc(1, sizeof(*f));
	f->start = addr;
	f->length = length;
	f->blocksize = 4;
	f->erase = stm32lx_nvm_data_erase;
	f->write = stm32lx_nvm_data_write;
	f->align = 1;
	target_add_flash(t, f);
}

/** Query MCU memory for an indication as to whether or not the
    currently attached target is served by this module.  We detect the
    STM32L0xx parts as well as the STM32L1xx's. */
bool stm32l0_probe(target* t)
{
	uint32_t idcode;

	idcode = target_mem_read32(t, STM32L1_DBGMCU_IDCODE_PHYS) & 0xfff;
	switch (idcode) {
	case 0x416:                   /* CAT. 1 device */
	case 0x429:                   /* CAT. 2 device */
	case 0x427:                   /* CAT. 3 device */
	case 0x436:                   /* CAT. 4 device */
	case 0x437:                   /* CAT. 5 device  */
		t->idcode = idcode;
		t->driver = "STM32L1x";
		target_add_ram(t, 0x20000000, 0x14000);
		stm32l_add_flash(t, 0x8000000, 0x80000, 0x100);
		//stm32l_add_eeprom(t, 0x8080000, 0x4000);
		target_add_commands(t, stm32lx_cmd_list, "STM32L1x");
		return true;
	}

	idcode = target_mem_read32(t, STM32L0_DBGMCU_IDCODE_PHYS) & 0xfff;
	switch (idcode) {
	case 0x457:                   /* STM32L0xx Cat1 */
	case 0x425:                   /* STM32L0xx Cat2 */
	case 0x417:                   /* STM32L0xx Cat3 */
	case 0x447:                   /* STM32L0xx Cat5 */
		t->idcode = idcode;
		t->driver = "STM32L0x";
		target_add_ram(t, 0x20000000, 0x5000);
		stm32l_add_flash(t, 0x8000000, 0x10000, 0x80);
		stm32l_add_flash(t, 0x8010000, 0x10000, 0x80);
		stm32l_add_flash(t, 0x8020000, 0x10000, 0x80);
		stm32l_add_eeprom(t, 0x8080000, 0x1800);
		target_add_commands(t, stm32lx_cmd_list, "STM32L0x");
		return true;
	}

	return false;
}


/** Lock the NVM control registers preventing writes or erases. */
static void stm32lx_nvm_lock(target *t, uint32_t nvm)
{
        target_mem_write32(t, STM32Lx_NVM_PECR(nvm), STM32Lx_NVM_PECR_PELOCK);
}


/** Unlock the NVM control registers for modifying program or
    data flash.  Returns true if the unlock succeeds. */
static bool stm32lx_nvm_prog_data_unlock(target* t, uint32_t nvm)
{
        /* Always lock first because that's the only way to know that the
           unlock can succeed on the STM32L0's. */
        target_mem_write32(t, STM32Lx_NVM_PECR(nvm),  STM32Lx_NVM_PECR_PELOCK);
        target_mem_write32(t, STM32Lx_NVM_PEKEYR(nvm),  STM32Lx_NVM_PEKEY1);
        target_mem_write32(t, STM32Lx_NVM_PEKEYR(nvm),  STM32Lx_NVM_PEKEY2);
        target_mem_write32(t, STM32Lx_NVM_PRGKEYR(nvm), STM32Lx_NVM_PRGKEY1);
        target_mem_write32(t, STM32Lx_NVM_PRGKEYR(nvm), STM32Lx_NVM_PRGKEY2);

        return !(target_mem_read32(t, STM32Lx_NVM_PECR(nvm))
                 & STM32Lx_NVM_PECR_PRGLOCK);
}


/** Unlock the NVM control registers for modifying option bytes.
    Returns true if the unlock succeeds. */
static bool stm32lx_nvm_opt_unlock(target *t, uint32_t nvm)
{
        /* Always lock first because that's the only way to know that the
           unlock can succeed on the STM32L0's. */
        target_mem_write32(t, STM32Lx_NVM_PECR(nvm),  STM32Lx_NVM_PECR_PELOCK);
        target_mem_write32(t, STM32Lx_NVM_PEKEYR(nvm),  STM32Lx_NVM_PEKEY1);
        target_mem_write32(t, STM32Lx_NVM_PEKEYR(nvm),  STM32Lx_NVM_PEKEY2);
        target_mem_write32(t, STM32Lx_NVM_OPTKEYR(nvm), STM32Lx_NVM_OPTKEY1);
        target_mem_write32(t, STM32Lx_NVM_OPTKEYR(nvm), STM32Lx_NVM_OPTKEY2);

        return !(target_mem_read32(t, STM32Lx_NVM_PECR(nvm))
                 & STM32Lx_NVM_PECR_OPTLOCK);
}

/** Erase a region of program flash using operations through the debug
    interface.  This is slower than stubbed versions(see NOTES).  The
    flash array is erased for all pages from addr to addr+len
    inclusive.  NVM register file address chosen from target. */
static int stm32lx_nvm_prog_erase(struct target_flash* f,
                                  target_addr addr, size_t len)
{
	target *t = f->t;
	const size_t page_size = f->blocksize;
	const uint32_t nvm = stm32lx_nvm_phys(t);

	if (!stm32lx_nvm_prog_data_unlock(t, nvm))
	        return -1;

	/* Flash page erase instruction */
	target_mem_write32(t, STM32Lx_NVM_PECR(nvm),
	                   STM32Lx_NVM_PECR_ERASE | STM32Lx_NVM_PECR_PROG);

	uint32_t pecr = target_mem_read32(t, STM32Lx_NVM_PECR(nvm));
	if ((pecr & (STM32Lx_NVM_PECR_PROG | STM32Lx_NVM_PECR_ERASE))
	   != (STM32Lx_NVM_PECR_PROG | STM32Lx_NVM_PECR_ERASE))
		return -1;

	/* Clear errors.  Note that this only works when we wait for the NVM
	   block to complete the last operation. */
	target_mem_write32(t, STM32Lx_NVM_SR(nvm), STM32Lx_NVM_SR_ERR_M);

	while (len > 0) {
		/* Write first word of page to 0 */
		target_mem_write32(t, addr, 0);

		len  -= page_size;
		addr += page_size;
	}

	/* Disable further programming by locking PECR */
	stm32lx_nvm_lock(t, nvm);

	/* Wait for completion or an error */
	uint32_t sr;
	do {
		sr = target_mem_read32(t, STM32Lx_NVM_SR(nvm));
	} while (sr & STM32Lx_NVM_SR_BSY);

	if ((sr & STM32Lx_NVM_SR_ERR_M) || !(sr & STM32Lx_NVM_SR_EOP) ||
	    target_check_error(t))
			return -1;

	return 0;
}


/** Write to program flash using operations through the debug
    interface. */
static int stm32lx_nvm_prog_write(struct target_flash *f,
                                  target_addr dest,
                                  const void* src,
                                  size_t size)
{
	target *t = f->t;
	const uint32_t nvm = stm32lx_nvm_phys(t);

	if (!stm32lx_nvm_prog_data_unlock(t, nvm))
	        return -1;

	/* Wait for BSY to clear because we cannot write the PECR until
	   the previous operation completes on STM32Lxxx. */
	while (target_mem_read32(t, STM32Lx_NVM_SR(nvm))
	       & STM32Lx_NVM_SR_BSY)
		if (target_check_error(t))
			return -1;

	target_mem_write32(t, STM32Lx_NVM_PECR(nvm),
	                   STM32Lx_NVM_PECR_PROG | STM32Lx_NVM_PECR_FPRG);
	target_mem_write(t, dest, src, size);

	/* Disable further programming by locking PECR */
	stm32lx_nvm_lock(t, nvm);

	/* Wait for completion or an error */
	uint32_t sr;
	do {
		sr = target_mem_read32(t, STM32Lx_NVM_SR(nvm));
	} while (sr & STM32Lx_NVM_SR_BSY);

	if ((sr & STM32Lx_NVM_SR_ERR_M) || !(sr & STM32Lx_NVM_SR_EOP) ||
	    target_check_error(t))
			return -1;

	return 0;
}


/** Erase a region of data flash using operations through the debug
    interface .  The flash is erased for all pages from addr to
    addr+len, inclusive, on a word boundary.  NVM register file
    address chosen from target. */
static int stm32lx_nvm_data_erase(struct target_flash *f,
                                  target_addr addr, size_t len)
{
	target *t = f->t;
	const size_t page_size = f->blocksize;
	const uint32_t nvm = stm32lx_nvm_phys(t);

	/* Word align */
	len += (addr & 3);
	addr &= ~3;

	if (!stm32lx_nvm_prog_data_unlock(t, nvm))
		return -1;

	/* Flash data erase instruction */
	target_mem_write32(t, STM32Lx_NVM_PECR(nvm),
	                   STM32Lx_NVM_PECR_ERASE | STM32Lx_NVM_PECR_DATA);

	uint32_t pecr = target_mem_read32(t, STM32Lx_NVM_PECR(nvm));
	if ((pecr & (STM32Lx_NVM_PECR_ERASE | STM32Lx_NVM_PECR_DATA))
	   != (STM32Lx_NVM_PECR_ERASE | STM32Lx_NVM_PECR_DATA))
		return -1;

	while (len > 0) {
		/* Write first word of page to 0 */
		target_mem_write32(t, addr, 0);

		len  -= page_size;
		addr += page_size;
	}

	/* Disable further programming by locking PECR */
	stm32lx_nvm_lock(t, nvm);

	/* Wait for completion or an error */
	uint32_t sr;
	do {
		sr = target_mem_read32(t, STM32Lx_NVM_SR(nvm));
	} while (sr & STM32Lx_NVM_SR_BSY);

	if ((sr & STM32Lx_NVM_SR_ERR_M) || !(sr & STM32Lx_NVM_SR_EOP) ||
	    target_check_error(t))
			return -1;

	return 0;
}


/** Write to data flash using operations through the debug interface.
    NVM register file address chosen from target.  Unaligned
    destination writes are supported (though unaligned sources are
    not). */
static int stm32lx_nvm_data_write(struct target_flash *f,
                                  target_addr destination,
                                  const void* src,
                                  size_t size)
{
	target *t = f->t;
	const uint32_t nvm = stm32lx_nvm_phys(t);
	const bool is_stm32l1 = stm32lx_is_stm32l1(t);
	uint32_t* source = (uint32_t*) src;

	if (!stm32lx_nvm_prog_data_unlock(t, nvm))
		return -1;

	target_mem_write32(t, STM32Lx_NVM_PECR(nvm),
	                   is_stm32l1 ? 0 : STM32Lx_NVM_PECR_DATA);

	while (size) {
		size -= 4;
		uint32_t v = *source++;
		target_mem_write32(t, destination, v);
		destination += 4;

		if (target_check_error(t))
			return -1;
	}

	/* Disable further programming by locking PECR */
	stm32lx_nvm_lock(t, nvm);

	/* Wait for completion or an error */
	uint32_t sr;
	do {
		sr = target_mem_read32(t, STM32Lx_NVM_SR(nvm));
	} while (sr & STM32Lx_NVM_SR_BSY);

	if ((sr & STM32Lx_NVM_SR_ERR_M) || !(sr & STM32Lx_NVM_SR_EOP) ||
	    target_check_error(t))
			return -1;

	return 0;
}


/** Write one option word.  The address is the physical address of the
    word and the value is a complete word value.  The caller is
    responsible for making sure that the value satisfies the proper
    format where the upper 16 bits are the 1s complement of the lower
    16 bits.  The funtion returns when the operation is complete.
    The return value is true if the write succeeded. */
static bool stm32lx_option_write(target *t, uint32_t address, uint32_t value)
{
        const uint32_t nvm = stm32lx_nvm_phys(t);

        /* Erase and program option in one go. */
        target_mem_write32(t, STM32Lx_NVM_PECR(nvm), STM32Lx_NVM_PECR_FIX);
        target_mem_write32(t, address, value);

        uint32_t sr;
        do {
                sr = target_mem_read32(t, STM32Lx_NVM_SR(nvm));
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
static bool stm32lx_eeprom_write(target *t, uint32_t address,
                                 size_t cb, uint32_t value)
{
        const uint32_t nvm        = stm32lx_nvm_phys(t);
        const bool     is_stm32l1 = stm32lx_is_stm32l1(t);

        /* Clear errors. */
        target_mem_write32(t, STM32Lx_NVM_SR(nvm), STM32Lx_NVM_SR_ERR_M);

        /* Erase and program option in one go. */
        target_mem_write32(t, STM32Lx_NVM_PECR(nvm),
                           (is_stm32l1 ? 0 : STM32Lx_NVM_PECR_DATA)
                           | STM32Lx_NVM_PECR_FIX);
        if (cb == 4)
                target_mem_write32(t, address, value);
        else if (cb == 2)
                target_mem_write16(t, address, value);
        else if (cb == 1)
                target_mem_write8(t, address, value);
        else
                return false;

        uint32_t sr;
        do {
                sr = target_mem_read32(t, STM32Lx_NVM_SR(nvm));
        } while (sr & STM32Lx_NVM_SR_BSY);

        return !(sr & STM32Lx_NVM_SR_ERR_M);
}

static bool stm32lx_cmd_option(target* t, int argc, char** argv)
{
        const uint32_t nvm      = stm32lx_nvm_phys(t);
        const size_t   opt_size = stm32lx_nvm_option_size(t);

        if (!stm32lx_nvm_opt_unlock(t, nvm)) {
                tc_printf(t, "unable to unlock NVM option bytes\n");
                return true;
        }

        size_t cb = strlen(argv[1]);

        if (argc == 2 && !strncasecmp(argv[1], "obl_launch", cb)) {
                target_mem_write32(t, STM32Lx_NVM_PECR(nvm),
                                   STM32Lx_NVM_PECR_OBL_LAUNCH);
        }
        else if (argc == 4 && !strncasecmp(argv[1], "raw", cb)) {
                uint32_t addr = strtoul(argv[2], NULL, 0);
                uint32_t val  = strtoul(argv[3], NULL, 0);
                tc_printf(t, "raw %08x <- %08x\n", addr, val);
                if (   addr <  STM32Lx_NVM_OPT_PHYS
                    || addr >= STM32Lx_NVM_OPT_PHYS + opt_size
                    || (addr & 3))
                        goto usage;
                if (!stm32lx_option_write(t, addr, val))
                        tc_printf(t, "option write failed\n");
        }
        else if (argc == 4 && !strncasecmp(argv[1], "write", cb)) {
                uint32_t addr = strtoul(argv[2], NULL, 0);
                uint32_t val  = strtoul(argv[3], NULL, 0);
                val = (val & 0xffff) | ((~val & 0xffff) << 16);
                tc_printf(t, "write %08x <- %08x\n", addr, val);
                if (   addr <  STM32Lx_NVM_OPT_PHYS
                    || addr >= STM32Lx_NVM_OPT_PHYS + opt_size
                    || (addr & 3))
                        goto usage;
                if (!stm32lx_option_write(t, addr, val))
                        tc_printf(t, "option write failed\n");
        }
        else if (argc == 2 && !strncasecmp(argv[1], "show", cb))
                ;
        else
                goto usage;

        /* Report the current option values */
        for(unsigned i = 0; i < opt_size; i += sizeof(uint32_t)) {
                uint32_t addr = STM32Lx_NVM_OPT_PHYS + i;
                uint32_t val = target_mem_read32(t, addr);
                tc_printf(t, "0x%08x: 0x%04x 0x%04x %s\n",
                          addr, val & 0xffff, (val >> 16) & 0xffff,
                          ((val & 0xffff) == ((~val >> 16) & 0xffff))
                          ? "OK" : "ERR");
        }

        if (stm32lx_is_stm32l1(t)) {
                uint32_t optr   = target_mem_read32(t, STM32Lx_NVM_OPTR(nvm));
                uint8_t  rdprot = (optr >> STM32Lx_NVM_OPTR_RDPROT_S)
                        & STM32Lx_NVM_OPTR_RDPROT_M;
                if (rdprot == STM32Lx_NVM_OPTR_RDPROT_0)
                        rdprot = 0;
                else if (rdprot == STM32Lx_NVM_OPTR_RDPROT_2)
                        rdprot = 2;
                else
                        rdprot = 1;
                tc_printf(t, "OPTR: 0x%08x, RDPRT %d, SPRMD %d, "
                          "BOR %d, WDG_SW %d, nRST_STP %d, nRST_STBY %d, "
                          "nBFB2 %d\n",
                          optr, rdprot,
                          (optr &  STM32L1_NVM_OPTR_SPRMOD)     ? 1 : 0,
                          (optr >> STM32L1_NVM_OPTR_BOR_LEV_S)
                           & STM32L1_NVM_OPTR_BOR_LEV_M,
                          (optr &  STM32Lx_NVM_OPTR_WDG_SW)     ? 1 : 0,
                          (optr &  STM32L1_NVM_OPTR_nRST_STOP)  ? 1 : 0,
                          (optr &  STM32L1_NVM_OPTR_nRST_STDBY) ? 1 : 0,
                          (optr &  STM32L1_NVM_OPTR_nBFB2)      ? 1 : 0);
        }
        else {
                uint32_t optr   = target_mem_read32(t, STM32Lx_NVM_OPTR(nvm));
                uint8_t  rdprot = (optr >> STM32Lx_NVM_OPTR_RDPROT_S)
                        & STM32Lx_NVM_OPTR_RDPROT_M;
                if (rdprot == STM32Lx_NVM_OPTR_RDPROT_0)
                        rdprot = 0;
                else if (rdprot == STM32Lx_NVM_OPTR_RDPROT_2)
                        rdprot = 2;
                else
                        rdprot = 1;
                tc_printf(t, "OPTR: 0x%08x, RDPROT %d, WPRMOD %d, WDG_SW %d, "
                          "BOOT1 %d\n",
                          optr, rdprot,
                          (optr & STM32L0_NVM_OPTR_WPRMOD) ? 1 : 0,
                          (optr & STM32Lx_NVM_OPTR_WDG_SW) ? 1 : 0,
                          (optr & STM32L0_NVM_OPTR_BOOT1)  ? 1 : 0);
        }

        goto done;

usage:
        tc_printf(t, "usage: monitor option [ARGS]\n");
        tc_printf(t, "  show                   - Show options in NVM and as"
                  " loaded\n");
        tc_printf(t, "  obl_launch             - Reload options from NVM\n");
        tc_printf(t, "  write <addr> <value16> - Set option half-word; "
                  "complement computed\n");
        tc_printf(t, "  raw <addr> <value32>   - Set option word\n");
        tc_printf(t, "The value of <addr> must be word aligned and from 0x%08x "
                  "to +0x%x\n",
                  STM32Lx_NVM_OPT_PHYS,
                  STM32Lx_NVM_OPT_PHYS + opt_size - sizeof(uint32_t));

done:
        stm32lx_nvm_lock(t, nvm);
        return true;
}


static bool stm32lx_cmd_eeprom(target* t, int argc, char** argv)
{
        const uint32_t nvm = stm32lx_nvm_phys(t);

        if (!stm32lx_nvm_prog_data_unlock(t, nvm)) {
                tc_printf(t, "unable to unlock EEPROM\n");
                return true;
        }

        size_t cb = strlen(argv[1]);

        if (argc == 4) {
                uint32_t addr = strtoul(argv[2], NULL, 0);
                uint32_t val  = strtoul(argv[3], NULL, 0);

                if (   addr <  STM32Lx_NVM_EEPROM_PHYS
                    || addr >= STM32Lx_NVM_EEPROM_PHYS
                       	        + stm32lx_nvm_eeprom_size(t))
                        goto usage;

                if (!strncasecmp(argv[1], "byte", cb)) {
                        tc_printf(t, "write byte 0x%08x <- 0x%08x\n", addr, val);
                        if (!stm32lx_eeprom_write(t, addr, 1, val))
                                tc_printf(t, "eeprom write failed\n");
                } else if (!strncasecmp(argv[1], "halfword", cb)) {
                        val &= 0xffff;
                        tc_printf(t, "write halfword 0x%08x <- 0x%04x\n",
                                 addr, val);
                        if (addr & 1)
                                goto usage;
                        if (!stm32lx_eeprom_write(t, addr, 2, val))
                                tc_printf(t, "eeprom write failed\n");
                } else if (!strncasecmp(argv[1], "word", cb)) {
                        tc_printf(t, "write word 0x%08x <- 0x%08x\n", addr, val);
                        if (addr & 3)
                                goto usage;
                        if (!stm32lx_eeprom_write(t, addr, 4, val))
                                tc_printf(t, "eeprom write failed\n");
                }
                else
                        goto usage;
        }
        else
                goto usage;

        goto done;

usage:
        tc_printf(t, "usage: monitor eeprom [ARGS]\n");
        tc_printf(t, "  byte     <addr> <value8>  - Write a byte\n");
        tc_printf(t, "  halfword <addr> <value16> - Write a half-word\n");
        tc_printf(t, "  word     <addr> <value32> - Write a word\n");
        tc_printf(t, "The value of <addr> must in the interval [0x%08x, 0x%x)\n",
                  STM32Lx_NVM_EEPROM_PHYS,
                  STM32Lx_NVM_EEPROM_PHYS + stm32lx_nvm_eeprom_size(t));

done:
        stm32lx_nvm_lock(t, nvm);
        return true;
}
