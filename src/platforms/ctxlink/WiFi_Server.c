//
//	File:	WiFi_Server.c
//
//	TCP/IP client/server for WBMP
//

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

void traceSendData(void);

#define GDBServerPort        2159
#define UART_DebugServerPort 2160
#define SWO_TraceServerPort  2161

#define INPUT_BUFFER_SIZE 2048
static unsigned char inputBuffer[INPUT_BUFFER_SIZE] = {0}; ///< The input buffer[ input buffer size]
static volatile u_int32_t uiInputIndex = 0;                ///< Zero-based index of the input
static volatile u_int32_t uiOutputIndex = 0;               ///< Zero-based index of the output
static volatile u_int32_t uiBufferCount = 0;               ///< Number of buffers

static unsigned char localBuffer[INPUT_BUFFER_SIZE] = {0}; ///< The local buffer[ input buffer size]

static bool g_driverInitComplete = false;    ///< True to driver initialize complete
static bool g_wifi_connected = false;        ///< True if WiFi connected
static bool g_ipAddressAssigned = false;     ///< True if IP address assigned
static bool g_dnsResolved = false;           ///< True if DNS resolved
static bool g_gdbClientConnected = false;    ///< True if client connected
static bool g_gdbServerIsRunning = false;    ///< True if server is running
static bool g_newGDBClientConnected = false; ///< True if new client connected

static SOCKET gdbServerSocket = SOCK_ERR_INVALID; ///< The main gdb server socket
static SOCKET gdbClientSocket = SOCK_ERR_INVALID; ///< The gdb client socket

#define UART_DEBUG_INPUT_BUFFER_SIZE 32
static unsigned char localUartDebugBuffer[UART_DEBUG_INPUT_BUFFER_SIZE] = {0}; ///< The local buffer[ input buffer size]

static SOCKET uartDebugServerSocket = SOCK_ERR_INVALID;
static SOCKET uartDebugClientSocket = SOCK_ERR_INVALID;
static bool g_uartDebugClientConnected = false;
static bool g_userConfiguredUart = false;
static bool g_uartDebugServerIsRunning = false;
static bool g_newUartDebugClientconncted = false;

static SOCKET swoTraceServerSocket = SOCK_ERR_INVALID;
static SOCKET swoTraceClientSocket = SOCK_ERR_INVALID;
static bool g_swoTraceClientConnected = false;
static bool g_swoTraceServerIsRunning = false;
static bool g_newSwoTraceClientconncted = false;

#define SWO_TRACE_INPUT_BUFFER_SIZE 32
static unsigned char localSwoTraceBuffer[SWO_TRACE_INPUT_BUFFER_SIZE] = {0}; ///< The local buffer[ input buffer size]

#define WPS_LOCAL_TIMEOUT 30 // Timeout value in seconds

//
// Sign-on message for new UART data clients
//
static char uartClientSignon[] = "\r\nctxLink UART connection.\r\nPlease enter the UART setup as baud, bits, parity, "
								 "stop.\r\ne.g. 38400,8,N,1\r\n\r\n";

typedef enum WiFi_APP_STATES {
	APP_STATE_WAIT_FOR_DRIVER_INIT,         ///< 0
	APP_STATE_READ_MAC_ADDR,                ///< 1
	APP_STATE_CONNECT_TO_WIFI,              ///< 2
	APP_STATE_WAIT_WIFI_DISCONNECT_FOR_WPS, ///< 3
	APP_STATE_WAIT_WIFI_DISCONNECT_FOR_HTTP,
	APP_STATE_CONNECT_WPS,           ///< 5
	APP_STATE_WAIT_WPS_EVENT,        ///< 6
	APP_STATE_HTTP_PROVISION,        ///< 7
	APP_STATE_WAIT_PROVISION_EVENT,  ///< 8
	APP_STATE_WAIT_FOR_WIFI_CONNECT, ///< 9
	APP_STATE_START_SERVER,          ///< 10
	APP_STATE_WAIT_FOR_SERVER,       ///< 11
	APP_STATE_ERROR,                 ///< 12
	APP_STATE_CHECK_DEFAULT_CONN,    ///< 13
	APP_STATE_SPIN                   ///< 14
} APP_STATES;

APP_STATES appState; ///< State of the application
/*
 * Define the send queue, this is used in the socket event callback
 * to correctly process output and sync with ACKs
 */
#define SEND_QUEUE_SIZE        4
#define SEND_QUEUE_BUFFER_SIZE 1024

#define BUTTON_PRESS_WPS               2500 // Enter WPS mode if 2.5 Seconds > 2.5S < 5 Seconds
#define BUTTON_PRESS_HTTP_PROVISIONING 5000
#define BUTTON_PRESS_MODE_CANCEL       7500

typedef struct tagSendQueueEntry {
	unsigned char packet[SEND_QUEUE_BUFFER_SIZE]; ///< The packet[ send queue buffer size]
	unsigned len;                                 ///< The length
} SEND_QUEUE_ENTRY;

SEND_QUEUE_ENTRY gdbSendQueue[SEND_QUEUE_SIZE] = {0}; ///< The send queue[ send queue size]
unsigned volatile uiGDBSendQueueIn = 0;               ///< The send queue in
unsigned volatile uiGDBSendQueueOut = 0;              ///< The send queue out
unsigned volatile uiGDBSendQueueLength = 0;           ///< Length of the send queue
void DoGDBSend(void);

SEND_QUEUE_ENTRY uartDebugSendQueue[SEND_QUEUE_SIZE] = {0}; ///< The send queue[ send queue size]
unsigned volatile uiUartDebugSendQueueIn = 0;               ///< The send queue in
unsigned volatile uiUartDebugSendQueueOut = 0;              ///< The send queue out
unsigned volatile uiUartDebugSendQueueLength = 0;           ///< Length of the send queue
void DoUartDebugSend(void);

