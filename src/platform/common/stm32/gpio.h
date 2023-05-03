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

#ifndef PLATFORMS_STM32_GPIO_H
#define PLATFORMS_STM32_GPIO_H

#include <libopencm3/cm3/common.h>
#include <libopencm3/stm32/memorymap.h>
#include <libopencm3/stm32/gpio.h>

static inline void bmp_gpio_set(const uint32_t gpioport, const uint16_t gpios)
{
	/* NOLINTNEXTLINE(clang-diagnostic-int-to-pointer-cast) */
	GPIO_BSRR(gpioport) = gpios;
#if defined(STM32F4) || defined(STM32F7)
	/* FIXME: Check if doubling is still needed */
	/* NOLINTNEXTLINE(clang-diagnostic-int-to-pointer-cast) */
	GPIO_BSRR(gpioport) = gpios;
#endif
}

#define gpio_set bmp_gpio_set

static inline void bmp_gpio_clear(const uint32_t gpioport, const uint16_t gpios)
{
#ifdef GPIO_BRR
	/* NOLINTNEXTLINE(clang-diagnostic-int-to-pointer-cast) */
	GPIO_BRR(gpioport) = gpios;
#else
#if defined(STM32F4) || defined(STM32F7)
	/* NOLINTNEXTLINE(clang-diagnostic-int-to-pointer-cast) */
	GPIO_BSRR(gpioport) = gpios << 16U;
#endif
	/* NOLINTNEXTLINE(clang-diagnostic-int-to-pointer-cast) */
	GPIO_BSRR(gpioport) = gpios << 16U;
#endif
}

#define gpio_clear bmp_gpio_clear

static inline uint16_t bmp_gpio_get(const uint32_t gpioport, const uint16_t gpios)
{
	/* NOLINTNEXTLINE(clang-diagnostic-int-to-pointer-cast) */
	return GPIO_IDR(gpioport) & gpios;
}

#define gpio_get bmp_gpio_get

static inline void gpio_set_val(const uint32_t gpioport, const uint16_t gpios, const bool val)
{
	if (val)
		gpio_set(gpioport, gpios);
	else
		gpio_clear(gpioport, gpios);
}

#endif /* PLATFORMS_STM32_GPIO_H */
