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

#include "general.h"
#include "morse.h"

#include <libopencm3/stm32/f4/rcc.h>
#include <libopencm3/cm3/scb.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/exti.h>
#include <libopencm3/stm32/timer.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/stm32/syscfg.h>
#include <libopencm3/usb/usbd.h>

#include <libopencm3/stm32/f4/adc.h>
#include <libopencm3/stm32/f4/spi.h>

#include "WiFi_Server.h"

#include "platform.h"

#include "ctxLink_mode_led.h"

#include "winc1500_api.h"
#include "winc1500_driver_api_helpers.h"
#include "wf_socket.h"

void trace_send_data(void);

#define GDB_SERVER_PORT        2159
#define UART_DEBUG_SERVER_PORT 2160
#define SWO_TRACE_SERVER_PORT  2161

//
// TIMER2 is used to provide a 1mS tick
//
#define TIMER2_COMPARE_VALUE 50

#define TIMEOUT_TICK_COUNT 1000

#define INPUT_BUFFER_SIZE 2048

static uint8_t input_buffer[INPUT_BUFFER_SIZE] = {0}; ///< The input buffer[ input buffer size]
static volatile uint32_t input_index = 0;             ///< Zero-based index of the input
static volatile uint32_t output_index = 0;            ///< Zero-based index of the output
static volatile uint32_t buffer_count = 0;            ///< Number of buffers

static uint8_t local_buffer[INPUT_BUFFER_SIZE] = {0}; ///< The local buffer[ input buffer size]

static uint8_t send_buffer[1024] = {0}; ///< The send buffer[ 1024]
static uint32_t send_count = 0;         ///< Number of sends

static uint32_t timeout_seconds = 0;      // Used to perform a countdown in seconds for app tasks
static uint32_t timeout_tick_counter = 0; // counts Timer1 ticks (1mS) for an active timeout
static bool timeout = false;              // Asserted at end of timeout

typedef enum server_state {
	sm_home = 0,  ///< State of the TCP server
	sm_listening, ///< An enum constant representing the sm listening option
	sm_closing,   ///< An enum constant representing the sm closing option
	sm_idle,      ///< An enum constant representing the sm idle option
} server_state_e;

static server_state_e gdb_tcp_server_state = sm_idle;
static server_state_e uart_debug_tcp_server_state = sm_idle;
static server_state_e swo_trace_tcp_serverstate = sm_idle;

struct sockaddr_in gdb_addr = {0};

struct sockaddr_in uart_debug_addr = {0};
struct sockaddr_in swo_trace_addr = {0};

static volatile SOCKET socket_parameter = 0;

//
// Flag used to run the MODE LED state machine
//
static bool run_mode_led_task = false; ///< True to run mode LED task
static uint32_t press_timer = 0;       ///< The press timer

static bool driver_init_complete = false;     ///< True to driver initialize complete
static bool g_wifi_connected = false;         ///< True if WiFi connected
static bool ip_address_assigned = false;      ///< True if IP address assigned
static bool dns_resolved = false;             ///< True if DNS resolved
static bool gdb_client_connected = false;     ///< True if client connected
static bool gdb_server_is_running = false;    ///< True if server is running
static bool new_gdb_client_connected = false; ///< True if new client connected
static bool connection_info_received = false; ///< True when all connection information is received

static SOCKET gdb_server_socket = SOCK_ERR_INVALID; ///< The main gdb server socket
static SOCKET gdb_client_socket = SOCK_ERR_INVALID; ///< The gdb client socket

#define UART_DEBUG_INPUT_BUFFER_SIZE 32
static uint8_t local_uart_debug_buffer[UART_DEBUG_INPUT_BUFFER_SIZE] = {0}; ///< The local buffer[ input buffer size]

static SOCKET uart_debug_server_socket = SOCK_ERR_INVALID;
static SOCKET uart_debug_client_socket = SOCK_ERR_INVALID;
static bool uart_debug_client_connected = false;
static bool user_configured_uart = false;
static bool uart_debug_server_is_running = false;
static bool new_uart_debug_client_conncted = false;

static SOCKET swo_trace_server_socket = SOCK_ERR_INVALID;
static SOCKET swo_trace_client_socket = SOCK_ERR_INVALID;
static bool swo_trace_client_connected = false;
static bool swo_trace_server_is_running = false;
static bool new_swo_trace_client_conncted = false;

tstrM2MConnInfo conn_info;

#define SWO_TRACE_INPUT_BUFFER_SIZE 32
static uint8_t local_swo_trace_buffer[SWO_TRACE_INPUT_BUFFER_SIZE] = {0}; ///< The local buffer[ input buffer size]

#define WPS_LOCAL_TIMEOUT 30 // Timeout value in seconds

//
// Sign-on message for new UART data clients
//
static const char uart_client_signon[] = "\r\nctxLink UART connection.\r\nPlease enter the UART setup as baud, bits, "
										 "parity, "
										 "stop.\r\ne.g. 38400,8,N,1\r\n\r\n";

typedef enum wi_fi_app_states {
	app_state_wait_for_driver_init,          ///< 0
	app_state_read_mac_address,              ///< 1
	app_state_connect_to_wifi,               ///< 2
	app_state_wait_wifi_disconnect_for_wps,  ///< 3
	app_state_wait_wifi_disconnect_for_http, ///< 4
	app_state_connect_wps,                   ///< 5
	app_state_wait_wps_event,                ///< 6
	app_state_http_provision,                ///< 7
	app_state_wait_provision_event,          ///< 8
	app_state_wait_for_wifi_connect,         ///< 9
	app_state_start_server,                  ///< 10
	app_state_wait_for_server,               ///< 11
	app_state_error,                         ///< 12
	app_state_check_default_connections,     ///< 13
	app_state_spin,                          ///< 14
	app_state_wait_connection_info,          ///< 15
	app_state_wait_for_disconnect            ///< 16
} app_states_e;

app_states_e app_state; ///< State of the application
/*
 * Define the send queue, this is used in the socket event callback
 * to correctly process output and sync with ACKs
 */
#define SEND_QUEUE_SIZE        4
#define SEND_QUEUE_BUFFER_SIZE 1024

#define BUTTON_PRESS_WPS               2500 // Enter WPS mode if 2.5 Seconds > 2.5S < 5 Seconds
#define BUTTON_PRESS_HTTP_PROVISIONING 5000
#define BUTTON_PRESS_MODE_CANCEL       7500

typedef struct {
	uint8_t packet[SEND_QUEUE_BUFFER_SIZE]; ///< The packet[ send queue buffer size]
	uint32_t len;                           ///< The length
} send_queue_entry_s;

send_queue_entry_s gdb_send_queue[SEND_QUEUE_SIZE] = {0}; ///< The send queue[ send queue size]
uint32_t volatile gdb_send_queue_in = 0;                  ///< The send queue in
uint32_t volatile gdb_send_queue_out = 0;                 ///< The send queue out
uint32_t volatile gdb_send_queue_length = 0;              ///< Length of the send queue
void do_gdb_send(void);

send_queue_entry_s uart_debug_send_queue[SEND_QUEUE_SIZE] = {0}; ///< The send queue[ send queue size]
uint32_t volatile uart_debug_send_queue_in = 0;                  ///< The send queue in
uint32_t volatile uart_debug_send_queue_out = 0;                 ///< The send queue out
uint32_t volatile uart_debug_send_queue_length = 0;              ///< Length of the send queue
void do_uart_debug_send(void);

