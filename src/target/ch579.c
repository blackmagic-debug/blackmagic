#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"
#include "adiv5.h"
#include "buffer_utils.h"

static bool ch579_wait_flash(target_s *const t, platform_timeout_s *const timeout)
{
    uint16_t x;
	while (((x = target_mem_read16(t, 0x4000180a)) & 0xFF) != 0x40) {
        DEBUG_INFO("ch579 wait %04x\n", x);
		if (target_check_error(t))
			return false;
		if (timeout)
			target_print_progress(timeout);
	}
	return true;
}

static bool ch579_flash_erase(target_flash_s *f, target_addr_t addr, size_t len)
{
    (void)len;
    DEBUG_INFO("ch579 flash erase %08x\n", (int)addr);
    target_mem_write32(f->t, 0x40001804, addr);
    target_mem_write8(f->t, 0x40001808, 0xa6);

    if (!ch579_wait_flash(f->t, NULL))
        return false;
    return true;
}
static bool ch579_flash_write(target_flash_s *f, target_addr_t dest, const void *src, size_t len)
{
    (void)len;
    DEBUG_INFO("ch579 flash write %08x\n", (int)dest);
    target_mem_write32(f->t, 0x40001804, dest);
    target_mem_write32(f->t, 0x40001800, read_le4((const uint8_t *)src, 0));
    target_mem_write8(f->t, 0x40001808, 0x9a);

    if (!ch579_wait_flash(f->t, NULL))
        return false;
    return true;
}
static bool ch579_flash_prepare(target_flash_s *f)
{
    DEBUG_INFO("ch579 flash prepare\n");
    // just enable both write flags now, so that code/data flash can be treated as contiguous
    target_mem_write8(f->t, 0x40001809, 0b10001100);
    return true;
}
static bool ch579_flash_done(target_flash_s *f)
{
    DEBUG_INFO("ch579 flash done\n");
    target_mem_write8(f->t, 0x40001809, 0b10000000);
    return true;
}

static bool ch579_cmd_erase_info_dangerous(target_s *target, int argc, const char **argv)
{
    (void)argc;
    (void)argv;
    target_mem_write8(target, 0x40001809, 0b10001100);
    target_mem_write32(target, 0x40001804, 0x40000);
    target_mem_write8(target, 0x40001808, 0xa5);
    bool okay = ch579_wait_flash(target, NULL);
    target_mem_write8(target, 0x40001809, 0b10000000);
    return okay;
}
static bool ch579_cmd_write_info_dangerous(target_s *target, int argc, const char **argv)
{
	uint32_t addr;
	uint32_t val;

	if (argc == 3) {
		addr = strtoul(argv[1], NULL, 0);
		val = strtoul(argv[2], NULL, 0);
	} else
		return false;

    target_mem_write8(target, 0x40001809, 0b10001100);
    target_mem_write32(target, 0x40001804, addr);
    target_mem_write32(target, 0x40001800, val);
    target_mem_write8(target, 0x40001808, 0x99);
    bool okay = ch579_wait_flash(target, NULL);
    target_mem_write8(target, 0x40001809, 0b10000000);
    return okay;
}
static bool ch579_cmd_disable_bootloader(target_s *target, int argc, const char **argv)
{
    (void)argc;
    (void)argv;
    target_mem_write8(target, 0x40001809, 0b10001100);
    target_mem_write32(target, 0x40001804, 0x40010);
    target_mem_write32(target, 0x40001800, 0xFFFFFFBF);
    target_mem_write8(target, 0x40001808, 0x99);
    bool okay = ch579_wait_flash(target, NULL);
    target_mem_write8(target, 0x40001809, 0b10000000);
    return okay;
}

const command_s ch579_cmd_list[] = {
	{"void_warranty_erase_infoflash", ch579_cmd_erase_info_dangerous, "Erase info flash sector"},
	{"void_warranty_write_infoflash", ch579_cmd_write_info_dangerous, "Write to info flash: [address] [value]"},
    {"disable_bootloader", ch579_cmd_disable_bootloader, "Disables ISP bootloader"},
	{NULL, NULL, NULL},
};


bool ch579_probe(target_s *target)
{
    DEBUG_INFO("ch579 probe\n");
    uint8_t chip_id = target_mem_read8(target, 0x40001041);
    if (chip_id != 0x79) {
        DEBUG_ERROR("Not CH579! 0x%02x\n", chip_id);
        return false;
    }

    target->driver = "CH579";

    target_flash_s *f = calloc(1, sizeof(*f));
    if (!f) { /* calloc failed: heap exhaustion */
        DEBUG_ERROR("calloc: failed in %s\n", __func__);
        return false;
    }
    f->start = 0;
    f->length = 0x3f000;
    f->blocksize = 512;
    f->writesize = 4;
    f->erase = ch579_flash_erase;
    f->write = ch579_flash_write;
    f->prepare = ch579_flash_prepare;
    f->done = ch579_flash_done;
    f->erased = 0xff;
    target_add_flash(target, f);

    target_add_ram(target, 0x20000000, 0x8000);
    target_add_commands(target, ch579_cmd_list, target->driver);
    return true;
}
