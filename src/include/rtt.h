#ifndef RTT_H
#define RTT_H
#include <target.h>

#define MAX_RTT_CHAN 16

extern char rtt_ident[16];	    // string
extern bool rtt_enabled;	    // rtt on/off
extern bool rtt_found;              // control block found
extern uint32_t rtt_cbaddr;         // control block address
extern uint32_t rtt_min_poll_ms;    // min time between polls (ms)
extern uint32_t rtt_max_poll_ms;    // max time between polls (ms)
extern uint32_t rtt_max_poll_errs;  // max number of errors before disconnect
extern bool rtt_auto_channel;       // manual or auto channel selection
extern bool rtt_flag_skip;          // skip if host-to-target fifo full
extern bool rtt_flag_block;         // block if host-to-target fifo full

struct rtt_channel_struct {
	bool is_enabled;            // does user want to see this channel?
	bool is_configured;         // is channel configured in control block?
	bool is_output;
	uint32_t buf_addr;
	uint32_t buf_size;
	uint32_t head_addr;
	uint32_t tail_addr;
	uint32_t flag;
};

extern struct rtt_channel_struct rtt_channel[MAX_RTT_CHAN];

// true if target memory access does not work when target running
extern bool target_no_background_memory_access(target *cur_target);
extern void poll_rtt(target *cur_target);
#endif
