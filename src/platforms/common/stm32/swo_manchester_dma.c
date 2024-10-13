/*
 * This file is a contribution to the Black Magic Debug project.
 * 
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
 * SWO trace output for the Black Magic Debug Probe.
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
#include "swo.h"
#include "swo_internal.h"

#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/timer.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/dma.h>
#include <libopencm3/stm32/st_usbfs.h>

// Number of 16-bit samples captured for processing, the processing
// is triggered when half of the buffer is full, must be a power of two
#ifndef SWO_DMA_EDGE_SAMPLES
#define SWO_DMA_EDGE_SAMPLES	512
#endif

#define SWO_DMA_EDGE_MASK		((SWO_DMA_EDGE_SAMPLES)-1U)

#define SWO_BUFFER_MASK			((SWO_BUFFER_SIZE)-1U)

// Fixed length of a pulse that resets the decoder
// 72 MHz / 4096 = ~17.6 kHZ longest acceptable pulse
// limiting the lowest processable frequency to about 40 kHZ,
// but there is little reason to do that
#ifndef SWO_MAX_PULSE
#define SWO_MAX_PULSE 4096
#endif

// Include advanced recovery logic
// This makes sense only for very low frequencies when there is never
// a proper break in the pulse sequence
//#define SWO_ADVANCED_RECOVERY	1

#define FORCE_INLINE	inline __attribute__((always_inline))

// edge time buffer
static uint16_t swo_dma[SWO_DMA_EDGE_SAMPLES];

// helper to get the current write head of the DMA
static FORCE_INLINE uint16_t swo_dma_wx()
{
	return SWO_DMA_EDGE_SAMPLES - DMA_CNDTR(SWO_DMA_BUS, SWO_DMA_EDGE_CHAN);
}

static FORCE_INLINE void swo_buffer_write(uint8_t b)
{
	uint16_t wx = swo_buffer_write_index;
	swo_buffer[wx++] = b;
	swo_buffer_write_index = wx & SWO_BUFFER_MASK;
}

static FORCE_INLINE void swo_buffer_push(void)
{
	// just trigger the IRQ and let it check if it makes sense to do more -
	// it will be tail-chained after the main ISR anyway so an additional
	// check now would be a waste
	NVIC_STIR = SWO_DMA_SW_IRQ;
}

// decoder state
static struct state {
	uint16_t rx, t, q;
	uint8_t s;
	int32_t b;
} swo_s;

// Manchester decoder states
enum {
	ST_IDLE,		// line idle
	ST_INIT,		// line high before initial half-bit
	ST_BITL,		// line low at bit boundary
	ST_BITH,		// line high at bit boundary
	ST_MIDL,		// line low at mid-bit
	ST_MIDH,		// line high at mid-bit
	ST_INIL,		// line low after init (does not count for output)
#if SWO_ADVANCED_RECOVERY
	ST_RESH,		// line high before idle (special recovery state)
#endif
};

/*
 * Initializes and starts the decoder
 */
