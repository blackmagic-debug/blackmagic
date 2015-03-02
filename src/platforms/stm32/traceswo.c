/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2012  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
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

/* This file implements capture of the TRACESWO output.
 *
 * ARM DDI 0403D - ARMv7M Architecture Reference Manual
 * ARM DDI 0337I - Cortex-M3 Technical Reference Manual
 * ARM DDI 0314H - CoreSight Components Technical Reference Manual
 */

/* TDO/TRACESWO signal comes into pin PA6/TIM3_CH1
 * Manchester coding is assumed on TRACESWO, so bit timing can be detected.
 * The idea is to use TIM3 input capture modes to capture pulse timings.
 * These can be capture directly to RAM by DMA.
 * The core can then process the buffer to extract the frame.
 */
#include "general.h"
#include "cdcacm.h"

#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/timer.h>
#include <libopencm3/stm32/f1/rcc.h>

void traceswo_init(void)
{
	TRACE_TIM_CLK_EN();

	timer_reset(TRACE_TIM);

	/* Refer to ST doc RM0008 - STM32F10xx Reference Manual.
	 * Section 14.3.4 - 14.3.6 (General Purpose Timer - Input Capture)
	 *
	 * CCR1 captures cycle time, CCR2 captures high time
	 */

	/* Use TI1 as capture input for CH1 and CH2 */
	timer_ic_set_input(TRACE_TIM, TIM_IC1, TIM_IC_IN_TI1);
	timer_ic_set_input(TRACE_TIM, TIM_IC2, TIM_IC_IN_TI1);

	/* Capture CH1 on rising edge, CH2 on falling edge */
	timer_ic_set_polarity(TRACE_TIM, TIM_IC1, TIM_IC_RISING);
	timer_ic_set_polarity(TRACE_TIM, TIM_IC2, TIM_IC_FALLING);

	/* Trigger on Filtered Timer Input 1 (TI1FP1) */
	timer_slave_set_trigger(TRACE_TIM, TIM_SMCR_TS_IT1FP1);

	/* Slave reset mode: reset counter on trigger */
	timer_slave_set_mode(TRACE_TIM, TIM_SMCR_SMS_RM);

	/* Enable capture interrupt */
	nvic_set_priority(TRACE_IRQ, IRQ_PRI_TRACE);
	nvic_enable_irq(TRACE_IRQ);
	timer_enable_irq(TRACE_TIM, TIM_DIER_CC1IE);

	/* Enable the capture channels */
	timer_ic_enable(TRACE_TIM, TIM_IC1);
	timer_ic_enable(TRACE_TIM, TIM_IC2);

	timer_enable_counter(TRACE_TIM);
}

static uint8_t trace_usb_buf[64];
static uint8_t trace_usb_buf_size;

void trace_buf_push(uint8_t *buf, int len)
{
	if (usbd_ep_write_packet(usbdev, 0x85, buf, len) != len) {
		if (trace_usb_buf_size + len > 64) {
			/* Stall if upstream to too slow. */
			usbd_ep_stall_set(usbdev, 0x85, 1);
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

	usbd_ep_write_packet(dev, ep, trace_usb_buf, trace_usb_buf_size);
	trace_usb_buf_size = 0;
}

#define ALLOWED_DUTY_ERROR 5

void TRACE_ISR(void)
{
	uint16_t sr = TIM_SR(TRACE_TIM);
	uint16_t duty, cycle;
	static uint16_t bt;
	static uint8_t lastbit;
	static uint8_t decbuf[17];
	static uint8_t decbuf_pos;
	static uint8_t halfbit;
	static uint8_t notstart;

	/* Reset decoder state if capture overflowed */
	if (sr & (TIM_SR_CC1OF | TIM_SR_UIF)) {
		timer_clear_flag(TRACE_TIM, TIM_SR_CC1OF | TIM_SR_UIF);
		if (!(sr & (TIM_SR_CC2IF | TIM_SR_CC1IF)))
			goto flush_and_reset;
	}

	cycle = TIM_CCR1(TRACE_TIM);
	duty = TIM_CCR2(TRACE_TIM);

	/* Reset decoder state if crazy shit happened */
	if ((bt && (((duty / bt) > 2) || ((duty / bt) == 0))) || (duty == 0))
		goto flush_and_reset;

	if(!(sr & TIM_SR_CC1IF)) notstart = 1;

	if (!bt) {
		if (notstart) {
			notstart = 0;
			return;
		}
		/* First bit, sync decoder */
		duty -= ALLOWED_DUTY_ERROR;
		if (((cycle / duty) != 2) &&
		    ((cycle / duty) != 3))
			return;
		bt = duty;
		lastbit = 1;
		halfbit = 0;
		timer_set_period(TRACE_TIM, duty * 6);
		timer_clear_flag(TRACE_TIM, TIM_SR_UIF);
		timer_enable_irq(TRACE_TIM, TIM_DIER_UIE);
	} else {
		/* If high time is extended we need to flip the bit */
		if ((duty / bt) > 1) {
			if (!halfbit) /* lost sync somehow */
				goto flush_and_reset;
			halfbit = 0;
			lastbit ^= 1;
		}
		decbuf[decbuf_pos >> 3] |= lastbit << (decbuf_pos & 7);
		decbuf_pos++;
	}

	if (!(sr & TIM_SR_CC1IF) || (((cycle - duty) / bt) > 2))
		goto flush_and_reset;

	if (((cycle - duty) / bt) > 1) {
		/* If low time extended we need to pack another bit. */
		if (halfbit) /* this is a valid stop-bit or we lost sync */
			goto flush_and_reset;
		halfbit = 1;
		lastbit ^= 1;
		decbuf[decbuf_pos >> 3] |= lastbit << (decbuf_pos & 7);
		decbuf_pos++;
	}

	if (decbuf_pos < 128)
		return;

flush_and_reset:
	timer_set_period(TRACE_TIM, -1);
	timer_disable_irq(TRACE_TIM, TIM_DIER_UIE);
	trace_buf_push(decbuf, decbuf_pos >> 3);
	bt = 0;
	decbuf_pos = 0;
	memset(decbuf, 0, sizeof(decbuf));
}
