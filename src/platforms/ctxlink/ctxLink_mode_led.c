////////////////////////////////////////////////////////////////////////////////////////////////////
// file:	src\platforms\ctxLink\wdbp_mode_led.c
//
// summary:	Wdbp mode LED class, provides support for indicating the mode of ctxLink using a single LED
////////////////////////////////////////////////////////////////////////////////////////////////////

#include "general.h"
#include "platform.h"
#include "ctxLink_mode_led.h"

mode_led_modes_e led_mode = mode_led_idle; // Initial state of the mode led
mode_led_task_states_e mode_task_state = mode_led_idle_state;

u_int32_t led_mode_timeout = 0;       // Used to time the on/off state of the led
u_int32_t led_mode_reset_timeout = 0; // Used to time the on/off state of the led
u_int32_t led_mode_pulse_count = 0;   // Counts the number of led pulses each cycle

#define MODE_LED_ON_TIME        200 // LED on time in 1  milliseconds ticks
#define MODE_LED_PULSE_OFF_TIME MODE_LED_ON_TIME
#define MODE_LED_OFF_TIME       3000

void mode_set_parameters(mode_led_modes_e led_mode)
{
	switch (led_mode) {
	case mode_led_idle: {
		led_mode_reset_timeout = led_mode_timeout = 0; // LED off
		led_mode_pulse_count = mode_led_idle;          // No pulsing
		// LED OFF
		break;
	}
	case mode_led_battery_low: {
		led_mode_reset_timeout = led_mode_timeout = MODE_LED_ON_TIME;
		led_mode_pulse_count = mode_led_battery_low;
		break;
	}
	case mode_led_ap_connected: {
		led_mode_reset_timeout = led_mode_timeout = MODE_LED_ON_TIME;
		led_mode_pulse_count = mode_led_ap_connected;
		break;
	}
	case mode_led_wps_active: {
		led_mode_reset_timeout = led_mode_timeout = MODE_LED_ON_TIME;
		led_mode_pulse_count = mode_led_wps_active;
		break;
	}
	case mode_led_http_provisioning: {
		led_mode_reset_timeout = led_mode_timeout = MODE_LED_ON_TIME;
		led_mode_pulse_count = mode_led_http_provisioning;
		break;
	}
	default: {
		break;
	}
	}
}

bool mode_check_timeout()
{
	bool fResult = false;

	if (--led_mode_timeout == 0) {
		led_mode_timeout = led_mode_reset_timeout;
		fResult = true;
	}
	return (fResult);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
/// <summary> State machine to control the mode LED display.</summary>
///
/// <remarks> Sid Price, 3/21/2018.</remarks>
////////////////////////////////////////////////////////////////////////////////////////////////////

static mode_led_modes_e saved_mode = mode_led_invalid;

void mode_led_task(void)
{
	//
	// Use this periodic task to check the battery voltage
	//
	if (platform_check_battery_voltage() == false) {
		if (saved_mode == mode_led_invalid) {
			saved_mode = led_mode;
			led_mode = mode_led_battery_low;
		}
	} else {
		if (saved_mode != mode_led_invalid) {
			led_mode = saved_mode;
			saved_mode = mode_led_invalid;
		}
	}
	switch (mode_task_state) {
	case mode_led_idle_state: {
		/*
			 * Get the mode setting and check if still idle
			 */
		if (led_mode != mode_led_idle) {
			/*
				 * Set up the led control registers according to the requested mode
				 */
			mode_task_state = mode_led_state_on;
#ifndef INSTRUMENT
			gpio_set(LED_PORT, LED_MODE);
#endif
		}
		mode_set_parameters(led_mode);
		break;
	}
	case mode_led_state_on: {
		if (mode_check_timeout()) {
			/*
				 * check if pulse count is zero and pulsing is done
				 */
			if (--led_mode_pulse_count == 0) {
				/*
					 * End of pulse cycle, turn LED off for long period
					 */
				mode_task_state = mode_led_state_led_off;
				led_mode_timeout = MODE_LED_OFF_TIME;
			} else {
				mode_task_state = mode_led_state_pulse_off;
				led_mode_timeout = MODE_LED_PULSE_OFF_TIME;
			}
#ifndef INSTRUMENT
			gpio_clear(LED_PORT, LED_MODE);
#endif
		}
		break;
	}
	case mode_led_state_pulse_off: {
		if (mode_check_timeout()) {
			mode_task_state = mode_led_state_on;
			led_mode_timeout = MODE_LED_ON_TIME;
#ifndef INSTRUMENT
			gpio_set(LED_PORT, LED_MODE);
#endif
		}
		break;
	}
	case mode_led_state_led_off: {
		if (mode_check_timeout()) {
			mode_task_state = mode_led_state_on;
			led_mode_timeout = MODE_LED_ON_TIME;
			mode_set_parameters(led_mode); // Reset registers for next cycle
#ifndef INSTRUMENT
			gpio_set(LED_PORT, LED_MODE); // LED on
#endif
		}
		break;
	}

	default: {
		break;
	}
	}
}
