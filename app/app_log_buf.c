#include "target.h"
#include "target_internal.h"
#include "gdb_packet.h"
#include "gdb_if.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>

uint32_t config_addr;

#define CONFIG_HEX "636f6e666967"

int app_handle_packet(char *packet, int len) {
	(void)len;
    if (!strcmp (packet, "qSymbol::")) {
        /* Retrieve 'config' symbol. */
        config_addr = 0;
        gdb_putpacketz("qSymbol:" CONFIG_HEX);
        return 1;
    }
    else if (1 == sscanf(packet, "qSymbol:%" SCNx32 ":" CONFIG_HEX, &config_addr)) {
		/* Only expecting one symbol, so we're done. */
        gdb_putpacketz("OK");
        return 1;
    }
    else {
        /* Not handled. */
        return 0;
    }
}

/* Target contains this structure to describe the log buffer.  The
   buffer data follows the header.  Buffer size is always a power of
   two.  The _next pointers are rolling counters, and need to be
   interpreted modulo buffer size. */
struct log_buf_hdr {
	uint32_t write_next;
	uint32_t read_next;
	uint8_t  logsize;
	uint8_t  reserved[3];
};


/* Poll data from the on-target log buffer when the target is running.
   Data is displayed on the GDB console.  */
void app_poll(target *t)
{
	/* This uses the uc_tools config struct as root data structure.
	   See struct gdbstub_config in uc_tools/gdb/gdbstub_api.h
	   https://github.com/zwizwa/uc_tools

	   Details might change later.  The important bit is that we know
	   how to find log_buf_addr, the target memory address of the
	   log_buf_hdr struct. */
	if (!config_addr) return;
	const uint32_t log_buf_config_offset = 17;
	const uint32_t p_log_buf_addr = config_addr + log_buf_config_offset * 4;
	uint32_t log_buf_addr = target_mem_read32(t, p_log_buf_addr);
	if (!log_buf_addr) return;

	/* Get the location of the payload data from the header.
	   FIXME: This probably needs a consistency check.  It is possible
	   that the pointers became corrupt due to a target crash.  What
	   we expect here is that nb is between 0 and buf_size-1. */
	struct log_buf_hdr log_buf;
	target_mem_read(t, &log_buf, log_buf_addr, sizeof(log_buf));
	int32_t nb = log_buf.write_next - log_buf.read_next;
	if (!nb) return;
	uint32_t buf_size = 1 << log_buf.logsize;
	uint32_t buf_mask = buf_size - 1;
	uint32_t offset_start = log_buf.read_next & buf_mask;

	/* Transfer the chunk up to the end of the buffer.  Don't
	   implement wrap-around here, it will automatically happen on the
	   next poll. */
	uint32_t offset_endx = offset_start + nb;
	if (offset_endx > buf_size) offset_endx = buf_size;
	nb = offset_endx - offset_start;

	/* FIXME: how much room do we have on the stack? */
	if (nb > 64) nb = 64;
	char buf[nb];
	uint32_t data_addr = log_buf_addr + sizeof(struct log_buf_hdr) + offset_start;
	target_mem_read(t, buf, data_addr, nb);
	gdb_out_buf(buf, nb);

	/* Acknowledge log buffer read by updating the read pointer. */
	target_mem_write32(t, log_buf_addr + 4, log_buf.read_next + nb);
}


/* Keep hacks together, and clearly mark them as such. */
static bool app_cmd_config_addr(target *t, int argc, const char **argv) {
	(void)t;
	if (argc >= 2) {
		config_addr = strtol(argv[1], NULL, 0);
	}
	gdb_outf("config_addr = 0x%08x\n", config_addr);
	return true;
}

const struct command_s app_cmd_list[] = {
	{"config_address", (cmd_handler)app_cmd_config_addr, "Target config struct (address)"},
	{NULL, NULL, NULL}
};

const char app_name[] = "log_buf";

/* This is just a stub. */
#define APP_INPUT_BUF_LOGSIZE 8
#define APP_INPUT_BUF_SIZE (1 << APP_INPUT_BUF_LOGSIZE)
#define APP_INPUT_BUF_MASK (APP_INPUT_BUF_SIZE-1)
struct {
	char buf[APP_INPUT_BUF_SIZE];
	uint32_t write;
} app_input = {};

/* Just an illustration.  The point is to allow an alternative
   protocol on the main ttyACM in case non-RSP protocol is
   received. */
void app_switch_protocol(char c) {
	app_input.buf[app_input.write] = c;
	while ('$' != (c = gdb_if_getchar())) {
		app_input.buf[app_input.write++] = c;
		if (isspace(c)) {
			app_input.buf[app_input.write] = 0;
			/* FIXME: Hook this into:
			   https://github.com/zwizwa/uc_tools/blob/master/mod_forth.c
			   The input of that interpreter is just isolated commands or numbers.
			   For now, just print those to the console. */
			for (uint32_t i=0; i<app_input.write; i++) {
				gdb_if_putchar(app_input.buf[i], 0);
			}
			gdb_if_putchar('\r', 0);
			gdb_if_putchar('\n', 1);
			app_input.write = 0;
		}
	}
}
