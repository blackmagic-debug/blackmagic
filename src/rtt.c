
/*
 * This file is part of the Black Magic Debug project.
 *
 * MIT License
 *
 * Copyright (c) 2021 Koen De Vleeschauwer
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "general.h"
#include "platform.h"
#include "gdb_packet.h"
#include "target.h"
#include "target/target_internal.h"
#include "rtt.h"
#include "rtt_if.h"

bool rtt_enabled = false;
bool rtt_found = false;
static bool rtt_halt = false; // true if rtt needs to halt target to access memory
uint32_t rtt_cbaddr = 0;
bool rtt_auto_channel = true;
struct rtt_channel_struct rtt_channel[MAX_RTT_CHAN];

uint32_t rtt_min_poll_ms = 8;    /* 8 ms */
uint32_t rtt_max_poll_ms = 256;  /* 0.256 s */
uint32_t rtt_max_poll_errs = 10;
static uint32_t poll_ms;
static uint32_t poll_errs;
static uint32_t last_poll_ms;
/* flags for data from host to target */
bool rtt_flag_skip = false;
bool rtt_flag_block = false;

typedef enum rtt_retval {
	RTT_OK,
	RTT_IDLE,
	RTT_ERR
} rtt_retval;

#ifdef RTT_IDENT
#define Q(x) #x
#define QUOTE(x) Q(x)
char rtt_ident[16] = QUOTE(RTT_IDENT);
#else
char rtt_ident[16] = {0};
#endif

/* usb uart transmit buffer */
static char xmit_buf[RTT_UP_BUF_SIZE];

/*********************************************************************
*
*       rtt control block
*
**********************************************************************
*/

uint32_t fastsrch(target *cur_target)
{
	const uint32_t m = 16;
	const uint64_t q = 0x797a9691; /* prime */
	const uint64_t rm = 0x73b07d01;
	const uint64_t p = 0x444110cd;
	const uint32_t stride = 128;
	uint64_t t = 0;
	uint8_t srch_buf[m+stride];

	for (struct target_ram *r = cur_target->ram; r; r = r->next) {
		uint32_t ram_start = r->start;
		uint32_t ram_end = r->start + r->length;

		t = 0;
		memset(srch_buf, 0, sizeof(srch_buf));

		for (uint32_t addr = ram_start; addr < ram_end; addr += stride) {
			uint32_t buf_siz = MIN(stride, ram_end - addr);
			memcpy(srch_buf, srch_buf + stride, m);
			if (target_mem_read(cur_target, srch_buf + m, addr, buf_siz)) {
				gdb_outf("rtt: read fail at 0x%x\r\n", addr);
				return 0;
			}
			for (uint32_t i = 0; i < buf_siz; i++) {
				t = (t + q - rm * srch_buf[i] % q) % q;
				t = ((t << 8) + srch_buf[i + m]) % q;
				if (p == t) {
					uint32_t offset =  i - m + 1;
					return addr + offset;
				}
			}
		}
	}
	/* no match */
	return 0;
}

uint32_t memsrch(target *cur_target)
{
	char *srch_str = rtt_ident;
	uint32_t srch_str_len = strlen(srch_str);
	uint8_t srch_buf[128];

	if ((srch_str_len == 0) || (srch_str_len > sizeof(srch_buf) / 2))
		return 0;

	if (rtt_cbaddr && !target_mem_read(cur_target, srch_buf, rtt_cbaddr, srch_str_len)
		&& (strncmp((const char *)(srch_buf), srch_str, srch_str_len) == 0)) {
		/* still at same place */
		return rtt_cbaddr;
	}

	for (struct target_ram *r = cur_target->ram; r; r = r->next) {
		uint32_t ram_end = r->start + r->length;
		for (uint32_t addr = r->start; addr < ram_end; addr += sizeof(srch_buf) - srch_str_len - 1) {
			uint32_t buf_siz = MIN(ram_end - addr, sizeof(srch_buf));
			if (target_mem_read(cur_target, srch_buf, addr, buf_siz)) {
				gdb_outf("rtt: read fail at 0x%x\r\n", addr);
				continue;
			}
			for (uint32_t offset = 0; offset + srch_str_len + 1 < buf_siz; offset++) {
				if (strncmp((const char *)(srch_buf + offset), srch_str, srch_str_len) == 0) {
					uint32_t cb_addr = addr + offset;
					return cb_addr;
				}
			}
		}
	}
	return 0;
}

