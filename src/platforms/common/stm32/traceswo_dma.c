/*
 * Copyright (c) 2024 Stefan Simek, triaxis s.r.o.
 * 
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

/*
 * This is an alternate high-performance implementation of Manchester-encoded
 * TRACESWO for the Black Magic Debug Probe.
 *
 * This is a rough outline of the capture process:
 *
 * 1. all edge times of the signal are captured using a timer
 * 2. DMA is used to record the timings into a circular buffer
 * 3. the buffer is periodically processed in batches, transformig the edge
 *    stream into a byte stream for sending in another circular buffer, 
 *    resulting in effective processing time per sample on the order of
 *    several clock cycles
 * 4. the output buffer is processed in a lower-priority ISR as time permits
 *
 * The decoding is reasonably reliable for SWO frequencies from 100 kHZ up to
 * ~3 MHz and is mostly resilient against noise on the SWO input
 * 
 */

#include "general.h"
#include "platform.h"
#include "usb.h"
#include "traceswo.h"

#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/timer.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/dma.h>
#include <libopencm3/stm32/st_usbfs.h>

// Number of 16-bit samples captured for processing, the processing
// is triggered when half of the buffer is full, must be a power of two
#ifndef TRACE_DMA_SAMPLES
#define TRACE_DMA_SAMPLS	512
#endif

#define TRACE_DMA_MASK		((TRACE_DMA_SAMPLES)-1)

// Number of bytes buffered before sending over USB or decoding
// must be a power of two
#ifndef TRACE_DATA_SAMPLES
#define TRACE_DATA_SAMPLES	256
#endif

#define TRACE_DATA_MASK		((TRACE_DATA_SAMPLES)-1)

// Fixed length of a pulse that resets the decoder
// 72 MHz / 4096 = ~17.6 kHZ longest acceptable pulse
// limiting the lowest processable frequency to about 40 kHZ,
// but there is little reason to do that
#ifndef TRACE_MAX_PULSE
#define TRACE_MAX_PULSE 4096
#endif

// Include advanced recovery logic
// This makes sense only for very low frequencies when there is never
// a proper break in the pulse sequence
//#define TRACE_ADVANCED_RECOVERY	1

#define FORCE_INLINE	inline __attribute__((always_inline))

// edge time buffer
static uint16_t trace_dma[TRACE_DMA_SAMPLES];

// helper to get the current write head of the DMA
static FORCE_INLINE uint16_t trace_dma_wx()
{
	return TRACE_DMA_SAMPLES - DMA_CNDTR(TRACE_DMA_BUS, TRACE_DMA_CHAN);
}

// output data buffer
static uint8_t trace_data[TRACE_DATA_SAMPLES];
static uint16_t trace_data_rx, trace_data_wx;

static FORCE_INLINE uint16_t trace_data_available()
{
	return (trace_data_wx - trace_data_rx) & TRACE_DATA_MASK;
}

static FORCE_INLINE void trace_data_write(uint8_t b)
{
	uint16_t wx = trace_data_wx;
	trace_data[wx++] = b;
	trace_data_wx = wx & TRACE_DATA_MASK;
}

static FORCE_INLINE void trace_data_push(void)
{
	// just trigger the IRQ and let it check if it makes sense to do more -
	// it will be tail-chained after the main ISR anyway so an additional
	// check now would be a waste
	NVIC_STIR = TRACE_SW_IRQ;
}

static bool trace_decode = false;

/*
 * Initializes and starts the decoder
 */
