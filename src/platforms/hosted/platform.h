#ifndef __PLATFORM_H
#define __PLATFORM_H

#include "timing.h"

char *platform_ident(void);
void platform_buffer_flush(void);

#define PLATFORM_IDENT     "(PC-Hosted) "
#define SET_IDLE_STATE(x)
#define SET_RUN_STATE(x)

#define VENDOR_ID_BMP            0x1d50
#define PRODUCT_ID_BMP_BL        0x6017
#define PRODUCT_ID_BMP           0x6018

typedef enum bmp_type_s {
	BMP_TYPE_NONE = 0,
	BMP_TYPE_BMP,
	BMP_TYPE_STLINKV2,
	BMP_TYPE_LIBFTDI,
	BMP_TYPE_CMSIS_DAP,
	BMP_TYPE_JLINK
} bmp_type_t;

void gdb_ident(char *p, int count);
#endif