send_queue_entry_s swo_trace_send_queue[SEND_QUEUE_SIZE] = {0}; ///< The send queue[ send queue size]
uint32_t volatile swo_trace_send_queue_in = 0;                  ///< The send queue in
uint32_t volatile swo_trace_send_queue_out = 0;                 ///< The send queue out
uint32_t volatile swo_trace_send_queue_length = 0;              ///< Length of the send queue
void do_awo_trace_send(void);

static bool press_active = false; ///< True to press active
bool wps_active = false;          ///< True to wps active
bool http_active = false;         ///< True when HTTP provisioning is active
bool waiting_access_point =
	false; ///< Set true when http provisioning is started, this allows ignoring of that connection event

void exti9_5_isr(void)
{
	//
	// Is it EXTI9?
	//
	if (exti_get_flag_status(EXTI9) == EXTI9) {
		// Reset the interrupt state
		exti_reset_request(EXTI9);
		m2m_EintHandler();
	}
}

void start_press_timer(void)
{
	timer_disable_irq(TIM2, TIM_DIER_CC1IE);
	press_timer = 0; // Reset the press timer
	timer_enable_irq(TIM2, TIM_DIER_CC1IE);
}

uint32_t get_press_timer(void)
{
	timer_disable_irq(TIM2, TIM_DIER_CC1IE);
	const uint32_t tmp = press_timer;
	timer_enable_irq(TIM2, TIM_DIER_CC1IE);
	return tmp;
}

void tim2_start_seconds_timeout(uint32_t timeout)
{
	timer_disable_irq(TIM2, TIM_DIER_CC1IE);
	timeout_tick_counter = TIMEOUT_TICK_COUNT;
	timeout_seconds = timeout;
	timeout = false;
	timer_enable_irq(TIM2, TIM_DIER_CC1IE);
}

void tim2_cancel_seconds_timeout(void)
{
	timer_disable_irq(TIM2, TIM_DIER_CC1IE);
	timeout_tick_counter = 0;
	timeout_seconds = 0;
	timeout = false;
	timer_enable_irq(TIM2, TIM_DIER_CC1IE);
}

bool tim2_is_seconds_timeout(void)
{
	return timeout;
}

void tim2_isr(void)
{
	if (timer_get_flag(TIM2, TIM_SR_CC1IF)) {
		/* Clear compare interrupt flag. */
		timer_clear_flag(TIM2, TIM_SR_CC1IF);

		/*
		 * Get current timer value to calculate next
		 * compare register value.
		 */
		const uint16_t compare_time = timer_get_counter(TIM2);

		/* Calculate and set the next compare value. */
		const uint16_t frequency = TIMER2_COMPARE_VALUE;
		const uint16_t new_time = compare_time + frequency;

		timer_set_oc_value(TIM2, TIM_OC1, new_time);
		//timer_set_counter (TIM2, 0);
		m2m_TMR_ISR();
		run_mode_led_task = true;
		press_timer++;
	}
	/*
		Do we have a seconds timeout setup
	*/
	if (timeout_seconds != 0 && --timeout_tick_counter == 0) {
		if (--timeout_seconds == 0)
			timeout = true;
		else
			timeout_tick_counter = TIMEOUT_TICK_COUNT;
	}
	platform_read_adc();
}

void timer_init(void)
{
	/* Enable TIM2 clock. */
	rcc_periph_clock_enable(RCC_TIM2);

	/* Enable TIM2 interrupt. */
	nvic_enable_irq(NVIC_TIM2_IRQ);

	/* Reset TIM2 peripheral to defaults. */
	rcc_periph_reset_pulse(RST_TIM2);

	/* Timer global mode:
	 * - No divider
	 * - Alignment edge
	 * - Direction up
	 * (These are actually default values after reset above, so this call
	 * is strictly unnecessary, but demos the api for alternative settings)
	 */
	timer_set_mode(TIM2, TIM_CR1_CKD_CK_INT, TIM_CR1_CMS_EDGE, TIM_CR1_DIR_UP);

	/*
	 * Please take note that the clock source for STM32 timers
	 * might not be the raw APB1/APB2 clocks.  In various conditions they
	 * are doubled.  See the Reference Manual for full details!
	 * In our case, TIM2 on APB1 is running at double frequency, so this
	 * sets the prescaler to have the timer run at 50kHz
	 */
	timer_set_prescaler(TIM2, ((rcc_apb1_frequency * 2) / 50000));

	/* Disable preload. */
	timer_disable_preload(TIM2);
	timer_continuous_mode(TIM2);

	/* count full range, as we'll update compare value continuously */
	timer_set_period(TIM2, 65535);

	/* Set the initual output compare value for OC1. */
	timer_set_oc_value(TIM2, TIM_OC1, TIMER2_COMPARE_VALUE);

	/* Counter enable. */
	timer_enable_counter(TIM2);

	/* Enable Channel 1 compare interrupt to recalculate compare values */
	timer_enable_irq(TIM2, TIM_DIER_CC1IE);
}

void do_wifi_disconnect(void)
{
	//
	// Clients
	//
	if (gdb_client_socket != SOCK_ERR_INVALID) {
		close(gdb_client_socket);
		gdb_client_connected = false;
		gdb_client_socket = SOCK_ERR_INVALID;
	}
	if (uart_debug_client_socket != SOCK_ERR_INVALID) {
		close(uart_debug_client_socket);
		uart_debug_client_connected = false;
		uart_debug_client_socket = SOCK_ERR_INVALID;
	}
	if (swo_trace_client_socket != SOCK_ERR_INVALID) {
		close(swo_trace_client_socket);
		swo_trace_client_connected = false;
		swo_trace_client_socket = SOCK_ERR_INVALID;
	}
	//
	// Servers
	//
	if (gdb_server_socket != SOCK_ERR_INVALID) {
		gdb_tcp_server_state = sm_idle;
		gdb_server_is_running = false;
		close(gdb_server_socket);
		gdb_server_socket = SOCK_ERR_INVALID;
	}
	if (uart_debug_server_socket != SOCK_ERR_INVALID) {
		uart_debug_tcp_server_state = sm_idle;
		uart_debug_server_is_running = false;
		close(uart_debug_server_socket);
		uart_debug_server_socket = SOCK_ERR_INVALID;
	}
	if (swo_trace_server_socket != SOCK_ERR_INVALID) {
		swo_trace_tcp_serverstate = sm_idle;
		swo_trace_server_is_running = false;
		close(swo_trace_server_socket);
		swo_trace_server_socket = SOCK_ERR_INVALID;
	}
}

void gdb_tcp_server(void)
{
	switch (gdb_tcp_server_state) {
	case sm_idle:
		break; // Startup and testing do nothing state
	case sm_home: {
		//
		// Allocate a socket for this server to listen and accept connections on
		//
		gdb_server_socket = socket(AF_INET, SOCK_STREAM, 0);
		if (gdb_server_socket < SOCK_ERR_NO_ERROR)
			return;
		//
		// Bind the socket
		//
		gdb_addr.sin_addr.s_addr = 0;
		gdb_addr.sin_family = AF_INET;
		gdb_addr.sin_port = _htons(GDB_SERVER_PORT);
		const int8_t result = bind(gdb_server_socket, (struct sockaddr *)&gdb_addr, sizeof(gdb_addr));
		if (result != SOCK_ERR_NO_ERROR)
			return;
		gdb_tcp_server_state = sm_listening;
		break;
	}

	case sm_listening:
		//
		// No need to perform any flush.
		// TCP data in TX FIFO will automatically transmit itself after it accumulates for a while.
		// If you want to decrease latency (at the expense of wasting network bandwidth on TCP overhead),
		// perform and explicit flush via the TCPFlush() API.
		break;

	case sm_closing:
		// Close the socket connection.
		close(gdb_server_socket);
		gdb_server_socket = SOCK_ERR_INVALID;
		gdb_tcp_server_state = sm_idle;
		break;
	}
}

