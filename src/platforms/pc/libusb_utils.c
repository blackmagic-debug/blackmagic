/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2020 Uwe Bonnes (bon@elektron.ikp.physik.tu-darmstadt.de)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.	 If not, see <http://www.gnu.org/licenses/>.
 */
#include "general.h"
#include "cl_utils.h"

static void LIBUSB_CALL on_trans_done(struct libusb_transfer *trans)
{
    struct trans_ctx * const ctx = trans->user_data;

    if (trans->status != LIBUSB_TRANSFER_COMPLETED)
    {
		fprintf(stderr, "on_trans_done: ");
        if(trans->status == LIBUSB_TRANSFER_TIMED_OUT)  {
            fprintf(stderr, " Timeout\n");
        } else if (trans->status == LIBUSB_TRANSFER_CANCELLED) {
            fprintf(stderr, " cancelled\n");
        } else if (trans->status == LIBUSB_TRANSFER_NO_DEVICE) {
            fprintf(stderr, " no device\n");
        } else {
            fprintf(stderr, " unknown\n");
		}
        ctx->flags |= TRANS_FLAGS_HAS_ERROR;
    }
    ctx->flags |= TRANS_FLAGS_IS_DONE;
}

static int submit_wait(usb_link_t *link, struct libusb_transfer *trans) {
	struct trans_ctx trans_ctx;
	enum libusb_error error;

	trans_ctx.flags = 0;

	/* brief intrusion inside the libusb interface */
	trans->callback = on_trans_done;
	trans->user_data = &trans_ctx;

	if ((error = libusb_submit_transfer(trans))) {
		fprintf(stderr, "libusb_submit_transfer(%d): %s\n", error,
			  libusb_strerror(error));
		exit(-1);
	}

	uint32_t start_time = platform_time_ms();
	while (trans_ctx.flags == 0) {
		struct timeval timeout;
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;
		if (libusb_handle_events_timeout(link->ul_libusb_ctx, &timeout)) {
			fprintf(stderr, "libusb_handle_events()\n");
			return -1;
		}
		uint32_t now = platform_time_ms();
		if (now - start_time > 1000) {
			libusb_cancel_transfer(trans);
			fprintf(stderr, "libusb_handle_events() timeout\n");
			return -1;
		}
	}
	if (trans_ctx.flags & TRANS_FLAGS_HAS_ERROR) {
		fprintf(stderr, "libusb_handle_events() | has_error\n");
		return -1;
	}

	return 0;
}

/* One USB transaction */
int send_recv(usb_link_t *link,
					 uint8_t *txbuf, size_t txsize,
					 uint8_t *rxbuf, size_t rxsize)
{
	int res = 0;
	if( txsize) {
		int txlen = txsize;
		libusb_fill_bulk_transfer(link->req_trans,
								  link->ul_libusb_device_handle,
								  link->ep_tx | LIBUSB_ENDPOINT_OUT,
								  txbuf, txlen,
								  NULL, NULL, 0);
		if (cl_debuglevel & BMP_DEBUG_WIRE) {
			int i = 0;
			printf(" Send (%3d): ", txlen);
			for (; i < txlen; i++) {
				printf("%02x", txbuf[i]);
				if ((i & 7) == 7)
					printf(".");
				if ((i & 31) == 31)
					printf("\n             ");
			}
			if (!(i & 31))
				printf("\n");
		}
		if (submit_wait(link, link->req_trans)) {
			libusb_clear_halt(link->ul_libusb_device_handle, link->ep_tx);
			return -1;
		}
	}
	/* send_only */
	if (rxsize != 0) {
		/* read the response */
		libusb_fill_bulk_transfer(link->rep_trans, link->ul_libusb_device_handle,
								  link->ep_rx | LIBUSB_ENDPOINT_IN,
								  rxbuf, rxsize, NULL, NULL, 0);

		if (submit_wait(link, link->rep_trans)) {
			DEBUG("clear 1\n");
			libusb_clear_halt(link->ul_libusb_device_handle, link->ep_rx);
			return -1;
		}
		res = link->rep_trans->actual_length;
		if (res >0) {
			int i;
			uint8_t *p = rxbuf;
			if (cl_debuglevel & BMP_DEBUG_WIRE) {
				printf(" Rec (%zu/%d)", rxsize, res);
				for (i = 0; i < res && i < 32 ; i++) {
					if ( i && ((i & 7) == 0))
						printf(".");
					printf("%02x", p[i]);
				}
			}
		}
	}
	if (cl_debuglevel & BMP_DEBUG_WIRE)
		printf("\n");
	return res;
}
