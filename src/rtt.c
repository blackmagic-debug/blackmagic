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
uint32_t rtt_num_up_chan = 0;
uint32_t rtt_num_down_chan = 0;
bool rtt_auto_channel = true;
bool rtt_channel_enabled[MAX_RTT_CHAN] = {0}; // true if user wants to see channel
rtt_channel_s rtt_channel[MAX_RTT_CHAN];

uint32_t rtt_min_poll_ms = 8;   /* 8 ms */
uint32_t rtt_max_poll_ms = 256; /* 0.256 s */
uint32_t rtt_max_poll_errs = 10;
static uint32_t poll_ms;
static uint32_t poll_errs;
static uint32_t last_poll_ms;
/* flags for data from host to target */
bool rtt_flag_skip = false;
bool rtt_flag_block = false;
/* limit rtt ram accesses */
bool rtt_flag_ram;                      // limit ram scanned by rtt
uint32_t rtt_ram_start;                 // if rtt_flag_ram set, lower limit of ram scanned by rtt
uint32_t rtt_ram_end;                   // if rtt_flag_ram set, upper limit of ram scanned by rtt
static uint32_t saved_cblock_header[6]; // first 24 bytes of control block

typedef enum rtt_retval {
	RTT_OK,
	RTT_IDLE,
	RTT_ERR
} rtt_retval_e;

#ifdef RTT_IDENT
#define Q(x)     #x
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

static uint32_t fast_search(target_s *const cur_target, const uint32_t ram_start, const uint32_t ram_end)
{
	static const uint32_t m = 16;
	static const uint64_t p = 0x444110cd;
	static const uint64_t q = 0x797a9691; /* prime */
	static const uint64_t r = 0x73b07d01;
	static const uint32_t stride = 128;
	uint64_t t = 0;
	uint8_t srch_buf[m + stride];

	memset(srch_buf, 0, sizeof(srch_buf));

	for (uint32_t addr = ram_start; addr < ram_end; addr += stride) {
		uint32_t buf_siz = MIN(stride, ram_end - addr);
		memcpy(srch_buf, srch_buf + stride, m);
		if (target_mem_read(cur_target, srch_buf + m, addr, buf_siz)) {
			gdb_outf("rtt: read fail at 0x%" PRIx32 "\r\n", addr);
			return 0;
		}
		for (uint32_t i = 0; i < buf_siz; i++) {
			t = (t + q - r * srch_buf[i] % q) % q;
			t = ((t << 8U) + srch_buf[i + m]) % q;
			if (p == t)
				return addr + i - m + 1U;
		}
	}
	/* no match */
	return 0;
}

static uint32_t memory_search(target_s *const cur_target, const uint32_t ram_start, const uint32_t ram_end)
{
	const char *const srch_str = rtt_ident;
	const uint32_t srch_str_len = strlen(srch_str);
	uint8_t srch_buf[128];

	if (srch_str_len == 0 || srch_str_len > sizeof(srch_buf) / 2U)
		return 0;

	for (uint32_t addr = ram_start; addr < ram_end; addr += sizeof(srch_buf) - srch_str_len - 1U) {
		uint32_t buf_siz = MIN(ram_end - addr, sizeof(srch_buf));
		if (target_mem_read(cur_target, srch_buf, addr, buf_siz)) {
			gdb_outf("rtt: read fail at 0x%" PRIx32 "\r\n", addr);
			continue;
		}
		for (uint32_t offset = 0; offset + srch_str_len + 1U < buf_siz; offset++) {
			if (strncmp((const char *)(srch_buf + offset), srch_str, srch_str_len) == 0)
				return addr + offset;
		}
	}
	return 0;
}

