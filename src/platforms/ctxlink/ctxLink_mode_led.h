/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2024 Sid Price <sid@sidprice.com>
 * Written by Sid Price <sid@sidprice.com>
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
///////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef WDP_MODE_LED_H
#define WDP_MODE_LED_H

typedef enum {
	mode_led_idle_state = 0,  //	 0
	mode_led_state_on,        //	 1
	mode_led_state_pulse_off, //	 2
	mode_led_state_led_off    //	 3
} mode_led_task_states_e;

extern mode_led_task_states_e mode_task_state;

//
// Define the various modes of the "mode led"
//
typedef enum {
	mode_led_idle = 0,          //	 0
	mode_led_battery_low,       //	 1
	mode_led_ap_connected,      //	 2
	mode_led_wps_active,        //	 3
	mode_led_http_provisioning, //	 4
	mode_led_invalid = 255U
} mode_led_modes_e;

extern mode_led_modes_e led_mode;

void mode_led_task(void);

#endif // WDP_MODE_LED_H
