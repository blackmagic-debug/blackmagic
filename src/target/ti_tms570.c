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
static bool tms570_flash_erase(target_flash_s *flash, target_addr_t addr, size_t len);
static bool tms570_flash_write(target_flash_s *flash, target_addr_t dest, const void *src, size_t len);
static bool tms570_flash_prepare(target_flash_s *flash);
static bool tms570_flash_done(target_flash_s *flash);
static bool tms570_flash_initialize(target_s *const target);

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

	// target_flash_s *flash = calloc(1, sizeof(*flash));
	// if (!flash) { /* calloc failed: heap exhaustion */
	// 	DEBUG_ERROR("calloc: failed in %s\n", __func__);
	// 	return false;
	// }

	tms570_flash_initialize(target);

	// flash->start = TMS570_FLASH_BASE_ADDR;
	// flash->length = flash_size;
	// flash->blocksize = TMS570_FLASH_SECTOR_SIZE;
	// flash->writesize = CH579_FLASH_WRITE_SIZE;
	// flash->erase = tms570_flash_erase;
	// flash->write = tms570_flash_write;
	// flash->prepare = tms570_flash_prepare;
	// flash->done = tms570_flash_done;
	// flash->erased = 0xffU;
	// target_add_flash(target, flash);

	return true;
}

static uint32_t accumulate(uint32_t arg, const uint32_t val)
{
	arg += (val & 0xffffU);
	return (arg & 0xffffU) + (arg >> 16);
}

/// The flash configuration data is burned into a special OTP block on the target.
/// This data is hashed with a Fletcher checksum, which has the nice property that
/// it can tell us if the endianness is backwards or if we're not talking to the
/// correct device. The checksum value is stored in the last word of the OTP area.
static bool fletcher_checksum(const uint32_t *const otp_data, const size_t otp_data_count, const uint32_t comparison)
{
	uint32_t check_low = 0xffffU;
	uint32_t check_high = 0xffffU;

	for (size_t i = 0; i < otp_data_count; i++) {
		uint32_t word = otp_data[i];
		// Accumulate low word
		check_low = accumulate(check_low, word);
		check_high = accumulate(check_high, check_low);
		// Accumulate high word
		check_low = accumulate(check_low, word >> 16);
		check_high = accumulate(check_high, check_low);
	}

	uint32_t result = (check_high << 16) | check_low;
	DEBUG_TARGET("Comparing result %08" PRIx32 " to check value %08" PRIx32 "\n", result, comparison);
	return comparison == result;
}

