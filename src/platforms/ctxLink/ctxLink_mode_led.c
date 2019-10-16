////////////////////////////////////////////////////////////////////////////////////////////////////
// file:	src\platforms\ctxLink\wdbp_mode_led.c
//
// summary:	Wdbp mode LED class, provides support for indicating the mode of ctxLink using a single LED
////////////////////////////////////////////////////////////////////////////////////////////////////

#include "general.h"
#include "ctxLink_mode_led.h"

MODE_LED_MODES	led_mode = MODE_LED_IDLE;	// Initial state of the mode led
MODE_LED_TASK_STATES modeTaskState = MODE_LED_STATE_IDLE;

u_int32_t ledModeTimeout = 0; 		// Used to time the on/off state of the led
u_int32_t ledModeResetTimeout = 0; 		// Used to time the on/off state of the led
u_int32_t ledModePulseCount = 0;  	// Counts the number of led pulses each cycle

#define MODE_LED_ON_TIME		200		// LED on time in 1  milliseconds ticks
#define MODE_LED_PULSE_OFF_TIME	MODE_LED_ON_TIME
#define MODE_LED_OFF_TIME		3000

void mode_setparameters(MODE_LED_MODES ledMode)
{
	switch ( ledMode )
	{
		case MODE_LED_IDLE:
		{
			ledModeResetTimeout = ledModeTimeout = 0; 					// LED off
			ledModePulseCount = MODE_LED_IDLE;	// No pulsing
			// LED OFF
			break ;
		}
		case MODE_LED_BATTERY_LOW:
		{
			ledModeResetTimeout = ledModeTimeout = MODE_LED_ON_TIME;
			ledModePulseCount = MODE_LED_BATTERY_LOW;
			break ;
		}
		case MODE_LED_AP_CONNECTED:
		{
			ledModeResetTimeout = ledModeTimeout = MODE_LED_ON_TIME;
			ledModePulseCount = MODE_LED_AP_CONNECTED;
			break ;
		}
		case MODE_LED_WPS_ACTIVE:
		{
			ledModeResetTimeout = ledModeTimeout = MODE_LED_ON_TIME;
			ledModePulseCount = MODE_LED_WPS_ACTIVE;
			break ;
		}
		case MODE_LED_HTTP_PROVISIONING:
		{
			ledModeResetTimeout = ledModeTimeout = MODE_LED_ON_TIME;
			ledModePulseCount = MODE_LED_HTTP_PROVISIONING;
			break ;
		}
		default:
		{
			break ;
		}
	}
}

bool modeCheckTimeout()
{
	bool	fResult = false;
	
	if ( --ledModeTimeout == 0 )
	{
		ledModeTimeout = ledModeResetTimeout;
		fResult = true;
	}
	return (fResult) ;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
/// <summary> State machine to control the mode LED display.</summary>
///
/// <remarks> Sid Price, 3/21/2018.</remarks>
////////////////////////////////////////////////////////////////////////////////////////////////////

static MODE_LED_MODES savedMode = MODE_LED_INVALID;

void mode_led_task(void)
{
	//
	// Use this periodic task to check the battery voltage
	//  
	if (platform_check_battery_voltage () == false) {
		if (savedMode == MODE_LED_INVALID)
		{
			savedMode = led_mode;
			led_mode = MODE_LED_BATTERY_LOW;
		}
	}
	else {
		if (savedMode != MODE_LED_INVALID)
		{
			led_mode = savedMode;
			savedMode = MODE_LED_INVALID;
		}
	}
	switch ( modeTaskState )
	{
		case MODE_LED_STATE_IDLE:
		{
			/*
			 * Get the mode setting and check if still idle
			 */
			if ( led_mode != MODE_LED_IDLE )
			{
				/*
				 * Set up the led control registers according to the requested mode
				 */
				modeTaskState = MODE_LED_STATE_ON;
#ifndef INSTRUMENT
				gpio_set(LED_PORT, LED_MODE);
#endif
			}
			mode_setparameters (led_mode);
			break;	
		}
		case MODE_LED_STATE_ON:
		{
			if ( modeCheckTimeout() == true )
			{
				/*
				 * check if pulse count is zero and pulsing is done
				 */
				if ( --ledModePulseCount == 0 )
				{
					/*
					 * End of pulse cycle, turn LED off for long period
					 */
					modeTaskState = MODE_LED_STATE_LED_OFF;
					ledModeTimeout = MODE_LED_OFF_TIME;
				}
				else
				{
					modeTaskState = MODE_LED_STATE_PULSE_OFF;
					ledModeTimeout = MODE_LED_PULSE_OFF_TIME;
				}
#ifndef INSTRUMENT
				gpio_clear(LED_PORT, LED_MODE);
#endif
			}
			break;	
		}
		case MODE_LED_STATE_PULSE_OFF:
		{
			if ( modeCheckTimeout() == true )
			{
				modeTaskState = MODE_LED_STATE_ON;
				ledModeTimeout = MODE_LED_ON_TIME;
#ifndef INSTRUMENT
				gpio_set(LED_PORT, LED_MODE);
#endif
			}
			break;	
		}
		case MODE_LED_STATE_LED_OFF:
		{
			if ( modeCheckTimeout() == true )
			{
				modeTaskState = MODE_LED_STATE_ON;
				ledModeTimeout = MODE_LED_ON_TIME;
				mode_setparameters(led_mode); 		// Reset registers for next cycle
#ifndef INSTRUMENT
				gpio_set(LED_PORT, LED_MODE);		// LED on
#endif
			}
			break;	
		}
		
		default:
		{
			break;
		}
	}
}