SEND_QUEUE_ENTRY swoTraceSendQueue[SEND_QUEUE_SIZE] = {0}; ///< The send queue[ send queue size]
unsigned volatile uiSwoTraceSendQueueIn = 0;               ///< The send queue in
unsigned volatile uiSwoTraceSendQueueOut = 0;              ///< The send queue out
unsigned volatile uiSwoTraceSendQueueLength = 0;           ///< Length of the send queue
void DoSwoTraceSend(void);

static bool pressActive = false; ///< True to press active
bool wpsActive = false;          ///< True to wps active
bool httpActive = false;         ///< True when HTTP provisioning is active
bool waitingAccessPoint =
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

//
// TIMER2 is used to provide a 1mS tick
//
#define TIMER2_COMPARE_VALUE 50

//
// Flag used to run the MODE LED state machine
//
static bool runModeLedTask = false; ///< True to run mode LED task

static uint32_t pressTimer = 0; ///< The press timer

void startPressTimer(void)
{
	timer_disable_irq(TIM2, TIM_DIER_CC1IE);
	pressTimer = 0; // Reset the press timer
	timer_enable_irq(TIM2, TIM_DIER_CC1IE);
}

uint32_t getPressTimer(void)
{
	timer_disable_irq(TIM2, TIM_DIER_CC1IE);
	const uint32_t tmp = pressTimer;
	timer_enable_irq(TIM2, TIM_DIER_CC1IE);
	return tmp;
}

static uint32_t timeoutSeconds = 0;     // Used to perform a countdown in seconds for app tasks
static uint32_t timeoutTickCounter = 0; // counts Timer1 ticks (1mS) for an active timeout
static bool fTimeout = false;           // Asserted at end of timeout
#define TIMEOUT_TICK_COUNT 1000

void tim2_startSecondsTimeout(uint32_t uiSeconds)
{
	timer_disable_irq(TIM2, TIM_DIER_CC1IE);
	timeoutTickCounter = TIMEOUT_TICK_COUNT;
	timeoutSeconds = uiSeconds;
	fTimeout = false;
	timer_enable_irq(TIM2, TIM_DIER_CC1IE);
}

void tim2_cancelSecondsTimeout(void)
{
	timer_disable_irq(TIM2, TIM_DIER_CC1IE);
	timeoutTickCounter = 0;
	timeoutSeconds = 0;
	fTimeout = false;
	timer_enable_irq(TIM2, TIM_DIER_CC1IE);
}

bool tim2_isSecondsTimeout(void)
{
	return fTimeout;
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
		runModeLedTask = true;
		pressTimer++;
	}
	/*
		Do we have a seconds timeout setup
	*/
	if (timeoutSeconds != 0) {
		if (--timeoutTickCounter == 0) {
			// Decrement seconds
			if (--timeoutSeconds == 0) {
				fTimeout = true;
			} else {
				timeoutTickCounter = TIMEOUT_TICK_COUNT;
			}
		}
	}
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

static enum WiFi_TCPServerStates {
	SM_HOME = 0,  ///< State of the TCP server
	SM_LISTENING, ///< An enum constant representing the sm listening option
	SM_CLOSING,   ///< An enum constant representing the sm closing option
	SM_IDLE,      ///< An enum constant representing the sm idle option
} GDB_TCPServerState = SM_IDLE, UART_DEBUG_TCPServerState = SM_IDLE, SWO_TRACE_TCPServerState = SM_IDLE;

struct sockaddr_in gdb_addr = {0};

void doWiFiDisconnect(void)
{
	//
	// Clients
	//
	if (gdbClientSocket != SOCK_ERR_INVALID) {
		close(gdbClientSocket);
		g_gdbClientConnected = false;
		gdbClientSocket = SOCK_ERR_INVALID;
	}
	if (uartDebugClientSocket != SOCK_ERR_INVALID) {
		close(uartDebugClientSocket);
		g_uartDebugClientConnected = false;
		uartDebugClientSocket = SOCK_ERR_INVALID;
	}
	if (swoTraceClientSocket != SOCK_ERR_INVALID) {
		close(swoTraceClientSocket);
		g_swoTraceClientConnected = false;
		swoTraceClientSocket = SOCK_ERR_INVALID;
	}
	//
	// Servers
	//
	if (gdbServerSocket != SOCK_ERR_INVALID) {
		GDB_TCPServerState = SM_IDLE;
		g_gdbServerIsRunning = false;
		close(gdbServerSocket);
		gdbServerSocket = SOCK_ERR_INVALID;
	}
	if (uartDebugServerSocket != SOCK_ERR_INVALID) {
		UART_DEBUG_TCPServerState = SM_IDLE;
		g_uartDebugServerIsRunning = false;
		close(uartDebugServerSocket);
		uartDebugServerSocket = SOCK_ERR_INVALID;
	}
	if (swoTraceServerSocket != SOCK_ERR_INVALID) {
		SWO_TRACE_TCPServerState = SM_IDLE;
		g_swoTraceServerIsRunning = false;
		close(swoTraceServerSocket);
		swoTraceServerSocket = SOCK_ERR_INVALID;
	}
}

void gdb_tcp_server(void)
{
	switch (GDB_TCPServerState) {
	case SM_IDLE:
		break; // Startup and testing do nothing state
	case SM_HOME:
		//
		// Allocate a socket for this server to listen and accept connections on
		//
		gdbServerSocket = socket(AF_INET, SOCK_STREAM, 0);
		if (gdbServerSocket < SOCK_ERR_NO_ERROR) {
			return;
		}
		//
		// Bind the socket
		//
		gdb_addr.sin_addr.s_addr = 0;
		gdb_addr.sin_family = AF_INET;
		gdb_addr.sin_port = _htons(GDBServerPort);
		int8_t result = bind(gdbServerSocket, (struct sockaddr *)&gdb_addr, sizeof(gdb_addr));
		if (result != SOCK_ERR_NO_ERROR) {
			return;
		}
		GDB_TCPServerState = SM_LISTENING;
		break;

	case SM_LISTENING:
		//
		// No need to perform any flush.
		// TCP data in TX FIFO will automatically transmit itself after it accumulates for a while.
		// If you want to decrease latency (at the expense of wasting network bandwidth on TCP overhead),
		// perform and explicit flush via the TCPFlush() API.
		break;

	case SM_CLOSING:
		// Close the socket connection.
		close(gdbServerSocket);
		gdbServerSocket = SOCK_ERR_INVALID;
		GDB_TCPServerState = SM_IDLE;
		break;
	}
}