void swo_manchester_init(void)
{
	// enable required peripherals
	SWO_TIM_CLK_EN();
	rcc_periph_clock_enable(SWO_DMA_CLK);

	// eliminate glitches shorter than 16 clocks
	// this limits maximum pulse frequency to ~4.5 MHz,
	// increasing resiliency of the input against noise
	timer_ic_set_filter(SWO_TIM, TIM_IC1, TIM_IC_DTF_DIV_2_N_8);

	// slave trigger on all TI1 edges (trigger start functionality not used, 
	// this just to enable the TRC event)
	timer_slave_set_trigger(SWO_TIM, TIM_SMCR_TS_TI1F_ED);
	timer_slave_set_mode(SWO_TIM, TIM_SMCR_SMS_TM);

	// capture all edges using CH4
	timer_ic_set_input(SWO_TIM, TIM_IC4, TIM_IC_IN_TRC);
	timer_ic_set_polarity(SWO_TIM, TIM_IC4, TIM_IC_RISING);
	timer_ic_enable(SWO_TIM, TIM_IC4);

	// use CH3 for a mid-cycle timeout to make sure idle periods are not missed
	timer_set_oc_value(SWO_TIM, TIM_OC3, 0x8000);
	timer_ic_enable(SWO_TIM, TIM_IC3);

#if SWO_ADVANCED_RECOVERY
	// optionally capture exact rising/falling edges on CH1/2,
	// this is used only by the advanced recovery 
	timer_ic_set_input(SWO_TIM, TIM_IC1, TIM_IC_IN_TI1);
	timer_ic_set_input(SWO_TIM, TIM_IC2, TIM_IC_IN_TI1);
	timer_ic_set_polarity(SWO_TIM, TIM_IC1, TIM_IC_RISING);
	timer_ic_set_polarity(SWO_TIM, TIM_IC2, TIM_IC_FALLING);

	timer_ic_enable(SWO_TIM, TIM_IC1);
	timer_ic_enable(SWO_TIM, TIM_IC2);
#endif

	// interrupt fires twice pre timer cycle (CH3 and UPDATE)
	// also enable DMA from CH4
	timer_enable_irq(SWO_TIM, TIM_DIER_UIE | TIM_DIER_CC3IE | TIM_DIER_CC4DE);

	// configure DMA to read edge times into a circular buffer
	dma_channel_reset(SWO_DMA_BUS, SWO_DMA_EDGE_CHAN);

	dma_set_read_from_peripheral(SWO_DMA_BUS, SWO_DMA_EDGE_CHAN);
	dma_set_peripheral_size(SWO_DMA_BUS, SWO_DMA_EDGE_CHAN, DMA_CCR_PSIZE_16BIT);
	dma_set_memory_size(SWO_DMA_BUS, SWO_DMA_EDGE_CHAN, DMA_CCR_MSIZE_16BIT);
	dma_set_priority(SWO_DMA_BUS, SWO_DMA_EDGE_CHAN, DMA_CCR_PL_HIGH);

	dma_set_peripheral_address(SWO_DMA_BUS, SWO_DMA_EDGE_CHAN, (uint32_t)&TIM_CCR4(SWO_TIM));
	dma_set_memory_address(SWO_DMA_BUS, SWO_DMA_EDGE_CHAN, (uint32_t)swo_dma);
	dma_set_number_of_data(SWO_DMA_BUS, SWO_DMA_EDGE_CHAN, SWO_DMA_EDGE_SAMPLES);
	dma_enable_circular_mode(SWO_DMA_BUS, SWO_DMA_EDGE_CHAN);
	dma_enable_memory_increment_mode(SWO_DMA_BUS, SWO_DMA_EDGE_CHAN);
	dma_enable_channel(SWO_DMA_BUS, SWO_DMA_EDGE_CHAN);

	// enable two DMA interrupts per buffer cycle
	dma_enable_half_transfer_interrupt(SWO_DMA_BUS, SWO_DMA_EDGE_CHAN);
	dma_enable_transfer_complete_interrupt(SWO_DMA_BUS, SWO_DMA_EDGE_CHAN);

	// enable DMA interrupt
	nvic_set_priority(SWO_DMA_EDGE_IRQ, IRQ_PRI_SWO_DMA);
	nvic_enable_irq(SWO_DMA_EDGE_IRQ);

	// enable timer interrupt
	nvic_set_priority(SWO_TIM_IRQ, IRQ_PRI_SWO_TIM);
	nvic_enable_irq(SWO_TIM_IRQ);

	// extra interrupt used for outbound data processing, triggered via NVIC
	// runs at USB priority to avoid preempting with the regular drain callback
	nvic_set_priority(SWO_DMA_SW_IRQ, IRQ_PRI_USB);
	nvic_enable_irq(SWO_DMA_SW_IRQ);

	// start the engine
	timer_enable_counter(SWO_TIM);
}

/*
 * Stops the SWO capture
 */
void swo_manchester_deinit(void)
{
	timer_disable_counter(SWO_TIM);
	dma_disable_channel(SWO_DMA_BUS, SWO_DMA_EDGE_CHAN);
	timer_slave_set_mode(SWO_TIM, TIM_SMCR_SMS_OFF);

	// we can leave the rest of the peripheral configuration alone, just
	// make sure the restarts in a known state
	swo_s.s = ST_IDLE;
	swo_s.rx = 0;
}

/*
 * Dedicated handler for processing and outputting trace data
 * Note the swo_send_buffer is also a callback for the USB SWO_ENDPOINT
 * that gets called from the main USB ISR, so this one must have the
 * same priority to avoid accidental preemption
 */
