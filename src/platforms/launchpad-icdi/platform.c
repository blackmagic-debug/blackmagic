#include "platform.h"
#include "gdb_if.h"
#include "usbuart.h"

#include <libopencm3/lm4f/rcc.h>
#include <libopencm3/lm4f/nvic.h>
#include <libopencm3/lm4f/uart.h>
#include <libopencm3/cm3/systick.h>

#define SYSTICKHZ	100
#define SYSTICKMS	(1000 / SYSTICKHZ)

#define PLL_DIV_80MHZ	5
#define PLL_DIV_25MHZ	16

extern void trace_tick(void);

jmp_buf fatal_error_jmpbuf;
uint8_t running_status;
volatile uint32_t timeout_counter;

const char *morse_msg;

void morse(const char *msg, char repeat)
{
	(void) msg;
	(void) repeat;
}

void sys_tick_handler(void)
{
	if(timeout_counter)
		timeout_counter--;
	trace_tick();
}

int
platform_init(void)
{
        int i;
        for(i=0; i<1000000; i++);

	rcc_sysclk_config(OSCSRC_MOSC, XTAL_16M, PLL_DIV_80MHZ);
	
	// Enable all JTAG ports and set pins to output
	periph_clock_enable(RCC_GPIOA);
	periph_clock_enable(RCC_GPIOB);

	gpio_enable_ahb_aperture();

	gpio_mode_setup(TMS_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, TMS_PIN);
	gpio_mode_setup(TCK_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, TCK_PIN);
	gpio_mode_setup(TDI_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, TDI_PIN);
	gpio_mode_setup(TDO_PORT, GPIO_MODE_INPUT, GPIO_PUPD_NONE, TDO_PIN);
	gpio_mode_setup(SRST_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, SRST_PIN);
	gpio_set_output_config(SRST_PORT, GPIO_OTYPE_OD, GPIO_DRIVE_2MA, SRST_PIN);
	gpio_set(SRST_PORT, SRST_PIN);

	systick_set_clocksource(STK_CSR_CLKSOURCE_AHB_DIV8);
	systick_set_reload(rcc_get_system_clock_frequency() / (SYSTICKHZ * 8));

	systick_interrupt_enable();
	systick_counter_enable();

	nvic_enable_irq(NVIC_SYSTICK_IRQ);
	nvic_enable_irq(NVIC_UART0_IRQ);

	usbuart_init();
	cdcacm_init();

	//jtag_scan(NULL);

	return 0;
}

void platform_delay(uint32_t delay)
{
	timeout_counter = delay * 10;
	while(timeout_counter);
}

const char *platform_target_voltage(void)
{
	return "not supported";
}