struct sockaddr_in uart_debug_addr = {0};
struct sockaddr_in swo_trace_addr = {0};

void data_tcp_server(void)
{
	//
	// UART Server
	//
	switch (UART_DEBUG_TCPServerState) {
	case SM_IDLE: {
		break; // Startup and testing do nothing state
	}
	case SM_HOME: {
		//
		// Allocate a socket for this server to listen and accept connections on
		//
		uartDebugServerSocket = socket(AF_INET, SOCK_STREAM, 0);
		if (uartDebugServerSocket < SOCK_ERR_NO_ERROR) {
			return;
		}
		//
		// Bind the socket
		//
		uart_debug_addr.sin_addr.s_addr = 0;
		uart_debug_addr.sin_family = AF_INET;
		uart_debug_addr.sin_port = _htons(UART_DebugServerPort);
		int8_t result = bind(uartDebugServerSocket, (struct sockaddr *)&uart_debug_addr, sizeof(uart_debug_addr));
		if (result != SOCK_ERR_NO_ERROR) {
			return;
		}
		UART_DEBUG_TCPServerState = SM_LISTENING;
		break;
	}
	case SM_LISTENING: {
		//
		// No need to perform any flush.
		// TCP data in TX FIFO will automatically transmit itself after it accumulates for a while.
		// If you want to decrease latency (at the expense of wasting network bandwidth on TCP overhead),
		// perform and explicit flush via the TCPFlush() API.
		break;
	}
	case SM_CLOSING: {
		// Close the socket connection.
		close(uartDebugServerSocket);
		uartDebugServerSocket = SOCK_ERR_INVALID;
		UART_DEBUG_TCPServerState = SM_IDLE;
	} break;
	}
	//
	// SWO Trace Data server
	//
	switch (SWO_TRACE_TCPServerState) {
	case SM_IDLE: {
		break; // Startup and testing do nothing state
	}
	case SM_HOME: {
		//
		// Allocate a socket for this server to listen and accept connections on
		//
		swoTraceServerSocket = socket(AF_INET, SOCK_STREAM, 0);
		if (swoTraceServerSocket < SOCK_ERR_NO_ERROR) {
			return;
		}
		//
		// Bind the socket
		//
		swo_trace_addr.sin_addr.s_addr = 0;
		swo_trace_addr.sin_family = AF_INET;
		swo_trace_addr.sin_port = _htons(SWO_TraceServerPort);
		int8_t result = bind(swoTraceServerSocket, (struct sockaddr *)&swo_trace_addr, sizeof(swo_trace_addr));
		if (result != SOCK_ERR_NO_ERROR) {
			return;
		}
		SWO_TRACE_TCPServerState = SM_LISTENING;
		break;
	}
	case SM_LISTENING: {
		//
		// No need to perform any flush.
		// TCP data in TX FIFO will automatically transmit itself after it accumulates for a while.
		// If you want to decrease latency (at the expense of wasting network bandwidth on TCP overhead),
		// perform an explicit flush via the TCPFlush() API.
		break;
	}
	case SM_CLOSING: {
		// Close the socket connection.
		close(swoTraceServerSocket);
		swoTraceServerSocket = SOCK_ERR_INVALID;
		SWO_TRACE_TCPServerState = SM_IDLE;
		break;
	}
	}
}

void wifi_setup_swo_trace_server(void)
{
	//
	// Is there a UART client connected?
	//
	if (g_uartDebugClientConnected) {
		// Close the connection to the client
		close(uartDebugClientSocket);
		uartDebugClientSocket = SOCK_ERR_INVALID; // Mark socket invalid
		g_uartDebugClientConnected = false;       // No longer connected
		g_userConfiguredUart = false;
	}
	//
	// If the UART server is up, close it timeDown
	//
	if (uartDebugServerSocket != SOCK_ERR_INVALID) {
		close(uartDebugServerSocket);
		uartDebugServerSocket = SOCK_ERR_INVALID;
	}
	//
	// Set up the SWO Trace Server
	//
	SWO_TRACE_TCPServerState = SM_HOME;
}

