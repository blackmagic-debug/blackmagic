////////////////////////////////////////////////////////////////////////////////////////////////////
// file:	wdbp_mode_led.h
//
// summary:	Declares the ctxLink mode_LED class
////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

typedef enum
{
	MODE_LED_STATE_IDLE = 0,	//	 0
	MODE_LED_STATE_ON,			//	 1
	MODE_LED_STATE_PULSE_OFF,	//	 2
	MODE_LED_STATE_LED_OFF		//	 3
} MODE_LED_TASK_STATES;

extern MODE_LED_TASK_STATES modeTaskState;
//
// Define the various modes of the "mode led"
//
typedef enum
{
	MODE_LED_INVALID = -1,
	MODE_LED_IDLE= 0,			//	 0
	MODE_LED_BATTERY_LOW,		//	 1
	MODE_LED_AP_CONNECTED,		//	 2
	MODE_LED_WPS_ACTIVE,		//	 3
	MODE_LED_HTTP_PROVISIONING	//	 4
} MODE_LED_MODES;

extern MODE_LED_MODES led_mode;

void mode_led_task(void);