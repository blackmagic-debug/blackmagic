#ifndef __PLATFORM_H
#define __PLATFORM_H

#include "timing.h"

void platform_buffer_flush(void);

#define PLATFORM_IDENT() "NONE"
#define SET_IDLE_STATE(x)
#define SET_RUN_STATE(x)

#define VENDOR_ID_BMP            0x1d50
#define PRODUCT_ID_BMP_BL        0x6017
#define PRODUCT_ID_BMP           0x6018

#ifndef HOSTED_BMP_ONLY

#include <libusb-1.0/libusb.h>
#include "libusb_utils.h"
#include <libftdi1/ftdi.h>

typedef enum bmp_type_s {
	BMP_TYPE_NONE = 0,
	BMP_TYPE_BMP,
	BMP_TYPE_STLINKV2,
	BMP_TYPE_LIBFTDI,
	BMP_TYPE_CMSIS_DAP,
	BMP_TYPE_JLINK
} bmp_type_t;

#endif /* HOSTED_BMP_ONLY */

typedef struct bmp_info_s {
#ifndef HOSTED_BMP_ONLY
	bmp_type_t bmp_type;
	libusb_context *libusb_ctx;
	struct ftdi_context *ftdic;
	usb_link_t *usb_link;
	unsigned int vid;
	unsigned int pid;
	char dev;
	char serial[64];
	char manufacturer[128];
#endif /* HOSTED_BMP_ONLY */
	char product[128];
} bmp_info_t;

extern bmp_info_t info;

#endif
