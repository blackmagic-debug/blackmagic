#include "general.h"
#include "target.h"
#include "cortexm.h"
#include "lpc_common.h"

#define IAP_PGM_CHUNKSIZE	512	/* should fit in RAM on any device */

#define MIN_RAM_SIZE            1024
#define RAM_USAGE_FOR_IAP_ROUTINES	32	/* IAP routines use 32 bytes at top of ram */

#define IAP_ENTRYPOINT	0x03000205
#define IAP_RAM_BASE	0x02000000

#define LPC15XX_DEVICE_ID  0x400743F8

static int lpc15xx_flash_write(struct target_flash *f,
                               uint32_t dest, const void *src, size_t len);

void lpc15xx_add_flash(target *t, uint32_t addr, size_t len, size_t erasesize)
{
	struct lpc_flash *lf = lpc_add_flash(t, addr, len);
	lf->f.blocksize = erasesize;
	lf->f.buf_size = IAP_PGM_CHUNKSIZE;
	lf->f.write_buf = lpc15xx_flash_write;
	lf->iap_entry = IAP_ENTRYPOINT;
	lf->iap_ram = IAP_RAM_BASE;
	lf->iap_msp = IAP_RAM_BASE + MIN_RAM_SIZE - RAM_USAGE_FOR_IAP_ROUTINES;
}

bool
lpc15xx_probe(target *t)
{
	uint32_t idcode;
	uint32_t ram_size = 0;

	/* read the device ID register */
	idcode = target_mem_read32(t, LPC15XX_DEVICE_ID);
	switch (idcode) {
	case 0x00001549:
	case 0x00001519:
		ram_size = 0x9000;
		break;
	case 0x00001548:
	case 0x00001518:
		ram_size = 0x5000;
		break;
	case 0x00001547:
	case 0x00001517:
		ram_size = 0x3000;
		break;
	}
	if (ram_size) {
		t->driver = "LPC15xx";
		target_add_ram(t, 0x02000000, ram_size);
		lpc15xx_add_flash(t, 0x00000000, 0x40000, 0x1000);
		return true;
	}

	return false;
}

static int lpc15xx_flash_write(struct target_flash *f,
                               uint32_t dest, const void *src, size_t len)
{
	if (dest == 0) {
		/* Fill in the magic vector to allow booting the flash */
		uint32_t *w = (uint32_t *)src;
		uint32_t sum = 0;

		for (unsigned i = 0; i < 7; i++)
			sum += w[i];
		w[7] = ~sum + 1;
	}
	return lpc_flash_write(f, dest, src, len);
}