void SWO_DMA_SW_ISR(void)
{
	// proceed only if there is a chance to send more data
	if ((*USB_EP_REG(SWO_ENDPOINT) & USB_EP_TX_STAT) != USB_EP_TX_STAT_VALID)
	{
		swo_send_buffer(usbdev, SWO_ENDPOINT);
	}
}

/*
 * This is the main Manchester input decoder
 *
 * For debugging, SWO_DIAG_ISR may be defined, containing a bitmask
 * for optional diagnostic output instead of regular SWO output
 * 
 * The following events are defined:
 *   1 - output DMA trigger events as [status]
 *   2 - output TIM trigger events as <status>
 *   4 - output processing results as {nbits}
 *   8 - original original decoded bytes in addition to diagnostic events
 *       (this is normally suppressed whenever SWO_DIAG_ISR is defined)
 *  16 - output repeating hex digits instead of actual decoded bytes
 *       (this an be used to identify decoding overflow vs USB overflow)
 *  32 - output symbols indicating polarity and length of each pulse
 */

//#define SWO_DIAG_ISR	(4 | 8)

// enable assembly optimizations
#define SWO_ASM_OPTIMIZATIONS	1

static FORCE_INLINE void swo_diag_nibble(uint32_t v)
{
	swo_buffer_write("0123456789ABCDEF"[v & 0xF]);
}

static inline void swo_diag_hex(uint32_t v)
{
	// CLZ >> 2 = number of leading zero nibbles which we want to skip,
	// so n is number of output nibbles
	unsigned n = v ? 8 - (__builtin_clz(v) >> 2) : 1;
	while (n--) {
		swo_diag_nibble(v >> (n << 2));
	}
}

/*
 * DMA ISR called twice per buffer, it does nothing, just clears the interrupt
 * flags and initiates a tail-chained TIM ISR which handles all the processing
 */
void SWO_DMA_EDGE_ISR(void)
{
	uint32_t status = DMA_ISR(SWO_DMA_BUS) & DMA_ISR_MASK(SWO_DMA_EDGE_CHAN);
	DMA1_IFCR = status;

#if SWO_DIAG_ISR & 1
	swo_buffer_write('[');
	swo_diag_hex(status >> DMA_FLAG_OFFSET(SWO_DMA_EDGE_CHAN));
	swo_buffer_write(']');
	swo_buffer_push();
#endif

	NVIC_STIR = SWO_TIM_IRQ;
}

/*
 * Main edge-to-data processing ISR
 */
void SWO_TIM_ISR(void)
{
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
#if SWO_ADVANCED_RECOVERY
		// ST_RESH (recovery)
		{ ST_IDLE, ST_IDLE },
#endif
	};

	// clear all interrupts, we don't care about details at all
	uint32_t status = TIM_SR(SWO_TIM);
	TIM_SR(SWO_TIM) = ~status;

#if SWO_DIAG_ISR & 2
	swo_buffer_write('<');
	swo_diag_nibble(status >> 9);
	swo_diag_nibble(status >> 1);
	if (status & TIM_SR_TIF)
	{
		swo_buffer_write('T');
	}
	if (status & TIM_SR_UIF)
	{
		swo_buffer_write('U');
	}
	swo_buffer_write('>');
