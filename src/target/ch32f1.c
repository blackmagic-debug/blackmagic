/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
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

/* This file implements CH32F1xx target specific functions. 
	The ch32 flash is rather slow so this code is using the so called fast mode (ch32 specific).
	128 bytes are copied to a write buffer, then the write buffer is committed to flash
	/!\ There is some sort of bus stall/bus arbitration going on that does NOT work when
	programmed through SWD/jtag
	The workaround is to wait a few cycles before filling the write buffer. This is performed by reading the flash a few times 

 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"

#if PC_HOSTED == 1
	#define DEBUG_CH DEBUG_INFO
	#define ERROR_CH DEBUG_WARN
#else
	#define DEBUG_CH(...) {} //DEBUG_WARN //(...) {}
	#define ERROR_CH DEBUG_WARN //DEBUG_WARN
#endif

extern const struct command_s stm32f1_cmd_list[]; // Reuse stm32f1 stuff

 static int ch32f1_flash_erase(struct target_flash *f,
								target_addr addr, size_t len);
 static int ch32f1_flash_write(struct target_flash *f,
								target_addr dest, const void *src, size_t len);


// these are common with stm32f1/gd32f1/...
#define FPEC_BASE						0x40022000
#define FLASH_ACR						(FPEC_BASE+0x00)
#define FLASH_KEYR						(FPEC_BASE+0x04)
#define FLASH_SR						(FPEC_BASE+0x0C)
#define FLASH_CR						(FPEC_BASE+0x10)
#define FLASH_AR						(FPEC_BASE+0x14)
#define FLASH_CR_LOCK					(1 << 7)
#define FLASH_CR_STRT					(1 << 6)
#define FLASH_SR_BSY					(1 << 0)
#define KEY1 							0x45670123
#define KEY2 							0xCDEF89AB
#define SR_ERROR_MASK					0x14
#define SR_EOP							0x20
#define DBGMCU_IDCODE					0xE0042000
#define FLASHSIZE	 					0x1FFFF7E0

// these are specific to ch32f1
#define FLASH_MAGIC			   			(FPEC_BASE+0x34)
#define FLASH_MODEKEYR_CH32 			(FPEC_BASE+0x24) // Fast mode for CH32F10x
#define FLASH_CR_FLOCK_CH32	   			(1<<15) // fast unlock
#define FLASH_CR_FTPG_CH32				(1<<16) // fast page program
#define FLASH_CR_FTER_CH32				(1<<17) // fast page erase
#define FLASH_CR_BUF_LOAD_CH32			(1<<18) // Buffer load
#define FLASH_CR_BUF_RESET_CH32   		(1<<19) // Buffer reset
#define FLASH_SR_EOP			  		(1<<5)  // End of programming
#define FLASH_BEGIN_ADDRESS_CH32  		0x8000000




/**
		\fn ch32f1_add_flash
		\brief "fast" flash driver for CH32F10x chips
*/
static void ch32f1_add_flash(target *t, uint32_t addr, size_t length, size_t erasesize)
{
	struct target_flash *f = calloc(1, sizeof(*f));
	if (!f) {			/* calloc failed: heap exhaustion */
		DEBUG_WARN("calloc: failed in %s\n", __func__);
		return;
	}

	f->start = addr;
	f->length = length;
	f->blocksize = erasesize;
	f->erase = ch32f1_flash_erase;
	f->write = ch32f1_flash_write;
	f->buf_size = erasesize;
	f->erased = 0xff;
	target_add_flash(t, f);
}

#define WAIT_BUSY() 	do { \
				sr = target_mem_read32(t, FLASH_SR); \
				if(target_check_error(t)) { \
						ERROR_CH("ch32f1 flash write: comm error\n"); \
						return -1; \
				} \
		} while (sr & FLASH_SR_BSY);

#define WAIT_EOP()  	do { \
				sr = target_mem_read32(t, FLASH_SR); \
				if(target_check_error(t)) { \
						ERROR_CH("ch32f1 flash write: comm error\n"); \
						return -1; \
				} \
		} while (!(sr & FLASH_SR_EOP));

#define CLEAR_EOP()	 target_mem_write32(t, FLASH_SR,FLASH_SR_EOP)

#define SET_CR(bit) {	ct = target_mem_read32(t, FLASH_CR); \
						ct|=(bit); \
						target_mem_write32(t, FLASH_CR, ct);}


