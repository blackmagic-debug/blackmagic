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
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/bos.h>
#include <libopencm3/usb/dwc/otg_hs.h>
#include "usb_private.h"
#include "usb_dwc_common.h"

/* Receive FIFO size in 32-bit words. */
#define RX_FIFO_SIZE 512

/*
 * These are some register definitions, not yet provided by libopencm3,
 * or definitions that need hacking in order to use here.
 */
#define USBPHYC_BASE                        (PERIPH_BASE_APB2 + 0x7c00)
#define RCC_OTGPHYC                         (((0x44) << 5) + (31))
#define OTG_GCCFG_PHYHSEN                   (1 << 23)
#define OTG_PHYC_PLL1                       0x000
#define OTG_PHYC_TUNE                       0x00c
#define OTG_PHYC_LDO                        0x018
#define OTG_HS_PHYC_PLL1                    MMIO32(USBPHYC_BASE + OTG_PHYC_PLL1)
#define OTG_HS_PHYC_TUNE                    MMIO32(USBPHYC_BASE + OTG_PHYC_TUNE)
#define OTG_HS_PHYC_LDO                     MMIO32(USBPHYC_BASE + OTG_PHYC_LDO)
#define OTG_PHYC_LDO_DISABLE                (1 << 2)
#define OTG_PHYC_LDO_STATUS                 (1 << 1)
#define OTG_PHYC_PLL1_ENABLE                (1 << 0)
#define OTG_PHYC_PLL1_SEL_25MHZ             (0x5 << 1)
#define OTG_PHYC_TUNE_INCURRINT             (1 << 1)
#define OTG_PHYC_TUNE_INCURREN              (1 << 0)
#define OTG_PHYC_TUNE_HSDRVDCCUR            (1 << 4)
#define OTG_PHYC_TUNE_HSDRVRFRED            (1 << 8)
#define OTG_PHYC_TUNE_HSDRVCHKITRM_20_935MA (0x7 << 9)

#define MAX_BULK_PACKET_SIZE 512
#define USB_ENDPOINT_COUNT   9

static struct incoming_packet {
	bool is_packet_present;
	int packet_length;
	uint8_t packet_data[MAX_BULK_PACKET_SIZE];
} stashed_packets[USB_ENDPOINT_COUNT];

static struct _usbd_device _usbd_dev;
extern const struct _usbd_driver stm32f723_usb_driver;

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
	rcc_periph_clock_enable(RCC_OTGPHYC);
	rcc_periph_clock_enable(RCC_OTGHSULPI);

	rcc_periph_clock_enable(RCC_OTGHS);
	OTG_HS_GINTSTS = OTG_GINTSTS_MMIS;

	// ??? What is this??? It is not documented in the manual
	OTG_HS_GCCFG |= OTG_GCCFG_PHYHSEN;

	OTG_HS_PHYC_LDO |= OTG_PHYC_LDO_DISABLE;
	while (!(OTG_HS_PHYC_LDO & OTG_PHYC_LDO_STATUS))
		continue;
	OTG_HS_PHYC_PLL1 = OTG_PHYC_PLL1_SEL_25MHZ;
	OTG_HS_PHYC_TUNE |= OTG_PHYC_TUNE_INCURREN | OTG_PHYC_TUNE_INCURRINT | OTG_PHYC_TUNE_HSDRVDCCUR |
		OTG_PHYC_TUNE_HSDRVRFRED | OTG_PHYC_TUNE_HSDRVCHKITRM_20_935MA;
	OTG_HS_PHYC_PLL1 |= OTG_PHYC_PLL1_ENABLE;
	/* 2ms Delay required to get internal phy clock stable
	 * Used by DFU too, so platform_xxx not available.
	 * Some Stlinkv3-Set did not cold start w/o the delay
	 */
	for (volatile size_t i = 0; i < 200 * 1000; ++i)
		continue;

	/* Wait for AHB idle. */
	while (!(OTG_HS_GRSTCTL & OTG_GRSTCTL_AHBIDL))
		continue;
	/* Do core soft reset. */
	OTG_HS_GRSTCTL |= OTG_GRSTCTL_CSRST;
	while (OTG_HS_GRSTCTL & OTG_GRSTCTL_CSRST)
		continue;

	/* Force peripheral only mode. */
	OTG_HS_GUSBCFG |= OTG_GUSBCFG_FDMOD | OTG_GUSBCFG_TRDT_MASK;

	/* Restart the PHY clock. */
	OTG_HS_PCGCCTL = 0;

	OTG_HS_GRXFSIZ = stm32f723_usb_driver.rx_fifo_size;
	_usbd_dev.fifo_mem_top = stm32f723_usb_driver.rx_fifo_size;

	OTG_HS_DCTL |= OTG_DCTL_SDIS;

	/* Unmask interrupts for TX and RX. */
	OTG_HS_GAHBCFG |= OTG_GAHBCFG_GINT;
	OTG_HS_GINTMSK = OTG_GINTMSK_ENUMDNEM | OTG_GINTMSK_RXFLVLM | OTG_GINTMSK_IEPINT | OTG_GINTMSK_OEPINT |
		OTG_GINTMSK_USBSUSPM | OTG_GINTMSK_WUIM;

	OTG_HS_DAINTMSK = 0xffffffff;

	OTG_HS_DOEPMSK |= OTG_DOEPMSK_STUPM | OTG_DOEPMSK_XFRCM;
	OTG_HS_DIEPMSK |= OTG_DIEPMSK_XFRCM;

	OTG_HS_DCTL &= ~OTG_DCTL_SDIS;

	return &_usbd_dev;
}

