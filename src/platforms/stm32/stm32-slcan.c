/* ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <info@gerhard-bertelsmann.de> wrote this file. As long as you retain this
 * notice you can do whatever you want with this stuff. If we meet some day,
 * and you think this stuff is worth it, you can buy me a beer in return
 * Gerhard Bertelsmann
 * ----------------------------------------------------------------------------
 */

/*
 * This file is derived from the libopencm3 project examples
 */

/* Adapted for use in blackmagic/stlinkv3 as USB slcan adapter */
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/can.h>

#include "general.h"
#include "cdcacm.h"

#define CAN_MAX_RETRY   10000

extern struct ring output_ring;
extern struct ring input_ring;
volatile unsigned int counter;
volatile uint8_t status;
volatile uint8_t commands_pending;
uint8_t d_data[8];

enum CAN_SPEED_INDEX {
	CAN_10k = 0,
	CAN_20k,
	CAN_50k,
	CAN_100k,
	CAN_125k,
	CAN_250k,
	CAN_500k,
	CAN_800k,
	CAN_1000k
};

/* Define true for loopback test */
#define LPBK_MODE false

/* CAN_ABP_FREQUENCY must be defined and a multiple of 18 MHz*/
static int can_speed(enum CAN_SPEED_INDEX index) {
	int ret;

	/*
	 S0 = 10 kBaud
	 S1 = 20 kBaud
	 S2 = 50 kBaud
	 S3 = 100 kBaud
	 S4 = 125 kBaud
	 S5 = 250 kBaud
	 S6 = 500 kBaud
	 S7 = 800 kBaud
	 S8 = 1 MBaud

	   10 k Sample 75.0 % : 36MHZ / 180 = 200kHz -> TQ 20   SJW + TS1 15 TS2 5
	   20 k Sample 75.0 % : 36MHZ /  90 = 400kHz -> TQ 20   SJW + TS1 15 TS2 5
	   50 k Sample 75.0 % : 36MHZ /  36 = 1MHz   -> TQ 20   SJW + TS1 15 TS2 5
	  100 k Sample 80.0 % : 36MHZ /  36 = 1MHz   -> TQ 10   SJW + TS1  8 TS2 2
	  125 k Sample 87.5 % : 36MHZ /  18 = 2MHz   -> TQ 16   SJW + TS1 14 TS2 2
	  250 k Sample 87.5 % : 36MHZ /   9 = 4MHz   -> TQ 16   SJW + TS1 14 TS2 2
	  500 k Sample 87.5 % : 36MHZ /   9 = 4MHz   -> TQ  8   SJW + TS1  7 TS2 1
	  800 k Sample 86.7 % : 36MHZ /   3 = 12MHz  -> TQ 15   SJW + TS1 13 TS2 2
	 1000 k Sample 88.9 % : 36MHZ /   2 = 18MHz  -> TQ 18   SJW + TS1 16 TS2 2

	 TTCM: Time triggered comm mode
	 ABOM: Automatic bus-off management
	 AWUM: Automatic wakeup mode
	 NART: No automatic retransmission
	 RFLM: Receive FIFO locked mode
	 TXFP: Transmit FIFO priority
	 */
	switch (index) {
	case CAN_10k:
		ret = can_init(CAN1, false, true, false, false, false, false,
				CAN_BTR_SJW_1TQ, CAN_BTR_TS1_14TQ, CAN_BTR_TS2_5TQ,
					   CAN_APB_FREQUENCY / (200 * 1000), LPBK_MODE,
				false);
		break;
	case CAN_20k:
		ret = can_init(CAN1, false, true, false, false, false, false,
				CAN_BTR_SJW_1TQ, CAN_BTR_TS1_14TQ, CAN_BTR_TS2_5TQ,
					   CAN_APB_FREQUENCY / (400 * 1000), LPBK_MODE,
				false);
		break;
	case CAN_50k:
		ret = can_init(CAN1, false, true, false, false, false, false,
				CAN_BTR_SJW_1TQ, CAN_BTR_TS1_14TQ, CAN_BTR_TS2_5TQ,
					   CAN_APB_FREQUENCY / (1 * 1000 * 1000), LPBK_MODE,
				false);
		break;
	case CAN_100k:
		ret = can_init(CAN1, false, true, false, false, false, false,
				CAN_BTR_SJW_1TQ, CAN_BTR_TS1_8TQ, CAN_BTR_TS2_2TQ,
					   CAN_APB_FREQUENCY / (1 * 1000 * 1000), LPBK_MODE,
				false);
		break;
	case CAN_125k:
		ret = can_init(CAN1, false, true, false, false, false, false,
				CAN_BTR_SJW_1TQ, CAN_BTR_TS1_13TQ, CAN_BTR_TS2_2TQ,
					   CAN_APB_FREQUENCY / (2 * 1000 * 1000), LPBK_MODE,
				false);
		break;
	case CAN_250k:
		ret = can_init(CAN1, false, true, false, false, false, false,
				CAN_BTR_SJW_1TQ, CAN_BTR_TS1_13TQ, CAN_BTR_TS2_2TQ,
					   CAN_APB_FREQUENCY / (4 * 1000 * 1000), LPBK_MODE,
				false);
		break;
	case CAN_500k:
		ret = can_init(CAN1, false, true, false, false, false, false,
				CAN_BTR_SJW_1TQ, CAN_BTR_TS1_7TQ, CAN_BTR_TS2_1TQ,
					   CAN_APB_FREQUENCY / (4 * 1000 * 1000), LPBK_MODE,
				false);
		break;
	case CAN_800k:
		ret = can_init(CAN1, false, true, false, false, false, false,
				CAN_BTR_SJW_1TQ, CAN_BTR_TS1_12TQ, CAN_BTR_TS2_2TQ,
					   CAN_APB_FREQUENCY / (12 * 1000 * 1000), LPBK_MODE,
				false);
		break;
	case CAN_1000k:
		ret = can_init(CAN1, false, true, false, false, false, false,
				CAN_BTR_SJW_1TQ, CAN_BTR_TS1_15TQ, CAN_BTR_TS2_2TQ,
					   CAN_APB_FREQUENCY / (18 * 1000 * 1000), LPBK_MODE,
				false);
		break;
	default:
		ret = -1;
		break;
	}
	return ret;
}