static void find_rtt(target *cur_target)
{
	rtt_found = false;
	poll_ms = rtt_max_poll_ms;
	poll_errs = 0;
	last_poll_ms = 0;

	if (!cur_target || !rtt_enabled)
		return;

	if (rtt_ident[0] == 0) rtt_cbaddr = fastsrch(cur_target);
	else rtt_cbaddr = memsrch(cur_target);
	DEBUG_INFO("rtt: match at 0x%" PRIx32 "\r\n", rtt_cbaddr);

	if (rtt_cbaddr) {
		uint32_t num_buf[2];
		int32_t num_up_buf;
		int32_t num_down_buf;
		if (target_mem_read(cur_target, num_buf, rtt_cbaddr + 16, sizeof(num_buf)))
			return;
		num_up_buf = num_buf[0];
		num_down_buf = num_buf[1];

		if ((num_up_buf > 255) || (num_down_buf > 255)) {
			gdb_out("rtt: bad cblock\r\n");
			rtt_enabled = false;
			return;
		} else if ((num_up_buf == 0) && (num_down_buf == 0))
			gdb_out("rtt: empty cblock\r\n");

		for (int32_t i = 0; i < MAX_RTT_CHAN; i++) {
			uint32_t buf_desc[6];

			rtt_channel[i].is_configured = false;
			rtt_channel[i].is_output = false;
			rtt_channel[i].buf_addr = 0;
			rtt_channel[i].buf_size = 0;
			rtt_channel[i].head_addr = 0;
			rtt_channel[i].tail_addr = 0;
			rtt_channel[i].flag = 0;

			if (i >= num_up_buf + num_down_buf) continue;
			if (target_mem_read(cur_target, buf_desc, rtt_cbaddr + 24 + i * 24, sizeof(buf_desc)))
				return;
			rtt_channel[i].is_output = i < num_up_buf;
			rtt_channel[i].buf_addr = buf_desc[1];
			rtt_channel[i].buf_size = buf_desc[2];
			rtt_channel[i].head_addr = rtt_cbaddr + 24 + i * 24 + 12;
			rtt_channel[i].tail_addr = rtt_cbaddr + 24 + i * 24 + 16;
			rtt_channel[i].flag = buf_desc[5];
			rtt_channel[i].is_configured = (rtt_channel[i].buf_addr != 0) && (rtt_channel[i].buf_size != 0);
		}

		/* auto channel: enable output channels 0 and 1 and first input channel */
		if (rtt_auto_channel) {
			for (uint32_t i = 0; i < MAX_RTT_CHAN; i++)
				rtt_channel[i].is_enabled = false;
			rtt_channel[0].is_enabled = num_up_buf > 0;
			rtt_channel[1].is_enabled = num_up_buf > 1;
			if ((num_up_buf < MAX_RTT_CHAN) && (num_down_buf > 0))
				rtt_channel[num_up_buf].is_enabled = true;
		}

		/* get flags for data from host to target */
		rtt_flag_skip = false;
		rtt_flag_block = false;
		for (uint32_t i = 0; i < MAX_RTT_CHAN; i++)
			if (rtt_channel[i].is_enabled && rtt_channel[i].is_configured && !rtt_channel[i].is_output) {
				rtt_flag_skip = rtt_channel[i].flag == 0;
				rtt_flag_block = rtt_channel[i].flag == 2;
				break;
			}

		rtt_found = true;
		DEBUG_INFO("rtt found\n");
	}
	return;
}

