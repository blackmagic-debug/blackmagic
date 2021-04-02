#include "target.h"
#include "target_internal.h"
#include "gdb_packet.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

uint32_t target_config;

int app_handle_packet(char *packet, int len) {
	(void)len;
    if (!strcmp (packet, "qSymbol::")) {
        /* Retrieve 'config' symbol. */
        target_config = 0;
        gdb_putpacketz("qSymbol:636f6e666967");
        return 1;
    }
    else if (1 == sscanf(packet, "qSymbol:%" SCNx32 ":636f6e666967", &target_config)) {
		/* Only expecting one symbol, so we're done. */
        gdb_putpacketz("OK");
        return 1;
    }
    else {
        /* Not handled. */
        return 0;
    }
}


struct log_buf {
	uint32_t write_next;
	uint32_t read_next;
	uint8_t  logsize;
	uint8_t  reserved[3];
};
void app_poll(target *t)
{
	/* This uses the uc_tools config struct as root data structure.
	   See struct gdbstub_config in uc_tools/gdb/gdbstub_api.h
	   https://github.com/zwizwa/uc_tools */
	if (!target_config) return;
	/* A simpler proof-of-concept and a scratch for an immediate itch:
	   read from the target log buffer directly.  For uc_tools this
	   requires no additional target support other than providing the
	   buffer address in the config struct. */

	const uint32_t log_buf_config_offset = 17;
	uint32_t log_buf_addr =
            target_mem_read32(t, target_config + log_buf_config_offset * sizeof(uint32_t));
	if (!log_buf_addr) return;
	/* Simplest to just read the whole thing at once. */
	struct log_buf log_buf;
	target_mem_read(t, &log_buf, log_buf_addr, sizeof(log_buf));
	int32_t nb = log_buf.write_next - log_buf.read_next;
	if (!nb) return;
	/* FIXME: This probably needs a consistency check.  It is possible
	   that the pointers became corrupt due to a target crash.  What
	   we expect here is that nb is between 0 and buf_size-1. */
	uint32_t buf_size = 1 << log_buf.logsize;
	uint32_t buf_mask = buf_size - 1;
	uint32_t offset_start = log_buf.read_next & buf_mask;
	/* Keep it simple and just transfer the chunk up to the end of the
	   buffer. If there is wrap around, the remainder will be read on
	   the next poll. */
	uint32_t offset_endx = offset_start + nb;
	if (offset_endx > buf_size) offset_endx = buf_size;
	nb = offset_endx - offset_start;

	/* FIXME: Limit chunk to available stack size. */
	if (nb > 64) nb = 64;
	char buf[nb];
	uint32_t data_addr = log_buf_addr + sizeof(struct log_buf) + offset_start;
	target_mem_read(t, buf, data_addr, nb);
	gdb_out_buf(buf, nb);
	/* Acknowledge by updating the read pointer. */
	target_mem_write32(t, log_buf_addr + 4, log_buf.read_next + nb);
}


/* Keep hacks together, and clearly mark them as such. */
static bool app_cmd_target_config(target *t, int argc, const char **argv) {
	(void)t;
	if (argc >= 2) {
		target_config = strtol(argv[1], NULL, 0);
	}
	gdb_outf("target_config = 0x%08x\n", target_config);
	return true;
}

const struct command_s app_cmd_list[] = {
	{"target_config", (cmd_handler)app_cmd_target_config, "Target config struct (address)"},
	{NULL, NULL, NULL}
};

const char app_name[] = "log_buf";
