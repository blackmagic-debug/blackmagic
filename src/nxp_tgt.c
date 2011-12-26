
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#include "general.h"
#include "adiv5.h"
#include "target.h"
#include "nxp_tgt.h"

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

static void lpc11x_iap_call(struct target_s *target, struct flash_param *param, unsigned param_len);
static int lpc11xx_flash_prepare(struct target_s *target, uint32_t addr, int len);
static int lpc11xx_flash_erase(struct target_s *target, uint32_t addr, int len);
static int lpc11xx_flash_write_words(struct target_s *target, uint32_t dest, const uint32_t *src, 
			  int len);

/* 
 * Note that this memory map is actually for the largest of the lpc11xx devices;
 * There seems to be no good way to decode the part number to determine the RAM
 * and flash sizes.
 */
static const char lpc11xx_xml_memory_map[] = "<?xml version=\"1.0\"?>"
/*	"<!DOCTYPE memory-map "
	"             PUBLIC \"+//IDN gnu.org//DTD GDB Memory Map V1.0//EN\""
	"                    \"http://sourceware.org/gdb/gdb-memory-map.dtd\">"*/
	"<memory-map>"
	"  <memory type=\"flash\" start=\"0x00000000\" length=\"0x8000\">"
	"    <property name=\"blocksize\">0x1000</property>"
	"  </memory>"
	"  <memory type=\"ram\" start=\"0x10000000\" length=\"0x2000\"/>"
	"</memory-map>";


int
lpc11xx_probe(struct target_s *target)
{
	struct target_ap_s *t = (void *)target;
	uint32_t idcode;

	/* read the device ID register */
	idcode = adiv5_ap_mem_read(t->ap, 0x400483F4);

	switch (idcode) {

	case 0x041E502B:
	case 0x2516D02B:
	case 0x0416502B:
	case 0x2516902B:	/* lpc1111 */
	case 0x2524D02B:
	case 0x0425502B:
	case 0x2524902B:
	case 0x1421102B:	/* lpc1112 */
	case 0x0434502B:
	case 0x2532902B:
	case 0x0434102B:
	case 0x2532102B:	/* lpc1113 */
	case 0x0444502B:
	case 0x2540902B:
	case 0x0444102B:
	case 0x2540102B:
	case 0x1440102B:	/* lpc1114 */
	case 0x1431102B:	/* lpc11c22 */
	case 0x1430102B:	/* lpc11c24 */
		target->driver = "lpc11xx";
		target->xml_mem_map = lpc11xx_xml_memory_map;
		target->flash_erase = lpc11xx_flash_erase;
		target->flash_write_words = lpc11xx_flash_write_words;

		return 0;

	default:
		break;
	}

	return -1;
}

