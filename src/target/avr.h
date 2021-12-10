#ifndef ATMEL___H
#define ATMEL___H

#include "jtag_scan.h"

typedef struct Atmel_DP_s {
	int refcnt;

	uint32_t idcode;

	uint8_t dp_jd_index;
} AVR_DP_t;

void avr_dp_init(AVR_DP_t *dp);

void avr_jtag_dp_handler(uint8_t jd_index, uint32_t j_idcode);
int platform_avr_jtag_dp_init(AVR_DP_t *dp);

#endif /*ATMEL___H*/