void data_tcp_server(void)
{
	//
	// UART Server
	//
	switch (uart_debug_tcp_server_state) {
	case sm_idle: {
		break; // Startup and testing do nothing state
	}
	case sm_home: {
		//
		// Allocate a socket for this server to listen and accept connections on
		//
		uart_debug_server_socket = socket(AF_INET, SOCK_STREAM, 0);
		if (uart_debug_server_socket < SOCK_ERR_NO_ERROR)
			return;
		//
		// Bind the socket
		//
		uart_debug_addr.sin_addr.s_addr = 0;
		uart_debug_addr.sin_family = AF_INET;
		uart_debug_addr.sin_port = _htons(UART_DEBUG_SERVER_PORT);
		int8_t result = bind(uart_debug_server_socket, (struct sockaddr *)&uart_debug_addr, sizeof(uart_debug_addr));
		if (result != SOCK_ERR_NO_ERROR)
			return;
		uart_debug_tcp_server_state = sm_listening;
		break;
	}
	case sm_listening: {
		//
		// No need to perform any flush.
		// TCP data in TX FIFO will automatically transmit itself after it accumulates for a while.
		// If you want to decrease latency (at the expense of wasting network bandwidth on TCP overhead),
		// perform and explicit flush via the TCPFlush() API.
		break;
	}
	case sm_closing: {
		// Close the socket connection.
		close(uart_debug_server_socket);
		uart_debug_server_socket = SOCK_ERR_INVALID;
		uart_debug_tcp_server_state = sm_idle;
		break;
	}
	}
	//
	// SWO Trace Data server
	//
	switch (swo_trace_tcp_serverstate) {
	case sm_idle: {
		break; // Startup and testing do nothing state
	}
	case sm_home: {
		//
		// Allocate a socket for this server to listen and accept connections on
		//
		swo_trace_server_socket = socket(AF_INET, SOCK_STREAM, 0);
		if (swo_trace_server_socket < SOCK_ERR_NO_ERROR)
			return;
		//
		// Bind the socket
		//
		swo_trace_addr.sin_addr.s_addr = 0;
		swo_trace_addr.sin_family = AF_INET;
		swo_trace_addr.sin_port = _htons(SWO_TRACE_SERVER_PORT);
		int8_t result = bind(swo_trace_server_socket, (struct sockaddr *)&swo_trace_addr, sizeof(swo_trace_addr));
		if (result != SOCK_ERR_NO_ERROR)
			return;
		swo_trace_tcp_serverstate = sm_listening;
		break;
	}
	case sm_listening: {
		//
		// No need to perform any flush.
		// TCP data in TX FIFO will automatically transmit itself after it accumulates for a while.
		// If you want to decrease latency (at the expense of wasting network bandwidth on TCP overhead),
		// perform an explicit flush via the TCPFlush() API.
		break;
	}
	case sm_closing: {
		// Close the socket connection.
		close(swo_trace_server_socket);
		swo_trace_server_socket = SOCK_ERR_INVALID;
		swo_trace_tcp_serverstate = sm_idle;
		break;
	}
	}
}

void wifi_setup_swo_trace_server(void)
{
	//
	// Is there a UART client connected?
	//
	if (uart_debug_client_connected) {
		// Close the connection to the client
		close(uart_debug_client_socket);
		uart_debug_client_socket = SOCK_ERR_INVALID; // Mark socket invalid
		uart_debug_client_connected = false;         // No longer connected
		user_configured_uart = false;
	}
	//
	// If the UART server is up, close it timeDown
	//
	if (uart_debug_server_socket != SOCK_ERR_INVALID) {
		close(uart_debug_server_socket);
		uart_debug_server_socket = SOCK_ERR_INVALID;
	}
	//
	// Set up the SWO Trace Server
	//
	swo_trace_tcp_serverstate = sm_home;
}

static void app_wifi_callback(uint8_t msg_type, void *msg)
{
	(void)msg;
	switch (msg_type) {
	case M2M_WIFI_DRIVER_INIT_EVENT: {
		driver_init_complete = true;
		break;
	}

	case M2M_WIFI_CONN_INFO_RESPONSE_EVENT: {
		tstrM2MConnInfo *connection_info = (tstrM2MConnInfo *)msg;
		memcpy(&conn_info, connection_info, sizeof(tstrM2MConnInfo));
		connection_info_received = true;
		break;
	}
	case M2M_WIFI_CONN_STATE_CHANGED_EVENT: {
		tstrM2mWifiStateChanged *wifi_state = (tstrM2mWifiStateChanged *)msg;
		if (wifi_state->u8CurrState == M2M_WIFI_CONNECTED) {
			//
			// If we are in http access mode there will be a connection event when the
			// provisioning client connects, we need to ignore this event.
			//
			// The connection event is the provisioning client if:
			//		http_active == true && waiting_access_point == true
			//
			// Otherwise it is a valid ctxLink-> AP connection
			//
			if (http_active && waiting_access_point)
				waiting_access_point = false; // Next event is the ctxLink to AP event
			else {
				DEBUG_WARN("APP_WIFI_CB[%d]: Connected to AP\r\n", msg_type);
				g_wifi_connected = true;
			}
		} else if (wifi_state->u8CurrState == M2M_WIFI_DISCONNECTED) {
			DEBUG_WARN("APP_WIFI_CB[%d]: Disconnected from AP\r\n", msg_type);
			g_wifi_connected = false;
		} else
			DEBUG_WARN("APP_WIFI_CB[%d]: Unknown WiFi state change\r\n", msg_type);
		break;
	}

	case M2M_WIFI_IP_ADDRESS_ASSIGNED_EVENT: {
		tstrM2MIPConfig *ip_config = (tstrM2MIPConfig *)msg;
		if (ip_config != NULL) {
			ip_address_assigned = true;
			//
			// Request the connection info, user may request it
			//
			m2m_wifi_get_connection_info();
		} else
			ip_address_assigned = false;
		break;
	}

	case M2M_WIFI_WPS_EVENT: {
		tstrM2MWPSInfo *wifi_wps_info = (tstrM2MWPSInfo *)msg;
		DEBUG_WARN("Wi-Fi request WPS\r\n");
		DEBUG_WARN("SSID : %s, authtyp : %d pw : %s\n", wifi_wps_info->au8SSID, wifi_wps_info->u8AuthType,
			wifi_wps_info->au8PSK);
		if (wifi_wps_info->u8AuthType == 0) {
			DEBUG_WARN("WPS is not enabled OR Timedout\r\n");
			/*
					WPS monitor timeout.
				 */
			m2m_wifi_wps_disable();
			wps_active = false;
		} else {
			DEBUG_WARN("Request Wi-Fi connect\r\n");
			m2m_wifi_connect_sc((char *)wifi_wps_info->au8SSID, strlen((char *)wifi_wps_info->au8SSID),
				wifi_wps_info->u8AuthType, wifi_wps_info->au8PSK, wifi_wps_info->u8Ch);
		}

		break;
	}
	case M2M_WIFI_PROVISION_INFO_EVENT: {
		tstrM2MProvisionInfo *provision_info = (tstrM2MProvisionInfo *)msg;
		if (provision_info->u8Status == M2M_SUCCESS)
			m2m_wifi_connect_sc((char *)provision_info->au8SSID, strlen((char const *)provision_info->au8SSID),
				provision_info->u8SecType, provision_info->au8Password, M2M_WIFI_CH_ALL);
		else
			m2m_wifi_stop_provision_mode();
		break;
	}
	case M2M_WIFI_DEFAULT_CONNNECT_EVENT: {
		//tstrM2MDefaultConnResp *pDefaultConnResp = (tstrM2MDefaultConnResp *) msg;
		DEBUG_WARN("APP_WIFI_CB[%d]: Un-implemented state\r\n", msg_type);
		break;
	}
		/* Unused states. Can be implemented if needed  */
	case M2M_WIFI_SCAN_DONE_EVENT:
	case M2M_WIFI_SCAN_RESULT_EVENT:
	case M2M_WIFI_SYS_TIME_EVENT:
	case M2M_WIFI_PRNG_EVENT:
	case M2M_WIFI_IP_CONFLICT_EVENT:
	case M2M_WIFI_INVALID_WIFI_EVENT:
	case M2M_WIFI_RSSI_EVENT: {
		DEBUG_WARN("APP_WIFI_CB[%d]: Un-implemented state\r\n", msg_type);
		break;
	}

	default: {
		DEBUG_WARN("APP_WIFI_CB[%d]: Unknown WiFi state\r\n", msg_type);
		break;
	}
	}
}

