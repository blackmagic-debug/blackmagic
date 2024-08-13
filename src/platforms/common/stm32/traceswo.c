/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2012 Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 * Modified by Uwe Bonnes <bon@elektron.ikp.physik.tu-darmstadt.de>
 * Copyright (C) 2024 1BitSquared <info@1bitsquared.com>
 * Modified by Rachel Mant <git@dragonmux.network>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * This file implements capture of Machester SWO trace output
 *
 * References:
 * DDI0403 - ARMv7-M Architecture Reference Manual, version E.e
 * - https://developer.arm.com/documentation/ddi0403/latest/
 * DDI0314 - CoreSight Components Technical Reference Manual, version 1.0, rev. H
 * - https://developer.arm.com/documentation/ddi0314/latest/
 *
 * The basic idea is that SWO comes in on a pin connected to a timer block,
 * and because Manchester coding is self-clocking we can determine the timing
 * for that input signal when it's active, so: use the timer to capture edge
 * transition timings; fire an interrupt each complete cycle; and then use some
 * timing analysis on the CPU to extract the SWO data sequence.
 *
 * We use the first capture channel of a pair to capture the cycle time and
 * thee second to capture the high time (mark period).
 */

#include "general.h"
#include "platform.h"
#include "usb.h"
#include "traceswo.h"

#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/timer.h>
#include <libopencm3/stm32/rcc.h>

/* SWO decoding */
static bool decoding = false;

static uint8_t trace_usb_buf[64];
static uint8_t trace_usb_buf_size;

void traceswo_init(const uint32_t swo_chan_bitmask)
{
	/* Make sure the timer block is clocked on platforms that don't do this in their `platform_init()` */
	TRACE_TIM_CLK_EN();
	/* Then make sure the IO pin used is properly set up as an input routed to the timer */
	gpio_set_mode(SWO_PORT, GPIO_MODE_INPUT, GPIO_CNF_INPUT_FLOAT, SWO_PIN);

	/*
	 * Start setting the timer block up by picking a pair of cross-linked capture channels suitable for the input,
	 * and configure them to consume the input channel for the SWO pin. We use one in rising edge mode and the
	 * other in falling to get the mark period and cycle period - together these define all elements of a wiggle.
	 * NB: "TRACE_IC" here refers to the Input Capture channels being used
	 */
	timer_ic_set_input(TRACE_TIM, TRACE_IC_RISING, TRACE_IC_IN);
	timer_ic_set_polarity(TRACE_TIM, TRACE_IC_RISING, TIM_IC_RISING);
	timer_ic_set_input(TRACE_TIM, TRACE_IC_FALLING, TRACE_IC_IN);
	timer_ic_set_polarity(TRACE_TIM, TRACE_IC_FALLING, TIM_IC_FALLING);

	/*
	 * Use reset mode to trigger the timer, which makes the counter reset and start counting anew
	 * when a rising edge is detected on the input pin via the filtered input channel as a trigger source
	 */
	timer_slave_set_trigger(TRACE_TIM, TRACE_TRIG_IN);
	timer_slave_set_mode(TRACE_TIM, TIM_SMCR_SMS_RM);

	/* Enable capture interrupt */
	nvic_set_priority(TRACE_IRQ, IRQ_PRI_TRACE);
	nvic_enable_irq(TRACE_IRQ);
	timer_enable_irq(TRACE_TIM, TIM_DIER_CC1IE);

	/* Enable the capture channels */
	timer_ic_enable(TRACE_TIM, TRACE_IC_RISING);
	timer_ic_enable(TRACE_TIM, TRACE_IC_FALLING);

	timer_enable_counter(TRACE_TIM);

#if defined(STM32F4) || defined(STM32F0) || defined(STM32F3)
	/* AF2: TIM3/TIM4/TIM5 */
	gpio_mode_setup(TDO_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, TDO_PIN);
	gpio_set_af(TDO_PORT, TRACE_TIM_PIN_AF, TDO_PIN);
#endif

	traceswo_setmask(swo_chan_bitmask);
	decoding = swo_chan_bitmask != 0;
}

void traceswo_deinit(void)
{
	timer_disable_counter(TRACE_TIM);
}