/* Note: the original 'usb_dwc_common.c' source code
 * handles incoming OUT and SETUP usb packets as soon as they are available
 * in the packet FIFO.
 *
 * I inspected the stm-cube sources, and there, the incoming OUT and
 * SETUP packets are handled only when a XFRC - transfer complete interrupt is
 * received. This means that, at this point, it is guaranteed that the respective
 * usb endpoint 'enable' bit is cleared by the usb core. From the st manual:
 *
 * The core clears this (EPENA) bit before setting any of the following interrupts on this endpoint:
 * - SETUP phase done
 * - Endpoint disabled
 * - Transfer completed
 *
 * Maybe this is not relevant, but it seems to me that such handling is more
 * robust. I spent a significant amount of time debugging some other usb issues,
 * and while debugging, I adjusted the code to behave more like the stm-cube
 * sources. When I was done with debugging, I did not investigate if the original
 * usb_dwc_common driver code for handling incoming (OUT) usb packages would work with.
 * the adjustments I made. It seems to me that the stm-cube handling of incoming (OUT)
 * usb packages is more robust, and I decided to keep it this way.
 *
 * However, for that to work, it is needed that the packets are read from the usb FIFO packet
 * memory in some temporary storage, and are handed to the upper usb layers only
 * when the corresponding XFRC interrupts are received. This means that some temporary
 * storage is necessary, so this variable serves this purpose.
 */

static void stm32f723_ep_setup(usbd_device *usbd_dev, uint8_t addr, uint8_t type, uint16_t max_size,
	void (*callback)(usbd_device *usbd_dev, uint8_t ep))
{
	/*
	 * Configure endpoint address and type. Allocate FIFO memory for
	 * endpoint. Install callback function.
	 */
	uint8_t dir = addr & 0x80;
	addr &= 0x7f;

	if (addr == 0) { /* For the default control endpoint */
		/* Configure IN part. */
		if (max_size >= 64)
			OTG_HS_DIEPCTL0 = OTG_DIEPCTL0_MPSIZ_64;
		else if (max_size >= 32)
			OTG_HS_DIEPCTL0 = OTG_DIEPCTL0_MPSIZ_32;
		else if (max_size >= 16)
			OTG_HS_DIEPCTL0 = OTG_DIEPCTL0_MPSIZ_16;
		else
			OTG_HS_DIEPCTL0 = OTG_DIEPCTL0_MPSIZ_8;

		OTG_HS_DIEPTSIZ0 = max_size & OTG_DIEPSIZ0_XFRSIZ_MASK;
		OTG_HS_DIEPCTL0 |= OTG_DIEPCTL0_SNAK;

		/* Configure OUT part. */
		usbd_dev->doeptsiz[0] = OTG_DIEPSIZ0_STUPCNT_1 | OTG_DIEPSIZ0_PKTCNT | (max_size & OTG_DIEPSIZ0_XFRSIZ_MASK);
		OTG_HS_DOEPTSIZ(0) = usbd_dev->doeptsiz[0];
		OTG_HS_DOEPCTL(0) |= OTG_DOEPCTL0_EPENA | OTG_DIEPCTL0_SNAK;

		OTG_HS_GNPTXFSIZ = ((max_size / 4) << 16) | usbd_dev->driver->rx_fifo_size;
		usbd_dev->fifo_mem_top += max_size / 4;
		usbd_dev->fifo_mem_top_ep0 = usbd_dev->fifo_mem_top;

		return;
	}

	if (dir) {
		OTG_HS_DIEPTXF(addr) = ((max_size / 4) << 16) | usbd_dev->fifo_mem_top;
		usbd_dev->fifo_mem_top += max_size / 4;

		OTG_HS_DIEPTSIZ(addr) = max_size & OTG_DIEPSIZ0_XFRSIZ_MASK;
		OTG_HS_DIEPCTL(addr) |=
			OTG_DIEPCTL0_SNAK | (type << 18) | OTG_DIEPCTL0_USBAEP | OTG_DIEPCTLX_SD0PID | (addr << 22) | max_size;

		if (callback)
			usbd_dev->user_callback_ctr[addr][USB_TRANSACTION_IN] = (void *)callback;
	}

	if (!dir) {
		usbd_dev->doeptsiz[addr] = OTG_DIEPSIZ0_PKTCNT | (max_size & OTG_DIEPSIZ0_XFRSIZ_MASK);
		OTG_HS_DOEPTSIZ(addr) = usbd_dev->doeptsiz[addr];
		OTG_HS_DOEPCTL(addr) |= OTG_DOEPCTL0_EPENA | OTG_DOEPCTL0_USBAEP | OTG_DIEPCTL0_CNAK | OTG_DOEPCTLX_SD0PID |
			(type << 18) | max_size;

		if (callback)
			usbd_dev->user_callback_ctr[addr][USB_TRANSACTION_OUT] = (void *)callback;
	}
}