bool is_driver_init_complete(void)
{
	bool res = driver_init_complete;
	driver_init_complete = false;
	return res;
}

bool is_wifi_connected(void)
{
	bool res = g_wifi_connected;
	// no need to reset flag "g_wifi_connected" to false. Event will do that.
	return res;
}

bool is_ip_address_assigned(void)
{
	bool res = ip_address_assigned;
	ip_address_assigned = false;
	return res;
}

//
// Format the current connection data for display to user
//
//	SSID = 'name'
//	RSSI = xx
//	ip = xxx.xxx.xxx.xxx
//
void wifi_get_ip_address(char *buffer, uint32_t size)
{
	char local_buffer[64] = {0};
	memset(buffer, 0x00, size);
	if (g_wifi_connected) {
		snprintf(local_buffer, sizeof(local_buffer), "SSID = %s\n", conn_info.acSSID);
		strncpy(buffer, local_buffer, size);
		snprintf(local_buffer, sizeof(local_buffer), "RSSI = %d\n", conn_info.s8RSSI);
		strncat(buffer, local_buffer, size);
		snprintf(local_buffer, sizeof(local_buffer), "IP = %d.%d.%d.%d\n", conn_info.au8IPAddr[0],
			conn_info.au8IPAddr[1], conn_info.au8IPAddr[2], conn_info.au8IPAddr[3]);
		strncat(buffer, local_buffer, size);
	} else {
		memcpy(buffer, "Not connected\n", strlen("Not connected\n"));
	}
}

//
// Wait for app_state to spin with timeout
//
void app_task_wait_spin(void)
{
	uint32_t wait_timeout = 2000U;
	while (true) {
		platform_tasks();
		if (app_state == app_state_spin)
			break;
		platform_delay(1U);
		if (wait_timeout-- == 0U)
			break;
	}
}

//
// Using the passed arguments, attempt to connect to a Wi-Fi AP
//
void wifi_connect(size_t argc, const char **argv, char *buffer, uint32_t size)
{
	char ssid[64] = {0};
	char pass_phrase[64] = {0};
	char *output_buffer = ssid;
	bool first_element = true;
	memset(buffer, 0x00, size);
	//
	// Iterate over the arguments received to build the SSID and passphrase
	//
	// The BMF command line parser treats spaces as delimiters and both SSID
	// and the passphrase may have embedded spaces. The SSID and passphrase
	// should have a comma separator.
	//
	// The following loop concatenates each arg and adds back the space delimiter.
	//
	// It does this initially into the SSID name, up until an argument is found
	// that contains a comma. The string before the comma is concatenated with
	// the SSID and the string after the comma is used as the first element of
	// the passphrase.
	//
	// The remaining arguments are then concatenated into the passphrase with
	// an space added between them.
	//
	for (size_t loop = 1; loop < argc; loop++) {
		char *const delimeter = strchr(argv[loop], ',');
		if (delimeter == NULL || output_buffer == pass_phrase) {
			if (!first_element)
				strcat(output_buffer, " ");
			strcat(output_buffer, argv[loop]);
			first_element = false;
		} else {
			if (!first_element)
				strcat(output_buffer, " ");
			*delimeter = 0x00;                 // Null terminate string before comma
			strcat(output_buffer, argv[loop]); // Complete the string in the output buffer
			//
			// Start the passphrase with the remaining string
			//
			output_buffer = pass_phrase;
			first_element = false; // The end of the split argument contained the first element
			strcat(output_buffer, delimeter + 1);
		}
	}
	//
	// If we have both SSID and Passphrase attempt to connect
	//
	if (ssid[0] != '\0' && pass_phrase[0] != '\0') {
		if (is_wifi_connected()) {
			//
			// Issue a disconnect first
			//
			app_state = app_state_wait_for_disconnect;
			wifi_disconnect();
			app_task_wait_spin();
		}
		//
		// Force app_task into wait for wifi connect
		//
		app_state = app_state_wait_for_wifi_connect;
		m2m_wifi_connect_sc(ssid, strlen(ssid), M2M_WIFI_SEC_WPA_PSK, &pass_phrase, M2M_WIFI_CH_ALL);
	}
}

//
// Disconnect from network
//
void wifi_disconnect(void)
{
	app_state = app_state_wait_for_disconnect;
	m2m_wifi_disconnect();
}

void handle_socket_bind_event(SOCKET *sock, bool *running_state)
{
	if (m2m_wifi_get_socket_event_data()->bindStatus == 0)
		listen(*sock, 0);
	else {
		close(*sock);
		*sock = SOCK_ERR_INVALID;
		*running_state = false;
	}
}

void handle_socket_listen_event(SOCKET *sock, bool *running_state)
{
	if (m2m_wifi_get_socket_event_data()->listenStatus == 0) {
		accept(*sock, NULL, NULL);
		*running_state = true;
	} else {
		close(*sock);
		*sock = SOCK_ERR_INVALID;
		*running_state = false;
	}
}

void handle_socket_accept_event(t_socketAccept *accept_data, SOCKET *client_socket, bool *client_connected_state,
	bool *new_client_connected, uint8_t msg_type)
{
	(void)msg_type;
	if (accept_data->sock >= 0) {
		//
		// Only allow a single client connection
		//
		if (*client_socket >= 0) {
			/*
			 * close the new client socket, refusing connection
			 *
			 */
			DEBUG_WARN("APP_SOCK_CB[%d]: Second connection rejected\r\n", msg_type);
			close(accept_data->sock);
		} else {
			*client_socket = accept_data->sock;
			*client_connected_state = true;
			*new_client_connected = true;
		}
	} else {
		*client_socket = SOCK_ERR_INVALID;
		*client_connected_state = false;
	}
}

