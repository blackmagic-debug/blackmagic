/*
 * This file is part of the libopencm3 project.
 *
 * Copyright (C) 2011 Gareth McMullin <gareth@blacksphere.co.nz>
 * Portions (C) 2020 Stoyan Shopov <stoyan.shopov@gmail.com>
 *
 * This library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <string.h>
#include <libopencm3/cm3/common.h>
#include <libopencm3/stm32/tools.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/dwc/otg_hs.h>
#include "usb_private.h"
#include "usb_dwc_common.h"

/*
 * These are some register definitions, not yet provided by libopencm3,
 * or definitions that need hacking in order to use here.
 */
/* ??? This bit is present in the st header files, but I could not find it described in the documentation. */
#define OTG_GCCFG_PHYHSEN		(1 << 23)
#define OTG_PHYC_LDO_DISABLE		(1 << 2)
#define OTG_PHYC_LDO_STATUS		(1 << 1)

/* USB PHY controller registers. */
#define USBPHYC_BASE			0x40017C00
#define OTG_HS_PHYC_PLL1		MMIO32(USBPHYC_BASE + 0)

#define OTG_PHYC_PLL1_ENABLE		1

#define OTG_HS_PHYC_TUNE		MMIO32(USBPHYC_BASE + 0xc)
#define OTG_HS_PHYC_LDO			MMIO32(USBPHYC_BASE + 0x18)
/* ??? The st header files have this:
 * #define USB_HS_PHYC_LDO_ENABLE                   USB_HS_PHYC_LDO_DISABLE
 * ...go figure...
 */
#define OTG_PHYC_LDO_DISABLE		(1 << 2)
#define OTG_PHYC_LDO_STATUS		(1 << 1)


/* Yes, this is unpleasant. It does not belong here. */
#define _REG_BIT(base, bit)             (((base) << 5) + (bit))
/* STM32F7x3xx and STM32F730xx devices have an internal usb high-speed usb phy controller */
enum
{
	RCC_OTGPHYC	= _REG_BIT(0x44, 31),
};

/* Receive FIFO size in 32-bit words. */
#define RX_FIFO_SIZE 512

static usbd_device *stm32f723_usbd_init(void);

static struct _usbd_device usbd_dev;

const struct _usbd_driver stm32f723_usb_driver = {
	.init = stm32f723_usbd_init,
	.set_address = dwc_set_address,
	.ep_setup = dwc_ep_setup,
	.ep_reset = dwc_endpoints_reset,
	.ep_stall_set = dwc_ep_stall_set,
	.ep_stall_get = dwc_ep_stall_get,
	.ep_nak_set = dwc_ep_nak_set,
	.ep_write_packet = dwc_ep_write_packet,
	.ep_read_packet = dwc_ep_read_packet,
	.poll = dwc_poll,
	.disconnect = dwc_disconnect,
	.base_address = USB_OTG_HS_BASE,
	.set_address_before_status = 1,
	.rx_fifo_size = RX_FIFO_SIZE,
};

/*
 * Initialize the USB device controller hardware of the STM32.
 *
 * Note: this initialization code was compiled from the libopencm3 usb_f207.c
 * source, and the st cube sources. Note that the st manuals state that some delays
 * are necessary at certain places. This code works for usb hosts that I tested on
 * without the delays, but this is bending the rules.
 *
 * If for someone the code below does not work, one thing to try is perhaps to
 * enable the delays before starting to debug other stuff. */
static usbd_device *stm32f723_usbd_init(void)
{
	rcc_periph_clock_enable(RCC_GPIOB);
	gpio_mode_setup(GPIOB, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO14 | GPIO15);
	gpio_set_output_options(GPIOB, GPIO_OTYPE_PP, GPIO_OSPEED_100MHZ, GPIO14 | GPIO15);
	gpio_set_af(GPIOB, GPIO_AF12, GPIO14 | GPIO15);

	rcc_periph_clock_enable((enum rcc_periph_clken) RCC_OTGPHYC);
	rcc_periph_clock_enable(RCC_OTGHSULPI);

	// TODO - check the preemption and subpriority, they are unified here
	nvic_set_priority(NVIC_OTG_HS_IRQ, 0);

	rcc_periph_clock_enable(RCC_OTGHS);
	OTG_HS_GINTSTS = OTG_GINTSTS_MMIS;

	// ??? What is this??? It is not documented in the manual
	OTG_HS_GCCFG |= OTG_GCCFG_PHYHSEN;

	/* ??? The st header files have this:
	 * #define USB_HS_PHYC_LDO_ENABLE                   USB_HS_PHYC_LDO_DISABLE
	 * ...go figure...
	 */
	OTG_HS_PHYC_LDO |= OTG_PHYC_LDO_DISABLE;
	while (!(OTG_HS_PHYC_LDO & OTG_PHYC_LDO_STATUS))
		;
	/* This setting is for a HSE clock of 25 MHz. */
	OTG_HS_PHYC_PLL1 = 5 << 1;
	OTG_HS_PHYC_TUNE |= 0x00000F13U;
	OTG_HS_PHYC_PLL1 |= OTG_PHYC_PLL1_ENABLE;
	/* 2ms Delay required to get internal phy clock stable
	 * Used by DFU too, so platform_xxx not available.
	 * Some Stlinkv3-Set did not cold start w/o the delay
	 */
	volatile int i = 200*1000;
	while(i--);

	////OTG_HS_GUSBCFG |= OTG_GUSBCFG_PHYSEL;
	/* Enable VBUS sensing in device mode and power down the PHY. */
	////OTG_HS_GCCFG |= OTG_GCCFG_VBUSBSEN | OTG_GCCFG_PWRDWN;

	/* Wait for AHB idle. */
	while (!(OTG_HS_GRSTCTL & OTG_GRSTCTL_AHBIDL));
	/* Do core soft reset. */
	OTG_HS_GRSTCTL |= OTG_GRSTCTL_CSRST;
	while (OTG_HS_GRSTCTL & OTG_GRSTCTL_CSRST);

	/* Force peripheral only mode. */
	OTG_HS_GUSBCFG |= OTG_GUSBCFG_FDMOD | OTG_GUSBCFG_TRDT_MASK;
	//HAL_Delay(50U);

	/* Full speed device. */
	////OTG_HS_DCFG |= OTG_DCFG_DSPD;

	/* Restart the PHY clock. */
	OTG_HS_PCGCCTL = 0;

	OTG_HS_GRXFSIZ = stm32f723_usb_driver.rx_fifo_size;
	usbd_dev.fifo_mem_top = stm32f723_usb_driver.rx_fifo_size;

	OTG_HS_DCTL |= OTG_DCTL_SDIS;
	//HAL_Delay(10);

	/* Unmask interrupts for TX and RX. */
	OTG_HS_GAHBCFG |= OTG_GAHBCFG_GINT;
	OTG_HS_GINTMSK = OTG_GINTMSK_ENUMDNEM |
			 OTG_GINTMSK_RXFLVLM |
			 OTG_GINTMSK_IEPINT |
			 OTG_GINTMSK_OEPINT |
			 //OTG_GINTMSK_USBRST |
			 OTG_GINTMSK_USBSUSPM |
			 OTG_GINTMSK_WUIM;

	OTG_HS_DAINTMSK = 0xffffffff;

	OTG_HS_DOEPMSK |= OTG_DOEPMSK_STUPM | OTG_DOEPMSK_XFRCM ;
	OTG_HS_DIEPMSK |= OTG_DIEPMSK_XFRCM ;

	OTG_HS_DCTL &=~ OTG_DCTL_SDIS;

	return &usbd_dev;
}
