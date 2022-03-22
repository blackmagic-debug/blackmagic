#include "target.h"
#include "target_internal.h"
#include "gdb_packet.h"
#include "gdb_if.h"
#include "applet.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>

/* Target address of the configuration structure.  This is _very_
   application-specific, and one reason why we want to hide this
   knowledge in an applet, away from the main BMP firmware.  The only
   thing to know here is that this address contains an array of
   pointers, and at position 17 there is a pointer to log_buf_hdr
   struct described below. */
uint32_t config_addr;
const uint32_t log_buf_config_offset = 17;

/* Target contains this structure to describe the log buffer.  The log
   buffer char data follows the header.  Buffer size is always a power
   of two.  The _next pointers are rolling counters, and need to be
   interpreted modulo buffer size. */
struct log_buf_hdr {
	uint32_t write_next;
	uint32_t read_next;
	uint8_t  logsize;
	uint8_t  reserved[3];
};

/* To find the target configuration structure, we use GDB RSP qSymbol
   functionality, not supported by BMP. */
/* We're only looking up one symbol and only need its hex form. */
#define CONFIG_HEX "636f6e666967"
bool applet_handle_packet(char *packet, int len) {
	(void)len;
    if (!strcmp (packet, "qSymbol::")) {
		/* When loading an ELF, GDB will send this command to indicate
		   it is ready to start symbol lookup.  We request the
		   'config' symbol. */
        config_addr = 0;
        gdb_putpacketz("qSymbol:" CONFIG_HEX);
        return true;
    }
    else if (1 == sscanf(packet, "qSymbol:%" SCNx32 ":" CONFIG_HEX, &config_addr)) {
		/* That's all we need.  Indicate to GDB that we're done
		   looking up symbols. */
        gdb_putpacketz("OK");
        return true;
    }
    else {
        /* Not handled. */
        return false;
    }
}

/* This function is called when BMP is in its main halt polling loop,
 * waiting for the target to halt.  At that point we can use
 * target_mem_* functions to interact with the target while it is
 * running.  We poll the log buffer and if we find data, we dump it to
 * the GDB console. */
void applet_poll(target *t)
{
	/* For more information, see struct gdbstub_config in
	   uc_tools/gdb/gdbstub_api.h https://github.com/zwizwa/uc_tools

	   Details might change later.  The important bit is that we know
	   how to find log_buf_addr, the target memory address of the
	   log_buf_hdr struct. */
	if (!config_addr) return;
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

/* Display the config struct address, and allow it to be specified in
   case the symbol lookup did not find it. */
static bool applet_cmd_config_addr(target *t, int argc, const char **argv) {
	(void)t;
	if (argc >= 2) {
		config_addr = strtol(argv[1], NULL, 0);
	}
	gdb_outf("config_addr = 0x%08x\n", config_addr);
	return true;
}
const struct command_s applet_cmd_list[] = {
	{"config_address", (cmd_handler)applet_cmd_config_addr, "Target config struct (address)"},
	{NULL, NULL, NULL}
};
const char applet_name[] = "log_buf";




/* BMP firmware already supports two protocols: GDB RSP and the remote
   protocol.  It is possible to implement an additional custom
   protocol, as long as it uses a preamble that is different from
   GDB's '$' and the remote protocol '!'.

   Here we use the enter key '\r' to activate a command console.  We
   can use gdb_if_putchar() to print characters.

   TODO: Add functionality that polls the target log data in a similar
   way as is done in the halt loop.
*/

static void console_newline(void) {
	gdb_if_putchar('\r', 0);
	gdb_if_putchar('\n', 1);
}
void console_println(const char *line) {
	while(*line) gdb_if_putchar(*line++, 0);
	console_newline();
}

char applet_switch_protocol(char c) {
	if (c != '\r') {
		/* ENTER key activates the console.  Anything else is ignored
		   in the same way as if there was no applet linked into the
		   BMP firmware.  We are required to return a new character to
		   avoid an infinite loop. */
		return gdb_if_getchar();
	}
	/* ENTER was pressed.  The protocol is now interactive user
	   commands. */
	console_println("Activating console.");
	c = gdb_if_getchar();
	for(;;) {
		/* Echo. */
		if (c == '\r') gdb_if_putchar('\n', 0);
		gdb_if_putchar(c, 1);

		c = gdb_if_getchar();

		/* If we want smooth interoperability with GDB RSP and remote
		   protocols, we need to reserve these characters and excape
		   back to gdb_getpacket(). */
		if ('$' == c) break;
		if ('!' == c) break;
		if (0x04 == c) break;

		/* Note that another way to handle this is to just stay in
		   this mode forever, and let the user restart the board to
		   make it go back into GDB RSP or remote protocol mode.
		   Whatever works... */
	}
	return c;
}
