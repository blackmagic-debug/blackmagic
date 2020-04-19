#ifndef __PLATFORM_H
#define __PLATFORM_H

#include <libusb-1.0/libusb.h>
#include "libusb_utils.h"
#include <libftdi1/ftdi.h>

#include "timing.h"

char *platform_ident(void);
void platform_buffer_flush(void);

#define PLATFORM_IDENT() "NONE"
#define SET_IDLE_STATE(x)
#define SET_RUN_STATE(x)

typedef enum bmp_type_s {
	BMP_TYPE_NONE = 0,
	BMP_TYPE_BMP,
	BMP_TYPE_STLINKV2,
	BMP_TYPE_LIBFTDI,
	BMP_TYPE_CMSIS_DAP,
	BMP_TYPE_JLINK
} bmp_type_t;

typedef struct bmp_info_s {
	bmp_type_t bmp_type;
	libusb_context *libusb_ctx;
	struct ftdi_context *ftdic;
	usb_link_t *usb_link;
	unsigned int vid;
	unsigned int pid;
	char dev;
	char serial[64];
	char manufacturer[128];
	char product[128];
} bmp_info_t;

extern bmp_info_t info;

#endif