static void find_rtt(target_s *const cur_target)
{
	rtt_found = false;
	poll_ms = rtt_max_poll_ms;
	poll_errs = 0;
	last_poll_ms = 0;

	if (!cur_target || !rtt_enabled)
		return;

	rtt_cbaddr = 0;
	if (!rtt_flag_ram) {
		/* search all of target ram */
		for (const target_ram_s *r = cur_target->ram; r; r = r->next) {
			const uint32_t ram_start = r->start;
			const uint32_t ram_end = r->start + r->length;
			if (rtt_ident[0] == 0)
				rtt_cbaddr = fast_search(cur_target, ram_start, ram_end);
			else
				rtt_cbaddr = memory_search(cur_target, ram_start, ram_end);
			if (rtt_cbaddr)
				break;
		}
	} else {
		/* search  only given target address range */
		if (rtt_ident[0] == 0)
			rtt_cbaddr = fast_search(cur_target, rtt_ram_start, rtt_ram_end);
		else
			rtt_cbaddr = memory_search(cur_target, rtt_ram_start, rtt_ram_end);
	}
	DEBUG_INFO("rtt: match at 0x%" PRIx32 "\r\n", rtt_cbaddr);

	if (rtt_cbaddr) {
		/* read number of rtt up and down channels from target */
		uint32_t num_buf[2];
		if (target_mem_read(cur_target, num_buf, rtt_cbaddr + 16U, sizeof(num_buf)))
			return;
		rtt_num_up_chan = num_buf[0];
		if (rtt_num_up_chan > MAX_RTT_CHAN)
			rtt_num_up_chan = MAX_RTT_CHAN;
		rtt_num_down_chan = num_buf[1];
		if (rtt_num_up_chan + rtt_num_down_chan > MAX_RTT_CHAN)
			rtt_num_down_chan = MAX_RTT_CHAN - rtt_num_up_chan;

		/* sanity checks */
		if (rtt_num_up_chan > 255U || rtt_num_down_chan > 255U) {
			gdb_out("rtt: bad cblock\r\n");
			rtt_enabled = false;
			return;
		}
		if (rtt_num_up_chan == 0 && rtt_num_down_chan == 0) {
			gdb_out("rtt: empty cblock\r\n");
			rtt_enabled = false;
			return;
		}

		/* clear channel data */
		memset(rtt_channel, 0, sizeof rtt_channel);

		/* auto channel: enable output channel 0, channel 1 and first input channel */
		if (rtt_auto_channel) {
			for (uint32_t i = 0; i < MAX_RTT_CHAN; i++)
				rtt_channel_enabled[i] = false;
			rtt_channel_enabled[0] = rtt_num_up_chan > 0;
			rtt_channel_enabled[1] = rtt_num_up_chan > 1U;
			if (rtt_num_up_chan < MAX_RTT_CHAN && rtt_num_down_chan > 0)
				rtt_channel_enabled[rtt_num_up_chan] = true;
		}

		/* save first 24 bytes of control block */
		if (target_mem_read(cur_target, saved_cblock_header, rtt_cbaddr, sizeof(saved_cblock_header)))
			return;

		rtt_found = true;
		DEBUG_INFO("rtt found\n");
	}
}

/*********************************************************************
*
*       rtt from host to target
*
**********************************************************************
*/

/* poll if host has new data for target */
static rtt_retval_e read_rtt(target_s *const cur_target, const uint32_t i)
{
	/* copy data from recv_buf to target rtt 'down' buffer */
	if (rtt_nodata())
		return RTT_IDLE;

	if (cur_target == NULL || rtt_channel[i].buf_addr == 0 || rtt_channel[i].buf_size == 0)
		return RTT_IDLE;

	if (rtt_channel[i].head >= rtt_channel[i].buf_size || rtt_channel[i].tail >= rtt_channel[i].buf_size)
		return RTT_ERR;

	/* write recv_buf to target rtt 'down' buf */
	while (true) {
		const uint32_t next_head = (rtt_channel[i].head + 1U) % rtt_channel[i].buf_size;
		if (rtt_channel[i].tail == next_head)
			break;
		const int ch = rtt_getchar();
		if (ch == -1)
			break;
		if (target_mem_write(cur_target, rtt_channel[i].buf_addr + rtt_channel[i].head, &ch, 1))
			return RTT_ERR;
		/* advance head pointer */
		rtt_channel[i].head = next_head;
	}

	/* update head of target 'down' buffer */
	const uint32_t head_addr = rtt_cbaddr + 24U + i * 24U + 12U;
	if (target_mem_write(cur_target, head_addr, &rtt_channel[i].head, sizeof(rtt_channel[i].head)))
		return RTT_ERR;
	return RTT_OK;
}

/*********************************************************************
*
*       rtt from target to host
*
**********************************************************************
*/

/* rtt_aligned_mem_read(): same as target_mem_read, but word aligned for speed.
   note: dest has to be len + 8 bytes, to allow for alignment and padding.
 */
uint32_t rtt_aligned_mem_read(target_s *t, void *dest, target_addr_t src, size_t len)
{
	const uint32_t offset = src & 0x3U;
	const uint32_t src0 = src - offset;
	uint32_t len0 = len + offset;
	if (len0 & 0x3U)
		len0 = (len0 + 4U) & ~0x3U;

	if (src0 == src && len0 == len)
		return target_mem_read(t, dest, src, len);

	const uint32_t retval = target_mem_read(t, dest, src0, len0);
	memmove(dest, dest + offset, len);
	return retval;
}