void traceswo_init(uint32_t swo_chan_bitmask)
{
	// initialize the data decoder
	traceswo_setmask(swo_chan_bitmask);
	trace_decode = !!swo_chan_bitmask;

	// enable required peripherals
	TRACE_TIM_CLK_EN();
	rcc_periph_clock_enable(TRACE_DMA_CLK);

	// eliminate glitches shorter than 16 clocks
	// this limits maximum pulse frequency to ~4.5 MHz,
	// increasing resiliency of the input against noise
	timer_ic_set_filter(TRACE_TIM, TIM_IC1, TIM_IC_DTF_DIV_2_N_8);

	// slave trigger on all TI1 edges (trigger start functionality not used, 
	// this just to enable the TRC event)
	timer_slave_set_trigger(TRACE_TIM, TIM_SMCR_TS_TI1F_ED);
	timer_slave_set_mode(TRACE_TIM, TIM_SMCR_SMS_TM);

	// capture all edges using CH4
	timer_ic_set_input(TRACE_TIM, TIM_IC4, TIM_IC_IN_TRC);
	timer_ic_set_polarity(TRACE_TIM, TIM_IC4, TIM_IC_RISING);
	timer_ic_enable(TRACE_TIM, TIM_IC4);

	// use CH3 for a mid-cycle timeout to make sure idle periods are not missed
	timer_set_oc_value(TRACE_TIM, TIM_OC3, 0x8000);
	timer_ic_enable(TRACE_TIM, TIM_IC3);

#if TRACE_ADVANCED_RECOVERY
	// optionally capture exact rising/falling edges on CH1/2,
	// this is used only by the advanced recovery 
	timer_ic_set_input(TRACE_TIM, TIM_IC1, TIM_IC_IN_TI1);
	timer_ic_set_input(TRACE_TIM, TIM_IC2, TIM_IC_IN_TI1);
	timer_ic_set_polarity(TRACE_TIM, TIM_IC1, TIM_IC_RISING);
	timer_ic_set_polarity(TRACE_TIM, TIM_IC2, TIM_IC_FALLING);

	timer_ic_enable(TRACE_TIM, TIM_IC1);
	timer_ic_enable(TRACE_TIM, TIM_IC2);
#endif

	// interrupt fires twice pre timer cycle (CH3 and UPDATE)
	// also enable DMA from CH4
	timer_enable_irq(TRACE_TIM, TIM_DIER_UIE | TIM_DIER_CC3IE | TIM_DIER_CC4DE);

	// configure DMA to read edge times into a circular buffer
	dma_channel_reset(TRACE_DMA_BUS, TRACE_DMA_CHAN);

	dma_set_read_from_peripheral(TRACE_DMA_BUS, TRACE_DMA_CHAN);
	dma_set_peripheral_size(TRACE_DMA_BUS, TRACE_DMA_CHAN, DMA_CCR_PSIZE_16BIT);
	dma_set_memory_size(TRACE_DMA_BUS, TRACE_DMA_CHAN, DMA_CCR_MSIZE_16BIT);
	dma_set_priority(TRACE_DMA_BUS, TRACE_DMA_CHAN, DMA_CCR_PL_HIGH);

	dma_set_peripheral_address(TRACE_DMA_BUS, TRACE_DMA_CHAN, (uint32_t)&TIM_CCR4(TRACE_TIM));
	dma_set_memory_address(TRACE_DMA_BUS, TRACE_DMA_CHAN, (uint32_t)trace_dma);
	dma_set_number_of_data(TRACE_DMA_BUS, TRACE_DMA_CHAN, TRACE_DMA_SAMPLES);
	dma_enable_circular_mode(TRACE_DMA_BUS, TRACE_DMA_CHAN);
	dma_enable_memory_increment_mode(TRACE_DMA_BUS, TRACE_DMA_CHAN);
	dma_enable_channel(TRACE_DMA_BUS, TRACE_DMA_CHAN);

	// enable two DMA interrupts per buffer cycle
	dma_enable_half_transfer_interrupt(TRACE_DMA_BUS, TRACE_DMA_CHAN);
	dma_enable_transfer_complete_interrupt(TRACE_DMA_BUS, TRACE_DMA_CHAN);

	// enable DMA interrupt
	nvic_set_priority(TRACE_DMA_IRQ, IRQ_PRI_TRACE);
	nvic_enable_irq(TRACE_DMA_IRQ);

	// enable timer interrupt
	nvic_set_priority(TRACE_IRQ, IRQ_PRI_TRACE);
	nvic_enable_irq(TRACE_IRQ);

	// extra interrupt used for outbound data processing, triggered via NVIC
	// runs at USB priority to avoid preempting with the regular drain callback
	nvic_set_priority(TRACE_SW_IRQ, IRQ_PRI_USB);
	nvic_enable_irq(TRACE_SW_IRQ);

	// start the engine
	timer_enable_counter(TRACE_TIM);
}

/*
 * Callback for when the USB peripheral is read to accept more data,
 * doubles for pushing out new data
 */
