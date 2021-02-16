#ifdef ENABLE_SRTT

#include "srtt.h"

#include "general.h"
#include "gdb_packet.h"
#include "target/target_internal.h"

#ifndef SRTT_MAX_BUFFERS
#	 define SRTT_MAX_BUFFERS 4
#endif

#ifndef SRTT_MAX_NAME_LEN
#	 define SRTT_MAX_NAME_LEN 16
#endif

#ifndef SRTT_IO_CHUNK_LEN
#	 define SRTT_IO_CHUNK_LEN 64
#endif

#ifndef SRTT_MIN_POLL_PERIOD
#  define SRTT_MIN_POLL_PERIOD 300
#endif

#define field_offset(struc, field) ((size_t)((char*)&((struc*)(0))->field))
#define end_of_array(array) (&(array)[sizeof(array)/sizeof((array)[0])])

typedef struct srtt_buf srtt_buf_t;

struct srtt_buf {
	const char *name;
	char *data_pointer;
	int data_length;
	int write_offset;
	int read_offset;
	int flags;
};

#define SRTT_ID_SIZE 16

typedef struct srtt_cb srtt_cb_t;

struct srtt_cb {
	int up_buffers;
	int down_buffers;
	srtt_buf_t buffer[SRTT_MAX_BUFFERS];
};

typedef bool (*cmd_fn)(target *t, int argc, const char **argv);

typedef struct cmd_s cmd_t;

struct cmd_s {
	const char *cmd;
	cmd_fn fn;
	const char *help;
};

#define NONE_ADDR ((target_addr)-1)

// The address of found control block
static target_addr srtt_cb_addr = NONE_ADDR;

// The control block metadata
static srtt_cb_t srtt_cb;

// The names of device buffers
static char srtt_buf_name[SRTT_MAX_BUFFERS][SRTT_MAX_NAME_LEN];

// The current target
static target *srtt_target;

// The attached buffers
static bool srtt_attached[SRTT_MAX_BUFFERS];

// The latest polling time which is used to throttle polls frequency
static uint32_t srtt_last_poll_time = 0;

bool srtt_available()
{
	return srtt_cb_addr != NONE_ADDR;
}

static void srtt_list_buffers()
{
	gdb_outf("RTT up/down buffers: %u/%u\n", srtt_cb.up_buffers, srtt_cb.down_buffers);

	// total number of buffers
	size_t n = MIN(srtt_cb.up_buffers + srtt_cb.down_buffers, SRTT_MAX_BUFFERS);

	for (size_t i = 0; i < n; i++) {
		srtt_buf_t *buf = &srtt_cb.buffer[i];

		gdb_outf("	%s buffer #%u",
						 i < (size_t)srtt_cb.up_buffers ? (srtt_attached[i] ? "UP (attached)" : "UP") : "DOWN",
						 1 + (i < (size_t)srtt_cb.up_buffers ? i : i - (size_t)srtt_cb.up_buffers));

		if (buf->name) {
			gdb_outf(" \"%s\"", srtt_buf_name[i]);
		}

		gdb_outf(" at %p of %u bytes\n", buf->data_pointer, buf->data_length);
	}
}

static bool srtt_display_buffers(target *t, int argc, const char **argv)
{
	(void)t;
	(void)argc;
	(void)argv;

	srtt_list_buffers();

	return true;
}

static bool srtt_read_control_block(target *t) {
	// read control block head
	if (target_mem_read(t, &srtt_cb,
											srtt_cb_addr + SRTT_ID_SIZE,
											field_offset(srtt_cb_t, buffer))) {
		gdb_out("Error when reading RTT control block header\n");
		return false;
	}

	// total number of buffers
	size_t n = MIN(srtt_cb.up_buffers + srtt_cb.down_buffers, SRTT_MAX_BUFFERS);

	// read descriptors of buffers
	if (target_mem_read(t, &srtt_cb.buffer,
											srtt_cb_addr + SRTT_ID_SIZE + field_offset(srtt_cb_t, buffer),
											n * sizeof(srtt_buf_t))) {
		gdb_out("Error when reading RTT buffers descriptors\n");
		return false;
	}

	// read names of buffers
	for (size_t i = 0; i < n; i++) {
		char *name = srtt_buf_name[i];
		if (target_mem_read(t, name,
												(target_addr)srtt_cb.buffer[i].name,
												SRTT_MAX_NAME_LEN)) {
			gdb_out("Error when reading RTT buffer name\n");
			return false;
		}
		size_t c;
		for (c = 0; c < SRTT_MAX_NAME_LEN && name[c] != '\0'; c++);
		if (c == SRTT_MAX_NAME_LEN) {
			name[SRTT_MAX_NAME_LEN - 4] = '.';
			name[SRTT_MAX_NAME_LEN - 3] = '.';
			name[SRTT_MAX_NAME_LEN - 2] = '.';
			name[SRTT_MAX_NAME_LEN - 1] = '\0';
		}
	}

	return true;
}