void process_recv_error(SOCKET socket, t_socketRecv *recv_data, uint8_t msg_type)
{
	(void)msg_type;
	//
	// Process socket recv errors
	//
	switch (recv_data->bufSize) // error is in the buffer size element
	{
	case SOCK_ERR_CONN_ABORTED: // Peer closed connection
	{
		//
		// Process depending upon the client that called the event
		//
		if (socket == gdb_client_socket) {
			close(gdb_client_socket);
			gdb_client_socket = SOCK_ERR_INVALID; // Mark socket invalid
			gdb_client_connected = false;         // No longer connected
		} else if (socket == uart_debug_client_socket) {
			close(uart_debug_client_socket);
			uart_debug_client_socket = SOCK_ERR_INVALID; // Mark socket invalid
			uart_debug_client_connected = false;         // No longer connected
			user_configured_uart = false;
		} else if (socket == swo_trace_client_socket) {
			close(swo_trace_client_socket);
			swo_trace_client_socket = SOCK_ERR_INVALID; // Mark socket invalid
			swo_trace_client_connected = false;         // No longer connected
		}
		DEBUG_WARN("APP_SOCK_CB[%d]: Connection closed by peer\r\n", msg_type);
		break;
	}
	case SOCK_ERR_INVALID_ADDRESS:
	case SOCK_ERR_ADDR_ALREADY_IN_USE:
	case SOCK_ERR_MAX_TCP_SOCK:
	case SOCK_ERR_MAX_UDP_SOCK:
	case SOCK_ERR_INVALID_ARG:
	case SOCK_ERR_MAX_LISTEN_SOCK:
	case SOCK_ERR_INVALID:
	case SOCK_ERR_ADDR_IS_REQUIRED:
	case SOCK_ERR_TIMEOUT:
	case SOCK_ERR_BUFFER_FULL:
	default: {
		DEBUG_WARN("APP_SOCK_CB[%d]: Unknown/unhandled error code %d bytes\r\n", msg_type, recv_data->bufSize);
		break;
	}
	}
}

/* NOLINTNEXTLINE(readability-function-cognitive-complexity) */
static void app_socket_callback(SOCKET sock, uint8_t msg_type, void *msg)
{
	(void)msg;
	socket_parameter = sock;
	switch (msg_type) {
	case M2M_SOCKET_DNS_RESOLVE_EVENT: {
		dns_resolved = true;
		break;
	}

	case M2M_SOCKET_CONNECT_EVENT: {
		// t_socketConnect *pSockConnResp = (t_socketConnect *) pvMsg;
		//
		// This event occurs when ctxLink establishes a connection back to
		// a client.
		//
		// At this time it is not used
		//
		break;
	}

	case M2M_SOCKET_BIND_EVENT: {
		//
		// Route the evenbt according to which server sent it
		//
		if (sock == gdb_server_socket)
			handle_socket_bind_event(&gdb_server_socket, &gdb_client_connected);
		else if (sock == uart_debug_server_socket)
			handle_socket_bind_event(&uart_debug_server_socket, &uart_debug_server_is_running);
		else if (sock == swo_trace_server_socket)
			handle_socket_bind_event(&swo_trace_server_socket, &swo_trace_server_is_running);
		else
			//
			// Unknown server ... TODO
			//
			DEBUG_WARN("APP_SOCK_CB[%d]: Bind for unknown server\r\n", msg_type);
		break;
	}
	case M2M_SOCKET_LISTEN_EVENT: {
		//
		// Route the event according to which server sent it
		//
		if (sock == gdb_server_socket)
			handle_socket_listen_event(&gdb_server_socket, &gdb_server_is_running);
		else if (sock == uart_debug_server_socket)
			handle_socket_listen_event(&uart_debug_server_socket, &uart_debug_server_is_running);
		else if (sock == swo_trace_server_socket)
			handle_socket_listen_event(&swo_trace_server_socket, &swo_trace_server_is_running);
		else
			//
			// Unknown server ... TODO
			//
			DEBUG_WARN("APP_SOCK_CB[%d]: Listen event for unknown server\r\n", msg_type);
		break;
	}
	case M2M_SOCKET_ACCEPT_EVENT: {
		t_socketAccept *accept_data = (t_socketAccept *)msg; // recover the message data
		//
		// Process for the specific server
		//
		/* NOLINTNEXTLINE(bugprone-branch-clone) */
		if (sock == gdb_server_socket)
			handle_socket_accept_event(
				accept_data, &gdb_client_socket, &gdb_client_connected, &new_gdb_client_connected, msg_type);
		else if (sock == uart_debug_server_socket) {
			//
			// Disable any active UART setup by killing the baud rate
			//
			usart_set_baudrate(USBUSART, 0);
			handle_socket_accept_event(accept_data, &uart_debug_client_socket, &uart_debug_client_connected,
				&new_uart_debug_client_conncted, msg_type);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
			send(uart_debug_client_socket, (void *)&uart_client_signon[0], strlen(&uart_client_signon[0]), 0);
#pragma GCC diagnostic pop
		} else if (sock == swo_trace_server_socket)
			handle_socket_accept_event(accept_data, &swo_trace_client_socket, &swo_trace_client_connected,
				&new_swo_trace_client_conncted, msg_type);
		else {
			//
			// Unknown server ... TODO
			//
			DEBUG_WARN("APP_SOCK_CB[%d]: Connection from unknown server\r\n", msg_type);
			close(accept_data->sock);
		}
		break;
	}
	case M2M_SOCKET_RECV_EVENT: {
		t_socketRecv *recv_data = (t_socketRecv *)msg;
		//
		// Process the data for the specific server's client
		//
		if (sock == gdb_client_socket) {
			//
			// if we have good data copy it to the input_buffer
			// circular buffer
			//
			if (recv_data->bufSize > 0) {
				//
				// Got good data, copy to circular buffer
				//
				int16_t local_count = recv_data->bufSize;
				//
				// Copy data to circular input buffer
				//
				for (int16_t i = 0; local_count != 0;
					 i++, local_count--, input_index = (input_index + 1) % INPUT_BUFFER_SIZE) {
					input_buffer[input_index] = local_buffer[i];
				}
				buffer_count += recv_data->bufSize;
				DEBUG_WARN("Received -> %d, queued -> %ld\r\n", recv_data->bufSize, buffer_count);
				//
				// Start another receive operation so we always get data
				//
				recv(gdb_client_socket, &local_buffer[0], INPUT_BUFFER_SIZE, 0);
			} else
				process_recv_error(sock, recv_data, msg_type);
		} else if (sock == uart_debug_client_socket) {
			if (recv_data->bufSize > 0) {
				if (!user_configured_uart) {
					if (!platform_configure_uart((char *)&local_uart_debug_buffer[0]))
						//
						// Setup failed, tell user
						//
						send(uart_debug_client_socket, "Syntax error in setup string\r\n",
							strlen("Syntax error in setup string\r\n"), 0);
					else
						user_configured_uart = true;
				} else {
					//
					// Forward data to target MCU
					//
					gpio_set(LED_PORT_UART, LED_UART);
					for (int i = 0; i < recv_data->bufSize; i++)
						usart_send_blocking(USBUSART, local_uart_debug_buffer[i]);
					gpio_clear(LED_PORT_UART, LED_UART);
				}
				memset(&local_uart_debug_buffer[0], 0x00, sizeof(local_uart_debug_buffer));
				//
				// Setup to receive future data
				//
				recv(uart_debug_client_socket, &local_uart_debug_buffer[0], UART_DEBUG_INPUT_BUFFER_SIZE, 0);
			}
		} else if (sock == swo_trace_client_socket) {
			if (recv_data->bufSize > 0)
				//
				// Setup to receive future data
				//
				recv(swo_trace_client_socket, &local_swo_trace_buffer[0], SWO_TRACE_INPUT_BUFFER_SIZE, 0);
			else
				process_recv_error(sock, recv_data, msg_type);
		} else
			//
			// Unknown server ... TODO
			//
			DEBUG_WARN("APP_SOCK_CB[%d]: Data from unknown server\r\n", msg_type);
		break;
	}
	case M2M_SOCKET_SEND_EVENT: {
#ifndef DEBUG_WARN_IS_NOOP
		DEBUG_WARN("Send event -> %d\r\n", m2m_wifi_get_socket_event_data()->numSendBytes);
#endif
		//
		// Disable interrupts to protect the send queue processing
		//
		m2mStub_EintDisable();
		//
		// Process for the specific server
		//
		if (sock == gdb_server_socket) {
			if (gdb_send_queue_length != 0)
				do_gdb_send();
		} else if (sock == uart_debug_client_socket) {
			if (uart_debug_send_queue_length != 0)
				do_uart_debug_send();
		} else if (sock == swo_trace_client_socket) {
			swo_trace_send_queue_out = (swo_trace_send_queue_out + 1) % SEND_QUEUE_SIZE;
			swo_trace_send_queue_length -= 1;
			if (swo_trace_send_queue_length != 0)
				do_awo_trace_send();
		} else
			//
			// Unknown server ... TODO
			//
			DEBUG_WARN("APP_SOCK_CB[%d]: Send event from unknown server\r\n", msg_type);
		//
		// Re-enable interrupts
		//
		m2mStub_EintEnable();
		break;
	}
	case M2M_SOCKET_SENDTO_EVENT:
	case M2M_SOCKET_RECVFROM_EVENT:
	case M2M_SOCKET_PING_RESPONSE_EVENT: {
		DEBUG_WARN("APP_SOCK_CB[%d]: Un-implemented state\r\n", msg_type);
		break;
	}

	default: {
		DEBUG_WARN("APP_SOCK_CB[%d]: Unknown socket state\r\n", msg_type);
		break;
	}
	}
}

