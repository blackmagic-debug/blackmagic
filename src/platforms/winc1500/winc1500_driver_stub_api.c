
/**
  WINC1500 Driver Stub API Source File 

  @Company
    Microchip Technology Inc.

  @File Name
    winc1500_driver_stub_api.c

  @Summary
    Before one can use the WINC1500 Driver API, there are some MCU-specific stub 
    functions that must be coded. The WINC1500 Driver will call these functions 
    during run-time.

  @Description
    The function prototypes are provided using the MPLAB Code Configurator (MCC)
    Plug-in. The MLA Driver will call these functions, but they must be coded by 
    the user. The stub functions control MUC-specific hardware and event handling:
       - SPI Interface
       - GPIO control
       - 1ms Timer
       - Interrupt from WINC1500
       - Event handling for Wi-Fi, socket, and OTA events
 */

/*
    (c) 2016 Microchip Technology Inc. and its subsidiaries. You may use this
    software and any derivatives exclusively with Microchip products.

    THIS SOFTWARE IS SUPPLIED BY MICROCHIP "AS IS". NO WARRANTIES, WHETHER
    EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS SOFTWARE, INCLUDING ANY IMPLIED
    WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY, AND FITNESS FOR A
    PARTICULAR PURPOSE, OR ITS INTERACTION WITH MICROCHIP PRODUCTS, COMBINATION
    WITH ANY OTHER PRODUCTS, OR USE IN ANY APPLICATION.

    IN NO EVENT WILL MICROCHIP BE LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE,
    INCIDENTAL OR CONSEQUENTIAL LOSS, DAMAGE, COST OR EXPENSE OF ANY KIND
    WHATSOEVER RELATED TO THE SOFTWARE, HOWEVER CAUSED, EVEN IF MICROCHIP HAS
    BEEN ADVISED OF THE POSSIBILITY OR THE DAMAGES ARE FORESEEABLE. TO THE
    FULLEST EXTENT ALLOWED BY LAW, MICROCHIP'S TOTAL LIABILITY ON ALL CLAIMS IN
    ANY WAY RELATED TO THIS SOFTWARE WILL NOT EXCEED THE AMOUNT OF FEES, IF ANY,
    THAT YOU HAVE PAID DIRECTLY TO MICROCHIP FOR THIS SOFTWARE.

    MICROCHIP PROVIDES THIS SOFTWARE CONDITIONALLY UPON YOUR ACCEPTANCE OF THESE
    TERMS.
 */

//==============================================================================
// INCLUDES
//==============================================================================    
#include "winc1500_api.h"
#include "winc1500_driver_api_helpers.h"

#include "general.h"
#include "cdcacm.h"
#include "usbuart.h"
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
//==============================================================================
// GPIO Stub Functions:
// --------------------
//    - The WINC1500 driver needs to control three GPIO outputs to the WINC1500.
//      connected to WINC1500's Chip enable, Reset and SPI slave select.
//    - The GPIO's described in this section should be configured as outputs and 
//      defaulted high prior to the WINC1500 driver running.     
//==============================================================================

void m2mStub_PinSet_CE(t_m2mWifiPinAction action)
{
    if (action == M2M_WIFI_PIN_LOW)
    {
	    //
	    // Set CHIP_EN output low
	    //
	    gpio_clear(WINC1500_CHIP_EN_PORT, WINC1500_CHIP_EN);
	}
    else
    {
	    //
	    // Set CHIP_EN output high
	    //
	    gpio_set(WINC1500_CHIP_EN_PORT, WINC1500_CHIP_EN);
    }
}

void m2mStub_PinSet_RESET(t_m2mWifiPinAction action)
{
    if (action == M2M_WIFI_PIN_LOW)
    {
	    //
	    // Set reset output low
	    //
	    gpio_clear(WINC1500_RESET_PORT, WINC1500_RESET);
    }
    else
    {
	    //
	    // Set reset output high
	    //
	    gpio_set(WINC1500_RESET_PORT, WINC1500_RESET);
    }
}

void m2mStub_PinSet_SPI_SS(t_m2mWifiPinAction action)
{
    if (action == M2M_WIFI_PIN_LOW)
    {
	    //
	    // Set SS output low
	    //
	    gpio_clear(WINC1500_PORT, WINC1500_SPI_NCS);
    }
    else
    {
	    //
	    // Set SS output high
	    //
	    gpio_set(WINC1500_PORT, WINC1500_SPI_NCS);
    }
}

//==============================================================================
// Interrupt Stub Functions:
// --------------------------
//    - The WINC1500 will interrupt the host MCU when events occur by setting the 
//      IRQN line low. 
//    - The Host MCU should be configured to trigger an interrupt on a falling edge.
//==============================================================================

