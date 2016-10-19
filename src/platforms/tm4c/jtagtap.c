#include "general.h"
#include "jtagtap.h"

int
jtagtap_init(void)
{
	TMS_SET_MODE();

	for(int i = 0; i <= 50; i++) jtagtap_next(1,0);
	jtagtap_tms_seq(0xE73C, 16);
	jtagtap_soft_reset();

	return 0;
}

void
jtagtap_reset(void)
{
#ifdef TRST_PORT
	volatile int i;
	gpio_clear(TRST_PORT, TRST_PIN);
	for(i = 0; i < 10000; i++) asm("nop");
	gpio_set(TRST_PORT, TRST_PIN);
#endif
	jtagtap_soft_reset();
}

uint8_t
jtagtap_next(const uint8_t dTMS, const uint8_t dTDI)
{
	uint16_t ret;

	gpio_set_val(TMS_PORT, TMS_PIN, dTMS);
	gpio_set_val(TDI_PORT, TDI_PIN, dTDI);
	gpio_set(TCK_PORT, TCK_PIN);
	ret = gpio_get(TDO_PORT, TDO_PIN);
	gpio_clear(TCK_PORT, TCK_PIN);

	DEBUG("jtagtap_next(TMS = %d, TDI = %d) = %d\n", dTMS, dTDI, ret);

	return ret != 0;
}