bool srtt_scan(target *t)
{
	// Scan target memory for "SEGGER RTT\0\0\0\0\0\0"
	const uint8_t magic[16] = {'S', 'E', 'G', 'G', 'E', 'R', ' ', 'R', 'T', 'T', [10 ... 15] = 0};
	uint8_t region[128];
	struct target_ram *ram;
	target_addr addr, end;
	size_t pos = 0, off, len;

	for (ram = t->ram; ram != NULL; ram = ram->next) {
		end = ram->start + ram->length;

		for (addr = ram->start; addr < end; addr += len) {
			len = MIN(sizeof(region), end - addr);
			if (target_mem_read(t, region, addr, len)) {
				goto rtt_cb_not_found;
			}
			// seek over the memory region
			for (off = 0; off < len; off++) {
				if (region[off] == magic[pos]) {
					// bytes matched, go to the next byte of magic
					pos ++;
				} else {
					// bytes does not match, go back to the first byte
					pos = 0;
				}
				if (pos == sizeof(magic)) { // found RTT control block
					// set control block address
					srtt_cb_addr = addr + off + 1 - sizeof(magic);

					gdb_outf("Found RTT control block at %p\n", srtt_cb_addr);

					if (!srtt_read_control_block(t)) {
						return false;
					}

					srtt_target = t;

					srtt_list_buffers();

					return true;
				}
			}
		}
	}

 rtt_cb_not_found:
	gdb_outf("No RTT control block found.\n");

	return false;
}

static int srtt_find_up_buffer(const char *str) {
	if (str[0] == '#') {
		int i = atoi(str + 1);
		if (i >= 1 && i <= srtt_cb.up_buffers) {
			return i - 1;
		}
		gdb_out("Invalid UP buffer #number.\n");
	} else {
		size_t i;
		for (i = 0; i < (size_t)srtt_cb.up_buffers; i++) {
			if (!strcmp(str, srtt_buf_name[i])) {
				return i;
			}
		}
		gdb_out("No UP buffer found.\n");
	}
	return -1;
}

static bool srtt_attach_buffer(target *t, int argc, const char **argv) {
	(void)t;

	if (argc < 2) {
		gdb_out("Missing UP buffer name or #number.\n");
		return false;
	}

	int n = srtt_find_up_buffer(argv[1]);

	if (n < 0) {
		return false;
	}

	srtt_attached[n] = true;

	return true;
}

static bool srtt_detach_buffer(target *t, int argc, const char **argv) {
	(void)t;

	if (argc < 2) {
		gdb_out("Missing UP buffer name or #number.\n");
		return false;
	}

	int n = srtt_find_up_buffer(argv[1]);

	if (n < 0) {
		return false;
	}

	if (!srtt_attached[n]) {
		gdb_out("Buffer doest not attached.\n");
		return false;
	}

	srtt_attached[n] = false;

	return true;
}

static bool srtt_read_up_chunk(target *t, const char* addr, size_t from, size_t to) {
	char data[SRTT_IO_CHUNK_LEN];
	target_addr ptr = (target_addr)addr + from, end = (target_addr)addr + to;

	for (; ptr < end; ) {
		size_t len = MIN(end - ptr, sizeof(data) - 1);
		if (target_mem_read(t, data, ptr, len)) {
			gdb_out("Unable to read RTT UP buffer.\n");
			return false;
		}
		data[len] = '\0';
		gdb_out(data);
		ptr += len;
	}

	return true;
}