bool is_gdb_server_running(void)
{
	return gdb_server_is_running;
}

bool swo_trace_server_active(void)
{
	return swo_trace_server_socket != SOCK_ERR_INVALID;
}

bool is_dns_resolved(void)
{
	bool res = dns_resolved;
	dns_resolved = false;
	return res;
}

bool is_gdb_client_connected(void)
{
	bool res = gdb_client_connected;
	// no need to reset flag "g_clientConnected" to false. App will do that.
	return res;
}

bool is_uart_client_connected(void)
{
	return user_configured_uart;
}

bool is_swo_trace_client_connected(void)
{
	return swo_trace_client_connected;
}

void app_initialize(void)
{
	/* register callback functions for Wi-Fi and Socket events */
	registerWifiCallback(app_wifi_callback);
	registerSocketCallback(app_socket_callback);

	app_state = app_state_wait_for_driver_init;
	//
	// Initialize the WINC1500 interface hardware
	//
	rcc_periph_clock_enable(WINC1500_RCC_SPI);
	//
	// Set up the control outputs for the WINC1500
	//
	//		RESET output
	//
	gpio_mode_setup(WINC1500_RESET_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, WINC1500_RESET);
	//
	//		Chip select output
	//
	gpio_mode_setup(WINC1500_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, WINC1500_SPI_NCS);
	//
	//		CHIP_EN Output
	//
	gpio_mode_setup(WINC1500_CHIP_EN_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, WINC1500_CHIP_EN);
	//
	// Negate all outputs to WINC1500
	//
	gpio_set(WINC1500_RESET_PORT, WINC1500_RESET);
	gpio_set(WINC1500_PORT, WINC1500_SPI_NCS);
	//
	// Rev 1.4 PCB does not use the WAKE input of the WINC1500
	//
	//gpio_set(WINC1500_WAKE_PORT, WINC1500_WAKE);
	gpio_clear(WINC1500_CHIP_EN_PORT, WINC1500_CHIP_EN);
	//
	// Need to make the irq pin an external interrupt on falling edge
	//
	//	First enable the SYSCFG clock
	//
	rcc_periph_clock_enable(RCC_SYSCFG);

	gpio_mode_setup(WINC1500_PORT, GPIO_MODE_INPUT, GPIO_PUPD_NONE, WINC1500_IRQ); // Input signal with pulldown

	exti_select_source(WINC1500_IRQ, WINC1500_PORT);
	exti_set_trigger(WINC1500_IRQ, EXTI_TRIGGER_FALLING);
	//
	// Set the port pins of the SPI channel to high-speed I/O
	//
	gpio_set_output_options(WINC1500_SPI_DATA_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ,
		WINC1500_SPI_CLK | WINC1500_SPI_MISO | WINC1500_SPI_MOSI);
	//
	// Enable alternate function for SPI2_CLK PB10 AF5
	//
	gpio_mode_setup(WINC1500_SPI_CLK_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, WINC1500_SPI_CLK);
	gpio_set_af(WINC1500_SPI_CLK_PORT, GPIO_AF5, WINC1500_SPI_CLK);
	//
	// Enable SPI alternate function pins - MISO and MOSI
	//
	gpio_mode_setup(WINC1500_SPI_DATA_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, WINC1500_SPI_MISO | WINC1500_SPI_MOSI);
	gpio_set_af(WINC1500_SPI_DATA_PORT, GPIO_AF5, WINC1500_SPI_MISO | WINC1500_SPI_MOSI);
	//
	// I think this is Mode_0, 8 bit data, MSB first, the clock rate is 42MHz with core of 84MHz
	//
	spi_init_master(WINC1500_SPI_CHANNEL, SPI_CR1_BAUDRATE_FPCLK_DIV_2, SPI_CR1_CPOL_CLK_TO_0_WHEN_IDLE,
		SPI_CR1_CPHA_CLK_TRANSITION_1, SPI_CR1_DFF_8BIT, SPI_CR1_MSBFIRST);
	//
	// Set NSS to software management and also ensure NSS is high, if not written high no data will be sent
	//
	spi_enable_software_slave_management(WINC1500_SPI_CHANNEL);
	spi_set_nss_high(WINC1500_SPI_CHANNEL);
	//
	// Enable the SPI channel
	//
	spi_enable(WINC1500_SPI_CHANNEL);
	exti_enable_request(WINC1500_IRQ);
	nvic_enable_irq(NVIC_EXTI9_5_IRQ);
	//
	// WINC1500 requires a 1mS tick, this is provided by TIMER2
	//
	timer_init();
}