static void
lpc11x_iap_call(struct target_s *target, struct flash_param *param, unsigned param_len)
{
	uint32_t	regs[target->regs_size];

	/* fill out the remainder of the parameters and copy the structure to RAM */
	param->opcodes[0] = 0xbe00;
	param->opcodes[1] = 0x0000;
	target_mem_write_words(target, IAP_RAM_BASE, (void *)param, param_len);

	/* set up for the call to the IAP ROM */
	target_regs_read(target, regs);
	regs[0] = IAP_RAM_BASE + offsetof(struct flash_param, command);
	regs[1] = IAP_RAM_BASE + offsetof(struct flash_param, result);

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
lpc11xx_flash_prepare(struct target_s *target, uint32_t addr, int len)
{	
	/* prepare the sector(s) to be erased */
	memset(&flash_pgm.p, 0, sizeof(flash_pgm.p));
	flash_pgm.p.command[0] = IAP_CMD_PREPARE;
	flash_pgm.p.command[1] = addr / 4096;
	flash_pgm.p.command[2] = flash_pgm.p.command[1] + ((len + 4095) / 4096) - 1;

	lpc11x_iap_call(target, &flash_pgm.p, sizeof(flash_pgm.p));
	if (flash_pgm.p.result[0] != IAP_STATUS_CMD_SUCCESS) {
		return -1;
	}

	return 0;
}

static int
lpc11xx_flash_erase(struct target_s *target, uint32_t addr, int len)
{

	if (addr % 4096)
		return -1;

	/* prepare... */
	if (lpc11xx_flash_prepare(target, addr, len))
		return -1;

	/* and now erase them */
	flash_pgm.p.command[0] = IAP_CMD_ERASE;
	flash_pgm.p.command[1] = addr / 4096;
	flash_pgm.p.command[2] = flash_pgm.p.command[1] + ((len + 4095) / 4096) - 1;
	flash_pgm.p.command[3] = 12000;	/* XXX safe to assume this? */
	lpc11x_iap_call(target, &flash_pgm.p, sizeof(flash_pgm.p));
	if (flash_pgm.p.result[0] != IAP_STATUS_CMD_SUCCESS) {
		return -1;
	}
	flash_pgm.p.command[0] = IAP_CMD_BLANKCHECK;
	lpc11x_iap_call(target, &flash_pgm.p, sizeof(flash_pgm.p));
	if (flash_pgm.p.result[0] != IAP_STATUS_CMD_SUCCESS) {
		return -1;
	}

	return 0;
}

static int
lpc11xx_flash_write_words(struct target_s *target, uint32_t dest, const uint32_t *src, int len)
{
	const uint8_t *s = (const uint8_t *)src;
	unsigned first_chunk = dest / IAP_PGM_CHUNKSIZE;
	unsigned last_chunk = (dest + len - 1) / IAP_PGM_CHUNKSIZE;
	unsigned chunk_offset = dest % IAP_PGM_CHUNKSIZE;
	unsigned chunk;

	for (chunk = first_chunk; chunk <= last_chunk; chunk++) {
		
		printf("chunk %u len %d\n", chunk, len);
		/* first and last chunk may require special handling */
		if ((chunk == first_chunk) || (chunk == last_chunk)) {

			/* fill with all ff to avoid sector rewrite corrupting other writes */
			memset(flash_pgm.data, 0xff, sizeof(flash_pgm.data));
		
			/* copy as much as fits */				
			int copylen = IAP_PGM_CHUNKSIZE - chunk_offset;
			if (copylen > len)
				copylen = len;
			memcpy(&flash_pgm.data[chunk_offset], s, copylen);

			/* update to suit */
			len -= copylen;
			s += copylen;
			chunk_offset = 0;

			/* if we are programming the vectors, calculate the magic number */
			if (chunk == 0) {
				uint32_t *w = (uint32_t *)(&flash_pgm.data[0]);
				uint32_t sum = 0;

				for (unsigned i = 0; i < 7; i++)
					sum += w[i];
				w[7] = 0 - sum;
			}

		} else {

			/* interior chunk, must be aligned and full-sized */
			memcpy(flash_pgm.data, s, IAP_PGM_CHUNKSIZE);
			len -= IAP_PGM_CHUNKSIZE;
			s += IAP_PGM_CHUNKSIZE;
		}

		/* prepare... */
		if (lpc11xx_flash_prepare(target, chunk * IAP_PGM_CHUNKSIZE, IAP_PGM_CHUNKSIZE))
			return -1;

		/* set the destination address and program */
		flash_pgm.p.command[0] = IAP_CMD_PROGRAM;
		flash_pgm.p.command[1] = chunk * IAP_PGM_CHUNKSIZE;
		flash_pgm.p.command[2] = IAP_RAM_BASE + offsetof(struct flash_program, data);
		flash_pgm.p.command[3] = IAP_PGM_CHUNKSIZE;
		flash_pgm.p.command[4] = 12000;	/* XXX safe to presume this? */
		lpc11x_iap_call(target, &flash_pgm.p, sizeof(flash_pgm));
		if (flash_pgm.p.result[0] != IAP_STATUS_CMD_SUCCESS) {
			return -1;
		}

	}

	return 0;
}