void slcan_init(void) {
	/* Enable peripheral clocks */
	rcc_periph_clock_enable(RCC_CAN1);

	/* NVIC setup */
	nvic_set_priority(CAN_RX0_IRQ, IRQ_PRI_CAN_RX0);
	nvic_enable_irq(CAN_RX0_IRQ);

	/* Use CAN TX interrupt as delay interrupt */
	nvic_set_priority(CAN_TX_IRQ, IRQ_PRI_CAN_TX);
	nvic_enable_irq(CAN_TX_IRQ);

	/* Reset CAN */
	can_reset(CAN1);
	/* Ignore for now if normal mode has not yet been reached*/
	can_speed(CAN_1000k);
	/* CAN filter 0 init. */
	can_filter_id_mask_32bit_init(0, 0, /* CAN ID */
	0, /* CAN ID mask */
	0, /* FIFO assignment (here: FIFO0) */
	true); /* Enable the filter. */

	/* Enable CAN RX interrupt. */
	can_enable_irq(CAN1, CAN_IER_FMPIE0);
}

void CAN_RX0_ISR(void) {
	uint32_t id;
	bool ext, rtr;
	uint8_t i, dlc, data[8], fmi;
	char buf[32], c, *p = buf;

	can_receive(CAN1, 0, false, &id, &ext, &rtr, &fmi, &dlc, data, NULL);

	*p++ = ((rtr)? 'r' : 't') - ((ext) ? ('r' - 'R') : 0);
	if (ext) {
		p+= sprintf(p, "%08" PRIx32, id);
	} else {
		p+= sprintf(p, "%03" PRIx32, id & 0x7ff);
	}
	c = (dlc & 0x0f) | 0x30;
	*p++ = c;
	for (i = 0; i < dlc; i++)
		p+= sprintf(p, "%02x", data[i]);
	*p++ = '\r';
	can_fifo_release(CAN1, 0);
	usbd_ep_write_packet(usbdev, CDCACM_SLCAN_ENDPOINT, buf, p - buf);
}

static volatile uint32_t count_new;
static uint8_t double_buffer_out[CDCACM_PACKET_SIZE];
void slcan_usb_out_cb(usbd_device *dev, uint8_t ep)
{
	(void)ep;
	usbd_ep_nak_set(dev, CDCACM_SLCAN_ENDPOINT, 1);
	if (count_new) /* Last command not yet handled*/
		return;
	if (!can_available_mailbox(CAN1)) {
		can_enable_irq(CAN1, CAN_IER_TMEIE);
		return;
	}
	count_new = usbd_ep_read_packet(dev, CDCACM_SLCAN_ENDPOINT,
									double_buffer_out, CDCACM_PACKET_SIZE);
	usbd_ep_nak_set(dev, CDCACM_SLCAN_ENDPOINT, 0);
	nvic_set_pending_irq(CAN_TX_IRQ);
}

static uint16_t get_2byte_unique(void)
{
	uint16_t res = 0;
	volatile uint16_t *unique_id_p = (volatile uint16_t *)DESIG_UNIQUE_ID_BASE;
	res = *unique_id_p++;
	res ^= *unique_id_p++;
	res ^= *unique_id_p++;
	res ^= *unique_id_p++;
	res ^= *unique_id_p++;
	res ^= *unique_id_p++;
	return res;
}

