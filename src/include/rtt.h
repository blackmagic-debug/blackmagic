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

#ifndef INCLUDE_RTT_H
#define INCLUDE_RTT_H
#include <target.h>

// MAX_RTT_CHAN can be set as low as 6.
#define MAX_RTT_CHAN 16U

extern char rtt_ident[16];                     // string
extern bool rtt_enabled;                       // rtt on/off
extern bool rtt_found;                         // control block found
extern uint32_t rtt_cbaddr;                    // control block address
extern uint32_t rtt_num_up_chan;               // number of 'up' channels
extern uint32_t rtt_num_down_chan;             // number of 'down' channels
extern uint32_t rtt_min_poll_ms;               // min time between polls (ms)
extern uint32_t rtt_max_poll_ms;               // max time between polls (ms)
extern uint32_t rtt_max_poll_errs;             // max number of errors before disconnect
extern bool rtt_flag_ram;                      // limit ram scanned by rtt to range rtt_ram_start .. rtt_ram_end
extern uint32_t rtt_ram_start;                 // if rtt_flag_ram set, lower limit of ram scanned by rtt
extern uint32_t rtt_ram_end;                   // if rtt_flag_ram set, upper limit of ram scanned by rtt
extern bool rtt_auto_channel;                  // manual or auto channel selection
extern bool rtt_flag_skip;                     // skip if host-to-target fifo full
extern bool rtt_flag_block;                    // block if host-to-target fifo full
extern bool rtt_channel_enabled[MAX_RTT_CHAN]; // true if user wants to see channel

typedef struct rtt_channel {
	uint32_t name_addr;
	uint32_t buf_addr;
	uint32_t buf_size;
	uint32_t head;
	uint32_t tail;
	uint32_t flag;
} rtt_channel_s;

extern rtt_channel_s rtt_channel[MAX_RTT_CHAN];

void poll_rtt(target_s *cur_target);

#endif /* INCLUDE_RTT_H */
