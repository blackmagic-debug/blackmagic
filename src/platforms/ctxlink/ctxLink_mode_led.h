////////////////////////////////////////////////////////////////////////////////////////////////////
// file:	wdbp_mode_led.h
//
// summary:	Declares the ctxLink mode_LED class
////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

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
	mode_led_invalid = -1,
	mode_led_idle = 0,         //	 0
	mode_led_battery_low,      //	 1
	mode_led_ap_connected,     //	 2
	mode_led_wps_active,       //	 3
	mode_led_http_provisioning //	 4
} mode_led_modes_e;

extern mode_led_modes_e led_mode;

void mode_led_task(void);
