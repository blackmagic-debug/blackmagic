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

/* This file implements capture of the TRACESWO output.
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

#include <libopencmsis/core_cm3.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/timer.h>
#include <libopencm3/stm32/f1/rcc.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/stm32/dma.h>

/* For speed the USB_BUF_SIZE is a multiple of the SWO packet and USB transfer size */
#define TRACE_USB_BUF_SIZE (512)
#define FULL_SWO_PACKET    (64)

#define DEFAULTSPEED       (4500000)

/* Packets waiting to be sent to the USB interface */
static uint8_t trace_usb_buf[TRACE_USB_BUF_SIZE];
static uint32_t tb_wp;
static uint32_t tb_rp;

/* Packet arriving from the SWO interface */
static uint8_t trace_rx_buf[2*FULL_SWO_PACKET];

void trace_buf_drain(usbd_device *dev, uint8_t ep)
{
  if (tb_wp==tb_rp)
    return;

  /* Attempt to write everything we buffered */
  if (usbd_ep_write_packet(dev, ep, &trace_usb_buf[tb_rp], 64))
      tb_rp=(tb_rp+64)%TRACE_USB_BUF_SIZE;
}

static void dma_read(char *data, int size)
{
    /* Reset DMA channel*/
    dma_channel_reset(SWODMABUS, SWDDMACHAN);
    dma_set_peripheral_address(SWODMABUS, SWDDMACHAN, (uint32_t)&SWOUSARTDR);
    dma_set_memory_address(SWODMABUS, SWDDMACHAN, (uint32_t)data);
    dma_set_number_of_data(SWODMABUS, SWDDMACHAN, size);
    dma_set_read_from_peripheral(SWODMABUS, SWDDMACHAN);
    dma_enable_memory_increment_mode(SWODMABUS, SWDDMACHAN);
    dma_enable_circular_mode(SWODMABUS, SWDDMACHAN);
    dma_set_peripheral_size(SWODMABUS, SWDDMACHAN, DMA_CCR_PSIZE_8BIT);
    dma_set_memory_size(SWODMABUS, SWDDMACHAN, DMA_CCR_MSIZE_8BIT);
    dma_set_priority(SWODMABUS, SWDDMACHAN, DMA_CCR_PL_HIGH);
    dma_enable_half_transfer_interrupt(SWODMABUS, SWDDMACHAN);
    dma_enable_transfer_complete_interrupt(SWODMABUS, SWDDMACHAN);
    dma_enable_channel(SWODMABUS, SWDDMACHAN);
    usart_enable(SWOUSART);
    usart_enable_rx_dma(SWOUSART);
    nvic_enable_irq(SWODMAIRQ);
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
  dma_read((char *)trace_rx_buf, 2*FULL_SWO_PACKET);
}


void dma1_channel5_isr(void)

{
    /* If the buffer has overrun there's not much we can do about it, so don't bother ... it takes time to check */
    while (DMA1_ISR & (DMA_ISR_HTIF5 | DMA_ISR_TCIF5))
        {
            if (DMA1_ISR & DMA_ISR_HTIF5)
                {
                    tb_wp=(tb_wp+1)%TRACE_USB_BUF_SIZE;
                    memcpy(&trace_usb_buf[tb_wp],trace_rx_buf,FULL_SWO_PACKET);
                    tb_wp+=(FULL_SWO_PACKET-1);
                    DMA1_IFCR |= DMA_ISR_HTIF5;
                }

            if (DMA1_ISR & DMA_ISR_TCIF5)
                {
                    tb_wp=(tb_wp+1)%TRACE_USB_BUF_SIZE;
                    memcpy(&trace_usb_buf[tb_wp],&(trace_rx_buf[FULL_SWO_PACKET]),FULL_SWO_PACKET);
                    tb_wp+=(FULL_SWO_PACKET-1);
                    DMA1_IFCR |= DMA_ISR_TCIF5;
                }
        }
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