static bool srtt_read_up_buffer(target *t, size_t i) {
	srtt_buf_t *b = &srtt_cb.buffer[i];

	// read `write_offset` and `read_offset` fields from target side
	if (target_mem_read(t, (char*)b + field_offset(srtt_buf_t, write_offset),
											srtt_cb_addr + SRTT_ID_SIZE + field_offset(srtt_cb_t, buffer) +
											i * sizeof(srtt_buf_t) + field_offset(srtt_buf_t, write_offset),
											field_offset(srtt_buf_t, flags) - field_offset(srtt_buf_t, write_offset))) {
		gdb_out("Unable to poll RTT UP buffer.\n");
		return false;
	}

	if (b->write_offset == b->read_offset) {
		// no data to read
		return true;
	}

	// read and output buffered text
	if (b->read_offset < b->write_offset) {
		if (!srtt_read_up_chunk(t, b->data_pointer, b->read_offset, b->write_offset)) {
			return false;
		}
	} else {
		if (!srtt_read_up_chunk(t, b->data_pointer, b->read_offset, b->data_length)) {
			return false;
		}
		if (!srtt_read_up_chunk(t, b->data_pointer, 0, b->write_offset)) {
			return false;
		}
	}

	b->read_offset = b->write_offset;

	// update `read_offset` field on target side
	if (target_mem_write(t, srtt_cb_addr + SRTT_ID_SIZE + field_offset(srtt_cb_t, buffer) +
											 i * sizeof(srtt_buf_t) + field_offset(srtt_buf_t, read_offset),
											 (char*)b + field_offset(srtt_buf_t, read_offset),
											 field_offset(srtt_buf_t, flags) - field_offset(srtt_buf_t, read_offset))) {
		gdb_out("Unable to sync RTT UP buffer.\n");
		return false;
	}

	return true;
}

static bool srtt_receive_up_buffer(target *t, int argc, const char **argv) {
	if (argc < 2) {
		gdb_out("Missing UP buffer name or #number.\n");
		return false;
	}

	int n = srtt_find_up_buffer(argv[1]);

	if (n < 0) {
		return false;
	}

	if (srtt_attached[n]) {
		gdb_out("Buffer is attached so it cannot be read synchronously.\n");
		return false;
	}

	return srtt_read_up_buffer(t, n);
}

static int srtt_find_down_buffer(const char *str) {
	if (str[0] == '#') {
		int i = atoi(str + 1);
		if (i >= 1 && i <= srtt_cb.down_buffers) {
			return i - 1 + srtt_cb.up_buffers;
		}
		gdb_out("Invalid DOWN buffer #number.\n");
	} else {
		size_t n = srtt_cb.up_buffers + srtt_cb.down_buffers;
		size_t i;
		for (i = srtt_cb.up_buffers; i < n; i++) {
			if (!strcmp(str, srtt_buf_name[i])) {
				return i;
			}
		}
		gdb_out("No DOWN buffer found.");
	}
	return -1;
}

static bool srtt_send_down_buffer(target *t, int argc, const char **argv) {
	(void)t;

	if (argc < 2) {
		gdb_out("Missing DOWN buffer name or #number.\n");
		return false;
	}

	int n = srtt_find_down_buffer(argv[1]);

	if (n < 0) {
		return false;
	}

	// write data to buffer

	return false;
}

static bool srtt_poll(target *t, int argc, const char **argv) {
	(void) argc;
	(void) argv;

	// read data from buffers
	for (size_t i = 0; i < (size_t)srtt_cb.up_buffers; i++) {
		if (srtt_attached[i]) {
			srtt_read_up_buffer(t, i);
		}
	}
	return false;
}

static const cmd_t srtt_cmds[] = {
	{"srtt_buffers", srtt_display_buffers, "Display list of available RTT buffers"},
	{"srtt_attach", srtt_attach_buffer, "Attach to UP buffer to receive outgoing device messages"},
	{"srtt_detach", srtt_detach_buffer, "Detach from UP buffer"},
	{"srtt_recv", srtt_receive_up_buffer, "Receive text from UP buffer"},
	{"srtt_send", srtt_send_down_buffer, "Send text to DOWN buffer"},
	{"srtt_poll", srtt_poll, "Poll attached buffers"},
};

int srtt_command(target *t, int argc, const char **argv) {
	for (size_t i = 0; i < sizeof(srtt_cmds)/sizeof(cmd_t); i++) {
		const cmd_t *c = &srtt_cmds[i];
		if (argc < 1 || !strncmp(c->cmd, argv[0], strlen(argv[0]))) {
			return !c->fn(t, argc, argv);
		}
	}

	gdb_out("Unrecognized RTT command.\n");

	return 1;
}

void srtt_command_help(void) {
	gdb_out("RTT commands:\n");
	for (const cmd_t *c = srtt_cmds; c < end_of_array(srtt_cmds); c++) {
		gdb_outf("\t%s -- %s\n", c->cmd, c->help);
	}
}

void srtt_do_poll(void) {
	if (!running_status)
		return;

	// poll rtt every second
	uint32_t now = platform_time_ms();
	if (now - srtt_last_poll_time < SRTT_MIN_POLL_PERIOD)
		return;

	srtt_last_poll_time = now;

	target *t = srtt_target;

	// read data from buffers
	for (size_t i = 0; i < (size_t)srtt_cb.up_buffers; i++) {
		if (srtt_attached[i]) {
			srtt_read_up_buffer(t, i);
		}
	}
}

#endif
