#ifndef __PLATFORM_H
#define __PLATFORM_H

#include "timing.h"

char *platform_ident(void);
void platform_buffer_flush(void);

#define PLATFORM_IDENT     "(PC-Hosted) "
#define SET_IDLE_STATE(x)
#define SET_RUN_STATE(x)

#define SYSTICKHZ 1000

#define VENDOR_ID_BMP            0x1d50
#define PRODUCT_ID_BMP_BL        0x6017
#define PRODUCT_ID_BMP           0x6018

#define VENDOR_ID_STLINK		 0x0483
#define PRODUCT_ID_STLINK_MASK	 0xffe0
#define PRODUCT_ID_STLINK_GROUP  0x3740
#define PRODUCT_ID_STLINKV1		 0x3744
#define PRODUCT_ID_STLINKV2		 0x3748
#define PRODUCT_ID_STLINKV21	 0x374b
#define PRODUCT_ID_STLINKV21_MSD 0x3752
#define PRODUCT_ID_STLINKV3_NO_MSD 0x3754
#define PRODUCT_ID_STLINKV3_BL	 0x374d
#define PRODUCT_ID_STLINKV3		 0x374f
#define PRODUCT_ID_STLINKV3E	 0x374e

#define VENDOR_ID_SEGGER         0x1366

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
