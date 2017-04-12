/*
 * This file is part of the Black Magic Debug project.
 *
 * Based on work that is Copyright (C) 2017 Black Sphere Technologies Ltd.
 * Copyright (C) 2017 Dave Marples <dave@marples.net>
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

/* This file implements capture of the TRACESWO output using ASYNC signalling.
 *
 * ARM DDI 0403D - ARMv7M Architecture Reference Manual
 * ARM DDI 0337I - Cortex-M3 Technical Reference Manual
 * ARM DDI 0314H - CoreSight Components Technical Reference Manual
 */

/* TDO/TRACESWO signal comes into the SWOUSART RX pin.
 */

#include "general.h"
#include "cdcacm.h"
#include "platform.h"

#ifdef TRACESWO_ASYNC

#include <libopencmsis/core_cm3.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/timer.h>
#include <libopencm3/stm32/f1/rcc.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/stm32/dma.h>

#define FULL_SWO_PACKET    (64)                             /* For speed this is set to the USB transfer size */
#define NUM_PACKETS        (192)                            /* This is an 12K buffer */

/* Default line rate....this is only used for setting the UART speed if we get a request without speed set */
#define DEFAULTSPEED       (2250000)

static volatile uint32_t w;                                 /* Which packet we are currently getting */
static volatile uint32_t r;                                 /* Which packet we are currently waiting to transmit to USB */
static uint8_t trace_rx_buf[NUM_PACKETS*FULL_SWO_PACKET];   /* Packet arriving from the SWO interface */



void trace_buf_drain(usbd_device *dev, uint8_t ep)

{
    static volatile char inBufDrain;

    /* If we are already in this routine then we don't need to come in again */
    if (__atomic_test_and_set (&inBufDrain, __ATOMIC_RELAXED))
        return;

    /* Attempt to write everything we buffered */
  if ((w!=r) && (usbd_ep_write_packet(dev, ep, &trace_rx_buf[r*FULL_SWO_PACKET], FULL_SWO_PACKET)))
      {
          r=(r+1)%NUM_PACKETS;
      }
  __atomic_clear (&inBufDrain, __ATOMIC_RELAXED);
}


void traceswo_setspeed(uint32_t speed)
{
  dma_disable_channel(SWODMABUS, SWDDMACHAN);
  usart_disable(SWOUSART);
  usart_set_baudrate(SWOUSART, speed);
  usart_set_databits(SWOUSART, 8);
  usart_set_stopbits(SWOUSART, USART_STOPBITS_1);
  usart_set_mode(SWOUSART, USART_MODE_RX);
  usart_set_parity(SWOUSART, USART_PARITY_NONE);
  usart_set_flow_control(SWOUSART, USART_FLOWCONTROL_NONE);

  /* Set up DMA channel*/
  dma_channel_reset(SWODMABUS, SWDDMACHAN);
  dma_set_peripheral_address(SWODMABUS, SWDDMACHAN, (uint32_t)&SWOUSARTDR);
  dma_set_read_from_peripheral(SWODMABUS, SWDDMACHAN);
  dma_enable_memory_increment_mode(SWODMABUS, SWDDMACHAN);
  dma_set_peripheral_size(SWODMABUS, SWDDMACHAN, DMA_CCR_PSIZE_8BIT);
  dma_set_memory_size(SWODMABUS, SWDDMACHAN, DMA_CCR_MSIZE_8BIT);
  dma_set_priority(SWODMABUS, SWDDMACHAN, DMA_CCR_PL_HIGH);
  dma_enable_transfer_complete_interrupt(SWODMABUS, SWDDMACHAN);
  usart_enable(SWOUSART);
  nvic_enable_irq(SWODMAIRQ);
  w=r=0;
  dma_set_memory_address(SWODMABUS, SWDDMACHAN, (uint32_t)trace_rx_buf);
  dma_set_number_of_data(SWODMABUS, SWDDMACHAN, FULL_SWO_PACKET);
  dma_enable_channel(SWODMABUS, SWDDMACHAN);
  usart_enable_rx_dma(SWOUSART);
}

void dma1_channel5_isr(void)

{
    if (DMA1_ISR & DMA_ISR_TCIF5)
        {
            w=(w+1)%NUM_PACKETS;
            dma_disable_channel(SWODMABUS, SWDDMACHAN);
            dma_set_memory_address(SWODMABUS, SWDDMACHAN, (uint32_t)(&trace_rx_buf[w*FULL_SWO_PACKET]));
            dma_set_number_of_data(SWODMABUS, SWDDMACHAN, FULL_SWO_PACKET);
            dma_enable_channel(SWODMABUS, SWDDMACHAN);
            usart_enable_rx_dma(SWOUSART);
        }
    DMA1_IFCR |= DMA_ISR_GIF5;
    trace_buf_drain(usbdev,0x85);
}

void traceswo_init(uint32_t speed)
{
    if (!speed)
        speed=DEFAULTSPEED;

  rcc_periph_clock_enable(SWOUSART_CLK);
  rcc_periph_clock_enable(RCC_DMA1);

  SWO_PIN_SETUP();
  nvic_set_priority(SWODMAIRQ, IRQ_PRI_SWODMA);
  nvic_enable_irq(SWODMAIRQ);
  traceswo_setspeed(speed);
}
#endif
