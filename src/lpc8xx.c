#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#include "general.h"
#include "adiv5.h"
#include "target.h"

#define IAP_PGM_CHUNKSIZE	256	/* should fit in RAM on any device */

struct flash_param {
	uint16_t	opcodes[2];	/* two opcodes to return to after calling the ROM */
	uint32_t	command[5];	/* command operands */
	uint32_t	result[4];	/* result data */
};

struct flash_program {
	struct flash_param	p;
	uint8_t			data[IAP_PGM_CHUNKSIZE];
};

static struct flash_program flash_pgm;

#define MSP				17												// Main stack pointer register number
#define MIN_RAM_SIZE_FOR_LPC8xx				1024
#define RAM_USAGE_FOR_IAP_ROUTINES			32							// IAP routines use 32 bytes at top of ram

#define FLASH_PAGE_SIZE 1024

#define IAP_ENTRYPOINT	0x1fff1ff1
#define IAP_RAM_BASE	0x10000000

#define IAP_CMD_PREPARE		50
#define IAP_CMD_PROGRAM		51
#define IAP_CMD_ERASE		52
#define IAP_CMD_BLANKCHECK	53

#define IAP_STATUS_CMD_SUCCESS		0
#define IAP_STATUS_INVALID_COMMAND	1
#define IAP_STATUS_SRC_ADDR_ERROR	2
#define IAP_STATUS_DST_ADDR_ERROR	3
#define IAP_STATUS_SRC_ADDR_NOT_MAPPED	4
#define IAP_STATUS_DST_ADDR_NOT_MAPPED	5
#define IAP_STATUS_COUNT_ERROR		6
#define IAP_STATUS_INVALID_SECTOR	7
#define IAP_STATUS_SECTOR_NOT_BLANK	8
#define IAP_STATUS_SECTOR_NOT_PREPARED	9
#define IAP_STATUS_COMPARE_ERROR	10
#define IAP_STATUS_BUSY			11

static void lpc8xx_iap_call(struct target_s *target, struct flash_param *param, unsigned param_len);
static int lpc8xx_flash_prepare(struct target_s *target, uint32_t addr, int len);
static int lpc8xx_flash_erase(struct target_s *target, uint32_t addr, int len);
static int lpc8xx_flash_write(struct target_s *target, uint32_t dest, const uint8_t *src,
			  int len);

/*
 * Memory map for the lpc8xx devices, which otherwise look much like the lpc11xx.
 *
 * We could decode the RAM/flash sizes, but we just encode the largest possible here.
 *
 * Note that the LPC810 and LPC811 map their flash oddly; see the NXP LPC800 user
 * manual (UM10601) for more details.
 */
static const char lpc8xx_xml_memory_map[] = "<?xml version=\"1.0\"?>"
/*	"<!DOCTYPE memory-map "
	"             PUBLIC \"+//IDN gnu.org//DTD GDB Memory Map V1.0//EN\""
	"                    \"http://sourceware.org/gdb/gdb-memory-map.dtd\">"*/
	"<memory-map>"
	"  <memory type=\"flash\" start=\"0x00000000\" length=\"0x4000\">"
	"    <property name=\"blocksize\">0x400</property>"
	"  </memory>"
	"  <memory type=\"ram\" start=\"0x10000000\" length=\"0x1000\"/>"
	"</memory-map>";


bool
lpc8xx_probe(struct target_s *target)
{
	uint32_t idcode;

	/* read the device ID register */
	idcode = adiv5_ap_mem_read(adiv5_target_ap(target), 0x400483F4);

	gdb_outf("id 0x%08x\n", idcode);

	switch (idcode) {

	//case 0x00008100:	/* LPC810M021FN8 */		/* datasheet value is wrong */
	//case 0x00008110:	/* LPC811M001FDH16 */	/* datasheet value is wrong */
	//case 0x00008120:	/* LPC812M101FDH16 */	/* datasheet value is wrong */
	//case 0x00008121:	/* LPC812M101FD20 */	/* datasheet value is wrong */	
	case 0x1812202b:	/* LPC812M101FDH20 */
		target->driver = "lpc8xx";
		target->xml_mem_map = lpc8xx_xml_memory_map;
		target->flash_erase = lpc8xx_flash_erase;
		target->flash_write = lpc8xx_flash_write;

		return true;
	}

	return false;
}

static void
lpc8xx_iap_call(struct target_s *target, struct flash_param *param, unsigned param_len)
{
	uint32_t regs[target->regs_size / 4];

	/* fill out the remainder of the parameters and copy the structure to RAM */
	param->opcodes[0] = 0xbe00;
	param->opcodes[1] = 0x0000;
	target_mem_write_words(target, IAP_RAM_BASE, (void *)param, param_len);

	/* set up for the call to the IAP ROM */
	target_regs_read(target, regs);
	regs[0] = IAP_RAM_BASE + offsetof(struct flash_param, command);
	regs[1] = IAP_RAM_BASE + offsetof(struct flash_param, result);

	regs[MSP] = IAP_RAM_BASE + MIN_RAM_SIZE_FOR_LPC8xx - RAM_USAGE_FOR_IAP_ROUTINES;// stack pointer - top of the smallest ram less 32 for IAP usage
	regs[14] = IAP_RAM_BASE | 1;
	regs[15] = IAP_ENTRYPOINT;
	target_regs_write(target, regs);

	/* start the target and wait for it to halt again */
	target_halt_resume(target, 0);
	while (!target_halt_wait(target));

	/* copy back just the parameters structure */
	target_mem_read_words(target, (void *)param, IAP_RAM_BASE, sizeof(struct flash_param));
}

