/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2015  Black Sphere Technologies Ltd.
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
#ifndef __GPIO_H
#define __GPIO_H

#include <libopencm3/cm3/common.h>

#ifndef STM32F4
#	include <libopencm3/stm32/f1/memorymap.h>
#	include <libopencm3/stm32/f1/gpio.h>
#else
#	include <libopencm3/stm32/f4/memorymap.h>
#	include <libopencm3/stm32/f4/gpio.h>
#endif

#define INLINE_GPIO

#define gpio_set_val(port, pin, val) do {	\
	if(val)					\
		gpio_set((port), (pin));	\
	else					\
		gpio_clear((port), (pin));	\
} while(0)

#ifdef INLINE_GPIO
static inline void _gpio_set(uint32_t gpioport, uint16_t gpios)
{
	GPIO_BSRR(gpioport) = gpios;
#ifdef STM32F4
	GPIO_BSRR(gpioport) = gpios;
#endif
}
#define gpio_set _gpio_set

static inline void _gpio_clear(uint32_t gpioport, uint16_t gpios)
{
#ifndef STM32F4
	GPIO_BRR(gpioport) = gpios;
#else
	GPIO_BSRR(gpioport) = gpios<<16;
	GPIO_BSRR(gpioport) = gpios<<16;
#endif
}
#define gpio_clear _gpio_clear

static inline uint16_t _gpio_get(uint32_t gpioport, uint16_t gpios)
{
	return (uint16_t)GPIO_IDR(gpioport) & gpios;
}
#define gpio_get _gpio_get
#endif

#endif