void trace_buf_push(uint8_t *buf, int len)
{
	if (decoding)
		traceswo_decode(usbdev, CDCACM_UART_ENDPOINT, buf, len);
	else if (usbd_ep_write_packet(usbdev, USB_REQ_TYPE_IN | TRACE_ENDPOINT, buf, len) != len) {
		if (trace_usb_buf_size + len > 64) {
			/* Stall if upstream to too slow. */
			usbd_ep_stall_set(usbdev, USB_REQ_TYPE_IN | TRACE_ENDPOINT, 1);
			trace_usb_buf_size = 0;
			return;
		}
		memcpy(trace_usb_buf + trace_usb_buf_size, buf, len);
		trace_usb_buf_size += len;
	}
}

void trace_buf_drain(usbd_device *dev, uint8_t ep)
{
	if (!trace_usb_buf_size)
		return;

	if (decoding)
		traceswo_decode(dev, CDCACM_UART_ENDPOINT, trace_usb_buf, trace_usb_buf_size);
	else
		usbd_ep_write_packet(dev, ep, trace_usb_buf, trace_usb_buf_size);
	trace_usb_buf_size = 0;
}

#define ALLOWED_DUTY_ERROR 5

void TRACE_ISR(void)
{
	uint16_t status = TIM_SR(TRACE_TIM);
	static uint16_t bt;
	static uint8_t lastbit;
	static uint8_t decbuf[17];
	static uint8_t decbuf_pos;
	static uint8_t halfbit;
	static uint8_t notstart;

	/* Reset decoder state if capture overflowed */
	if (status & (TIM_SR_CC1OF | TIM_SR_UIF)) {
		timer_clear_flag(TRACE_TIM, TIM_SR_CC1OF | TIM_SR_UIF);
		if (!(status & (TIM_SR_CC2IF | TIM_SR_CC1IF)))
			goto flush_and_reset;
	}

	const uint16_t cycle = TIM_CCR1(TRACE_TIM);
	uint16_t duty = TIM_CCR2(TRACE_TIM);

	/* Reset decoder state if crazy things happened */
	if ((bt && (duty / bt > 2U || duty / bt == 0)) || duty == 0)
		goto flush_and_reset;

	if (!(status & TIM_SR_CC1IF))
		notstart = 1;

	if (!bt) {
		if (notstart) {
			notstart = 0;
			return;
		}
		/* First bit, sync decoder */
		duty -= ALLOWED_DUTY_ERROR;
		const uint16_t duty_cycle = cycle / duty;
		if (duty_cycle != 2U && duty_cycle != 3U)
			return;
		bt = duty;
		lastbit = 1;
		halfbit = 0;
		timer_set_period(TRACE_TIM, duty * 6U);
		timer_clear_flag(TRACE_TIM, TIM_SR_UIF);
		timer_enable_irq(TRACE_TIM, TIM_DIER_UIE);
	} else {
		/* If high time is extended we need to flip the bit */
		if (duty / bt > 1U) {
			if (!halfbit) /* lost sync somehow */
				goto flush_and_reset;
			halfbit = 0;
			lastbit ^= 1U;
		}
		decbuf[decbuf_pos >> 3U] |= lastbit << (decbuf_pos & 7U);
		++decbuf_pos;
	}

	if (!(status & TIM_SR_CC1IF) || (cycle - duty) / bt > 2)
		goto flush_and_reset;

	if ((cycle - duty) / bt > 1) {
		/* If low time extended we need to pack another bit. */
		if (halfbit) /* this is a valid stop-bit or we lost sync */
			goto flush_and_reset;
		halfbit = 1;
		lastbit ^= 1U;
		decbuf[decbuf_pos >> 3U] |= lastbit << (decbuf_pos & 7U);
		++decbuf_pos;
	}

	if (decbuf_pos < 128U)
		return;

flush_and_reset:
	timer_set_period(TRACE_TIM, -1);
	timer_disable_irq(TRACE_TIM, TIM_DIER_UIE);
	trace_buf_push(decbuf, decbuf_pos >> 3U);
	bt = 0;
	decbuf_pos = 0;
	memset(decbuf, 0, sizeof(decbuf));
}
