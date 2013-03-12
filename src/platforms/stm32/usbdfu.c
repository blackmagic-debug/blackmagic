/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2013 Gareth McMullin <gareth@blacksphere.co.nz>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <string.h>
#include <libopencm3/cm3/systick.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/cm3/scb.h>

#include "usbdfu.h"

#if defined(DISCOVERY_STLINK)
uint8_t rev;
uint16_t led_idle_run;
u32 led2_state = 0;
int stlink_test_nrst(void) {
/* Test if JRST/NRST is pulled down*/
    int i;
    uint16_t nrst;
    uint16_t pin;

    /* First, get Board revision by pulling PC13/14 up. Read
     *  11 for ST-Link V1, e.g. on VL Discovery, tag as rev 0
     *  10 for ST-Link V2, e.g. on F4 Discovery, tag as rev 1
     */
    rcc_peripheral_enable_clock(&RCC_APB2ENR, RCC_APB2ENR_IOPCEN);
    gpio_set_mode(GPIOC, GPIO_MODE_INPUT,
                  GPIO_CNF_INPUT_PULL_UPDOWN, GPIO14|GPIO13);
    gpio_set(GPIOC, GPIO14|GPIO13);
    for (i=0; i< 100; i++)
        rev = (~(gpio_get(GPIOC, GPIO14|GPIO13))>>13) & 3;

    switch (rev)
    {
    case 0:
        pin = GPIO1;
        led_idle_run= GPIO8;
        break;
    default:
        pin = GPIO0;
        led_idle_run = GPIO9;
    }
    gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ,
                  GPIO_CNF_OUTPUT_PUSHPULL, led_idle_run);
    rcc_peripheral_enable_clock(&RCC_APB2ENR, RCC_APB2ENR_IOPBEN);
    gpio_set_mode(GPIOB, GPIO_MODE_INPUT,
                  GPIO_CNF_INPUT_PULL_UPDOWN, pin);
    gpio_set(GPIOB, pin);
    for (i=0; i< 100; i++)
        nrst = gpio_get(GPIOB, pin);
    return (nrst)?1:0;
}
#endif

void dfu_detach(void)
{
#if defined (DISCOVERY_STLINK)
	/* Disconnect USB cable by resetting USB Device
	   and pulling USB_DP low*/
	rcc_peripheral_reset(&RCC_APB1RSTR, RCC_APB1ENR_USBEN);
	rcc_peripheral_clear_reset(&RCC_APB1RSTR, RCC_APB1ENR_USBEN);
	rcc_peripheral_enable_clock(&RCC_APB1ENR, RCC_APB1ENR_USBEN);
	rcc_peripheral_enable_clock(&RCC_APB2ENR, RCC_APB2ENR_IOPAEN);
	gpio_clear(GPIOA, GPIO12);
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ,
		GPIO_CNF_OUTPUT_OPENDRAIN, GPIO12);
#else
        /* USB device must detach, we just reset... */
#endif
	scb_reset_system();
}