/*********************************************************************
*
*       rtt from host to target
*
**********************************************************************
*/

/* poll if host has new data for target */
static rtt_retval read_rtt(target *cur_target, uint32_t i)
{
	uint32_t head_tail[2];
	uint32_t buf_head;
	uint32_t buf_tail;
	uint32_t next_head;
	int ch;

	/* copy data from recv_buf to target rtt 'down' buffer */
	if (rtt_nodata())
		return RTT_IDLE;

	if ((cur_target == NULL) || rtt_channel[i].is_output || (rtt_channel[i].buf_addr == 0) || (rtt_channel[i].buf_size == 0))
		return RTT_IDLE;

	/* read down buffer head and tail from target */
	if (target_mem_read(cur_target, head_tail, rtt_channel[i].head_addr, sizeof(head_tail))) {
		return RTT_ERR;
	}

	buf_head = head_tail[0];
	buf_tail = head_tail[1];

	if ((buf_head >= rtt_channel[i].buf_size) || (buf_tail >= rtt_channel[i].buf_size)) {
		return RTT_ERR;
	}

	/* write recv_buf to target rtt 'down' buf */
	while (((next_head = ((buf_head + 1) % rtt_channel[i].buf_size)) != buf_tail) && ((ch = rtt_getchar()) != -1)) {
		if (target_mem_write(cur_target, rtt_channel[i].buf_addr + buf_head, &ch, 1)) {
			return RTT_ERR;
		}

		/* advance pointers */
		buf_head = next_head;
	}

	/* update head of target 'down' buffer */
	if (target_mem_write(cur_target, rtt_channel[i].head_addr, &buf_head, sizeof(buf_head))) {
		return RTT_ERR;
	}

	return RTT_OK;
}


/*********************************************************************
*
*       rtt from target to host
*
**********************************************************************
*/

/* target_mem_read, word aligned for speed.
   note: dest has to be len + 8 bytes, to allow for alignment and padding.
 */
int target_aligned_mem_read(target *t, void *dest, target_addr src, size_t len)
{
	uint32_t src0 = src;
	uint32_t len0 = len;
	uint32_t offset = src & 0x3;
	src0 -= offset;
	len0 += offset;
	if ((len0 & 0x3) != 0) len0 = (len0 + 4) & ~0x3;

	if ((src0 == src) && (len0 == len))
		return target_mem_read(t, dest, src, len);
	else {
		uint32_t retval = target_mem_read(t, dest, src0, len0);
		memmove(dest, dest + offset, len);
		return retval;
	}
}

/* poll if target has new data for host */
static rtt_retval print_rtt(target *cur_target, uint32_t i)
{
	uint32_t head;
	uint32_t tail;

	if (!cur_target || !rtt_channel[i].is_output || (rtt_channel[i].buf_addr == 0) || (rtt_channel[i].head_addr == 0))
		return RTT_IDLE;

	uint32_t head_tail[2];
	if (target_mem_read(cur_target, head_tail, rtt_channel[i].head_addr, sizeof(head_tail))) {
		return RTT_ERR;
	}
	head = head_tail[0];
	tail = head_tail[1];

	if ((head >= rtt_channel[i].buf_size) || (tail >= rtt_channel[i].buf_size)) {
		return RTT_ERR;
	}

	if (head == tail)
		return RTT_IDLE;

	uint32_t bytes_free = sizeof(xmit_buf) - 8; /* need 8 bytes for alignment and padding */
	uint32_t bytes_read = 0;

	if (tail > head) {
		uint32_t len = rtt_channel[i].buf_size - tail;
		if (len > bytes_free)
			len = bytes_free;
		if (target_aligned_mem_read(cur_target, xmit_buf + bytes_read, rtt_channel[i].buf_addr + tail, len))
			return RTT_ERR;
		bytes_free -= len;
		bytes_read += len;
		tail = (tail + len) % rtt_channel[i].buf_size;
	}

	if ((head > tail) && (bytes_free > 0)) {
		uint32_t len = head - tail;
		if (len > bytes_free)
			len = bytes_free;
		if (target_aligned_mem_read(cur_target, xmit_buf + bytes_read, rtt_channel[i].buf_addr + tail, len))
			return RTT_ERR;
		bytes_read += len;
		tail = (tail + len) % rtt_channel[i].buf_size;
	}

	/* update tail on target */
	if (target_mem_write(cur_target, rtt_channel[i].tail_addr, &tail, sizeof(tail)))
		return RTT_ERR;

	/* write buffer to usb */
	rtt_write(xmit_buf, bytes_read);

	return RTT_OK;
}