/* NOLINTNEXTLINE(readability-function-cognitive-complexity) */
void app_task(void)
{
	switch (app_state) {
	case app_state_wait_for_driver_init: {
		if (is_driver_init_complete()) {
			DEBUG_WARN("APP_TASK[%d]: WINC1500 driver initialized!\r\n", app_state);
			//
			// Set default device name
			//
			m2m_wifi_set_device_name(CTXLINK_NETWORK_NAME, strlen(CTXLINK_NETWORK_NAME));
			//
			// Select the "Manual" power mode
			//
			m2m_wifi_set_sleep_mode(M2M_WIFI_PS_MANUAL, 1);
			//
			// Get the WINC1500 firmware version
			//
			tstrM2mRev info = {0};
			nm_get_firmware_info(&info);
			//
			// Move to reading the MAC address state
			//
			app_state = app_state_read_mac_address; //app_state_connect_to_wifi;
		}
		break;
	}

	case app_state_read_mac_address: {
		uint8_t result;
		static uint8_t mac_addr[M2M_MAC_ADDRESS_LEN];
		uint8_t user_define_mac_address[] = {0xf8, 0xf0, 0x05, 0x20, 0x0b, 0x09};

		m2m_wifi_get_otp_mac_address(mac_addr, &result);
		if (!result) {
			DEBUG_WARN("APP_TASK[%d]: USER MAC Address : ", app_state);
			/* Cannot found MAC Address from OTP. Set user define MAC address. */
			m2m_wifi_set_mac_address(user_define_mac_address);
		} else
			DEBUG_WARN("APP_TASK[%d]: OTP MAC Address : ", app_state);

		/* Get MAC Address. */
		m2m_wifi_get_mac_address(mac_addr);
		DEBUG_WARN("%02X:%02X:%02X:%02X:%02X:%02X\r\n", mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4],
			mac_addr[5]);
		DEBUG_WARN("APP_TASK[%d]: Done.\r\n", app_state);
		m2m_wifi_default_connect();
		app_state = app_state_wait_for_wifi_connect;
		break;
	}
	case app_state_check_default_connections: // No action required just a parking state waiting for a Wi-Fi callback
	{
		break;
	}
	/*
			Begin the WPS provisioning process
		 */
	case app_state_connect_wps: {
		m2m_wifi_wps(WPS_PBC_TRIGGER, NULL);
		wps_active = true;
		/*
				Start a 30 second timeout, if no WPS connection then cancel the mode
			*/
		tim2_start_seconds_timeout(WPS_LOCAL_TIMEOUT);
		app_state = app_state_wait_wps_event;
		break;
	}
	/*
			Wait for the WPS provisioning process to complete or timeout
		 */
	case app_state_wait_wps_event: {
		if (is_wifi_connected()) {
			wps_active = false;
			//
			// We have a connection, start the TCP server
			//
			app_state = app_state_start_server;
			//
			// Change the LED mode to Connected to AP mode
			//
			led_mode = mode_led_ap_connected;
		} else if (!wps_active) // Did WPS timeout?
		{
			mode_task_state = mode_led_idle_state;
			led_mode = mode_led_idle;
			app_state = app_state_spin;
		} else if (tim2_is_seconds_timeout()) // Did we timeout waiting for WPS?
		{
			/*
				Cancel WPS mode
				*/
			m2m_wifi_wps_disable();
			mode_task_state = mode_led_idle_state;
			led_mode = mode_led_idle;
			m2m_wifi_default_connect(); // We may have had a prevous connection to an AP
			app_state = app_state_wait_for_wifi_connect;
		}
		break;
	}
	/*
			Begin the HTTP provisioning process
		 */
	case app_state_http_provision: {
		tstrM2MAPConfig apConfig;
		uint8_t enableRedirect = 1;
		strcpy((char *)apConfig.au8SSID, "ctxLink-AP");
		apConfig.u8ListenChannel = 1;
		apConfig.u8SecType = M2M_WIFI_SEC_OPEN;
		apConfig.u8SsidHide = 0;

		// IPAddress
		apConfig.au8DHCPServerIP[0] = 192;
		apConfig.au8DHCPServerIP[1] = 168;
		apConfig.au8DHCPServerIP[2] = 1;
		apConfig.au8DHCPServerIP[3] = 1;
		m2m_wifi_start_provision_mode(&apConfig, "ctxLink_Config.com", enableRedirect);
		http_active = true;
		waiting_access_point = true;
		app_state = app_state_wait_provision_event;
		break;
	}
	/*
			Wait for the HTTP provisioning process to respond
		 */
	case app_state_wait_provision_event: {
		if (is_wifi_connected()) {
			http_active = false;
			//
			// We have a connection, start the TCP server
			//
			app_state = app_state_start_server;
			//
			// Change the LED mode to Connected to AP mode
			//
			led_mode = mode_led_ap_connected;
		} else if (!http_active) // Did provisioning timeout?
		{
			mode_task_state = mode_led_idle_state;
			led_mode = mode_led_idle;
			app_state = app_state_spin;
		}
		break;
	}
	case app_state_wait_for_wifi_connect: {
		//
		// Are we connected?
		//
		if (is_wifi_connected()) {
			//
			// We have a connection, start the TCP server
			//
			app_state = app_state_start_server;
			//
			// Change the LED mode to Connected to AP mode
			//
			led_mode = mode_led_ap_connected;
		}
		break;
	}
	case app_state_wait_for_disconnect: {
		if (!is_wifi_connected()) {
			app_state = app_state_spin;
			mode_task_state = mode_led_idle_state;
			led_mode = mode_led_idle;
		}
		break;
	}
	case app_state_wait_wifi_disconnect_for_wps: {
		if (!is_wifi_connected()) {
			// enter WPS mode
			app_state = app_state_connect_wps;
			mode_task_state = mode_led_idle_state;
			led_mode = mode_led_wps_active;
		}
		break;
	}
	case app_state_wait_wifi_disconnect_for_http: {
		if (!is_wifi_connected()) {
			// enter HTTP provisioning mode
			app_state = app_state_http_provision;
			mode_task_state = mode_led_idle_state;
			led_mode = mode_led_http_provisioning;
		}
		break;
	}
	case app_state_start_server: {
		gdb_tcp_server_state = sm_home;
		uart_debug_tcp_server_state = sm_home;
		swo_trace_tcp_serverstate = sm_idle; // Will start up when requested
		//
		// Wait for the server to come up
		//
		app_state = app_state_wait_for_server;
		break;
	}
	case app_state_wait_for_server: {
		if (is_gdb_server_running() && is_ip_address_assigned())
			app_state = app_state_wait_connection_info;
		break;
	}

	case app_state_wait_connection_info: {
		if (connection_info_received) {
			connection_info_received = false;
			app_state = app_state_spin;
		}
		break;
	}

	case app_state_error: {
		/* Indicate error */
		// TODO: Turn ON LED or something..
		app_state = app_state_spin;

		/* Intentional fall */
		BMD_FALLTHROUGH
	}

	case app_state_spin: {
		//
		// Check for a new GDB client connection, if found start the receive process for the client
		//
		if (new_gdb_client_connected) {
			new_gdb_client_connected = false;
			//
			// Set up a recv call
			//
			recv(gdb_client_socket, &local_buffer[0], INPUT_BUFFER_SIZE, 0);
		}
		if (new_uart_debug_client_conncted) {
			new_uart_debug_client_conncted = false;
			recv(uart_debug_client_socket, &local_uart_debug_buffer[0], UART_DEBUG_INPUT_BUFFER_SIZE, 0);
		}
		if (new_swo_trace_client_conncted) {
			new_swo_trace_client_conncted = false;
			recv(swo_trace_client_socket, &local_swo_trace_buffer[0], SWO_TRACE_INPUT_BUFFER_SIZE, 0);
		}
		break;
	}

	default: {
		DEBUG_WARN("APP_TASK[%d]: Unknown state.\r\n", app_state);
	}
	}
	//
	// Check for swo trace data
	//
	// TODO Restore this when TRACESWO is implemented
	// trace_send_data();
	//
	// Run the mode led task?
	//
	timer_disable_irq(TIM2, TIM_DIER_CC1IE);
	if (run_mode_led_task) {
		run_mode_led_task = false;
		mode_led_task();
	}
	timer_enable_irq(TIM2, TIM_DIER_CC1IE);
	/*
		If the LED mode is in Low Battery mode then there is no reason to
		check any further button presses or mode changes
	*/
	if (led_mode != mode_led_battery_low) {
		//
		// Process the Mode button here.
		//
		if (!press_active) {
			if (gpio_get(SWITCH_PORT, SW_BOOTLOADER_PIN) == 0) {
				/*
				If the AppTask is in either app_state_wait_wps_event
				or app_state_wait_provision_event cancel the state
				*/
				if ((app_state == app_state_wait_wps_event) || (app_state == app_state_wait_provision_event)) {
					/*
					Cancel mode and revert
					*/
					if (app_state == app_state_wait_wps_event)
						/*
							Cancel WPS mode
						*/
						m2m_wifi_wps_disable();
					else
						/*
							Cancel HTTP provisioning mode
						*/
						m2m_wifi_stop_provision_mode();
					mode_task_state = mode_led_idle_state;
					led_mode = mode_led_idle;
					m2m_wifi_default_connect(); // We may have had a prevous connection to an AP
					app_state = app_state_wait_for_wifi_connect;
				} else {
					/*
					begin timing for new mode entry
					*/
					press_active = true;
					start_press_timer();
				}
			}
		} else {
			if (gpio_get(SWITCH_PORT, SW_BOOTLOADER_PIN) != 0) {
				uint32_t timeDown = get_press_timer();

				press_active = false;

				if (timeDown >= BUTTON_PRESS_HTTP_PROVISIONING) {
					/*
					If there is a connection wait till disconnected
					*/
					if (is_wifi_connected()) {
						do_wifi_disconnect();
						m2m_wifi_disconnect();
						app_state = app_state_wait_wifi_disconnect_for_http;
					} else
						// Enter http provisioning mode
						app_state = app_state_http_provision;
					led_mode = mode_led_http_provisioning;
					mode_task_state = mode_led_idle_state;
				} else if (timeDown >= BUTTON_PRESS_WPS) {
					/*
					If we have a connection, disconnect and wait for disconnect event
					*/
					if (is_wifi_connected()) {
						do_wifi_disconnect();
						m2m_wifi_disconnect();
						app_state = app_state_wait_wifi_disconnect_for_wps;
					} else
						// enter WPS mode
						app_state = app_state_connect_wps;
					mode_task_state = mode_led_idle_state;
					led_mode = mode_led_wps_active;
				}
			}
		}
	}
}