int main(void)
{
    /* Check the force bootloader pin*/
#if defined (DISCOVERY_STLINK)
	rcc_peripheral_enable_clock(&RCC_APB2ENR, RCC_APB2ENR_IOPAEN);
/* Check value of GPIOA1 configuration. This pin is unconnected on
 * STLink V1 and V2. If we have a value other than the reset value (0x4),
 * we have a warm start and request Bootloader entry
 */
	if(((GPIOA_CRL & 0x40) == 0x40) && stlink_test_nrst()) {
#elif defined (STM32_CAN)
	rcc_peripheral_enable_clock(&RCC_APB2ENR, RCC_APB2ENR_IOPAEN);
	if(!gpio_get(GPIOA, GPIO0)) {
#elif defined (F4DISCOVERY)
	rcc_peripheral_enable_clock(&RCC_AHB1ENR, RCC_AHB1ENR_IOPAEN);
	if(!gpio_get(GPIOA, GPIO0)) {
#elif defined (USPS_F407)
	rcc_peripheral_enable_clock(&RCC_AHB1ENR, RCC_AHB1ENR_IOPBEN);
        /* Pull up an look if external pulled low or if we restart with PB1 low*/
        GPIOB_PUPDR |= 4;
        {
            int i;
            for(i=0; i<100000; i++) __asm__("NOP");
        }
	if(gpio_get(GPIOB, GPIO1)) {
#else
	rcc_peripheral_enable_clock(&RCC_APB2ENR, RCC_APB2ENR_IOPBEN);
	if(gpio_get(GPIOB, GPIO12)) {
#endif
		dfu_jump_app_if_valid();
	}

	dfu_protect_enable();

        /* Set up clock*/
#if defined (F4DISCOVERY) || defined(USPS_F407)
        rcc_clock_setup_hse_3v3(&hse_8mhz_3v3[CLOCK_3V3_168MHZ]);
	systick_set_clocksource(STK_CTRL_CLKSOURCE_AHB_DIV8);
	systick_set_reload(2100000);
#else
	rcc_clock_setup_in_hse_8mhz_out_72mhz();
	systick_set_clocksource(STK_CTRL_CLKSOURCE_AHB_DIV8);
	systick_set_reload(900000);
#endif

        /* Handle USB disconnect/connect */
#if defined(DISCOVERY_STLINK)
	/* Just in case: Disconnect USB cable by resetting USB Device
         * and pulling USB_DP low
         * Device will reconnect automatically as Pull-Up is hard wired*/
	rcc_peripheral_reset(&RCC_APB1RSTR, RCC_APB1ENR_USBEN);
	rcc_peripheral_clear_reset(&RCC_APB1RSTR, RCC_APB1ENR_USBEN);
	rcc_peripheral_enable_clock(&RCC_APB1ENR, RCC_APB1ENR_USBEN);
	rcc_peripheral_enable_clock(&RCC_APB2ENR, RCC_APB2ENR_IOPAEN);
	gpio_clear(GPIOA, GPIO12);
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ,
		GPIO_CNF_OUTPUT_OPENDRAIN, GPIO12);
#elif defined(F4DISCOVERY)
#elif defined(USPS_F407)
#elif defined(STM32_CAN)
#else
	rcc_peripheral_enable_clock(&RCC_APB2ENR, RCC_APB2ENR_IOPAEN);
        rcc_peripheral_enable_clock(&RCC_APB1ENR, RCC_APB1ENR_USBEN);
	gpio_set_mode(GPIOA, GPIO_MODE_INPUT, 0, GPIO8);

#endif
	systick_interrupt_enable();
	systick_counter_enable();

/* Handle LEDs */
#if defined(F4DISCOVERY)
	rcc_peripheral_enable_clock(&RCC_AHB1ENR, RCC_AHB1ENR_IOPDEN);
	gpio_clear(GPIOD, GPIO12 | GPIO13 | GPIO14 |GPIO15);
	gpio_mode_setup(GPIOD, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE,
			GPIO12 | GPIO13 | GPIO14 |GPIO15);
#elif defined(USPS_F407)
	rcc_peripheral_enable_clock(&RCC_AHB1ENR, RCC_AHB1ENR_IOPBEN);
	gpio_clear(GPIOB, GPIO2);
	gpio_mode_setup(GPIOB, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE,
			GPIO2);
#elif defined (STM32_CAN)
	gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_2_MHZ,
			GPIO_CNF_OUTPUT_PUSHPULL, GPIO0);
#else
	gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_2_MHZ,
			GPIO_CNF_OUTPUT_PUSHPULL, GPIO11);
	gpio_set_mode(GPIOB, GPIO_MODE_INPUT,
			GPIO_CNF_INPUT_FLOAT, GPIO2 | GPIO10);

#endif

/* Set up USB*/
#if defined(STM32_CAN)
	rcc_peripheral_enable_clock(&RCC_APB2ENR, RCC_APB2ENR_IOPAEN);
	rcc_peripheral_enable_clock(&RCC_AHBENR, RCC_AHBENR_OTGFSEN);
	rcc_peripheral_enable_clock(&RCC_APB2ENR, RCC_APB2ENR_IOPBEN);
	dfu_init(&stm32f107_usb_driver);
#elif defined(STM32F2)||defined(STM32F4)
	rcc_peripheral_enable_clock(&RCC_AHB1ENR, RCC_AHB1ENR_IOPAEN);
	rcc_peripheral_enable_clock(&RCC_AHB2ENR, RCC_AHB2ENR_OTGFSEN);

	/* Set up USB Pins and alternate function*/
	gpio_mode_setup(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE,
		GPIO9 | GPIO10 | GPIO11 | GPIO12);
	gpio_set_af(GPIOA, GPIO_AF10, GPIO9 | GPIO10| GPIO11 | GPIO12);
	dfu_init(&stm32f107_usb_driver);
#else
	dfu_init(&stm32f103_usb_driver);
#endif

#if defined(BLACKMAGIC)
	gpio_set(GPIOA, GPIO8);
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ,
			GPIO_CNF_OUTPUT_PUSHPULL, GPIO8);
#endif

	dfu_main();
}


void sys_tick_handler()
{
#if defined(DISCOVERY_STLINK)
	if (rev == 0)
		gpio_toggle(GPIOA, led_idle_run);
	else
	{
		if (led2_state & 1)
			gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ,
				GPIO_CNF_OUTPUT_PUSHPULL, led_idle_run);
		else
			gpio_set_mode(GPIOA, GPIO_MODE_INPUT,
				GPIO_CNF_INPUT_ANALOG, led_idle_run);
		led2_state++;
	}
#elif defined (F4DISCOVERY)
	gpio_toggle(GPIOD, GPIO12);  /* Green LED on/off */
#elif defined (USPS_F407)
	gpio_toggle(GPIOB, GPIO2);  /* Green LED on/off */
#elif defined(STM32_CAN)
	gpio_toggle(GPIOB, GPIO0);  /* LED2 on/off */
#else
	gpio_toggle(GPIOB, GPIO11); /* LED2 on/off */
#endif
}