/*********************************************************************
*
*       target background memory access
*
**********************************************************************
*/

/* target_no_background_memory_access() is true if the target needs to be halted during jtag memory access
   target_no_background_memory_access() is false if the target allows jtag memory access while running */

bool target_no_background_memory_access(target *cur_target)
{
	/* if error message is 'rtt: read fail at' add target to expression below.
	   As a first approximation, assume all arm processors allow memory access while running, and no riscv does. */
	bool riscv_core = cur_target && target_core_name(cur_target) && strstr(target_core_name(cur_target), "RVDBG");
	return riscv_core;
}

/*********************************************************************
*
*       rtt top level
*
**********************************************************************
*/

void poll_rtt(target *cur_target)
{
	/* rtt off */
	if (!cur_target || !rtt_enabled) {
		return;
	}
	/* target present and rtt enabled */
	uint32_t now = platform_time_ms();
	bool rtt_err = false;
	bool rtt_busy = false;

	if ((last_poll_ms + poll_ms <= now) || (now < last_poll_ms)) {
		target_addr watch;
		enum target_halt_reason reason;
		bool resume_target = false;
		if (!rtt_found) {
			/* check if target needs to be halted during memory access */
			rtt_halt = target_no_background_memory_access(cur_target);
		}
		if (rtt_halt && (target_halt_poll(cur_target, &watch) == TARGET_HALT_RUNNING)) {
			/* briefly halt target during target memory access */
			target_halt_request(cur_target);
			while((reason = target_halt_poll(cur_target, &watch)) == TARGET_HALT_RUNNING);
			resume_target = reason == TARGET_HALT_REQUEST;
		}
		if (!rtt_found) {
			/* find rtt control block in target memory */
			find_rtt(cur_target);
		}
		/* do rtt i/o if control block found */
		if (rtt_found) {
			for (uint32_t i = 0; i < MAX_RTT_CHAN; i++) {
				rtt_retval v;
				if (rtt_channel[i].is_enabled && rtt_channel[i].is_configured) {
					if (rtt_channel[i].is_output)
						v = print_rtt(cur_target, i);
					else
						v = read_rtt(cur_target, i);
					if (v == RTT_OK) rtt_busy = true;
					else if (v == RTT_ERR) rtt_err = true;
				}
			}
		}
		/* continue target if halted */
		if (resume_target) {
			target_halt_resume(cur_target, false);
		}

		/* update last poll time */
		last_poll_ms = now;

		/* rtt polling frequency goes up and down with rtt activity */
		if (rtt_busy && !rtt_err)
			poll_ms /= 2;
		else poll_ms *= 2;
		if (poll_ms > rtt_max_poll_ms) poll_ms = rtt_max_poll_ms;
		else if (poll_ms < rtt_min_poll_ms) poll_ms = rtt_min_poll_ms;

		if (rtt_err) {
			gdb_out("rtt: err\r\n");
			poll_errs++;
			if ((rtt_max_poll_errs != 0) && (poll_errs > rtt_max_poll_errs)) {
				gdb_out("\r\nrtt lost\r\n");
				rtt_enabled = false;
			}
		}
	}
	return;
}

// not truncated