void trace_buf_drain(usbd_device *dev, uint8_t ep)
{
	unsigned count, processed;

	while ((count = trace_data_available()))
	{
		// do not go past the end of the buffer
		count = MIN(count, (unsigned)(TRACE_DATA_SAMPLES - trace_data_rx));

		if (trace_decode)
		{
			processed = traceswo_decode(dev, CDCACM_UART_ENDPOINT, trace_data + trace_data_rx, count);
		}
		else
		{
			processed = usbd_ep_write_packet(dev, ep, trace_data + trace_data_rx, MIN(count, TRACE_ENDPOINT_SIZE));
		}

		if (!processed)
		{
			break;
		}

		trace_data_rx = (trace_data_rx + processed) & TRACE_DATA_MASK;
	}
}

/*
 * Dedicated handler for processing and outputting trace data
 * Note the trace_buf_drain is also a callback for the USB TRACE_ENDPOINT
 * that gets called from the main USB ISR, so this one must have the
 * same priority to avoid accidental preemption
 */
void TRACE_SW_ISR(void)
{
	// proceed only if there is a chance to send more data
	if ((*USB_EP_REG(TRACE_ENDPOINT) & USB_EP_TX_STAT) != USB_EP_TX_STAT_VALID)
	{
		trace_buf_drain(usbdev, TRACE_ENDPOINT);
	}
}

/*
 * This is the main Manchester input decoder
 *
 * For debugging, TRACE_DIAG_ISR may be defined, containing a bitmask
 * for optional diagnostic output instead of regular SWO output
 * 
 * The following events are defined:
 *   1 - output DMA trigger events as [status]
 *   2 - output TIM trigger events as <status>
 *   4 - output processing results as {nbits}
 *   8 - original original decoded bytes in addition to diagnostic events
 *       (this is normally suppressed whenever TRACE_DIAG_ISR is defined)
 *  16 - output repeating hex digits instead of actual decoded bytes
 *       (this an be used to identify decoding overflow vs USB overflow)
 *  32 - output symbols indicating polarity and length of each pulse
 */

//#define TRACE_DIAG_ISR	(4 | 8)

// enable assembly optimizations
#define TRACE_ASM_OPTIMIZATIONS	1

static FORCE_INLINE void trace_diag_nibble(uint32_t v)
{
	trace_data_write("0123456789ABCDEF"[v & 0xF]);
}

static inline void trace_diag_hex(uint32_t v)
{
	// CLZ >> 2 = number of leading zero nibbles which we want to skip,
	// so n is number of output nibbles
	unsigned n = v ? 8 - (__builtin_clz(v) >> 2) : 1;
	while (n--) {
		trace_diag_nibble(v >> (n << 2));
	}
}

/*
 * DMA ISR called twice per buffer, it does nothing, just clears the interrupt
 * flags and initiates a tail-chained TIM ISR which handles all the processing
 */
void TRACE_DMA_ISR(void)
{
	uint32_t status = DMA_ISR(TRACE_DMA_BUS) & DMA_ISR_MASK(TRACE_DMA_CHAN);
	DMA1_IFCR = status;

#if TRACE_DIAG_ISR & 1
	trace_data_write('[');
	trace_diag_hex(status >> DMA_FLAG_OFFSET(TRACE_DMA_CHAN));
	trace_data_write(']');
	trace_data_push();
#endif

	NVIC_STIR = TRACE_IRQ;
}

// Manchester decoder states
enum {
	ST_IDLE,		// line idle
	ST_INIT,		// line high before initial half-bit
	ST_BITL,		// line low at bit boundary
	ST_BITH,		// line high at bit boundary
	ST_MIDL,		// line low at mid-bit
	ST_MIDH,		// line high at mid-bit
	ST_INIL,		// line low after init (does not count for output)
#if TRACE_ADVANCED_RECOVERY
	ST_RESH,		// line high before idle (special recovery state)
#endif
};

/*
 * Main edge-to-data processing ISR
 */