static void AppWifiCallback(uint8_t msg_type, void *pvMsg)
{
	switch (msg_type) {
	case M2M_WIFI_DRIVER_INIT_EVENT: {
		g_driverInitComplete = true;
		break;
	}

	case M2M_WIFI_CONN_STATE_CHANGED_EVENT: {
		tstrM2mWifiStateChanged *pstrWifiState = (tstrM2mWifiStateChanged *)pvMsg;
		if (pstrWifiState->u8CurrState == M2M_WIFI_CONNECTED) {
			//
			// If we are in http access mode there will be a connection event when the
			// provisioning client connects, we need to ignore this event.
			//
			// The connection event is the provisioning client if:
			//		httpActive == true && waitingAccessPoint == true
			//
			// Otherwise it is a valid ctxLink-> AP connection
			//
			if (httpActive == true && waitingAccessPoint == true) {
				waitingAccessPoint = false; // Next event is the ctxLink to AP event
			} else {
				DEBUG_WARN("APP_WIFI_CB[%d]: Connected to AP\r\n", msg_type);
				g_wifi_connected = true;
			}
		} else if (pstrWifiState->u8CurrState == M2M_WIFI_DISCONNECTED) {
			DEBUG_WARN("APP_WIFI_CB[%d]: Disconnected from AP\r\n", msg_type);
			g_wifi_connected = false;
		} else {
			DEBUG_WARN("APP_WIFI_CB[%d]: Unknown WiFi state change\r\n", msg_type);
		}
		break;
	}

	case M2M_WIFI_IP_ADDRESS_ASSIGNED_EVENT: {
		t_wifiEventData *p_wifiEventData = (t_wifiEventData *)pvMsg;
		if (p_wifiEventData != NULL) {
			g_ipAddressAssigned = true;
		}
		g_ipAddressAssigned = true;
		break;
	}

	case M2M_WIFI_WPS_EVENT: {
		tstrM2MWPSInfo *p_wfWpsInfo = (tstrM2MWPSInfo *)pvMsg;
		DEBUG_WARN("Wi-Fi request WPS\r\n");
		DEBUG_WARN(
			"SSID : %s, authtyp : %d pw : %s\n", p_wfWpsInfo->au8SSID, p_wfWpsInfo->u8AuthType, p_wfWpsInfo->au8PSK);
		if (p_wfWpsInfo->u8AuthType == 0) {
			DEBUG_WARN("WPS is not enabled OR Timedout\r\n");
			/*
					WPS monitor timeout.
				 */
			m2m_wifi_wps_disable();
			wpsActive = false;
		} else {
			DEBUG_WARN("Request Wi-Fi connect\r\n");
			m2m_wifi_connect_sc((char *)p_wfWpsInfo->au8SSID, strlen((char *)p_wfWpsInfo->au8SSID),
				p_wfWpsInfo->u8AuthType, p_wfWpsInfo->au8PSK, p_wfWpsInfo->u8Ch);
		}

		break;
	}
	case M2M_WIFI_PROVISION_INFO_EVENT: {
		tstrM2MProvisionInfo *provisionInfo = (tstrM2MProvisionInfo *)pvMsg;
		if (provisionInfo->u8Status == M2M_SUCCESS) {
			m2m_wifi_connect_sc((char *)provisionInfo->au8SSID, strlen((char const *)provisionInfo->au8SSID),
				provisionInfo->u8SecType, provisionInfo->au8Password, M2M_WIFI_CH_ALL);
		} else {
			m2m_wifi_stop_provision_mode();
		}
		break;
	}
	case M2M_WIFI_DEFAULT_CONNNECT_EVENT: {
		//tstrM2MDefaultConnResp *pDefaultConnResp = (tstrM2MDefaultConnResp *) pvMsg;
		DEBUG_WARN("APP_WIFI_CB[%d]: Un-implemented state\r\n", msg_type);
		break;
	}
		/* Unused states. Can be implemented if needed  */
	case M2M_WIFI_CONN_INFO_RESPONSE_EVENT:
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

bool isDriverInitComplete(void)
{
	bool res = g_driverInitComplete;
	g_driverInitComplete = false;
	return res;
}

bool isWifiConnected(void)
{
	bool res = g_wifi_connected;
	// no need to reset flag "g_wifi_connected" to false. Event will do that.
	return res;
}

bool isIpAddressAssigned(void)
{
	bool res = g_ipAddressAssigned;
	g_ipAddressAssigned = false;
	return res;
}

void handleSocketBindEvent(SOCKET *lpSocket, bool *lpRunningState)
{
	if (m2m_wifi_get_socket_event_data()->bindStatus == 0) {
		listen(*lpSocket, 0);
	} else {
		close(*lpSocket);
		*lpSocket = SOCK_ERR_INVALID;
		*lpRunningState = false;
	}
}

void handleSocketListenEvent(SOCKET *lpSocket, bool *lpRunningState)
{
	if (m2m_wifi_get_socket_event_data()->listenStatus == 0) {
		accept(*lpSocket, NULL, NULL);
		*lpRunningState = true;
	} else {
		close(*lpSocket);
		*lpSocket = SOCK_ERR_INVALID;
		*lpRunningState = false;
	}
}

void handleSocketAcceptEvent(t_socketAccept *lpAcceptData, SOCKET *lpClientSocket, bool *lpClientConnectedState,
	bool *lpNewClientConnected, uint8_t msgType)
{
	(void)msgType;
	if (lpAcceptData->sock >= 0) {
		//
		// Only allow a single client connection
		//
		if (*lpClientSocket >= 0) {
			/*
			 * close the new client socket, refusing connection
			 *
			 */
			DEBUG_WARN("APP_SOCK_CB[%d]: Second connection rejected\r\n", msgType);
			close(lpAcceptData->sock);
		} else {
			*lpClientSocket = lpAcceptData->sock;
			*lpClientConnectedState = true;
			*lpNewClientConnected = true;
		}
	} else {
		*lpClientSocket = SOCK_ERR_INVALID;
		*lpClientConnectedState = false;
	}
}

void processRecvError(SOCKET socket, t_socketRecv *lpRecvData, uint8_t msgType)
{
	(void)msgType;
	//
	// Process socket recv errors
	//
	switch (lpRecvData->bufSize) // error is in the buffer size element
	{
	case SOCK_ERR_CONN_ABORTED: // Peer closed connection
	{
		//
		// Process depending upon the client that called the event
		//
		if (socket == gdbClientSocket) {
			close(gdbClientSocket);
			gdbClientSocket = SOCK_ERR_INVALID; // Mark socket invalid
			g_gdbClientConnected = false;       // No longer connected
		} else if (socket == uartDebugClientSocket) {
			close(uartDebugClientSocket);
			uartDebugClientSocket = SOCK_ERR_INVALID; // Mark socket invalid
			g_uartDebugClientConnected = false;       // No longer connected
			g_userConfiguredUart = false;
		} else if (socket == swoTraceClientSocket) {
			close(swoTraceClientSocket);
			swoTraceClientSocket = SOCK_ERR_INVALID; // Mark socket invalid
			g_swoTraceClientConnected = false;       // No longer connected
		}
		DEBUG_WARN("APP_SOCK_CB[%d]: Connection closed by peer\r\n", msgType);
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
		DEBUG_WARN("APP_SOCK_CB[%d]: Unknown/unhandled error code %d bytes\r\n", msgType, lpRecvData->bufSize);
		break;
	}
	}
}

bool aFlag = false;
static volatile SOCKET sktParam = 0;

static void AppSocketCallback(SOCKET sock, uint8_t msgType, void *pvMsg)
{
	sktParam = sock;
	switch (msgType) {
	case M2M_SOCKET_DNS_RESOLVE_EVENT: {
		g_dnsResolved = true;
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
		if (sock == gdbServerSocket) {
			handleSocketBindEvent(&gdbServerSocket, &g_gdbClientConnected);
		} else if (sock == uartDebugServerSocket) {
			handleSocketBindEvent(&uartDebugServerSocket, &g_uartDebugServerIsRunning);
		} else if (sock == swoTraceServerSocket) {
			handleSocketBindEvent(&swoTraceServerSocket, &g_swoTraceServerIsRunning);
		} else {
			//
			// Unknown server ... TODO
			//
			DEBUG_WARN("APP_SOCK_CB[%d]: Bind for unknown server\r\n", msgType);
		}
		break;
	}
	case M2M_SOCKET_LISTEN_EVENT: {
		//
		// Route the event according to which server sent it
		//
		if (sock == gdbServerSocket) {
			handleSocketListenEvent(&gdbServerSocket, &g_gdbServerIsRunning);
		} else if (sock == uartDebugServerSocket) {
			handleSocketListenEvent(&uartDebugServerSocket, &g_uartDebugServerIsRunning);
		} else if (sock == swoTraceServerSocket) {
			handleSocketListenEvent(&swoTraceServerSocket, &g_swoTraceServerIsRunning);
		} else {
			//
			// Unknown server ... TODO
			//
			DEBUG_WARN("APP_SOCK_CB[%d]: Listen event for unknown server\r\n", msgType);
		}
		break;
	}
	case M2M_SOCKET_ACCEPT_EVENT: {
		t_socketAccept *pAcceptData = (t_socketAccept *)pvMsg; // recover the message data
		//
		// Process for the specific server
		//
		if (sock == gdbServerSocket) {
			handleSocketAcceptEvent(
				pAcceptData, &gdbClientSocket, &g_gdbClientConnected, &g_newGDBClientConnected, msgType);
		} else if (sock == uartDebugServerSocket) {
			//
			// Disable any active UART setup by killing the baud rate
			//
			usart_set_baudrate(USBUSART, 0);
			handleSocketAcceptEvent(pAcceptData, &uartDebugClientSocket, &g_uartDebugClientConnected,
				&g_newUartDebugClientconncted, msgType);
			send(uartDebugClientSocket, &uartClientSignon[0], strlen(&uartClientSignon[0]), 0);
		} else if (sock == swoTraceServerSocket) {
			handleSocketAcceptEvent(
				pAcceptData, &swoTraceClientSocket, &g_swoTraceClientConnected, &g_newSwoTraceClientconncted, msgType);
		} else {
			//
			// Unknown server ... TODO
			//
			DEBUG_WARN("APP_SOCK_CB[%d]: Connection from unknown server\r\n", msgType);
			close(pAcceptData->sock);
		}
		break;
	}
	case M2M_SOCKET_RECV_EVENT: {
		t_socketRecv *pRecvData = (t_socketRecv *)pvMsg;
		//
		// Process the data for the specific server's client
		//
		if (sock == gdbClientSocket) {
			//
			// if we have good data copy it to the inputBuffer
			// circular buffer
			//
			if (pRecvData->bufSize > 0) {
				//
				// Got good data, copy to circular buffer
				//
				int iLocalCount = pRecvData->bufSize;
				//
				// Copy data to circular input buffer
				//
				for (int i = 0; iLocalCount != 0;
					 i++, iLocalCount--, uiInputIndex = (uiInputIndex + 1) % INPUT_BUFFER_SIZE) {
					inputBuffer[uiInputIndex] = localBuffer[i];
				}
				uiBufferCount += pRecvData->bufSize;
				DEBUG_WARN("Received -> %d, queued -> %ld\r\n", pRecvData->bufSize, uiBufferCount);
				//
				// Start another receive operation so we always get data
				//
				recv(gdbClientSocket, &localBuffer[0], INPUT_BUFFER_SIZE, 0);
			} else {
				processRecvError(sock, pRecvData, msgType);
			}
		} else if (sock == uartDebugClientSocket) {
			if (pRecvData->bufSize > 0) {
				if (g_userConfiguredUart == false) {
					if (platform_configure_uart((char *)&localUartDebugBuffer[0]) == false) {
						//
						// Setup failed, tell user
						//
						send(uartDebugClientSocket, "Syntax error in setup string\r\n",
							strlen("Syntax error in setup string\r\n"), 0);
					} else {
						g_userConfiguredUart = true;
					}
				} else {
					//
					// Forward data to target MCU
					//
					gpio_set(LED_PORT_UART, LED_UART);
					for (int i = 0; i < pRecvData->bufSize; i++)
						usart_send_blocking(USBUSART, localUartDebugBuffer[i]);
					gpio_clear(LED_PORT_UART, LED_UART);
				}
				memset(&localUartDebugBuffer[0], 0x00, sizeof(localUartDebugBuffer));
				//
				// Setup to receive future data
				//
				recv(uartDebugClientSocket, &localUartDebugBuffer[0], UART_DEBUG_INPUT_BUFFER_SIZE, 0);
			}
		} else if (sock == swoTraceClientSocket) {
			if (pRecvData->bufSize > 0) {
				//
				// Setup to receive future data
				//
				recv(swoTraceClientSocket, &localSwoTraceBuffer[0], SWO_TRACE_INPUT_BUFFER_SIZE, 0);
			} else {
				processRecvError(sock, pRecvData, msgType);
			}
		} else {
			//
			// Unknown server ... TODO
			//
			DEBUG_WARN("APP_SOCK_CB[%d]: Data from unknown server\r\n", msgType);
		}
		break;
	}
	case M2M_SOCKET_SEND_EVENT: {
		int bytesSent = m2m_wifi_get_socket_event_data()->numSendBytes;
		DEBUG_WARN("Send event -> %d\r\n", bytesSent);
		//
		// Disable interrupts to protect the send queue processing
		//
		m2mStub_EintDisable();
		//
		// Process for the specific server
		//
		if (sock == gdbServerSocket) {
			if (uiGDBSendQueueLength != 0) {
				DoGDBSend();
			}
		} else if (sock == uartDebugClientSocket) {
			if (uiUartDebugSendQueueLength != 0) {
				DoUartDebugSend();
			}
		} else if (sock == swoTraceClientSocket) {
			uiSwoTraceSendQueueOut = (uiSwoTraceSendQueueOut + 1) % SEND_QUEUE_SIZE;
			uiSwoTraceSendQueueLength -= 1;
			if (uiSwoTraceSendQueueLength != 0) {
				DoSwoTraceSend();
			}
		} else {
			//
			// Unknown server ... TODO
			//
			DEBUG_WARN("APP_SOCK_CB[%d]: Send event from unknown server\r\n", msgType);
		}
		//
		// Re-enable interrupts
		//
		m2mStub_EintEnable();
		break;
	}
	case M2M_SOCKET_SENDTO_EVENT:
	case M2M_SOCKET_RECVFROM_EVENT:
	case M2M_SOCKET_PING_RESPONSE_EVENT: {
		DEBUG_WARN("APP_SOCK_CB[%d]: Un-implemented state\r\n", msgType);
		break;
	}

	default: {
		DEBUG_WARN("APP_SOCK_CB[%d]: Unknown socket state\r\n", msgType);
		break;
	}
	}
}

bool isGDBServerRunning(void)
{
	return g_gdbServerIsRunning;
}

bool swo_trace_server_active(void)
{
	return swoTraceServerSocket != SOCK_ERR_INVALID;
}

bool isDnsResolved(void)
{
	bool res = g_dnsResolved;
	g_dnsResolved = false;
	return res;
}

bool is_gdb_client_connected(void)
{
	bool res = g_gdbClientConnected;
	// no need to reset flag "g_clientConnected" to false. App will do that.
	return res;
}

bool is_uart_client_connected(void)
{
	return g_userConfiguredUart;
}

bool is_swo_trace_client_connected(void)
{
	return g_swoTraceClientConnected;
}

void app_initialize(void)
{
	/* register callback functions for Wi-Fi and Socket events */
	registerWifiCallback(AppWifiCallback);
	registerSocketCallback(AppSocketCallback);

	appState = APP_STATE_WAIT_FOR_DRIVER_INIT;
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

void app_task(void)
{
	switch (appState) {
	case APP_STATE_WAIT_FOR_DRIVER_INIT: {
		if (isDriverInitComplete()) {
			DEBUG_WARN("APP_TASK[%d]: WINC1500 driver initialized!\r\n", appState);
			//
			// Set default device name
			//
			m2m_wifi_set_device_name(ctxLink_NetName, strlen(ctxLink_NetName));
			//
			// Select the "Deep Automatic" power mode
			//
			m2m_wifi_set_sleep_mode(M2M_WIFI_PS_DEEP_AUTOMATIC, 1);
			//
			// Move to reading the MAC address state
			//
			appState = APP_STATE_READ_MAC_ADDR; //APP_STATE_CONNECT_TO_WIFI;
		}
		break;
	}

	case APP_STATE_READ_MAC_ADDR: {
		uint8_t result;
		static uint8_t mac_addr[M2M_MAC_ADDRESS_LEN];
		const char user_define_mac_address[] = {0xf8, 0xf0, 0x05, 0x20, 0x0b, 0x09};

		m2m_wifi_get_otp_mac_address(mac_addr, &result);
		if (!result) {
			DEBUG_WARN("APP_TASK[%d]: USER MAC Address : ", appState);
			/* Cannot found MAC Address from OTP. Set user define MAC address. */
			m2m_wifi_set_mac_address((uint8_t *)user_define_mac_address);
		} else {
			DEBUG_WARN("APP_TASK[%d]: OTP MAC Address : ", appState);
		}

		/* Get MAC Address. */
		m2m_wifi_get_mac_address(mac_addr);
		DEBUG_WARN("%02X:%02X:%02X:%02X:%02X:%02X\r\n", mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4],
			mac_addr[5]);
		DEBUG_WARN("APP_TASK[%d]: Done.\r\n", appState);
		m2m_wifi_default_connect();
		appState = APP_STATE_WAIT_FOR_WIFI_CONNECT;
		break;
	}
	case APP_STATE_CHECK_DEFAULT_CONN: // No action required just a parking state waiting for a Wi-Fi callback
	{
		break;
	}
	/*
			Begin the WPS provisioning process
		 */
	case APP_STATE_CONNECT_WPS: {
		m2m_wifi_wps(WPS_PBC_TRIGGER, NULL);
		wpsActive = true;
		/*
				Start a 30 second timeout, if no WPS connection then cancel the mode
			*/
		tim2_startSecondsTimeout(WPS_LOCAL_TIMEOUT);
		appState = APP_STATE_WAIT_WPS_EVENT;
		break;
	}
	/*
			Wait for the WPS provisioning process to complete or timeout
		 */
	case APP_STATE_WAIT_WPS_EVENT: {
		if (isWifiConnected() == true) {
			wpsActive = false;
			//
			// We have a connection, start the TCP server
			//
			appState = APP_STATE_START_SERVER;
			//
			// Change the LED mode to Connected to AP mode
			//
			led_mode = MODE_LED_AP_CONNECTED;
		} else if (wpsActive == false) // Did WPS timeout?
		{
			modeTaskState = MODE_LED_STATE_IDLE;
			led_mode = MODE_LED_IDLE;
			appState = APP_STATE_SPIN;
		} else if (tim2_isSecondsTimeout() == true) // Did we timeout waiting for WPS?
		{
			/*
				Cancel WPS mode
				*/
			m2m_wifi_wps_disable();
			modeTaskState = MODE_LED_STATE_IDLE;
			led_mode = MODE_LED_IDLE;
			m2m_wifi_default_connect(); // We may have had a prevous connection to an AP
			appState = APP_STATE_WAIT_FOR_WIFI_CONNECT;
		}
		break;
	}
	/*
			Begin the HTTP provisioning process
		 */
	case APP_STATE_HTTP_PROVISION: {
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
		httpActive = true;
		waitingAccessPoint = true;
		appState = APP_STATE_WAIT_PROVISION_EVENT;
		break;
	}
	/*
			Wait for the HTTP provisioning process to respond
		 */
	case APP_STATE_WAIT_PROVISION_EVENT: {
		if (isWifiConnected() == true) {
			httpActive = false;
			//
			// We have a connection, start the TCP server
			//
			appState = APP_STATE_START_SERVER;
			//
			// Change the LED mode to Connected to AP mode
			//
			led_mode = MODE_LED_AP_CONNECTED;
		} else if (httpActive == false) // Did provisioning timeout?
		{
			modeTaskState = MODE_LED_STATE_IDLE;
			led_mode = MODE_LED_IDLE;
			appState = APP_STATE_SPIN;
		}
		break;
	}
	case APP_STATE_WAIT_FOR_WIFI_CONNECT: {
		//
		// Are we connected?
		//
		if (isWifiConnected() == true) {
			//
			// We have a connection, start the TCP server
			//
			appState = APP_STATE_START_SERVER;
			//
			// Change the LED mode to Connected to AP mode
			//
			led_mode = MODE_LED_AP_CONNECTED;
		}
		break;
	}
	case APP_STATE_WAIT_WIFI_DISCONNECT_FOR_WPS: {
		if (isWifiConnected() == false) {
			// enter WPS mode
			appState = APP_STATE_CONNECT_WPS;
			modeTaskState = MODE_LED_STATE_IDLE;
			led_mode = MODE_LED_WPS_ACTIVE;
		}
		break;
	}
	case APP_STATE_WAIT_WIFI_DISCONNECT_FOR_HTTP: {
		if (isWifiConnected() == false) {
			// enter HTTP provisioning mode
			appState = APP_STATE_HTTP_PROVISION;
			modeTaskState = MODE_LED_STATE_IDLE;
			led_mode = MODE_LED_HTTP_PROVISIONING;
		}
		break;
	}
	case APP_STATE_START_SERVER: {
		GDB_TCPServerState = SM_HOME;
		UART_DEBUG_TCPServerState = SM_HOME;
		SWO_TRACE_TCPServerState = SM_IDLE; // Will start up when requested
		//
		// Wait for the server to come up
		//
		appState = APP_STATE_WAIT_FOR_SERVER;
		break;
	}
	case APP_STATE_WAIT_FOR_SERVER: {
		if (isGDBServerRunning() == true) {
			appState = APP_STATE_SPIN;
		}
		break;
	}
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"

	case APP_STATE_ERROR: {
		/* Indicate error */
		// TODO: Turn ON LED or something..
		appState = APP_STATE_SPIN;

		/* Intentional fall */
	}

	case APP_STATE_SPIN: {
		//
		// Check for a new GDB client connection, if found start the receive process for the client
		//
		if (g_newGDBClientConnected == true) {
			g_newGDBClientConnected = false;
			//
			// Set up a recv call
			//
			recv(gdbClientSocket, &localBuffer[0], INPUT_BUFFER_SIZE, 0);
		}
		if (g_newUartDebugClientconncted == true) {
			g_newUartDebugClientconncted = false;
			recv(uartDebugClientSocket, &localUartDebugBuffer[0], UART_DEBUG_INPUT_BUFFER_SIZE, 0);
		}
		if (g_newSwoTraceClientconncted == true) {
			g_newSwoTraceClientconncted = false;
			recv(swoTraceClientSocket, &localSwoTraceBuffer[0], SWO_TRACE_INPUT_BUFFER_SIZE, 0);
		}
		break;
	}
#pragma GCC diagnostic pop

	default: {
		DEBUG_WARN("APP_TASK[%d]: Unknown state.\r\n", appState);
	}
	}
	//
	// Check for swo trace data
	//
	// TODO Restore this when TRACESWO is implemented
	// traceSendData();
	//
	// Run the mode led task?
	//
	timer_disable_irq(TIM2, TIM_DIER_CC1IE);
	if (runModeLedTask == true) {
		runModeLedTask = false;
		mode_led_task();
	}
	timer_enable_irq(TIM2, TIM_DIER_CC1IE);
	/*
		If the LED mode is in Low Battery mode then there is no reason to
		check any further button presses or mode changes
	*/
	if (led_mode != MODE_LED_BATTERY_LOW) {
		//
		// Process the Mode button here.
		//
		if (pressActive == false) {
			if (gpio_get(SWITCH_PORT, SW_BOOTLOADER_PIN) == 0) {
				/*
				If the AppTask is in either APP_STATE_WAIT_WPS_EVENT
				or APP_STATE_WAIT_PROVISION_EVENT cancel the state
				*/
				if ((appState == APP_STATE_WAIT_WPS_EVENT) || (appState == APP_STATE_WAIT_PROVISION_EVENT)) {
					/*
					Cancel mode and revert
					*/
					if (appState == APP_STATE_WAIT_WPS_EVENT) {
						/*
							Cancel WPS mode
						*/
						m2m_wifi_wps_disable();
					} else {
						/*
							Cancel HTTP provisioning mode
						*/
						m2m_wifi_stop_provision_mode();
					}
					modeTaskState = MODE_LED_STATE_IDLE;
					led_mode = MODE_LED_IDLE;
					m2m_wifi_default_connect(); // We may have had a prevous connection to an AP
					appState = APP_STATE_WAIT_FOR_WIFI_CONNECT;
				} else {
					/*
					begin timing for new mode entry
					*/
					pressActive = true;
					startPressTimer();
				}
			}
		} else {
			if (gpio_get(SWITCH_PORT, SW_BOOTLOADER_PIN) != 0) {
				uint32_t timeDown = getPressTimer();

				pressActive = false;

				if (timeDown >= BUTTON_PRESS_HTTP_PROVISIONING) {
					/*
					If there is a connection wait till disconnected
					*/
					if (isWifiConnected() == true) {
						doWiFiDisconnect();
						m2m_wifi_disconnect();
						appState = APP_STATE_WAIT_WIFI_DISCONNECT_FOR_HTTP;
					} else {
						// Enter http provisioning mode
						appState = APP_STATE_HTTP_PROVISION;
					}
					led_mode = MODE_LED_HTTP_PROVISIONING;
					modeTaskState = MODE_LED_STATE_IDLE;
				} else if (timeDown >= BUTTON_PRESS_WPS) {
					/*
					If we have a connection, disconnect and wait for disconnect event
					*/
					if (isWifiConnected() == true) {
						doWiFiDisconnect();
						m2m_wifi_disconnect();
						appState = APP_STATE_WAIT_WIFI_DISCONNECT_FOR_WPS;
					} else {
						// enter WPS mode
						appState = APP_STATE_CONNECT_WPS;
					}
					modeTaskState = MODE_LED_STATE_IDLE;
					led_mode = MODE_LED_WPS_ACTIVE;
				}
			}
		}
	}
}

int WiFi_HaveInput(void)
{
	int iResult;
	// m2mStub_EintDisable ();
	iResult = uiBufferCount;
	// m2mStub_EintEnable ();
	return (iResult);
}

unsigned char wifi_get_next(void)
{
	unsigned char cReturn = 0x00;
	//
	// The buffer count is also managed in an ISR so protect this code
	//
	m2mStub_EintDisable();
	if (uiBufferCount != 0) {
		cReturn = inputBuffer[uiOutputIndex];
		uiOutputIndex = (uiOutputIndex + 1) % INPUT_BUFFER_SIZE;
		uiBufferCount -= 1;
	}
	m2mStub_EintEnable();
	return (cReturn);
}

unsigned char wifi_get_next_to(uint32_t timeout)
{
	platform_timeout_s t;
	unsigned char c = 0;
	int inputCount = 0;
	platform_timeout_set(&t, timeout);

	do {
		if ((inputCount = WiFi_HaveInput()) != 0) {
			break;
		}
		//
		// We must run the platform tasks or incomming data will not be transferred
		// to the input buffers
		//

		platform_tasks();

	} while (!platform_timeout_is_expired(&t));

	if (inputCount != 0) {
		c = wifi_get_next();
	}
	return (c);
}

void DoGDBSend(void)
{
	send(gdbClientSocket, &(gdbSendQueue[uiGDBSendQueueOut].packet[0]), gdbSendQueue[uiGDBSendQueueOut].len, 0);
	m2mStub_EintDisable();
	uiGDBSendQueueOut = (uiGDBSendQueueOut + 1) % SEND_QUEUE_SIZE;
	uiGDBSendQueueLength -= 1;
	m2mStub_EintEnable();
}

void DoUartDebugSend(void)
{
	send(uartDebugClientSocket, &(uartDebugSendQueue[uiUartDebugSendQueueOut].packet[0]),
		uartDebugSendQueue[uiUartDebugSendQueueOut].len, 0);
	m2mStub_EintDisable();
	uiUartDebugSendQueueOut = (uiUartDebugSendQueueOut + 1) % SEND_QUEUE_SIZE;
	uiUartDebugSendQueueLength -= 1;
	m2mStub_EintEnable();
}

void DoSwoTraceSend(void)
{
	send(swoTraceClientSocket, &(swoTraceSendQueue[uiSwoTraceSendQueueOut].packet[0]),
		swoTraceSendQueue[uiSwoTraceSendQueueOut].len, 0);
}

void send_uart_data(uint8_t *lpBuffer, uint8_t length)
{
	m2mStub_EintDisable();
	memcpy(uartDebugSendQueue[uiUartDebugSendQueueIn].packet, lpBuffer, length);
	uartDebugSendQueue[uiUartDebugSendQueueIn].len = length;
	uiUartDebugSendQueueIn = (uiUartDebugSendQueueIn + 1) % SEND_QUEUE_SIZE;
	uiUartDebugSendQueueLength += 1;
	m2mStub_EintEnable();
	DoUartDebugSend();
}

void send_swo_trace_data(uint8_t *buffer, uint8_t length)
{
	bool sendIt = false;

	m2mStub_EintDisable();
	memcpy(swoTraceSendQueue[uiSwoTraceSendQueueIn].packet, buffer, length);
	swoTraceSendQueue[uiSwoTraceSendQueueIn].len = length;
	uiSwoTraceSendQueueIn = (uiSwoTraceSendQueueIn + 1) % SEND_QUEUE_SIZE;
	uiSwoTraceSendQueueLength += 1;
	if (uiSwoTraceSendQueueLength == 1) //First data?
	{
		sendIt = true;
	} else {
		sendIt = false;
	}

	m2mStub_EintEnable();
	if (sendIt) {
		DoSwoTraceSend();
	}
}

static unsigned char sendBuffer[1024] = {0}; ///< The send buffer[ 1024]
static unsigned int sendCount = 0;           ///< Number of sends

void wifi_gdb_putchar(unsigned char theChar, int flush)
{
	sendBuffer[sendCount++] = theChar;
	if (flush != 0) {
		int len = sendCount;
		if (sendCount <= 0) {
			DEBUG_WARN("WiFi_putchar bad count\r\n");
		}
		sendCount = 0;
		DEBUG_WARN("Wifi_putchar %c\r\n", sendBuffer[0]);
		send(gdbClientSocket, &sendBuffer[0], len, 0);
		memset(&sendBuffer[0], 0x00, sizeof(sendBuffer));
	}
}
