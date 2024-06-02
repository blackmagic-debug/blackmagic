/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2024 1BitSquared <info@1bitsquared.com>
 * Written by OmniTechnoMancer <OmniTechnoMancer@wah.quest>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef BMDA_GPIOD_PLATFORM_H
#define BMDA_GPIOD_PLATFORM_H

#include <stdbool.h>

typedef struct gpiod_line gpiod_line_s;

void bmda_gpiod_set_pin(gpiod_line_s *pin, bool val);
bool bmda_gpiod_get_pin(gpiod_line_s *pin);

void bmda_gpiod_mode_input(gpiod_line_s *pin);
void bmda_gpiod_mode_output(gpiod_line_s *pin);

#define TMS_SET_MODE() \
	do {               \
	} while (false)

#define gpio_set(port, pin)          bmda_gpiod_set_pin(pin, true)
#define gpio_clear(port, pin)        bmda_gpiod_set_pin(pin, false)
#define gpio_get(port, pin)          bmda_gpiod_get_pin(pin)
#define gpio_set_val(port, pin, val) bmda_gpiod_set_pin(pin, val)

extern gpiod_line_s *bmda_gpiod_tck_pin;
#define TCK_PIN bmda_gpiod_tck_pin
extern gpiod_line_s *bmda_gpiod_tms_pin;
#define TMS_PIN bmda_gpiod_tms_pin
extern gpiod_line_s *bmda_gpiod_tdi_pin;
#define TDI_PIN bmda_gpiod_tdi_pin
extern gpiod_line_s *bmda_gpiod_tdo_pin;
#define TDO_PIN bmda_gpiod_tdo_pin

extern gpiod_line_s *bmda_gpiod_swdio_pin;
#define SWDIO_PIN bmda_gpiod_swdio_pin
extern gpiod_line_s *bmda_gpiod_swclk_pin;
#define SWCLK_PIN bmda_gpiod_swclk_pin

#define SWDIO_MODE_DRIVE() bmda_gpiod_mode_output(SWDIO_PIN)
#define SWDIO_MODE_FLOAT() bmda_gpiod_mode_input(SWDIO_PIN)

#endif
