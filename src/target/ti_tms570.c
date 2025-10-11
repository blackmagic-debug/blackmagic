#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexar.h"
#include "buffer_utils.h"

#define TMS570_SYS_BASE            0xFFFFFF00U
#define TMS570_SYS_DEVID           (TMS570_SYS_BASE + 0xF0U)
#define TMS570_SCM_DEVID_ID_MASK   0x0700FE7FU /* Note: value is swapped since target is BE */
#define TMS570_SCM_REVID_ID_TMS570 0x05004400U /* Note: value is swapped since target is BE */

#define TMS570_OTP_BANK0_MEMORY_INFORMATION 0xF008015CU
#define TMS570_OTP_BANK0_MEMORY_SIZE_MASK   0xFFFFU

/* Base address for the OCRAM regions, including their mirrors (including RETRAM) */
#define TMS570_SRAM_BASE     0x08000000U
#define TMS570_SRAM_ECC_BASE 0x08400000U
#define TMS570_SRAM_SIZE     0x80000U

#define TMS570_FLASH_BASE_ADDR   0x00000000U
#define TMS570_FLASH_SECTOR_ADDR (TMS570_L2FMC_BASE_ADDR + 0x408U)
#define TMS570_FLASH_SECTOR_SIZE

#define TMS570_L2FMC_BASE_ADDR       0xFFF87000U
#define TMS570_L2FMC_FSPRD_ADDR      (TMS570_FLASH_BASE_ADDR + 0x4U)
#define TMS570_L2FMC_FMAC_ADDR       (TMS570_FLASH_BASE_ADDR + 0x50U)
#define TMS570_L2FMC_FLOCK_ADDR      (TMS570_FLASH_BASE_ADDR + 0x64U)  /* Undocumented */
#define TMS570_L2FMC_FVREADCT_ADDR   (TMS570_FLASH_BASE_ADDR + 0x80U)  /* Undocumented */
#define TMS570_L2FMC_FVNVCT_ADDR     (TMS570_FLASH_BASE_ADDR + 0x8CU)  /* Undocumented */
#define TMS570_L2FMC_FVPPCT_ADDR     (TMS570_FLASH_BASE_ADDR + 0x90U)  /* Undocumented */
#define TMS570_L2FMC_FVWLCT_ADDR     (TMS570_FLASH_BASE_ADDR + 0x94U)  /* Undocumented */
#define TMS570_L2FMC_FEFUSE_ADDR     (TMS570_FLASH_BASE_ADDR + 0x98U)  /* Undocumented */
#define TMS570_L2FMC_FBSTROBES_ADDR  (TMS570_FLASH_BASE_ADDR + 0x100U) /* Undocumented */
#define TMS570_L2FMC_FPSTROBES_ADDR  (TMS570_FLASH_BASE_ADDR + 0x104U) /* Undocumented */
#define TMS570_L2FMC_FBMODE_ADDR     (TMS570_FLASH_BASE_ADDR + 0x108U) /* Undocumented */
#define TMS570_L2FMC_FTCR_ADDR       (TMS570_FLASH_BASE_ADDR + 0x10CU) /* Undocumented */
#define TMS570_L2FMC_FSM_WR_ENA_ADDR (TMS570_FLASH_BASE_ADDR + 0x288U)

#define TMS570_OTP_PACKAGE_AND_FLASH_MEMORY_SIZE 0xf008015cU

#define TMS570_OTP_BANK0_BASE 0xF0080000U
#define TMS570_FLASH_OTP_BASE (TMS570_OTP_BANK0_BASE + 0x170)

/*
 * Flash functions
 */

bool ti_tms570_probe(target_s *const target)
{
	const uint32_t part_id = target_mem32_read32(target, TMS570_SYS_DEVID);
	if (!part_id || ((part_id & TMS570_SCM_DEVID_ID_MASK) != TMS570_SCM_REVID_ID_TMS570)) {
		DEBUG_ERROR("Part ID 0x%08" PRIx32 " was unrecognized\n", part_id);
		return false;
	}

	target->driver = "TMS570";
	target_add_ram32(target, TMS570_SRAM_BASE, TMS570_SRAM_SIZE);
	target_add_ram32(target, TMS570_SRAM_ECC_BASE, TMS570_SRAM_SIZE);

	// Avoid toggling NRST, which will reset the icepick.
	target->target_options |= TOPT_INHIBIT_NRST;

	return true;
}