#endif

	// do not work with the state in RAM directly, it has to be loaded
	// into variables to allow the compiler to use them as registers
	// in the critical loop
	unsigned rx = swo_s.rx;	// read index
	unsigned s = swo_s.s;		// state
	uint16_t t = swo_s.t;		// last edge time
	uint16_t p;				// pulse time

	// number of samples available in the buffer
	unsigned avail = (swo_dma_wx() - rx) & SWO_DMA_EDGE_MASK;

	if (!avail)
	{
		// no data available
		if (s != ST_IDLE)
		{
			// if the state machine is still running, use current count to measure time elapsed since the last pulse
			// if enough time has elapsed, reset it, there is not much else we can do...
			p = TIM_CNT(SWO_TIM) - t;
			if (p >= SWO_MAX_PULSE)
			{
				// modify the state in RAM directly
				swo_s.s = ST_IDLE;
				swo_s.q = 0;
			}
		}
		
		// this is a good time to push out any unflushed bytes the 32-bit buffer
#if !SWO_DIAG_ISR || (SWO_DIAG_ISR & (8 | 16))
		uint32_t b = swo_s.b;
		if (b)	// b must not be zero, it would make the bitcount negative
		{
			// 31 - CTZ(b) == number of bits shifted into the buffer
			unsigned bits = 31 - __builtin_ctz(b);
			// full bytes and unaligned (yet unsent) bits
			unsigned bytes = bits >> 3;
			unsigned unaligned = bits & 7;
			// keep just the remaining bits in the register, writing 
			uint32_t terminator = 1u << 31 >> unaligned;
			swo_s.b = (b | terminator) & ~(terminator - 1);
			// extract the full bytes to be sent so they are aligned at LSB
			b = ~(b >> (32 - bits));

			unsigned wx = swo_buffer_write_index;
			while (bytes--)
			{
#if SWO_DIAG_ISR & 16
				swo_diag_nibble(swo_buf_wx);
#else
				swo_buffer[wx++ & SWO_BUFFER_MASK] = b;
#endif
				b >>= 8;
			}
			swo_buffer_write_index = wx & SWO_BUFFER_MASK;
		}
#endif
		
		// no need to go deeper, just trigger USB processing
		swo_buffer_push();
		return;
	}

	// load the remainder of the state
	uint16_t q = swo_s.q;	// 3/4 of bit time for differentiating between short and long pulses
	// bit buffer for 32 bits
	// bits are shifted in from the top since they are incoming LSB first
	// initialized to 1 << 31 so that when the init bit is shifted out, we know
	// the buffer is full
	uint32_t b = swo_s.b;

	unsigned n = 0;

	// inner processing loop - this has to be as fast as possible, every clock
	// counts - for example, even enabling SWO_ADVANCED_RECOVERY reduces
	// the maximum processable frequency to ~1 MHz
	while (avail--)
	{
		p = swo_dma[rx++] - t;
		rx &= SWO_DMA_EDGE_MASK;
		t += p;
		n++;

		if (p >= SWO_MAX_PULSE)
		{
#if SWO_DIAG_ISR & 32
			swo_buffer_write('!');
#endif
			s = ST_INIT;
			q = 0;
			continue;
		}

#if SWO_ADVANCED_RECOVERY
		if (q && (p < q / 2 || p > q * 2))
		{
			// invalid pulse length, try to recover by dropping all data 
			// and initializing according to current input polarity
			// determined by comparing the last capture times of CH1 and CH2
			q = 0;
			rx = swo_dma_wx();
			if ((int16_t)(TIM_CCR1(SWO_TIM) - TIM_CCR2(SWO_TIM)) > 0)
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
		
#if SWO_ASM_OPTIMIZATIONS
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

#if SWO_DIAG_ISR & 32
		swo_buffer_write(
			p < MIN_PULSE ? '?' :
			p >= MAX_PULSE ? '_' :
			((s & 1) ? 'A' : 'a') + (p >> 7)
			);
#endif

#if SWO_DIAG_ISR & 64
		swo_buffer_write("_IbBxXiR"[s]);
#endif

		// short-circuit for states requiring no extra action
		if (s < ST_MIDL) { continue; }

		// handle states requiring extra actions, primarily bit writing
#if SWO_ASM_OPTIMIZATIONS
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
			//swo_buffer_write(swo_buf_wx);
#if !SWO_DIAG_ISR || (SWO_DIAG_ISR & 8)
			b = ~b;
			uint32_t wx = swo_buffer_write_index;
			uint8_t* p = swo_buffer;
			if (wx + 4 <= SWO_BUFFER_SIZE)
			{
				// single write
				*(uint32_t*)&p[wx] = b;
				wx += 4;
			}
			else
			{
				// must split
				p[wx++ & SWO_BUFFER_MASK] = b;
				b >>= 8;
				p[wx++ & SWO_BUFFER_MASK] = b;
				b >>= 8;
				p[wx++ & SWO_BUFFER_MASK] = b;
				b >>= 8;
				p[wx++ & SWO_BUFFER_MASK] = b;
			}
			swo_buffer_write_index = wx & SWO_BUFFER_MASK;
#endif
#if SWO_DIAG_ISR & 16
			for (int i = 0; i < 4; i++)
			{
				swo_diag_nibble(swo_buf_wx);
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

#if SWO_DIAG_ISR & 4
	swo_buffer_write('{');
	swo_diag_hex(n);
	swo_buffer_write('}');
#endif

	// push out any new data
	swo_buffer_push();

	// store the state for next run
	swo_s = (struct state){ rx, t, q, s, b };
}