void TRACE_ISR(void)
{
	// decoder state
	static struct state {
		uint16_t rx, t, q;
		uint8_t s;
		int32_t b;
	} _s;

	// transitions on short/long pulse
	// careful, the lookup table order must match enum
	static const uint8_t transitions[][2] = {
		// ST_IDLE
		{ ST_INIT, ST_INIT },
		// ST_INIT
		{ ST_INIL, ST_INIL },
		// ST_BITL
		{ ST_MIDH, ST_INIT },
		// ST_BITH
		// the long pulse goes to INIT, because in this state it is most likely
		// we accidentally switched polarity at some point - this is an attempt
		// to recover it
		// it happens especially at low speeds when there is little chance to
		// find an idle period long enough to recover
		{ ST_MIDL, ST_INIT },
		// ST_MIDL
		{ ST_BITH, ST_MIDH },
		// ST_MIDH
		{ ST_BITL, ST_MIDL },
		// ST_INIL (same as ST_MIDL)
		{ ST_BITH, ST_MIDH },
#if TRACE_ADVANCED_RECOVERY
		// ST_RESH (recovery)
		{ ST_IDLE, ST_IDLE },
#endif
	};

	// clear all interrupts, we don't care about details at all
	uint32_t status = TIM_SR(TRACE_TIM);
	TIM_SR(TRACE_TIM) = ~status;

#if TRACE_DIAG_ISR & 2
	trace_data_write('<');
	trace_diag_nibble(status >> 9);
	trace_diag_nibble(status >> 1);
	if (status & TIM_SR_TIF)
	{
		trace_data_write('T');
	}
	if (status & TIM_SR_UIF)
	{
		trace_data_write('U');
	}
	trace_data_write('>');
#endif

	// do not work with the state in RAM directly, it has to be loaded
	// into variables to allow the compiler to use them as registers
	// in the critical loop
	unsigned rx = _s.rx;	// read index
	unsigned s = _s.s;		// state
	uint16_t t = _s.t;		// last edge time
	uint16_t p;				// pulse time

	// number of samples available in the buffer
	unsigned avail = (trace_dma_wx() - rx) & TRACE_DMA_MASK;

	if (!avail)
	{
		// no data available
		if (s != ST_IDLE)
		{
			// if the state machine is still running, use current count to measure time elapsed since the last pulse
			// if enough time has elapsed, reset it, there is not much else we can do...
			p = TIM_CNT(TRACE_TIM) - t;
			if (p >= TRACE_MAX_PULSE)
			{
				// modify the state in RAM directly
				_s.s = ST_IDLE;
				_s.q = 0;
			}
		}
		
		// this is a good time to push out any unflushed bytes the 32-bit buffer
#if !TRACE_DIAG_ISR || (TRACE_DIAG_ISR & (8 | 16))
		uint32_t b = _s.b;
		if (b)	// b must not be zero, it would make the bitcount negative
		{
			// 31 - CTZ(b) == number of bits shifted into the buffer
			unsigned bits = 31 - __builtin_ctz(b);
			// full bytes and unaligned (yet unsent) bits
			unsigned bytes = bits >> 3;
			unsigned unaligned = bits & 7;
			// keep just the remaining bits in the register, writing 
			uint32_t terminator = 1u << 31 >> unaligned;
			_s.b = (b | terminator) & ~(terminator - 1);
			// extract the full bytes to be sent so they are aligned at LSB
			b = ~(b >> (32 - bits));

			unsigned wx = trace_data_wx;
			while (bytes--)
			{
#if TRACE_DIAG_ISR & 16
				trace_diag_nibble(trace_buf_wx);
#else
				trace_data[wx++ & TRACE_DATA_MASK] = b;
#endif
				b >>= 8;
			}
			trace_data_wx = wx & TRACE_DATA_MASK;
		}
#endif
		
		// no need to go deeper, just trigger USB processing
		trace_data_push();
		return;
	}

	// load the remainder of the state
	uint16_t q = _s.q;	// 3/4 of bit time for differentiating between short and long pulses
	// bit buffer for 32 bits
	// bits are shifted in from the top since they are incoming LSB first
	// initialized to 1 << 31 so that when the init bit is shifted out, we know
	// the buffer is full
	uint32_t b = _s.b;

	unsigned n = 0;

	// inner processing loop - this has to be as fast as possible, every clock
	// counts - for example, even enabling TRACE_ADVANCED_RECOVERY reduces
	// the maximum processable frequency to ~1 MHz
	while (avail--)
	{
		p = trace_dma[rx++] - t;
		rx &= TRACE_DMA_MASK;
		t += p;
		n++;

		if (p >= TRACE_MAX_PULSE)
		{
#if TRACE_DIAG_ISR & 32
			trace_data_write('!');
#endif
			s = ST_INIT;
			q = 0;
			continue;
		}

#if TRACE_ADVANCED_RECOVERY
		if (q && (p < q / 2 || p > q * 2))
		{
			// invalid pulse length, try to recover by dropping all data 
			// and initializing according to current input polarity
			// determined by comparing the last capture times of CH1 and CH2
			q = 0;
			rx = trace_dma_wx();
			if ((int16_t)(TIM_CCR1(TRACE_TIM) - TIM_CCR2(TRACE_TIM)) > 0)
			{
				// last edge was rising
				s = ST_RESH;
			}
			else
			{
				// last edge was falling
				s = ST_IDLE;
			}
			break;
		}
#endif
		
#if TRACE_ASM_OPTIMIZATIONS
		uint32_t tbl_index;
		__asm__(
			"cmp %[p], %[q]\n"
			"adc %[r], %[s], %[s]"	// s + s + carry (p >= q) is exactly what we want
			: [r] "=r"(tbl_index)
			: [p] "r"(p), [q] "r"(q), [s] "r"(s));
		s = ((uint8_t*)transitions)[tbl_index];
#else
		s = transitions[s][p >= q];
#endif

#if TRACE_DIAG_ISR & 32
		trace_data_write(
			p < MIN_PULSE ? '?' :
			p >= MAX_PULSE ? '_' :
			((s & 1) ? 'A' : 'a') + (p >> 7)
			);
#endif

#if TRACE_DIAG_ISR & 64
		trace_data_write("_IbBxXiR"[s]);
#endif

		// short-circuit for states requiring no extra action
		if (s < ST_MIDL) { continue; }

		// handle states requiring extra actions, primarily bit writing
#if TRACE_ASM_OPTIMIZATIONS
		bool output;
		__asm__ goto (
			"cmp %[s], %[ST_MIDH]\n"	// C = s == MIDH
			"bhi %l[init_q]\n"			// s > MIDH
			// shift C into b - note that the value is actually inverted
			// (rising edge produces 1), this is compensated for when outputting
			// shift LSB into C to know if we have full output ready
			"rrxs %[b], %[b]"
			// NOTE: b is passed in as an input operand - this seems to be 
			// the only way to prevent the compiler from generating
			// spurious move instructions before and/or after the inline block
			// it works in practice, but is deep in the UB territory...
			: "=@cccs" (output)
			: [b] "r" (b), [s] "r" (s), [ST_MIDH] "i" (ST_MIDH)
			:
			: init_q
			);
#else
		// just to make it comparable with the assembly version :)
		if (s > ST_MIDH) { goto init_q; }
		bool output = b & 1;
		b = b >> 1 | ((s == ST_MIDH) << 31);
#endif
		// mid-bit transition == output bit
		if (output)
		{
			// we have shifted out the initial bit,
			// meaning full 32 bits have been collected
			//trace_data_write(trace_buf_wx);
#if !TRACE_DIAG_ISR || (TRACE_DIAG_ISR & 8)
			b = ~b;
			uint32_t wx = trace_data_wx;
			uint8_t* p = trace_data;
			if (wx + 4 <= TRACE_DATA_SAMPLES)
			{
				// single write
				*(uint32_t*)&p[wx] = b;
				wx += 4;
			}
			else
			{
				// must split
				p[wx++ & TRACE_DATA_MASK] = b;
				b >>= 8;
				p[wx++ & TRACE_DATA_MASK] = b;
				b >>= 8;
				p[wx++ & TRACE_DATA_MASK] = b;
				b >>= 8;
				p[wx++ & TRACE_DATA_MASK] = b;
			}
			trace_data_wx = wx & TRACE_DATA_MASK;
#endif
#if TRACE_DIAG_ISR & 16
			for (int i = 0; i < 4; i++)
			{
				trace_diag_nibble(trace_buf_wx);
			}
#endif
			b = 1 << 31;
		}
		continue;

init_q:
		// calculate differentiator, reset state
		q = p * 3 / 2;
		b = 1 << 31;
	}

#if TRACE_DIAG_ISR & 4
	trace_data_write('{');
	trace_diag_hex(n);
	trace_data_write('}');
#endif

	// push out any new data
	trace_data_push();

	// store the state for next run
	_s = (struct state){ rx, t, q, s, b };
}
