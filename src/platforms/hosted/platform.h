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

#ifndef PLATFORMS_HOSTED_PLATFORM_H
#define PLATFORMS_HOSTED_PLATFORM_H

#include "timing.h"

char *bmda_adaptor_ident(void);
void platform_buffer_flush(void);

#define PLATFORM_IDENT "(Black Magic Debug App) "
#define SET_IDLE_STATE(x) \
	do {                  \
	} while (0)
#define SET_RUN_STATE(x) \
	do {                 \
	} while (0)
#define PLATFORM_HAS_POWER_SWITCH

#define PRODUCT_ID_ANY 0xffffU

#define VENDOR_ID_BMP     0x1d50U
#define PRODUCT_ID_BMP_BL 0x6017U
#define PRODUCT_ID_BMP    0x6018U

#define VENDOR_ID_STLINK           0x0483U
#define PRODUCT_ID_STLINK_MASK     0xffe0U
#define PRODUCT_ID_STLINK_GROUP    0x3740U
#define PRODUCT_ID_STLINKV1        0x3744U
#define PRODUCT_ID_STLINKV2        0x3748U
#define PRODUCT_ID_STLINKV21       0x374bU
#define PRODUCT_ID_STLINKV21_MSD   0x3752U
#define PRODUCT_ID_STLINKV3_NO_MSD 0x3754U
#define PRODUCT_ID_STLINKV3_BL     0x374dU
#define PRODUCT_ID_STLINKV3        0x374fU
#define PRODUCT_ID_STLINKV3E       0x374eU

#define VENDOR_ID_SEGGER 0x1366U

#define VENDOR_ID_FTDI         0x0403U
#define PRODUCT_ID_FTDI_FT2232 0x6010U
#define PRODUCT_ID_FTDI_FT4232 0x6011U
#define PRODUCT_ID_FTDI_FT232  0x6014U

#define VENDOR_ID_ORBCODE   0x1209U
#define PRODUCT_ID_ORBTRACE 0x3443U

typedef enum probe_type {
	PROBE_TYPE_NONE = 0,
	PROBE_TYPE_BMP,
	PROBE_TYPE_STLINK_V2,
	PROBE_TYPE_FTDI,
	PROBE_TYPE_CMSIS_DAP,
	PROBE_TYPE_JLINK
} probe_type_e;

void gdb_ident(char *p, int count);

#endif /* PLATFORMS_HOSTED_PLATFORM_H */