/* poll if target has new data for host */
static rtt_retval_e print_rtt(target_s *const cur_target, const uint32_t i)
{
	if (!cur_target || rtt_channel[i].buf_addr == 0 || rtt_channel[i].buf_size == 0)
		return RTT_IDLE;

	if (rtt_channel[i].head >= rtt_channel[i].buf_size || rtt_channel[i].tail >= rtt_channel[i].buf_size)
		return RTT_ERR;
	if (rtt_channel[i].head == rtt_channel[i].tail)
		return RTT_IDLE;

	uint32_t bytes_free = sizeof(xmit_buf) - 8U; /* need 8 bytes for alignment and padding */
	uint32_t bytes_read = 0;

	if (rtt_channel[i].tail > rtt_channel[i].head) {
		uint32_t len = rtt_channel[i].buf_size - rtt_channel[i].tail;
		if (len > bytes_free)
			len = bytes_free;
		if (rtt_aligned_mem_read(cur_target, xmit_buf + bytes_read, rtt_channel[i].buf_addr + rtt_channel[i].tail, len))
			return RTT_ERR;
		bytes_free -= len;
		bytes_read += len;
		rtt_channel[i].tail = (rtt_channel[i].tail + len) % rtt_channel[i].buf_size;
	}

	if (rtt_channel[i].head > rtt_channel[i].tail && bytes_free > 0) {
		uint32_t len = rtt_channel[i].head - rtt_channel[i].tail;
		if (len > bytes_free)
			len = bytes_free;
		if (rtt_aligned_mem_read(cur_target, xmit_buf + bytes_read, rtt_channel[i].buf_addr + rtt_channel[i].tail, len))
			return RTT_ERR;
		bytes_read += len;
		rtt_channel[i].tail = (rtt_channel[i].tail + len) % rtt_channel[i].buf_size;
	}

	/* update tail of target 'up' buffer */
	const uint32_t tail_addr = rtt_cbaddr + 24U + i * 24U + 16U;
	if (target_mem_write(cur_target, tail_addr, &rtt_channel[i].tail, sizeof(rtt_channel[i].tail)))
		return RTT_ERR;

	/* write buffer to usb */
	rtt_write(xmit_buf, bytes_read);

	return RTT_OK;
}

/*********************************************************************
*
*       rtt top level
*
**********************************************************************
*/

void poll_rtt(target_s *const cur_target)
{
	/* rtt off */
	if (!cur_target || !rtt_enabled)
		return;

	/* target present and rtt enabled */
	uint32_t now = platform_time_ms();

	if (last_poll_ms + poll_ms <= now || now < last_poll_ms) {
		if (!rtt_found)
			/* check if target needs to be halted during memory access */
			rtt_halt = target_mem_access_needs_halt(cur_target);

		bool resume_target = false;
		target_addr_t watch;
		if (rtt_halt && target_halt_poll(cur_target, &watch) == TARGET_HALT_RUNNING) {
			/* briefly halt target during target memory access */
			target_halt_request(cur_target);

			target_halt_reason_e reason = TARGET_HALT_RUNNING;
			while (reason == TARGET_HALT_RUNNING)
				reason = target_halt_poll(cur_target, &watch);

			resume_target = reason == TARGET_HALT_REQUEST;
		}

		if (!rtt_found)
			/* find rtt control block in target memory */
			find_rtt(cur_target);

		if (rtt_found) {
			uint32_t cblock_header[6]; // first 24 bytes of control block
			/* check control block not changed or corrupted */
			if (target_mem_read(cur_target, cblock_header, rtt_cbaddr, sizeof(cblock_header)) ||
				memcmp(saved_cblock_header, cblock_header, sizeof(cblock_header)) != 0)
				rtt_found = false; // force searching control block next poll_rtt()
		}

		bool rtt_err = false;
		bool rtt_busy = false;
		/* do rtt i/o if control block found */
		if (rtt_found && rtt_cbaddr) {
			/* copy control block from target */
			uint32_t rtt_cblock_size = sizeof(rtt_channel[0]) * (rtt_num_up_chan + rtt_num_down_chan);
			if (target_mem_read(cur_target, rtt_channel, rtt_cbaddr + 24U, rtt_cblock_size)) {
				gdb_outf("rtt: read fail at 0x%" PRIx32 "\r\n", rtt_cbaddr + 24U);
				rtt_err = true;
			} else {
				for (uint32_t i = 0; i < rtt_num_up_chan + rtt_num_down_chan; i++) {
					if (rtt_channel_enabled[i]) {
						rtt_retval_e result;
						if (i < rtt_num_up_chan)
							result = print_rtt(cur_target, i); /* rtt from target to host */
						else {
							/* rtt from host to target */
							rtt_flag_skip = rtt_channel[i].flag == 0;
							rtt_flag_block = rtt_channel[i].flag == 2U;
							result = read_rtt(cur_target, i);
						}
						if (result == RTT_OK)
							rtt_busy = true;
						else if (result == RTT_ERR)
							rtt_err = true;
					}
				}
			}
		}

		/* continue target if halted */
		if (resume_target)
			target_halt_resume(cur_target, false);

		/* update last poll time */
		last_poll_ms = now;

		/* rtt polling frequency goes up and down with rtt activity */
		if (rtt_busy && !rtt_err)
			poll_ms /= 2U;
		else
			poll_ms *= 2U;

		if (poll_ms > rtt_max_poll_ms)
			poll_ms = rtt_max_poll_ms;
		else if (poll_ms < rtt_min_poll_ms)
			poll_ms = rtt_min_poll_ms;

		if (rtt_err) {
			gdb_out("rtt: err\r\n");
			poll_errs++;
			if (rtt_max_poll_errs != 0 && poll_errs > rtt_max_poll_errs) {
				gdb_out("\r\nrtt lost\r\n");
				rtt_enabled = false;
			}
		}
	}
}
