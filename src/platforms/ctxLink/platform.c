/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
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

/* This file implements the platform specific functions for the STM32
 * implementation.
 */
#include <ctype.h>
#include "general.h"
#include "cdcacm.h"
#include "usbuart.h"
#include "morse.h"

#include "platform.h"
#include "WiFi_Server.h"

#include <libopencm3/stm32/f4/rcc.h>
#include <libopencm3/cm3/scb.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/cm3/systick.h>
#include <libopencm3/stm32/exti.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/stm32/syscfg.h>
#include <libopencm3/stm32/f4/flash.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/cm3/cortex.h>
#include <libopencm3/stm32/f4/adc.h>
#include <libopencm3/stm32/f4/spi.h>
#include <libopencm3/stm32/timer.h>


#include "winc1500_api.h"

static void adc_init( void );
int usbuart_debug_write(const char *buf, size_t len);
void gdb_if_putchar(unsigned char c, int flush);

#if 0

////////////////////////////////////////////////////////////////////////////////////////////////////
/// <summary> Writes.</summary>
///
/// <remarks> Sid Price, 3/15/2018.</remarks>
///
/// <param name="fd">  The fd.</param>
/// <param name="ptr"> [in,out] If non-null, the pointer.</param>
/// <param name="len"> The length.</param>
///
/// <returns> An int.</returns>
////////////////////////////////////////////////////////////////////////////////////////////////////

int _write(int fd, char *ptr, int len)
{
	/* Write "len" of char from "ptr" to file id "fd"
	 * Return number of char written.
	 * 
	 * fd is assumed to be stdout and we
	 * just end the data to the USB USART
	 */
	usbuart_debug_write(ptr, len);
	return len;
}
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////
/// <summary> Platform waitm s.</summary>
///
/// <remarks> Sid Price, 3/15/2018.</remarks>
///
/// <param name="waitTime"> The wait time.</param>
////////////////////////////////////////////////////////////////////////////////////////////////////

// static void platform_WaitmS( u_int32_t waitTime )
// {
// 	u_int32_t	theTargetTime = platform_time_ms() + waitTime;
// 	while ( theTargetTime > platform_time_ms() )
// 		;
// }

jmp_buf fatal_error_jmpbuf; ///< The fatal error jmpbuf

////////////////////////////////////////////////////////////////////////////////////////////////////
/// <summary> Values that represent tag select SSID states.</summary>
///
/// <remarks> Sid Price, 3/15/2018.</remarks>
////////////////////////////////////////////////////////////////////////////////////////////////////

typedef enum tagSelectSSID_States
{
	eStart = 0, ///< .
	eSelectSSID ,   ///< An enum constant representing the select SSID option
	ePassPhrase,	///< An enum constant representing the pass phrase option
	eConnect	///< An enum constant representing the connect option
} SelectSSID_States;

////////////////////////////////////////////////////////////////////////////////////////////////////
/// <summary> WiFi initialize.</summary>
///
/// <remarks> Sid Price, 3/15/2018.</remarks>
////////////////////////////////////////////////////////////////////////////////////////////////////