static void stm32f723_endpoints_reset(usbd_device *usbd_dev)
{
	/* The core resets the endpoints automatically on reset. */
	usbd_dev->fifo_mem_top = usbd_dev->fifo_mem_top_ep0;

	/* Disable any currently active endpoints */
	for (size_t i = 1; i < USB_ENDPOINT_COUNT; ++i) {
		if (OTG_HS_DOEPCTL(i) & OTG_DOEPCTL0_EPENA)
			OTG_HS_DOEPCTL(i) |= OTG_DOEPCTL0_EPDIS;
		if (OTG_HS_DIEPCTL(i) & OTG_DIEPCTL0_EPENA)
			OTG_HS_DIEPCTL(i) |= OTG_DIEPCTL0_EPDIS;
	}

	/* Flush all tx/rx fifos */
	OTG_HS_GRSTCTL = OTG_GRSTCTL_TXFFLSH | OTG_GRSTCTL_TXFNUM_ALL | OTG_GRSTCTL_RXFFLSH;
}

static uint16_t stm32f723_ep_write_packet(usbd_device *usbd_dev, uint8_t addr, const void *buf, uint16_t len)
{
	(void)usbd_dev;
	const uint32_t *buf32 = buf;

	addr &= 0x7f;

	/* Note: because of the libopencm3 API specification of this
	 * function, the type of the return code is 'uint16_t'.
	 * This means that it is not possible to return a negative
	 * result code for error. Also, the API specification
	 * expects that the length of the data packet written to
	 * the usb core is returned. This means that zero-length
	 * usb data packets cannot be reliably sent with the current api,
	 * and zero-length packets are needed in some cases for usb
	 * cdcacm class interfaces to signify the end of a transfer.
	 * At the moment, the blackmagic firmware works around this
	 * limitation by sending a single byte (containing a zero)
	 * usb packet for the gdb cdcacm interface, whenever a zero-length
	 * packet needs to be sent. This particular workaround works
	 * for the blackmagic use case, because the gdb protocol is
	 * packetized, and the single zero byte will be silently discarded
	 * by gdb, but this approach is incorrect in general.
	 *
	 * For the time being, return zero here in case of error. */
	/* Return if endpoint is already enabled, which means a packet
	 * transfer is still in progress. */
	if (OTG_HS_DIEPCTL(addr) & OTG_DIEPCTL0_EPENA)
		return 0;
	if ((OTG_HS_DTXFSTS(addr) & 0xffffU) < (uint16_t)((len + 3) >> 2))
		return 0;

	/* Enable endpoint for transmission. */
	OTG_HS_DIEPTSIZ(addr) = OTG_DIEPSIZ0_PKTCNT | len;

	/* WARNING: it is not explicitly stated in the ST documentation,
	 * but the usb core fifo memory read/write accesses should not
	 * be interrupted by usb core fifo memory write/read accesses.
	 *
	 * For example, this function can be run from within the usb interrupt
	 * context, and also from outside of the usb interrupt context.
	 * When this function executes outside of the usb interrupt context,
	 * if it gets interrupted by the usb interrupt while it writes to
	 * the usb core fifo memory, and from within the usb
	 * interrupt the usb core fifo memory is read, then when control returns
	 * to this function, the usb core fifo memory write accesses can not
	 * simply continue, this will result in a transaction error on the
	 * usb line.
	 *
	 * In order to avoid such situation, the usb interrupt is masked here
	 * prior to writing the data to the usb core fifo memory.
	 */
	uint32_t saved_interrupt_mask = OTG_HS_GINTMSK;
	OTG_HS_GINTMSK = 0;
	OTG_HS_DIEPCTL(addr) |= OTG_DIEPCTL0_EPENA | OTG_DIEPCTL0_CNAK;

	/* Copy buffer to endpoint FIFO, note - memcpy does not work.
	 * ARMv7M supports non-word-aligned accesses, ARMv6M does not. */
	for (size_t i = 0; i < len; i += 4)
		MMIO32(OTG_HS_FIFO(addr)) = *buf32++;

	OTG_HS_GINTMSK = saved_interrupt_mask;
	return len;
}