int wifi_have_input(void)
{
	int result;
	// m2mStub_EintDisable ();
	result = buffer_count;
	// m2mStub_EintEnable ();
	return result;
}

uint8_t wifi_get_next(void)
{
	uint8_t result = 0x00;
	//
	// The buffer count is also managed in an ISR so protect this code
	//
	m2mStub_EintDisable();
	if (buffer_count != 0) {
		result = input_buffer[output_index];
		output_index = (output_index + 1) % INPUT_BUFFER_SIZE;
		buffer_count -= 1;
	}
	m2mStub_EintEnable();
	return result;
}

uint8_t wifi_get_next_to(uint32_t timeout)
{
	platform_timeout_s t;
	uint8_t count = 0;
	int input_count = 0;
	platform_timeout_set(&t, timeout);

	do {
		input_count = wifi_have_input();
		if (input_count != 0)
			break;
		//
		// We must run the platform tasks or incomming data will not be transferred
		// to the input buffers
		//

		platform_tasks();

	} while (!platform_timeout_is_expired(&t));

	if (input_count != 0)
		count = wifi_get_next();
	return count;
}

void do_gdb_send(void)
{
	send(gdb_client_socket, &(gdb_send_queue[gdb_send_queue_out].packet[0]), gdb_send_queue[gdb_send_queue_out].len, 0);
	m2mStub_EintDisable();
	gdb_send_queue_out = (gdb_send_queue_out + 1) % SEND_QUEUE_SIZE;
	gdb_send_queue_length -= 1;
	m2mStub_EintEnable();
}

void do_uart_debug_send(void)
{
	send(uart_debug_client_socket, &(uart_debug_send_queue[uart_debug_send_queue_out].packet[0]),
		uart_debug_send_queue[uart_debug_send_queue_out].len, 0);
	m2mStub_EintDisable();
	uart_debug_send_queue_out = (uart_debug_send_queue_out + 1) % SEND_QUEUE_SIZE;
	uart_debug_send_queue_length -= 1;
	m2mStub_EintEnable();
}

void do_awo_trace_send(void)
{
	send(swo_trace_client_socket, &(swo_trace_send_queue[swo_trace_send_queue_out].packet[0]),
		swo_trace_send_queue[swo_trace_send_queue_out].len, 0);
}

void send_uart_data(uint8_t *buffer, uint8_t length)
{
	m2mStub_EintDisable();
	memcpy(uart_debug_send_queue[uart_debug_send_queue_in].packet, buffer, length);
	uart_debug_send_queue[uart_debug_send_queue_in].len = length;
	uart_debug_send_queue_in = (uart_debug_send_queue_in + 1) % SEND_QUEUE_SIZE;
	uart_debug_send_queue_length += 1;
	m2mStub_EintEnable();
	do_uart_debug_send();
}

void send_swo_trace_data(uint8_t *buffer, uint8_t length)
{
	m2mStub_EintDisable();
	memcpy(swo_trace_send_queue[swo_trace_send_queue_in].packet, buffer, length);
	swo_trace_send_queue[swo_trace_send_queue_in].len = length;
	swo_trace_send_queue_in = (swo_trace_send_queue_in + 1) % SEND_QUEUE_SIZE;
	swo_trace_send_queue_length += 1;
	const bool send_it = swo_trace_send_queue_length == 1;

	m2mStub_EintEnable();
	if (send_it)
		do_awo_trace_send();
}

void wifi_gdb_putchar(const uint8_t ch, const bool flush)
{
	send_buffer[send_count++] = ch;
	if (flush || send_count >= sizeof(send_buffer))
		wifi_gdb_flush(flush);
}

void wifi_gdb_flush(const bool force)
{
	(void)force;

	/* Flush only if there is data to flush */
	if (send_count == 0U)
		return;

	// TODO is this check required now, looks like a debug test left in place?
	if (send_count <= 0U)
		DEBUG_WARN("WiFi_putchar bad count\r\n");
	DEBUG_WARN("Wifi_putchar %c\r\n", send_buffer[0]);
	send(gdb_client_socket, &send_buffer[0], send_count, 0);

	/* Reset the buffer */
	send_count = 0U;
	memset(&send_buffer[0], 0x00, sizeof(send_buffer));
}
