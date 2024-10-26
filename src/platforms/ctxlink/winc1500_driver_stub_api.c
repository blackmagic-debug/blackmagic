/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2024  Sid Price.
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

//==============================================================================
// INCLUDES
//==============================================================================
#include "winc1500_api.h"
#include "winc1500_driver_api_helpers.h"

#include "general.h"
// #include "cdcacm.h"
// #include "usbuart.h"
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

#include "platform.h"

#define SPI_TRANSFER_BUFFER_SIZE 256U

tpfAppSocketCb app_socket_cb = NULL;
tpfAppWifiCb app_wifi_cb = NULL;

static int interrupt_enabled = 1;
static _Atomic uint32_t one_ms_counter = 0;

//==============================================================================
// GPIO Stub Functions:
// --------------------
//    - The WINC1500 driver needs to control three GPIO outputs to the WINC1500.
//      connected to WINC1500's Chip enable, Reset and SPI slave select.
//    - The GPIO's described in this section should be configured as outputs and
//      defaulted high prior to the WINC1500 driver running.
//==============================================================================

/* NOLINTNEXTLINE(readability-identifier-naming) */
void m2mStub_PinSet_CE(t_m2mWifiPinAction action)
{
	gpio_set_val(WINC1500_CHIP_EN_PORT, WINC1500_CHIP_EN, action != M2M_WIFI_PIN_LOW);
}

/* NOLINTNEXTLINE(readability-identifier-naming) */
void m2mStub_PinSet_RESET(t_m2mWifiPinAction action)
{
	gpio_set_val(WINC1500_RESET_PORT, WINC1500_RESET, action != M2M_WIFI_PIN_LOW);
}

/* NOLINTNEXTLINE(readability-identifier-naming) */
void m2mStub_PinSet_SPI_SS(t_m2mWifiPinAction action)
{
	gpio_set_val(WINC1500_PORT, WINC1500_SPI_NCS, action != M2M_WIFI_PIN_LOW);
}

//==============================================================================
// Interrupt Stub Functions:
// --------------------------
//    - The WINC1500 will interrupt the host MCU when events occur by setting the
//      IRQN line low.
//    - The Host MCU should be configured to trigger an interrupt on a falling edge.
//==============================================================================

/* NOLINTNEXTLINE(readability-identifier-naming) */
void m2mStub_EintEnable(void)
{
	interrupt_enabled += 1;
	exti_enable_request(WINC1500_IRQ);
}

/* NOLINTNEXTLINE(readability-identifier-naming) */
void m2mStub_EintDisable(void)
{
	if (--interrupt_enabled == 0) {
		interrupt_enabled = 0;
		exti_disable_request(WINC1500_IRQ);
	}
}

//==============================================================================
// Timer Stub Functions:
// ---------------------
//    - The WINC1500 state machines require a timer with one millisecond resolution.
//    - The timer is a 32-bit counter that counts up starting at 0x00000000 and
//      wraps back to 0 after reaching 0xffffffff.
//==============================================================================

// A TMR peripheral can be set to interrupt every 1ms. The timer ISR increments
// a global counter.
//
// Implement this timer ISR or call this function in your timer ISR to
// increment one_ms_counter variable as below:
//
/* NOLINTNEXTLINE(readability-identifier-naming) */
void m2m_TMR_ISR(void)
{
	one_ms_counter++;
}

/* NOLINTNEXTLINE(readability-identifier-naming) */
uint32_t m2mStub_GetOneMsTimer(void)
{
	return one_ms_counter;
}

//==============================================================================
// SPI Stub Functions:
// ---------------------
//    - The Host MCU will communicate to the WINC1500 via the SPI interface.
//==============================================================================