int intEnabled = 1;
void m2mStub_EintEnable(void)
{
	intEnabled += 1;
	exti_enable_request(WINC1500_IRQ);
}

void m2mStub_EintDisable(void)
{
	if ( --intEnabled == 0 )
	{
		intEnabled = 0;
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

static volatile uint32_t g_oneMsCounter = 0;

// 
// Implement this timer ISR or call this function in your timer ISR to
// increment g_oneMsCounter variable as below:
//
void m2m_TMR_ISR(void) 
{
    g_oneMsCounter++;
}

uint32_t m2mStub_GetOneMsTimer(void)
{
    uint32_t tmp;

	timer_disable_irq(TIM2, TIM_DIER_CC1IE);
    tmp = g_oneMsCounter; // get a clean copy of counter variable 
	timer_enable_irq(TIM2, TIM_DIER_CC1IE);

	// return platform_time_ms();		// TODO, this looks wrong it should return the tmp value I think
	return tmp; 		// TODO, this looks wrong it should return the tmp value I think
}

//==============================================================================
// SPI Stub Functions:
// ---------------------
//    - The Host MCU will communicate to the WINC1500 via the SPI interface.
//==============================================================================

void m2mStub_SpiTxRx(uint8_t *p_txBuf,
                    uint16_t txLen,
                    uint8_t *p_rxBuf,
                    uint16_t rxLen)
{
    uint16_t byteCount;
    uint16_t i;
	//
	// The following are intermediate buffers used to ensure
	// the TX and RX message sizes handked by the transfer routine
	// are the same size.
	//
	uint8_t		outputBuffer[256] = { 0 };
	uint8_t		inputBuffer[256] = { 0 };
    /* 
     *	total number of byte to clock is whichever is larger, txLen or rxLen
     */
    byteCount = (txLen >= rxLen) ? txLen : rxLen;
	//
	// Copy the input data to the outputBuffer
	//
	memcpy(&outputBuffer[0], p_txBuf, txLen);
	//
	// Do the transfer
	//
	for(i = 0 ; i < byteCount ; i++)
	{
		inputBuffer[i] = spi_xfer(WINC1500_SPI_CHANNEL, outputBuffer[i]);
	}
	//
	// If we ewxpected to receive bytes copy them to the rx buffer
	//
	if (rxLen > 0)
	{
		memcpy(p_rxBuf, &inputBuffer[0], rxLen);
	}
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

volatile tpfAppWifiCb gpfAppWifiCb = NULL;

void registerWifiCallback(tpfAppWifiCb pfAppWifiCb)
{
    gpfAppWifiCb = pfAppWifiCb;
}

void m2m_wifi_handle_events(t_m2mWifiEventType eventCode, t_wifiEventData *p_eventData)
{
    if (gpfAppWifiCb)
        gpfAppWifiCb(eventCode, p_eventData);
    else
        dprintf("STUB_WIFI_EVENT[%d]: Wi-Fi event handler not registered!\r\n", eventCode);
}

//             --------------- * end of wifi event block * ---------------

volatile tpfAppSocketCb gpfAppSocketCb = NULL;

void registerSocketCallback(tpfAppSocketCb pfAppSocketCb)
{
    gpfAppSocketCb = pfAppSocketCb;
}

void m2m_socket_handle_events(SOCKET sock, t_m2mSocketEventType eventCode, t_socketEventData *p_eventData)
{
    if (gpfAppSocketCb)
        gpfAppSocketCb(sock, eventCode, p_eventData);
    else
        dprintf("STUB_SOCK_EVENT[%d]: Socket event handler not registered!\r\n", eventCode);
}

//             --------------- * end of socket event block * ---------------

void m2m_ota_handle_events(t_m2mOtaEventType eventCode, t_m2mOtaEventData *p_eventData)
{
    dprintf("STUB_OTA_EVENT[%d]: OTA event handler not registered!\r\n", eventCode);
}

void m2m_error_handle_events(uint32_t errorCode)
{
    dprintf("STUB_ERR_EVENT[x]: ERROR EVENT: %lu\n", errorCode);
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
    // uint16_t i;
    
    // for (i = 0; i < length; i++)
    // {
    //     console_byte_write(p_buf[i]);
    // }
}
//
// These functions are not used and return dummy values
// to avoid a compiler error (-Werror-return)
//
uint8_t m2m_wifi_console_read_data(void)
{	
    //return console_byte_read();
    return 0 ;
}

bool m2m_wifi_console_is_read_data(void)
{
	// true => Receive buffer has data, at least one more character can be read from console
    // false => Receive buffer is empty
    // return isConsoleDataAvaiable();
    return false ;
}

#endif // M2M_ENABLE_SPI_FLASH