void wifi_init(void)
{
	//
	// Initialze the WiFi server app
	//
	m2m_wifi_init();
	APP_Initialize();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
/// <summary> Platform initialize.</summary>
///
/// <remarks> Sid Price, 3/15/2018.</remarks>
////////////////////////////////////////////////////////////////////////////////////////////////////

void platform_init(void)
{
	// rcc_clock_setup_hse_3v3 (&rcc_hse_8mhz_3v3[RCC_CLOCK_3V3_84MHZ]);
	rcc_periph_clock_enable (RCC_GPIOA);
	rcc_periph_clock_enable (RCC_GPIOB);
	rcc_periph_clock_enable (RCC_GPIOC);
	//
	// Initialize the "Bootloader" input, it is used in
	// normal running mode as the WPS selector switch
	// The switch is active low and therefore needs
	// a pullup
	//
	gpio_mode_setup (SWITCH_PORT, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, SW_BOOTLOADER_PIN);
	/*
	Check the Bootloader button, not sure this is needed fro the native-derived hardware, need to check if
	this switch is looked at in the DFU bootloader
	*/
	if ( !(gpio_get (SWITCH_PORT, SW_BOOTLOADER_PIN)) )		// SJP - 0118_2016, changed to use defs in platform.h and the switch is active low!
	{
		platform_request_boot ();		// Does not return from this call		
	}
	//
	// Normal running ... set up clocks and peripherals
	//  
	//rcc_clock_setup_hse_3v3( &rcc_hse_8mhz_3v3[RCC_CLOCK_3V3_84MHZ] );
	rcc_clock_setup_pll(&rcc_hse_8mhz_3v3[RCC_CLOCK_3V3_84MHZ]) ;

	rcc_periph_clock_enable (RCC_GPIOA);
	rcc_periph_clock_enable (RCC_GPIOB);
	rcc_periph_clock_enable( RCC_GPIOC );
	/* Enable peripherals */
	rcc_periph_clock_enable( RCC_OTGFS );

	rcc_peripheral_enable_clock(&RCC_AHB1ENR, RCC_AHB1ENR_CRCEN);
	/*
		toggle the PWR_BR and SRST pins
		
		this is what the native BMP does, don't really know why
	*/
	gpio_port_write (GPIOA, 0xA102);
	gpio_port_write (GPIOB, 0x0000);

	gpio_port_write (GPIOA, 0xA182);
	gpio_port_write (GPIOB, 0x0002);

	/*
	 * Set up USB Pins and alternate function
	 * 
	 * Setup REN output
	 * 
	 */
	gpio_clear(USB_PU_PORT, USB_PU_PIN);
	gpio_mode_setup(USB_PU_PORT, GPIO_MODE_INPUT, GPIO_PUPD_NONE, USB_PU_PIN);
	/*
	 * USB DM & DP pins
	 */
	gpio_mode_setup(GPIOA, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, GPIO9);
	gpio_mode_setup(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE,
		GPIO11 | GPIO12);
	gpio_set_af(GPIOA, GPIO_AF10, GPIO9 | GPIO11 | GPIO12);
	//
	// SJP - 0119_2016
	//
	// The following sets the register speed for the JTAG/SWD bits
	//
	// See the spreadsheet "SWD Port speed bits - OneNote"
	//
	GPIOA_OSPEEDR &=~(TCK_PIN |TMS_PIN | TDI_PIN) ;	// Clear the speed bits for TCK, TMS, & TDI
	GPIOA_OSPEEDR |= (TCK_PIN | TMS_PIN | TDI_PIN);	// Set TCK, TMS,& TDI to "Fast speed"
	gpio_mode_setup(JTAG_PORT, GPIO_MODE_OUTPUT,
			GPIO_PUPD_NONE,
			TMS_DIR_PIN | TMS_PIN | TCK_PIN | TDI_PIN);

	gpio_mode_setup(TDO_PORT, GPIO_MODE_INPUT,
			GPIO_PUPD_NONE,
			TDO_PIN);
	//
	// Initialize the LED ports
	//
	gpio_mode_setup(LED_PORT,
		GPIO_MODE_OUTPUT,
		GPIO_PUPD_NONE,
		LED_IDLE_RUN | LED_ERROR | LED_MODE);
	gpio_mode_setup(LED_PORT_UART,
		GPIO_MODE_OUTPUT,
		GPIO_PUPD_NONE,
		LED_UART);
	//
	// Setup RST_SENSE as input
	//
	//	Give it a pullup (NOT reset) just in case similar issue to
	// native firmware.
	//
	gpio_mode_setup(SRST_SENSE_PORT, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, SRST_SENSE_PIN);
	/* Enable SRST output. Original uses a NPN to pull down, so setting the
	 * output HIGH asserts. Mini is directly connected so use open drain output
	 * and set LOW to assert.
	 */
	platform_srst_set_val(false);
	//
	// setup the iRSTR pin
	//
	gpio_mode_setup(SRST_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLUP, SRST_PIN);
	/* 
	 * Enable internal pull-up on PWR_BR so that we don't drive
	 * TPWR locally or inadvertently supply power to the target. 
	 */
	gpio_set(PWR_BR_PORT, PWR_BR_PIN);
	gpio_mode_setup(PWR_BR_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, PWR_BR_PIN);
	gpio_set_output_options(GPIOB, GPIO_OTYPE_OD, GPIO_OSPEED_50MHZ, PWR_BR_PIN);
	
	adc_init();
	platform_timing_init();
//	//
//	// Let's test some of the output ports
//	//
//	// Turn on target power to enable viewing signals on I/F connector
//	gpio_clear(PWR_BR_PORT, PWR_BR_PIN);
//	//
//	while(1)
//	{
//		gpio_set(TCK_PORT, TCK_PIN);
//		platform_delay(2);
//		gpio_clear(TCK_PORT, TCK_PIN);
//		platform_delay(2);
//	}
//
	wifi_init();			//	Setup the Wifi channel
#ifdef WINC_1500_FIRMWARE_UPDATE
	//
	// ONLY for firmware update
	//  
	// Perform WINC1500 reset sequence
	//
	m2mStub_PinSet_CE (M2M_WIFI_PIN_LOW);
	m2mStub_PinSet_RESET (M2M_WIFI_PIN_LOW);
	DelayMs (100);
	m2mStub_PinSet_CE (M2M_WIFI_PIN_HIGH);
	DelayMs (10);
	m2mStub_PinSet_RESET (M2M_WIFI_PIN_HIGH);
	DelayMs (10);
	while (1) {
		;
	}
#endif
	usbuart_init();
	cdcacm_init();
}

//
// Use the passed string to configure the USB UART
//
// e.g. 38400,8,N,1
bool platform_configure_uart (char * configurationString)
{
	bool fResult ;
	uint32_t baudRate;
	uint32_t bits;
	uint32_t stopBits;
	char	parity;
	uint32_t count;
	if (strlen (configurationString) > 5)
	{
		count = sscanf (configurationString, "%ld,%ld,%c,%ld", &baudRate, &bits, &parity, &stopBits);
		if (count == 4)
		{
			uint32_t parityValue;
			usart_set_baudrate (USBUSART, baudRate);
			usart_set_databits (USBUSART, bits);
			usart_set_stopbits (USBUSART, stopBits);
			switch (parity)
			{
				default:
				case 'N':
				{
					parityValue = USART_PARITY_NONE;
					break;
				}

				case 'O':
				{
					parityValue = USART_PARITY_ODD;
					break;
				}

				case 'E':
				{
					parityValue = USART_PARITY_EVEN;
					break;
				}
				usart_set_parity (USBUSART, parityValue);
			}
			fResult = true;
		}
	}
	else
	{
		fResult = true;		// ignore possible newline strings
	}
	return fResult;
}

//
// The following method is called in the main gdb loop in order to run 
// the app and wifi tasks
//
// It also checks for GDB packets from a connected WiFi client
//
//	Return "0" if no WiFi client or no data from client
//	Return number of bytes available from the WiFi client
//

static bool fStartup = true;	///< True to startup

////////////////////////////////////////////////////////////////////////////////////////////////////
/// <summary> Platform tasks.</summary>
///
/// <remarks> Sid Price, 3/15/2018.</remarks>
////////////////////////////////////////////////////////////////////////////////////////////////////

void platform_tasks(void)
{
	APP_Task();					// WiFi Server app tasks
	if(fStartup == true)
	{
		fStartup = false;
		platform_delay(1000);
	}
	m2m_wifi_task();			// WINC1500 tasks
	GDB_TCPServer();			// Run the TCP sever state machine
	UART_TCPServer ();			// Run the Uart/Debug TCP server
}

////////////////////////////////////////////////////////////////////////////////////////////////////
/// <summary> Platform srst set value.</summary>
///
/// <remarks> Sid Price, 3/15/2018.</remarks>
///
/// <param name="assert"> True to assert.</param>
////////////////////////////////////////////////////////////////////////////////////////////////////

void platform_srst_set_val(bool assert)
{
	gpio_set_val(TMS_PORT, TMS_PIN, 1);
	if ((platform_hwversion() == 0) ||
	    (platform_hwversion() >= 3)) {
		gpio_set_val(SRST_PORT, SRST_PIN, assert);
	} else {
		gpio_set_val(SRST_PORT, SRST_PIN, !assert);
	}
	if (assert) {
		for (int i = 0; i < 10000; i++) asm("nop");
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
/// <summary> Determines if we can platform srst get value.</summary>
///
/// <remarks> Sid Price, 3/15/2018.</remarks>
///
/// <returns> True if it succeeds, false if it fails.</returns>
////////////////////////////////////////////////////////////////////////////////////////////////////

bool platform_srst_get_val(void)
{
	if (platform_hwversion() == 0) {
		return gpio_get(SRST_SENSE_PORT, SRST_SENSE_PIN) == 0;
	} else if (platform_hwversion() >= 3) {
		return gpio_get(SRST_SENSE_PORT, SRST_SENSE_PIN) != 0;
	} else {
		return gpio_get(SRST_PORT, SRST_PIN) == 0;
	}
}

bool platform_target_get_power(void)
{
	if (platform_hwversion() > 0) {
		return !gpio_get(PWR_BR_PORT, PWR_BR_PIN);
	}
	return 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
/// <summary> Platform target set power.</summary>
///
/// <remarks> Sid Price, 3/15/2018.</remarks>
///
/// <param name="power"> True to power.</param>
////////////////////////////////////////////////////////////////////////////////////////////////////

void platform_target_set_power(bool power)
{
	if (platform_hwversion() > 0) {
		gpio_set_val(PWR_BR_PORT, PWR_BR_PIN, !power);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
/// <summary> ADC initialize.</summary>
///
/// <remarks> Sid Price, 3/15/2018.</remarks>
////////////////////////////////////////////////////////////////////////////////////////////////////

static void adc_init( void )
{
	rcc_periph_clock_enable( RCC_ADC1 );
	gpio_mode_setup (VTGT_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, VTGT_PIN);	// Target voltage monitor input
	gpio_mode_setup (VBAT_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, VBAT_PIN);	// Battery voltage monitor input

	adc_power_off( ADC1 );
	adc_disable_scan_mode( ADC1 );
	adc_set_sample_time_on_all_channels( ADC1, ADC_SMPR_SMP_480CYC);

	adc_power_on( ADC1 );
	/* Wait for ADC starting up. */
	for ( int i = 0; i < 800000; i++ )    /* Wait a bit. */
		__asm__( "nop" );
}

#define	CTXLINK_BATTERY_INPUT			0	// ADC Channel for battery input
#define	CTXLINK_TARGET_VOLTAGE_INPUT	8	// ADC Chanmel for target voltage

#define	CTXLINK_ADC_BATTERY	0
#define	CTXLINK_ADC_TARGET	1

static uint32_t	inputVoltages[2] = { 0 };

static uint8_t	adcChannels[] = { CTXLINK_BATTERY_INPUT, CTXLINK_TARGET_VOLTAGE_INPUT };	/// ADC channels used by ctxLink

////////////////////////////////////////////////////////////////////////////////////////////////////
/// <summary> Read all the ADC channels used by ctxLink</summary>
///
/// <remarks>
/// 		  </remarks>
////////////////////////////////////////////////////////////////////////////////////////////////////

void platform_adc_read (void)
{
	// PROBE_PIN;
	adc_set_regular_sequence (ADC1, 1, &(adcChannels[CTXLINK_ADC_BATTERY]));
	adc_start_conversion_regular (ADC1);
	/* Wait for end of conversion. */
	while (!adc_eoc (ADC1))
		;
	inputVoltages[CTXLINK_ADC_BATTERY] = adc_read_regular (ADC1);
	adc_set_regular_sequence (ADC1, 1, &(adcChannels[CTXLINK_ADC_TARGET]));
	adc_start_conversion_regular (ADC1);
	/* Wait for end of conversion. */
	while (!adc_eoc (ADC1))
		;
	inputVoltages[CTXLINK_ADC_TARGET] = adc_read_regular (ADC1);
}

//
// With a 3V3 reference voltage and using a 12 bit ADC each bit represents 0.8mV
//  Note the battery voltage is divided by 2 with resistor divider
// 
// No battery voltage 1 == 2.0v or a count of 1250
// No battery voltage 2 == 4.268v or a count of 2668
// Battery present (report voltage) < 4.268v or a count of 2667
// Low batter voltage == 3.6v or a count of 2250
//
#define	uiBattVoltage_1	1250
#define	uiBattVoltage_2	2668
#define uiLowBattery	2250

volatile uint32_t	retVal;
volatile uint32_t	batteryVoltage = 0;

bool	fLastState = true;
bool	fBatteryPresent = false;

#define voltagePerBit	0.000806

const char *platform_battery_voltage (void)
{
	static char ret[64] = { 0 };
	if (fBatteryPresent == true) {
		double	batteryVoltageAsDouble = (batteryVoltage * voltagePerBit) * 2 ;
		sprintf (&ret[0], "\n      Battery : %.3f", batteryVoltageAsDouble);
		//
		// Let's truncate to 2 places
		//
		ret[21] = 'V';
		ret[22] = '\n';
		ret[23] = 0x00;
	}
	else {
		sprintf (&ret[0], "\n      Battery : Not present");
	}
	return ret;
}

bool platform_check_battery_voltage (void)
{
	bool	fResult;
	platform_adc_read ();
	batteryVoltage = inputVoltages[CTXLINK_ADC_BATTERY];
	fResult = fLastState; 
	//
	// Is battery connected?
	// 
	if ((batteryVoltage <= uiBattVoltage_1) || (batteryVoltage >= uiBattVoltage_2)) {
		fBatteryPresent = false;
		fLastState = fResult = true;
	}
	else {
		fBatteryPresent = true;
		//
		// Is the voltage good?
		//  
		if (batteryVoltage <= uiLowBattery)
		{
			fLastState = fResult = false;
		}
		else
		{
			fLastState = fResult = true;
		}
		}
	return fResult;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
/// <summary> Platform target voltage.</summary>
///
/// <remarks> Sid Price, 3/15/2018.</remarks>
///
/// <returns> Null if it fails, else a pointer to a const char.</returns>
////////////////////////////////////////////////////////////////////////////////////////////////////

//
// This char array receives both the target and battrey voltages
//  
static char voltages[64] = { 0 };

const char *platform_target_voltage( void )
{
	//static char ret[] = "0.0V";
	//static uint32_t	retVal;

	//retVal =inputVoltages[CTXLINK_ADC_TARGET] * 99; /* 0-4095 */
	//ret[0] = '0' + retVal / 62200;
	//ret[2] = '0' + ( retVal / 6220 ) % 10;
	//strcpy (&voltages[0], &ret[0]);
	//strcat (&voltages[0], platform_battery_voltage());
	double targetVoltage = inputVoltages[CTXLINK_ADC_TARGET] * voltagePerBit;
	char	ret[64];
	sprintf (&ret[0], "%.3f", targetVoltage * 2 );
	//
	// Let's truncate to 2 places
	//
	ret[4] = 'V';
	ret[5] = 0x00;
	strcpy (&voltages[0], &ret[0]);
	strcat (&voltages[0], platform_battery_voltage());
	return voltages;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
/// <summary> Platform request boot.</summary>
///
/// <remarks> Sid Price, 3/15/2018.</remarks>
////////////////////////////////////////////////////////////////////////////////////////////////////

void platform_request_boot(void)
{
	typedef void (*pFunction)(void);
	const uint32_t ApplicationAddress = 0x1FFF0000;
	register uint32_t JumpAddress = 0;
	register uint32_t addr = 0;
	static pFunction Jump_To_Application;

	/* We start here */
	cm_disable_interrupts ();
	uint32_t value = 0 - 1;
	NVIC_ICER (0) = value;
	NVIC_ICER (1) = value;
	NVIC_ICER (2) = value;
	NVIC_ICPR (0) = value;
	NVIC_ICPR (1) = value; 
	NVIC_ICPR (2) = value;


	STK_CSR = 0;
	/* Reset the RCC clock configuration to the default reset state ------------*/
	/* Reset value of 0x83 includes Set HSION bit */
	RCC_CR |= (uint32_t) 0x00000082;
	/* Reset CFGR register */
	RCC_CFGR = 0x00000000;

	/* Disable all interrupts */
	RCC_CIR = 0x00000000;

	FLASH_ACR = 0;
	__asm volatile ("isb");
	__asm volatile ("dsb");
	cm_enable_interrupts ();
	addr = *((uint32_t  *)ApplicationAddress) ;
	JumpAddress = *((uint32_t  *) (ApplicationAddress + 4));
	Jump_To_Application = (pFunction) JumpAddress;
	/*
	set up the stack for the bootloader
	*/
	__asm__ ("mov sp,%[v]" : : [v]"r"(addr));
	//__asm (
	//	"movl"
	//	"ldr r0, =addr \n\t"
	//	"ldr sp, =addr"
	//);
	//
	Jump_To_Application ();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
/// <summary> Determines if we can platform WiFi client.</summary>
///
/// <remarks> Sid Price, 3/15/2018.</remarks>
///
/// <returns> True if it succeeds, false if it fails.</returns>
////////////////////////////////////////////////////////////////////////////////////////////////////

bool platform_wifi_client( void )
{
	//return ( WiFi_GotClient() ) ;
	return(false);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
/// <summary> Platform WiFi getpacket.</summary>
///
/// <remarks> Sid Price, 3/15/2018.</remarks>
///
/// <param name="pBuf">    [in,out] If non-null, the buffer.</param>
/// <param name="bufSize"> Size of the buffer.</param>
///
/// <returns> An int.</returns>
////////////////////////////////////////////////////////////////////////////////////////////////////

#define	UNUSED(x) (void)(x)

int  platform_wifi_getpacket( char * pBuf,  int bufSize )
{
	UNUSED(pBuf) ;
	UNUSED(bufSize) ;
	int iResult = 0;
	//iResult  = WiFi_GetPacket( pBuf, bufSize );
	return ( iResult ) ;
}

#define PACKET_SIZE	64

bool platform_has_network_client(uint8_t * lpBuf_rx, uint8_t * lpBuf_rx_in, uint8_t * lpBuf_rx_out, unsigned fifoSize)
{
	bool fResult = false ;
	if ( (fResult = isUARTClientConnected()) == false)
	{
		return fResult ;
	}
		/* if fifo empty, nothing further to do */
	if (*lpBuf_rx_in == *lpBuf_rx_out) {
		/* turn off LED, disable IRQ */
		timer_disable_irq(USBUSART_TIM, TIM_DIER_UIE);
		gpio_clear(LED_PORT_UART, LED_UART);
	}
	else
	{
		uint8_t packet_buf[PACKET_SIZE];
		uint8_t packet_size = 0;
		uint8_t buf_out = *lpBuf_rx_out;

		/* copy from uart FIFO into local network packet buffer */
		while (*lpBuf_rx_in != buf_out && packet_size < PACKET_SIZE)
		{
			packet_buf[packet_size++] = lpBuf_rx[buf_out++];

			/* wrap out pointer */
			if (buf_out >= fifoSize)
			{
				buf_out = 0;
			}

		}
		//
		// Send the data to the client
		//
		SendUartData(&packet_buf[0], packet_size) ;
		*lpBuf_rx_out += packet_size ;
		*lpBuf_rx_out %= fifoSize;
	}
	return fResult ;
}
//#ifdef ENABLE_DEBUG
//enum
//{
//	RDI_SYS_OPEN  = 0x01,
//	RDI_SYS_WRITE = 0x05,
//	RDI_SYS_ISTTY = 0x09,
//};
//
//int rdi_write( int fn, const char *buf, size_t len )
//{
//	( void )fn;
//	if ( debug_bmp )
//		return len - usbuart_debug_write( buf, len );
//
//	return 0;
//}
//
//struct ex_frame
//{
//	union
//	{
//		int syscall;
//		int retval;
//	};
//	const int *params;
//	uint32_t r2, r3, r12, lr, pc;
//};
//
//void debug_monitor_handler_c( struct ex_frame *sp )
//{
//	/* Return to after breakpoint instruction */
//	sp->pc += 2;
//
//	switch ( sp->syscall )
//	{
//		case RDI_SYS_OPEN:
//			sp->retval = 1;
//			break;
//		case RDI_SYS_WRITE:
//			sp->retval = rdi_write( sp->params[0], ( void* )sp->params[1], sp->params[2] );
//			break;
//		case RDI_SYS_ISTTY:
//			sp->retval = 1;
//			break;
//		default:
//			sp->retval = -1;
//	}
//
//}
//
//asm( ".globl debug_monitor_handler\n"
//    ".thumb_func\n"
//    "debug_monitor_handler: \n"
//    "    mov r0, sp\n"
//    "    b debug_monitor_handler_c\n" );
//
//#endif
