#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexar.h"

#define TMS570_SYS_BASE            0xffffff00U
#define TMS570_SYS_DEVID           (TMS570_SYS_BASE + 0xf0U)
#define TMS570_SCM_DEVID_ID_MASK   0x0700fe7fU /* Note: value is swapped since target is BE */
#define TMS570_SCM_REVID_ID_TMS570 0x05004400U /* Note: value is swapped since target is BE */

/* Base address for the OCRAM regions, including their mirrors (including RETRAM) */
#define TMS570_SRAM_BASE     0x08000000U
#define TMS570_SRAM_ECC_BASE 0x08400000U
#define TMS570_SRAM_SIZE     0x80000U

#define TMS570_OTP_PACKAGE_AND_FLASH_MEMORY_SIZE 0xf008015cU

bool ti_tms570_probe(target_s *const target)
{
	const uint32_t part_id = target_mem32_read32(target, TMS570_SYS_DEVID);
	if (!part_id || ((part_id & TMS570_SCM_DEVID_ID_MASK) != TMS570_SCM_REVID_ID_TMS570)) {
		fprintf(stderr, "Part ID 0x%08x was unrecognized\n", part_id);
		return false;
	}
	fprintf(stderr, "Part ID 0x%08x was recognized as a TMS570\n", part_id);

	target->driver = "TMS570";
	target_add_ram32(target, TMS570_SRAM_BASE, TMS570_SRAM_SIZE);
	target_add_ram32(target, TMS570_SRAM_ECC_BASE, TMS570_SRAM_SIZE);
	return true;
}