#define CLEAR_CR(bit) 	{ct = target_mem_read32(t, FLASH_CR); \
						ct&=~(bit); \
						target_mem_write32(t, FLASH_CR, ct);}

// Which one is the right value ?
#define MAGIC_WORD 0x100
// #define MAGIC_WORD 0x1000
#define MAGIC(adr) { magic=target_mem_read32(t,(adr) ^ MAGIC_WORD); \
					target_mem_write32(t, FLASH_MAGIC , magic); }

/**
  \fn ch32f1_flash_unlock
  \brief unlock ch32f103 in fast mode
*/
static int ch32f1_flash_unlock(target *t)
{
	DEBUG_CH("CH32: flash unlock \n");

	target_mem_write32(t, FLASH_KEYR , KEY1);
	target_mem_write32(t, FLASH_KEYR , KEY2);
	// fast mode
	target_mem_write32(t, FLASH_MODEKEYR_CH32 , KEY1);
	target_mem_write32(t, FLASH_MODEKEYR_CH32 , KEY2);
	uint32_t cr = target_mem_read32(t, FLASH_CR);
	if (cr & FLASH_CR_FLOCK_CH32){
		ERROR_CH("Fast unlock failed, cr: 0x%08" PRIx32 "\n", cr);
		return -1;
	}
	return 0;
}
static int ch32f1_flash_lock(target *t)
{
	volatile uint32_t ct;
	DEBUG_CH("CH32: flash lock \n");
	SET_CR(FLASH_CR_LOCK);
	return 0;
}

/**
	\brief identify the ch32f1 chip
				Actually grab all cortex m3 with designer = arm not caught earlier...
*/

bool ch32f1_probe(target *t)
{
	t->idcode = target_mem_read32(t, DBGMCU_IDCODE) & 0xfff;
	if ((t->cpuid & CPUID_PARTNO_MASK) != CORTEX_M3)
		return false;
	if(t->idcode !=0x410) { // only ch32f103
		return false;
	}

	// try to flock
	ch32f1_flash_lock(t);
	// if this fails it is not a CH32 chip
	if(ch32f1_flash_unlock(t)) {
		return false;
	}

	uint32_t signature = target_mem_read32(t, FLASHSIZE);
	uint32_t flashSize = signature & 0xFFFF;

	target_add_ram(t, 0x20000000, 0x5000);
	ch32f1_add_flash(t, FLASH_BEGIN_ADDRESS_CH32, flashSize*1024, 128);
	target_add_commands(t, stm32f1_cmd_list, "STM32 LD/MD/VL-LD/VL-MD");
	t->driver = "CH32F1 medium density (stm32f1 clone)";
	return true;
}
/**
  \fn ch32f1_flash_erase
  \brief fast erase of CH32
*/
int ch32f1_flash_erase (struct target_flash *f,  target_addr addr, size_t len)
{
	volatile uint32_t ct, sr, magic;
	target *t = f->t;
	DEBUG_CH("CH32: flash erase \n");

	if (ch32f1_flash_unlock(t)) {
		ERROR_CH("CH32: Unlock failed\n");
		return -1;
	}
	// Fast Erase 128 bytes pages (ch32 mode)
	while(len) {
		SET_CR(FLASH_CR_FTER_CH32);// CH32 PAGE_ER
		/* write address to FMA */
		target_mem_write32(t, FLASH_AR , addr);
		/* Flash page erase start instruction */
		SET_CR( FLASH_CR_STRT );
		WAIT_EOP();
		CLEAR_EOP();
		CLEAR_CR( FLASH_CR_STRT );
		// Magic
		MAGIC(addr);
		if (len > 128)
			len -= 128;
		else
			len = 0;
		addr += 128;
	}
	sr = target_mem_read32(t, FLASH_SR);
	ch32f1_flash_lock(t);
	if ((sr & SR_ERROR_MASK)) {
		ERROR_CH("ch32f1 flash erase error 0x%" PRIx32 "\n", sr);
		return -1;
	}
	return 0;
}

/**
	\fn ch32f1_wait_flash_ready
	\brief   Wait a bit for the previous operation to finish
			As per test result we need a time similar to 10 read operation over SWD
			We do 32 to have a bit of headroom, then we check we read ffff (erased flash)
			NB: Just reading fff is not enough as it could be a transient previous operation value
*/

