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

#include "winc1500_api.h"

#undef gdb_if_putchar

static void adc_init( void );
int usbuart_debug_write(const char *buf, size_t len);
void gdb_if_putchar(unsigned char c, int flush);

static bool fMenuIsActive = false;  ///< True if menu is active
static char	szMenuInputBuffer[128] = { 0 }; ///< The menu input buffer[ 128]
static char	szMenuInputForProcessing[128] = { 0 };  ///< The menu input for processing[ 128]
static u_int32_t	inputCount = 0; ///< Number of inputs
static bool fShowMenu = false;  ///< True to show, false to hide the menu
static char	cEscapePipeline[4] = { 0 }; ///< The escape pipeline[ 4]
static int	iEscapeIndex = 0;   ///< Zero-based index of the escape index
static char szPassPhrase[128] = { 0 };  ///< The pass phrase[ 128]

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

static void platform_WaitmS( u_int32_t waitTime )
{
	u_int32_t	theTargetTime = platform_time_ms() + waitTime;
	while ( theTargetTime > platform_time_ms() )
		;
}

jmp_buf fatal_error_jmpbuf; ///< The fatal error jmpbuf
//
// Send the command menu to the debug UASRT
//
static const char *lpszMenu = "\nWifi Config\n1. - Select SSID for connection\n2. - Enable DHCP Client mode\n3. - Enter static IP address\n4. - Exit\n";	///< The menu
static uint8_t uiMenuItem = UINT8_MAX;  ///< The menu item
static const char *lpszExitMessage = " <- Configuration exit\n";	///< Message describing the exit
static const char *lpszClearScreen = "\f";  ///< The clear screen

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

static SelectSSID_States SSID_State = eStart;   ///< State of the SSID
static u_int32_t	numberOfSSIDs = 0;  ///< Number of ssi ds
static u_int32_t	selectedSSID = -1;  ///< The selected SSID

static const char *lpszBadInput = " <-Invalid selection\n"; ///< The bad input
static const char *lpszOutOfRange = "<-Selection is out of range\n";	///< The out of range
static const char *lpszConnecting = "\nConnecting ..."; ///< The connecting

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
	rcc_clock_setup_hse_3v3( &rcc_hse_8mhz_3v3[RCC_CLOCK_3V3_84MHZ] );
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
	uint32_t	uiSize = 0;

	APP_Task();					// WiFi Server app tasks
	if(fStartup == true)
	{
		fStartup = false;
		platform_delay(1000);
	}
	m2m_wifi_task();			// WINC1500 tasks
	TCPServer();				// Run the TCP sever state machine
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

#define	WDBP_BATTERY_INPUT			0	// ADC Channel for battery input
#define	WDBP_TARGET_VOLTAGE_INPUT	8	// ADC Chanmel for target voltage

#define	WDBP_ADC_BATTERY	0
#define	WDBP_ADC_TARGET		1

static uint32_t	inputVoltages[2] = { 0 };
static uint8_t	adcChannels[] = { WDBP_BATTERY_INPUT ,WDBP_BATTERY_INPUT , WDBP_BATTERY_INPUT ,WDBP_BATTERY_INPUT , WDBP_TARGET_VOLTAGE_INPUT };

////////////////////////////////////////////////////////////////////////////////////////////////////
/// <summary> Read all the ADC channels used by WDBP</summary>
///
/// <remarks>
/// 		  Because of the high impedance of the battery input circuit it is necessary
/// 		  to samople that input multiple times. The regular read method returns the
/// 		  last converted value, this is the one the battery monitor uses.
/// 			Sid Price, 11/4/2018.</remarks>
////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t whichChannel = WDBP_ADC_BATTERY;

void platform_adc_read (void)
{
	// PROBE_PIN;
	adc_set_regular_sequence (ADC1, 1, &(adcChannels[WDBP_ADC_BATTERY]));
	adc_start_conversion_regular (ADC1);
	/* Wait for end of conversion. */
	while (!adc_eoc (ADC1))
		;
	inputVoltages[WDBP_ADC_BATTERY] = adc_read_regular (ADC1);
	adc_set_regular_sequence (ADC1, 1, &(adcChannels[WDBP_ADC_BATTERY]));
	adc_start_conversion_regular (ADC1);
	/* Wait for end of conversion. */
	while (!adc_eoc (ADC1))
		;
	inputVoltages[WDBP_ADC_BATTERY] = adc_read_regular (ADC1);

	adc_set_regular_sequence (ADC1, 1, &(adcChannels[4]));
	adc_start_conversion_regular (ADC1);
	/* Wait for end of conversion. */
	while (!adc_eoc (ADC1))
		;
	inputVoltages[WDBP_ADC_TARGET] = adc_read_regular (ADC1);
	//if (whichChannel == WDBP_ADC_BATTERY)
	//{
	//	whichChannel = WDBP_ADC_TARGET;
	//}
	//else
	//{
	//	whichChannel = WDBP_ADC_BATTERY;
	//}
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
volatile uint32_t	batteryAverage = 0;
volatile uint32_t	batteryTemp = 0;
volatile uint32_t	sampleCount = 0;
bool	fLastState = true;
bool	fBatteryPresent = false;
#define	SAMPLES	2000

const char *platform_battery_voltage (void)
{
	static char ret[64] = { 0 };
	if (fBatteryPresent == true) {
		uint32_t	temp;
		char voltage[] = "0.00V";
		temp = batteryAverage;
		temp *= 100;
		voltage[0] = '0' + temp / 62525;
		temp = (temp / 625) % 100;
		voltage[2] = '0' + temp / 10;
		voltage[3] = '0' + temp % 10;
		//sprintf (&ret[0], "\n          Count: %d -> Battery : %s", batteryAverage, &voltage[0]);
		sprintf (&ret[0], "\n      Battery : %s", &voltage[0]);
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
	retVal = inputVoltages[WDBP_ADC_BATTERY];
	//
	// Running average
	// 
	if (batteryTemp == 0)	// First sample? 
	{
		batteryTemp = retVal;
	}
	else {
		batteryTemp += retVal;
	}
	if (++sampleCount == SAMPLES) {
		sampleCount = 0;
		batteryTemp /= SAMPLES;
		batteryAverage = batteryTemp;
		//
		// Is battery connected?
		// 
		if ((batteryAverage <= uiBattVoltage_1) || (batteryAverage >= uiBattVoltage_2)) {
			fBatteryPresent = false;
			fLastState = fResult = true;
		}
		else {
			fBatteryPresent = true;
			//
			// Is the voltage good?
			//  
			if (batteryAverage <= uiLowBattery)
			{
				fLastState = fResult = false;
			}
			else
			{
				fLastState = fResult = true;
			}
		}
	}
	else
	{
		fResult = fLastState;
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
static char voltages[32] = { 0 };

const char *platform_target_voltage( void )
{
	static char ret[] = "0.0V";
	static uint32_t	retVal;

	retVal =inputVoltages[WDBP_ADC_TARGET] * 99; /* 0-4095 */
	ret[0] = '0' + retVal / 62200;
	ret[2] = '0' + ( retVal / 6220 ) % 10;
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
	register uint32_t addr = 0x20018000;
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

int  platform_wifi_getpacket( char * pBuf, int bufSize )
{
	int iResult = 0;
	//iResult  = WiFi_GetPacket( pBuf, bufSize );
	return ( iResult ) ;
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
