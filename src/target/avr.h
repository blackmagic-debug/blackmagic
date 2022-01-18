#ifndef ATMEL___H
#define ATMEL___H

#include "jtag_scan.h"
#include "target.h"

typedef struct Atmel_DP_s {
	uint32_t idcode;

	uint8_t dp_jd_index;
	enum target_halt_reason halt_reason;
} AVR_DP_t;

bool avr_dp_init(AVR_DP_t *dp);

void avr_jtag_dp_handler(uint8_t jd_index, uint32_t j_idcode);
int platform_avr_jtag_dp_init(AVR_DP_t *dp);

bool avr_jtag_shift_dr(jtag_proc_t *jp, uint8_t jd_index, uint8_t *dout, const uint8_t din);
bool avr_pdi_reg_write(AVR_DP_t *dp, uint8_t reg, uint8_t value);
uint8_t avr_pdi_reg_read(AVR_DP_t *dp, uint8_t reg);

bool avr_attach(target *t);
void avr_detach(target *t);
void avr_add_flash(target *t, uint32_t start, size_t length);

#endif /*ATMEL___H*/