static uint16_t stm32f723_ep_read_packet(usbd_device *usbd_dev, uint8_t addr, void *buf, uint16_t len)
{
	(void)usbd_dev;
	struct incoming_packet *packet = stashed_packets + addr;
	if (!packet->is_packet_present)
		return 0;
	len = MIN(len, packet->packet_length);
	packet->is_packet_present = false;
	memcpy(buf, packet->packet_data, len);
	return len;
}

static void stm32f723_ep_read_packet_internal(usbd_device *usbd_dev, int ep)
{
	struct incoming_packet *packet = stashed_packets + ep;
	uint32_t *buf32 = (uint32_t *)packet->packet_data;
	const size_t len = MIN(sizeof(packet->packet_data), usbd_dev->rxbcnt);

	/* ARMv7M supports non-word-aligned accesses, ARMv6M does not. */
	size_t i;
	for (i = len; i >= 4; i -= 4) {
		*buf32++ = MMIO32(OTG_HS_FIFO(0));
		usbd_dev->rxbcnt -= 4;
	}

	if (i) {
		uint32_t extra = MMIO32(OTG_HS_FIFO(0));
		/* we read 4 bytes from the fifo, so update rxbcnt */
		if (usbd_dev->rxbcnt < 4)
			/* Be careful not to underflow (rxbcnt is unsigned) */
			usbd_dev->rxbcnt = 0;
		else
			usbd_dev->rxbcnt -= 4;
		memcpy(buf32, &extra, i);
	}
	packet->is_packet_present = true;

	packet->packet_length = len;
}

