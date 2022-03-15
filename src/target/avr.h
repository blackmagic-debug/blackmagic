#ifndef ATMEL___H
#define ATMEL___H

#include "jtag_scan.h"
#include "target.h"

enum avr_error_e
{
	pdi_ok,
	pdi_failure,
};

#define AVR_MAX_BREAKPOINTS 2

typedef struct avr_pdi_s {
	uint32_t idcode;

	uint8_t dp_jd_index;
	enum target_halt_reason halt_reason;
	enum avr_error_e error_state;
	target_addr programCounter;

	target_addr hw_breakpoint[AVR_MAX_BREAKPOINTS];
	uint8_t hw_breakpoint_enabled;
	uint8_t hw_breakpoint_max;
} avr_pdi_t;

bool avr_pdi_init(avr_pdi_t *pdi);

void avr_jtag_pdi_handler(uint8_t jd_index);
int platform_avr_jtag_pdi_init(avr_pdi_t *pdi);

bool avr_jtag_shift_dr(jtag_proc_t *jp, uint8_t jd_index, uint8_t *dout, const uint8_t din);
bool avr_pdi_reg_write(avr_pdi_t *pdi, uint8_t reg, uint8_t value);
uint8_t avr_pdi_reg_read(avr_pdi_t *pdi, uint8_t reg);

bool avr_attach(target *t);
void avr_detach(target *t);
void avr_add_flash(target *t, uint32_t start, size_t length);

#endif /*ATMEL___H*/