static bool tms570_flash_initialize(target_s *const target)
{
	if (target->tms570_flash_initialized)
		return true;

	// Read the OTP data out of flash. This is protected with a Fletcher checksum
	uint32_t otp_data[12];
	for (int i = 0; i < 12; i++) {
		const uint32_t value = target_mem32_read32(target, TMS570_FLASH_OTP_BASE + (i * 4));
		otp_data[i] = read_be4((const uint8_t *)&value, 0);
		DEBUG_TARGET("Flash OTP[0x%08" PRIx32 "]: 0x%08" PRIx32 "\n", TMS570_FLASH_OTP_BASE + (i * 4), otp_data[i]);
	}
	if (!fletcher_checksum(otp_data, 11, otp_data[11])) {
		return false;
	}

	// Enable all three banks
	uint32_t fmac = target_mem32_read32(target, TMS570_L2FMC_FMAC_ADDR);
	target_mem32_write32(target, TMS570_L2FMC_FMAC_ADDR, fmac | 7);

	// Disable read margin control.
	// (Note: previously this set RMBSEL first and then RM0/1, but do both in one write now)
	target_mem32_write32(target, TMS570_L2FMC_FSPRD_ADDR, 0);

	// Disable the FSM
	target_mem32_write32(target, TMS570_L2FMC_FSM_WR_ENA_ADDR, 0);

	// Copy some timing values from OTP
	uint32_t unk_210 = target_mem32_read32(target, TMS570_L2FMC_BASE_ADDR + 0x210);
	target_mem32_write32(target, TMS570_L2FMC_BASE_ADDR + 0x210, (unk_210 & 0xffff0000) | (otp_data[0] >> 16));
	uint32_t unk_218 = target_mem32_read32(target, TMS570_L2FMC_BASE_ADDR + 0x218);
	target_mem32_write32(target, TMS570_L2FMC_BASE_ADDR + 0x218, (unk_218 & 0xffff0000) | (otp_data[0] & 0xffff));

	// Set the 2nd nibble after the first write is completed
	uint32_t unk_21c = target_mem32_read32(target, TMS570_L2FMC_BASE_ADDR + 0x21c);
	target_mem32_write32(target, TMS570_L2FMC_BASE_ADDR + 0x21c, (unk_21c & 0xffff00f0) | (otp_data[1] & 0xff0f));
	target_mem32_write32(target, TMS570_L2FMC_BASE_ADDR + 0x21c, (unk_21c & 0xffff0000) | (otp_data[1] & 0xffff));

	// ???
	uint8_t unk_214 = target_mem32_read32(target, TMS570_L2FMC_BASE_ADDR + 0x214);
	target_mem32_write32(target, TMS570_L2FMC_BASE_ADDR + 0x214, (unk_214 & 0xffff0fff) | (otp_data[3] & 0xf000));

	// ???
	uint8_t unk_220 = target_mem32_read32(target, TMS570_L2FMC_BASE_ADDR + 0x220);
	target_mem32_write32(target, TMS570_L2FMC_BASE_ADDR + 0x220, (unk_220 & 0xffffff00) | ((otp_data[1] >> 24) & 0xff));

	// ???
	uint32_t unk_224 = target_mem32_read32(target, TMS570_L2FMC_BASE_ADDR + 0x224);
	target_mem32_write32(target, TMS570_L2FMC_BASE_ADDR + 0x224, (unk_224 & 0xffffff00) | ((otp_data[2] >> 16) & 0xff));

	// ???
	uint16_t unk_268 = target_mem32_read32(target, TMS570_L2FMC_BASE_ADDR + 0x268);
	target_mem32_write32(target, TMS570_L2FMC_BASE_ADDR + 0x268, (unk_268 & 0xfffff000) | (otp_data[5] & 0xfff));

	// ???
	uint16_t unk_26c = target_mem32_read32(target, TMS570_L2FMC_BASE_ADDR + 0x26c);
	target_mem32_write16(
		target, TMS570_L2FMC_BASE_ADDR + 0x26c, (unk_26c & 0xfe00ffff) | ((otp_data[8] & 0x1ff) << 16));

	// NOTE: This used to be a 16-bit access -- ensure that it's properly swapped
	uint16_t unk_270 = target_mem32_read32(target, TMS570_L2FMC_BASE_ADDR + 0x270);
	target_mem32_write32(target, TMS570_L2FMC_BASE_ADDR + 0x270, unk_270 & 0xfe00ffff);

	uint32_t unk_278 = target_mem32_read32(target, TMS570_L2FMC_BASE_ADDR + 0x278);
	target_mem32_write32(target, TMS570_L2FMC_BASE_ADDR + 0x278, (unk_278 & 0xffffff80) | ((otp_data[4] - 1) & 0x7f));

	target_mem32_write32(target, TMS570_L2FMC_BASE_ADDR + 0x27c, 0x4500);

	// Unlock L2FMC
	target_mem32_write32(target, TMS570_L2FMC_FLOCK_ADDR, 0x55aa);

	uint32_t fvreadct = target_mem32_read32(target, TMS570_L2FMC_FVREADCT_ADDR);
	target_mem32_write32(target, TMS570_L2FMC_FVREADCT_ADDR, (fvreadct & 0xfffffff0) | ((otp_data[10] >> 8) & 0xf));

	target_mem32_write32(target, TMS570_L2FMC_FVNVCT_ADDR, 0);

	target_mem32_write32(target, TMS570_L2FMC_FBSTROBES_ADDR, 0x00010104);
	target_mem32_write32(target, TMS570_L2FMC_FPSTROBES_ADDR, 0x103);
	target_mem32_write32(target, TMS570_L2FMC_FBMODE_ADDR, 0);

	uint32_t ftcr = target_mem32_read32(target, TMS570_L2FMC_FTCR_ADDR);
	target_mem32_write32(target, TMS570_L2FMC_FTCR_ADDR, ftcr & 0xffffff80);

	uint32_t fvppct = target_mem32_read32(target, TMS570_L2FMC_FVPPCT_ADDR);
	target_mem32_write32(target, TMS570_L2FMC_FVPPCT_ADDR,
		(fvppct & 0xffffe0e0) | (((otp_data[9] >> 8) & 0x1f) << 8) | (otp_data[9] & 0x1f));

	uint8_t fvwlct = target_mem32_read32(target, TMS570_L2FMC_FVWLCT_ADDR);
	target_mem32_write32(target, TMS570_L2FMC_FVWLCT_ADDR, (fvwlct & ~0xf000) | ((otp_data[10] >> 24) & 0xf) << 12);

	uint32_t fefuse = target_mem32_read32(target, TMS570_L2FMC_FEFUSE_ADDR);
	target_mem32_write32(target, TMS570_L2FMC_FEFUSE_ADDR, (fefuse & 0xffffffe0) | ((otp_data[10] >> 16) & 0x1f));

	uint32_t unk_a8 = target_mem32_read32(target, TMS570_L2FMC_BASE_ADDR + 0x0a8);
	target_mem32_write32(target, TMS570_L2FMC_BASE_ADDR + 0x0a8, (unk_a8 & 0xffffff00) | ((otp_data[3] >> 16) & 0xff));

	target_mem32_write32(target, TMS570_L2FMC_FPSTROBES_ADDR, 0x103);

	target_mem32_write32(target, TMS570_L2FMC_FBSTROBES_ADDR, 0x10104);

	// Reads as 0x55aa
	target_mem32_write32(target, TMS570_L2FMC_FLOCK_ADDR, 0);

	// Re-enable the FSM
	uint32_t fsm = target_mem32_read32(target, TMS570_L2FMC_FSM_WR_ENA_ADDR);
	target_mem32_write32(target, TMS570_L2FMC_FSM_WR_ENA_ADDR, (fsm & 0xfffffff8) | 2);

	target->tms570_flash_initialized = true;
	return true;
}

static bool tms570_flash_prepare(target_flash_s *flash)
{
	target_s *const target = flash->t;

	if (!tms570_flash_initialize(target)) {
		DEBUG_ERROR("Checksum for OTP values doesn't match!");
		return false;
	}
}