static void stm32f723_poll(usbd_device *usbd_dev)
{
	/* Read interrupt status register. */
	uint32_t intsts = OTG_HS_GINTSTS;
	if (!(intsts & OTG_HS_GINTMSK))
		/* No interrupts to handle - can happen if this function is
		 * not invoked from the usb interrupt handler. */
		return;

	if (intsts & OTG_GINTSTS_ENUMDNE) {
		/* Handle USB RESET condition. */
		OTG_HS_GINTSTS = OTG_GINTSTS_ENUMDNE;
		usbd_dev->fifo_mem_top = usbd_dev->driver->rx_fifo_size;
		_usbd_reset(usbd_dev);
		return;
	}

	/* Handle IN endpoint interrupt requests. */
	if (intsts & OTG_GINTSTS_IEPINT) {
		for (size_t i = 0; i < USB_ENDPOINT_COUNT; ++i) { /* Iterate over endpoints. */
			if (OTG_HS_DIEPINT(i) & OTG_DIEPINTX_XFRC) {
				/* Transfer complete. */
				OTG_HS_DIEPINT(i) = OTG_DIEPINTX_XFRC;
				if (usbd_dev->user_callback_ctr[i][USB_TRANSACTION_IN])
					usbd_dev->user_callback_ctr[i][USB_TRANSACTION_IN](usbd_dev, i);
			}
		}
	}

	if (intsts & OTG_GINTSTS_RXFLVL) {
		/* Receive FIFO non-empty. */
		uint32_t rxstsp = OTG_HS_GRXSTSP;
		uint32_t pktsts = rxstsp & OTG_GRXSTSP_PKTSTS_MASK;
		uint8_t ep = rxstsp & OTG_GRXSTSP_EPNUM_MASK;

		/* Save packet size for dwc_ep_read_packet(). */
		usbd_dev->rxbcnt = (rxstsp & OTG_GRXSTSP_BCNT_MASK) >> 4;
		struct incoming_packet *packet = stashed_packets + ep;

		if (pktsts == OTG_GRXSTSP_PKTSTS_OUT) {
			if (usbd_dev->rxbcnt)
				stm32f723_ep_read_packet_internal(usbd_dev, ep);
			else
				packet->is_packet_present = true, packet->packet_length = 0;
		} else if (pktsts == OTG_GRXSTSP_PKTSTS_SETUP) {
			if (usbd_dev->rxbcnt)
				stm32f723_ep_read_packet_internal(usbd_dev, ep);
			else
				packet->is_packet_present = true, packet->packet_length = 0;
			stm32f723_ep_read_packet(usbd_dev, ep, &usbd_dev->control_state.req, 8);
		}
	}

	/* Handle OUT endpoint interrupt requests. */
	if (intsts & OTG_GINTSTS_OEPINT) {
		uint32_t daint = OTG_HS_DAINT;
		for (size_t epnum = 0; epnum < USB_ENDPOINT_COUNT; epnum++) {
			if (daint & (1 << (16 + epnum))) {
				uint32_t t = OTG_HS_DOEPINT(epnum);
				OTG_HS_DOEPINT(epnum) = t;

				if (t & OTG_DOEPINTX_XFRC) {
					OTG_HS_DOEPINT(epnum) = OTG_DOEPINTX_XFRC;
					if (usbd_dev->user_callback_ctr[epnum][USB_TRANSACTION_OUT])
						usbd_dev->user_callback_ctr[epnum][USB_TRANSACTION_OUT](usbd_dev, epnum);
					OTG_HS_DOEPTSIZ(epnum) = usbd_dev->doeptsiz[epnum];
					OTG_HS_DOEPCTL(epnum) |=
						OTG_DOEPCTL0_EPENA | (usbd_dev->force_nak[epnum] ? OTG_DOEPCTL0_SNAK : OTG_DOEPCTL0_CNAK);
				}
				if (t & OTG_DOEPINTX_STUP) {
					/* Special case for control endpoints - reception of OUT packets is
					 * always enabled. */
					OTG_HS_DOEPINT(epnum) = OTG_DOEPINTX_STUP;
					if (usbd_dev->user_callback_ctr[epnum][USB_TRANSACTION_SETUP])
						usbd_dev->user_callback_ctr[epnum][USB_TRANSACTION_SETUP](usbd_dev, epnum);
					OTG_HS_DOEPTSIZ(epnum) = usbd_dev->doeptsiz[epnum];
					OTG_HS_DOEPCTL(epnum) |=
						OTG_DOEPCTL0_EPENA | (usbd_dev->force_nak[epnum] ? OTG_DOEPCTL0_SNAK : OTG_DOEPCTL0_CNAK);
				}
				if (t & OTG_DOEPINTX_OTEPDIS)
					OTG_HS_DOEPINT(epnum) = OTG_DOEPINTX_OTEPDIS;
			}
		}
	}

	if (intsts & OTG_GINTSTS_USBSUSP) {
		if (usbd_dev->user_callback_suspend)
			usbd_dev->user_callback_suspend();
		OTG_HS_GINTSTS = OTG_GINTSTS_USBSUSP;
	}

	if (intsts & OTG_GINTSTS_WKUPINT) {
		if (usbd_dev->user_callback_resume)
			usbd_dev->user_callback_resume();
		OTG_HS_GINTSTS = OTG_GINTSTS_WKUPINT;
	}

	if (intsts & OTG_GINTSTS_SOF) {
		if (usbd_dev->user_callback_sof)
			usbd_dev->user_callback_sof();
		OTG_HS_GINTSTS = OTG_GINTSTS_SOF;
	}

	if (usbd_dev->user_callback_sof)
		OTG_HS_GINTMSK |= OTG_GINTMSK_SOFM;
	else
		OTG_HS_GINTMSK &= ~OTG_GINTMSK_SOFM;
}

const struct _usbd_driver stm32f723_usb_driver = {
	.init = stm32f723_usbd_init,
	.set_address = dwc_set_address,
	.ep_setup = stm32f723_ep_setup,
	.ep_reset = stm32f723_endpoints_reset,
	.ep_stall_set = dwc_ep_stall_set,
	.ep_stall_get = dwc_ep_stall_get,
	.ep_nak_set = dwc_ep_nak_set,
	.ep_write_packet = stm32f723_ep_write_packet,
	.ep_read_packet = stm32f723_ep_read_packet,
	.poll = stm32f723_poll,
	.disconnect = dwc_disconnect,
	.base_address = USB_OTG_HS_BASE,
	.set_address_before_status = 1,
	.rx_fifo_size = RX_FIFO_SIZE,
};