static int can_verbose_errors(char *txbuf)
{
	uint32_t msr = CAN_MSR(CAN1);
	uint32_t tsr = CAN_TSR(CAN1);
	uint32_t esr = CAN_ESR(CAN1);
	uint32_t btr = CAN_BTR(CAN1);
	return sprintf(txbuf, "MSR %08lx TSR  %08lx ESR %08lx BTR %08lx\r",
			msr, tsr, esr, btr);
}

static uint8_t can_get_errors(void)
{
	uint8_t res = 0;

	uint32_t rf0r = CAN_RF0R(CAN1);
	if (rf0r & CAN_RF0R_FULL0)
		res |= 0x01;
	if (rf0r & CAN_RF0R_FOVR0)
		res |= 0x08;
	if (!can_available_mailbox(CAN1))
		res |= 0x02;
	uint32_t tsr = CAN_TSR(CAN1);
	if (tsr & CAN_TSR_ALST0)
		res |= 0x40;
	uint32_t esr = CAN_ESR(CAN1);
	if (esr & CAN_ESR_EWGF)
		res |= 0x04;
	if (esr & CAN_ESR_EPVF)
		res |= 0x20;
	if (esr & CAN_ESR_BOFF)
		res |= 0x80;
	return res;
}

/* Handle commands in the low prority CAN TX
 * interrupt as kind of software IRQ
 */
void CAN_TX_ISR(void) {
	uint32_t tx_status = CAN_TSR(CAN1);
	uint32_t tx_mask = CAN_TSR_RQCP2 | CAN_TSR_RQCP1 | CAN_TSR_RQCP0;
	if (tx_status & tx_mask) {
		CAN_TSR(CAN1) = tx_status & tx_mask;
		can_disable_irq(CAN1, CAN_IER_TMEIE);
		usbd_ep_nak_set(usbdev, CDCACM_SLCAN_ENDPOINT, 0);
	}
	if (!double_buffer_out[0])
		return;
	char *p = (char*)double_buffer_out;
	char txbuf[128], *q = txbuf;
	do {
		bool ext, rtr;
		uint8_t i, dlc, data[8];
		uint32_t id;
		char c;
		bool send;
		int res = -1;

		id = 0;
		dlc = 0;
		ext = true;
		send = false;
		rtr = false;

		c = *p++;
		switch (c) {
		case 'R':
			rtr = true;
			/* fall through */
		case 'T':
			sscanf(p, "%8lx", &id);
			p += 8;
			dlc = *p++ - '0';
			send = true;
			break;
		case 'r':
			rtr = true;
			/* fall through */
		case 't':
			ext = false;
			sscanf(p, "%3lx", &id);
			p += 3;
			dlc = *p++ - '0';
			send = true;
			break;
		case 'S':
			can_speed(*p++ - '0');
			res = 0;
			break;
		case 'v': /* FIXME: not found in docs*/
			res = 0;
			break;
		case 'V':
			q += sprintf(q, "V123");
			res = 0;
			break;
		case 'F':
			q += sprintf(q, "F%02x", can_get_errors());
			res = 0;
			break;
		case 'f':
			q += can_verbose_errors(q);
			res = 0;
			break;
		case 'N':
			q += sprintf(q, "N%04x", get_2byte_unique());
			res = 0;
			break;
		case 'C':
			res = 0;
			break;
		default:
			res = 0;
			break;
		}
		if(dlc <= 8) {
			for (i = 0; i < dlc; i++) {
				/* Suspect: sscanf(p, "%2hhx", data + i); reads decimal*/
				uint32_t x;
				sscanf(p,  "%2lx", &x);
				data[i] = x & 0xff;
				p += 2;
			}
		}
		/* consume chars until cr or null termination is reached */
		for (; (*p && (*p != '\r') &&
				((uint8_t*)p < (double_buffer_out + sizeof(double_buffer_out))));
			 p++);
		if (*p == '\r') /* skip over CR */
			p++;
		if ((uint8_t*)p < (double_buffer_out + sizeof(double_buffer_out))) {
			/* Do not transmit when not in normal mode */
			if (send && !(CAN_MSR(CAN1) & (CAN_MSR_SLAK | CAN_MSR_INAK))) {
				res = can_transmit(CAN1, id, ext, rtr, dlc, data);
			}
		}
		if (res == -1)
			*q++ = '\b';
		else
			*q++ = '\r';

	} while (*p && ((uint8_t *)p < (double_buffer_out + count_new)));
	*q++ = 0;
	count_new = 0; /* Command processed*/
	size_t len = strlen(txbuf);
	usbd_ep_write_packet(usbdev, CDCACM_SLCAN_ENDPOINT, txbuf, len);

}
