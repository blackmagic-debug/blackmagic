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
#if !defined(__LIBUSB_UTILS_H)
#define      __LIBUSB_UTILS_H
#include <libusb-1.0/libusb.h>

struct trans_ctx {
#define TRANS_FLAGS_IS_DONE (1 << 0)
#define TRANS_FLAGS_HAS_ERROR (1 << 1)
    volatile unsigned long flags;
};

typedef struct {
	libusb_context        *ul_libusb_ctx;
	libusb_device_handle  *ul_libusb_device_handle;
	unsigned char         ep_tx;
	unsigned char         ep_rx;
	struct libusb_transfer* req_trans;
	struct libusb_transfer* rep_trans;
	void                  *priv;
} usb_link_t;

int send_recv(usb_link_t *link, uint8_t *txbuf, size_t txsize,
			  uint8_t *rxbuf, size_t rxsize);
#endif
