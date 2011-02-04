/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
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

/* This file implements the low-level JTAG TAP interface.  */

#include <libopencm3/stm32/gpio.h>
#include <stdio.h>

#include "general.h"

#include "jtagtap.h"

int jtagtap_init(void)
{
	/* This needs some fixing... */
	/* Toggle required to sort out line drivers... */
	gpio_port_write(GPIOA, 0x8100);
	gpio_port_write(GPIOB, 0x0000);

	gpio_port_write(GPIOA, 0x8180);
	gpio_port_write(GPIOB, 0x0002);
	
	gpio_set_mode(JTAG_PORT, GPIO_MODE_OUTPUT_10_MHZ, 
		GPIO_CNF_OUTPUT_PUSHPULL, TMS_PIN); 

	/* Go to JTAG mode for SWJ-DP */
	for(int i = 0; i <= 50; i++) jtagtap_next(1, 0); /* Reset SW-DP */
	jtagtap_tms_seq(0xE73C, 16);		/* SWD to JTAG sequence */
	jtagtap_soft_reset();

	return 0;
}

void jtagtap_reset(void)
{
	volatile int i;
	gpio_clear(GPIOB, GPIO1);
	for(i = 0; i < 10000; i++) asm("nop");
	gpio_set(GPIOB, GPIO1);
	jtagtap_soft_reset();
}

void jtagtap_srst(void)
{
	volatile int i;
	gpio_set(GPIOA, GPIO2);
	for(i = 0; i < 10000; i++) asm("nop");
	gpio_clear(GPIOA, GPIO2);
}

inline uint8_t jtagtap_next(uint8_t dTMS, uint8_t dTDO)
{
	uint8_t ret;

	gpio_set_val(JTAG_PORT, TMS_PIN, dTMS);
	gpio_set_val(JTAG_PORT, TDI_PIN, dTDO);
	gpio_set(JTAG_PORT, TCK_PIN);
	ret = gpio_get(JTAG_PORT, TDO_PIN);
	gpio_clear(JTAG_PORT, TCK_PIN);

	DEBUG("jtagtap_next(TMS = %d, TDO = %d) = %d\n", dTMS, dTDO, ret);

	return ret;
}



#define PROVIDE_GENERIC_JTAGTAP_TMS_SEQ
#define PROVIDE_GENERIC_JTAGTAP_TDI_TDO_SEQ
#define PROVIDE_GENERIC_JTAGTAP_TDI_SEQ

#include "jtagtap_generic.c"

