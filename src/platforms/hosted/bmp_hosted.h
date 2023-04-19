/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2020-2021 Uwe Bonnes (bon@elektron.ikp.physik.tu-darmstadt.de)
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holders nor the names of
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef PLATFORMS_HOSTED_BMP_HOSTED_H
#define PLATFORMS_HOSTED_BMP_HOSTED_H

#if HOSTED_BMP_ONLY != 1
#include <libusb.h>
#endif
#include "cli.h"
#include "platform.h"

#if HOSTED_BMP_ONLY != 1
#define TRANSFER_IS_DONE   (1U << 0U)
#define TRANSFER_HAS_ERROR (1U << 1U)

typedef struct transfer_ctx {
	volatile size_t flags;
} transfer_ctx_s;

typedef struct libusb_config_descriptor libusb_config_descriptor_s;
typedef struct libusb_interface_descriptor libusb_interface_descriptor_s;
typedef struct libusb_endpoint_descriptor libusb_endpoint_descriptor_s;
typedef struct libusb_interface libusb_interface_s;
typedef enum libusb_error libusb_error_e;

typedef struct ftdi_context ftdi_context_s;

typedef struct usb_link {
	libusb_context *context;
	libusb_device_handle *device_handle;
	uint8_t interface;
	uint8_t ep_tx;
	uint8_t ep_rx;
	void *priv;
} usb_link_s;
#endif

typedef struct bmp_info {
	bmp_type_t bmp_type;
	char dev;
	char serial[64];
	char manufacturer[512];
	char product[256];
	char version[256];
	bool is_jtag;
#if HOSTED_BMP_ONLY != 1
	libusb_context *libusb_ctx;
	ftdi_context_s *ftdi_ctx;
	usb_link_s *usb_link;
	uint16_t vid;
	uint16_t pid;
	uint8_t interface_num;
	uint8_t in_ep;
	uint8_t out_ep;
#endif
} bmp_info_s;

typedef struct timeval timeval_s;

extern bmp_info_s info;
void bmp_ident(bmp_info_s *info);
int find_debuggers(bmda_cli_options_s *cl_opts, bmp_info_s *info);
void libusb_exit_function(bmp_info_s *info);

#if HOSTED_BMP_ONLY == 1
bool device_is_bmp_gdb_port(const char *device);
#else
int bmda_usb_transfer(usb_link_s *link, const void *tx_buffer, size_t tx_len, void *rx_buffer, size_t rx_len);
#endif

#endif /* PLATFORMS_HOSTED_BMP_HOSTED_H */