static int
lpc8xx_flash_prepare(struct target_s *target, uint32_t addr, int len)
{
	/* prepare the sector(s) to be erased */
	memset(&flash_pgm.p, 0, sizeof(flash_pgm.p));
	flash_pgm.p.command[0] = IAP_CMD_PREPARE;
	flash_pgm.p.command[1] = addr / FLASH_PAGE_SIZE;
	flash_pgm.p.command[2] = (addr + len - 1) / FLASH_PAGE_SIZE;

	lpc8xx_iap_call(target, &flash_pgm.p, sizeof(flash_pgm.p));
	if (flash_pgm.p.result[0] != IAP_STATUS_CMD_SUCCESS) {
		return -1;
	}

	return 0;
}

static int
lpc8xx_flash_erase(struct target_s *target, uint32_t addr, int len)
{

	if (addr % FLASH_PAGE_SIZE)
		return -1;

	/* prepare... */
	if (lpc8xx_flash_prepare(target, addr, len))
		return -1;

	/* and now erase them */
	flash_pgm.p.command[0] = IAP_CMD_ERASE;
	flash_pgm.p.command[1] = addr / FLASH_PAGE_SIZE;
	flash_pgm.p.command[2] = (addr + len - 1) / FLASH_PAGE_SIZE;
	flash_pgm.p.command[3] = 12000;	/* XXX safe to assume this? */
	lpc8xx_iap_call(target, &flash_pgm.p, sizeof(flash_pgm.p));
	if (flash_pgm.p.result[0] != IAP_STATUS_CMD_SUCCESS) {
		return -1;
	}
	flash_pgm.p.command[0] = IAP_CMD_BLANKCHECK;
	lpc8xx_iap_call(target, &flash_pgm.p, sizeof(flash_pgm.p));
	if (flash_pgm.p.result[0] != IAP_STATUS_CMD_SUCCESS) {
		return -1;
	}

	return 0;
}

static int
lpc8xx_flash_write(struct target_s *target, uint32_t dest, const uint8_t *src, int len)
{
	unsigned first_chunk = dest / IAP_PGM_CHUNKSIZE;
	unsigned last_chunk = (dest + len - 1) / IAP_PGM_CHUNKSIZE;
	unsigned chunk_offset = dest % IAP_PGM_CHUNKSIZE;
	unsigned chunk;

	for (chunk = first_chunk; chunk <= last_chunk; chunk++) {

		DEBUG("chunk %u len %d\n", chunk, len);
		/* first and last chunk may require special handling */
		if ((chunk == first_chunk) || (chunk == last_chunk)) {

			/* fill with all ff to avoid sector rewrite corrupting other writes */
			memset(flash_pgm.data, 0xff, sizeof(flash_pgm.data));

			/* copy as much as fits */
			int copylen = IAP_PGM_CHUNKSIZE - chunk_offset;
			if (copylen > len)
				copylen = len;
			memcpy(&flash_pgm.data[chunk_offset], src, copylen);

			/* update to suit */
			len -= copylen;
			src += copylen;
			chunk_offset = 0;

			/* if we are programming the vectors, calculate the magic number */
			if (chunk == 0) {
				uint32_t *w = (uint32_t *)(&flash_pgm.data[0]);
				uint32_t sum = 0;

				if (copylen >= 7) {
					for (unsigned i = 0; i < 7; i++)
						sum += w[i];
					w[7] = 0 - sum;
				} else {
					/* We can't possibly calculate the magic number */
					return -1;
				}
			}

		} else {

			/* interior chunk, must be aligned and full-sized */
			memcpy(flash_pgm.data, src, IAP_PGM_CHUNKSIZE);
			len -= IAP_PGM_CHUNKSIZE;
			src += IAP_PGM_CHUNKSIZE;
		}

		/* prepare... */
		if (lpc8xx_flash_prepare(target, chunk * IAP_PGM_CHUNKSIZE, IAP_PGM_CHUNKSIZE))
			return -1;

		/* set the destination address and program */
		flash_pgm.p.command[0] = IAP_CMD_PROGRAM;
		flash_pgm.p.command[1] = chunk * IAP_PGM_CHUNKSIZE;
		flash_pgm.p.command[2] = IAP_RAM_BASE + offsetof(struct flash_program, data);
		flash_pgm.p.command[3] = IAP_PGM_CHUNKSIZE;
		flash_pgm.p.command[4] = 12000;	/* assuming we are running off IRC - safe lower bound */
		lpc8xx_iap_call(target, &flash_pgm.p, sizeof(flash_pgm));
		if (flash_pgm.p.result[0] != IAP_STATUS_CMD_SUCCESS) {
			return -1;
		}

	}

	return 0;
}