/* NOLINTNEXTLINE(readability-identifier-naming) */
void m2mStub_SpiTxRx(uint8_t *p_txBuf, uint16_t txLen, uint8_t *p_rxBuf, uint16_t rxLen)
{
	uint16_t byteCount;
	//
	// The following are intermediate buffers used to ensure
	// the TX and RX message sizes handled by the transfer routine
	// are the same size.
	//
	uint8_t outputBuffer[SPI_TRANSFER_BUFFER_SIZE] = {0};
	uint8_t inputBuffer[SPI_TRANSFER_BUFFER_SIZE] = {0};
	/*
     *	total number of byte to clock is whichever is larger, txLen or rxLen
     */
	byteCount = txLen >= rxLen ? txLen : rxLen;
	if (txLen > SPI_TRANSFER_BUFFER_SIZE || rxLen > SPI_TRANSFER_BUFFER_SIZE)
		return; // No way to communicate error, silently ignore data
	//
	// Copy the input data to the outputBuffer
	//
	memcpy(&outputBuffer[0], p_txBuf, txLen);
	//
	// Do the transfer
	//
	for (uint16_t i = 0; i < byteCount; i++)
		inputBuffer[i] = spi_xfer(WINC1500_SPI_CHANNEL, outputBuffer[i]);
	//
	// If we expected to receive bytes copy them to the rx buffer
	//
	if (rxLen > 0)
		memcpy(p_rxBuf, &inputBuffer[0], rxLen);
}

//==============================================================================
// Event Stub Functions:
// ---------------------
//    These are callback functions that the WINC1500 host driver calls to notify
//    the application of events. There are four categories of events:
//      - Wi-Fi events
//      - Socket events
//      - OTA (Over-The-Air) update events
//      - Error Events
//==============================================================================

/* NOLINTNEXTLINE(readability-identifier-naming) */
void registerWifiCallback(tpfAppWifiCb pfAppWifiCb)
{
	app_wifi_cb = pfAppWifiCb;
}

void m2m_wifi_handle_events(t_m2mWifiEventType eventCode, t_wifiEventData *p_eventData)
{
	if (app_wifi_cb)
		app_wifi_cb(eventCode, p_eventData);
	else
		DEBUG_WARN("STUB_WIFI_EVENT[%d]: Wi-Fi event handler not registered!\r\n", eventCode);
}

//             --------------- * end of wifi event block * ---------------

/* NOLINTNEXTLINE(readability-identifier-naming) */
void registerSocketCallback(tpfAppSocketCb pfAppSocketCb)
{
	app_socket_cb = pfAppSocketCb;
}

void m2m_socket_handle_events(SOCKET sock, t_m2mSocketEventType eventCode, t_socketEventData *p_eventData)
{
	if (app_socket_cb)
		app_socket_cb(sock, eventCode, p_eventData);
	else
		DEBUG_WARN("STUB_SOCK_EVENT[%d]: Socket event handler not registered!\r\n", eventCode);
}

//             --------------- * end of socket event block * ---------------

void m2m_ota_handle_events(t_m2mOtaEventType eventCode, t_m2mOtaEventData *p_eventData)
{
#if ENABLE_DEBUG == 0
	(void)eventCode;
#endif
	(void)p_eventData;
	DEBUG_WARN("STUB_OTA_EVENT[%d]: OTA event handler not registered!\r\n", eventCode);
}

void m2m_error_handle_events(uint32_t errorCode)
{
#if ENABLE_DEBUG == 0
	(void)errorCode;
#endif
	DEBUG_WARN("STUB_ERR_EVENT[x]: ERROR EVENT: %lu\n", errorCode);
}

#if defined(M2M_ENABLE_SPI_FLASH)

//==============================================================================
// Wi-Fi Console Functions:
// ---------------------
//    - Functions for firmware update utility
//    - Implement if necessary, otherwise leave blank.
//==============================================================================
void m2m_wifi_console_write_data(uint16_t length, uint8_t *p_buf)
{
	(void)length;
	(void)p_buf;
}

//
// These functions are not used and return dummy values
// to avoid a compiler error (-Werror-return)
//
uint8_t m2m_wifi_console_read_data(void)
{
	//return console_byte_read();
	return 0;
}

bool m2m_wifi_console_is_read_data(void)
{
	// true => Receive buffer has data, at least one more character can be read from console
	// false => Receive buffer is empty
	// return isConsoleDataAvaiable();
	return false;
}

#endif // M2M_ENABLE_SPI_FLASH