static bool ch32f1_wait_flash_ready(target *t, uint32_t adr)
{
	uint32_t ff;
	for (int i = 0; i < 32; i++)
		ff = target_mem_read32(t, adr);
	if (ff != 0xffffffffUL) {
		ERROR_CH("ch32f1 Not erased properly at %" PRIx32 " or flash access issue\n", adr);
		return false;
	}
	return true;
}
/**
  \fn ch32f1_flash_write
  \brief fast flash for ch32. Load 128 bytes chunk and then flash them
*/

static int ch32f1_upload(target *t, uint32_t dest, const void  *src, uint32_t offset)
{
	volatile uint32_t ct, sr, magic;
	const uint32_t *ss = (const uint32_t *)(src+offset);
	uint32_t dd = dest+offset;

	SET_CR(FLASH_CR_FTPG_CH32);
	target_mem_write32(t, dd+0,ss[0]);
	target_mem_write32(t, dd+4,ss[1]);
	target_mem_write32(t, dd+8,ss[2]);
	target_mem_write32(t, dd+12,ss[3]);
	SET_CR(FLASH_CR_BUF_LOAD_CH32); /* BUF LOAD */
	WAIT_EOP();
	CLEAR_EOP();
	CLEAR_CR(FLASH_CR_FTPG_CH32);
	MAGIC((dest+offset));
	return 0;
}
/**
	\fn ch32f1_buffer_clear
	\brief clear the write buffer
*/
int ch32f1_buffer_clear(target *t)
{
	volatile uint32_t ct,sr;
	SET_CR(FLASH_CR_FTPG_CH32); // Fast page program 4-
	SET_CR(FLASH_CR_BUF_RESET_CH32); // BUF_RESET 5-
	WAIT_BUSY(); // 6-
	CLEAR_CR(FLASH_CR_FTPG_CH32); // Fast page program 4-
	return 0;
}
//#define CH32_VERIFY

/**

*/
static int ch32f1_flash_write(struct target_flash *f,
							   target_addr dest, const void *src, size_t len)
{
	volatile uint32_t ct, sr, magic;
	target *t = f->t;
	size_t length = len;
#ifdef CH32_VERIFY
	target_addr orgDest=dest;
	const void *orgSrc=src;
#endif
	DEBUG_CH("CH32: flash write 0x%x ,size=%d\n",dest,len);

	while(length > 0)
	{
		if(ch32f1_flash_unlock(t)) {
			ERROR_CH("ch32f1 cannot fast unlock\n");
			return -1;
		}
		WAIT_BUSY();

		// Buffer reset...
		ch32f1_buffer_clear(t);
		// Load 128 bytes to buffer
		if(!ch32f1_wait_flash_ready(t,dest)) {
			return -1;
		}
		for(int i = 0; i < 8; i++) {
			if(ch32f1_upload(t,dest,src, 16*i)) {
	  			ERROR_CH("Cannot upload to buffer\n");
				return -1;
			}
		}
		// write buffer
		SET_CR(FLASH_CR_FTPG_CH32);
		target_mem_write32(t, FLASH_AR, dest); // 10
		SET_CR(FLASH_CR_STRT); // 11 Start
		WAIT_EOP(); // 12
		CLEAR_EOP();
		CLEAR_CR(FLASH_CR_FTPG_CH32);

		MAGIC((dest));

		// next
		if(length > 128)
			length -=128;
		else
			length = 0;
		dest += 128;
		src += 128;

		sr = target_mem_read32(t, FLASH_SR); // 13
		ch32f1_flash_lock(t);
		if ((sr & SR_ERROR_MASK) ) {
			ERROR_CH("ch32f1 flash write error 0x%" PRIx32 "\n", sr);
			return -1;
		}

	}
#ifdef CH32_VERIFY
	DEBUG_CH("Verifying\n");
	size_t i = 0;
	for(i = 0; i < len; i+= 4)
	{
		uint32_t mem=target_mem_read32(t, orgDest+i);
		uint32_t mem2=*(uint32_t *)(orgSrc+i);
		if(mem!=mem2)
		{
			ERROR_CH(">>>>write mistmatch at address 0x%x\n",orgDest+i);
			ERROR_CH(">>>>expected 0x%x\n",mem2);
			ERROR_CH(">>>>flash 0x%x\n",mem);
			return -1;
		}
	}
#endif

	return 0;
}
// EOF
