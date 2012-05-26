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

#include <libopencm3/stm32/f1/rcc.h>
#include <libopencm3/stm32/systick.h>
#include <libopencm3/stm32/f1/scb.h>
#include <libopencm3/stm32/nvic.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/cm3/scs.h>
#include <libopencm3/stm32/f1/adc.h>

#include "platform.h"
#include "jtag_scan.h"

#include <ctype.h>

uint8_t running_status;
volatile uint32_t timeout_counter;

jmp_buf fatal_error_jmpbuf;

void morse(const char *msg, char repeat);
static void morse_update(void);

#ifdef INCLUDE_UART_INTERFACE
static void uart_init(void);
#endif
static void adc_init(void);

/* Pins PB[7:5] are used to detect hardware revision.
 * 000 - Original production build.
 * 001 - Mini production build.
 */
int platform_hwversion(void)
{
	static int hwversion = -1;
	if (hwversion == -1) {
		gpio_set_mode(GPIOB, GPIO_MODE_INPUT, 
				GPIO_CNF_INPUT_PULL_UPDOWN,
				GPIO7 | GPIO6 | GPIO5);
		gpio_clear(GPIOB, GPIO7 | GPIO6 | GPIO5);
		hwversion = gpio_get(GPIOB, GPIO7 | GPIO6 | GPIO5) >> 5;
	}
	return hwversion;
}

int platform_init(void)
{
	rcc_clock_setup_in_hse_8mhz_out_72mhz();

	/* Enable peripherals */
	rcc_peripheral_enable_clock(&RCC_APB1ENR, RCC_APB1ENR_USBEN);
	rcc_peripheral_enable_clock(&RCC_APB1ENR, RCC_APB1ENR_TIM2EN);
	rcc_peripheral_enable_clock(&RCC_APB2ENR, RCC_APB2ENR_IOPAEN);
	rcc_peripheral_enable_clock(&RCC_APB2ENR, RCC_APB2ENR_IOPBEN);
	rcc_peripheral_enable_clock(&RCC_APB2ENR, RCC_APB2ENR_IOPDEN);

	/* Setup GPIO ports */
	gpio_clear(USB_PU_PORT, USB_PU_PIN);
	gpio_set_mode(USB_PU_PORT, GPIO_MODE_INPUT, GPIO_CNF_INPUT_FLOAT, 
			USB_PU_PIN);

	gpio_set_mode(JTAG_PORT, GPIO_MODE_OUTPUT_50_MHZ, 
			GPIO_CNF_OUTPUT_PUSHPULL,
			TMS_PIN | TCK_PIN | TDI_PIN);

	gpio_set_mode(LED_PORT, GPIO_MODE_OUTPUT_2_MHZ, 
			GPIO_CNF_OUTPUT_PUSHPULL, 
			LED_RUN | LED_IDLE | LED_ERROR);

	/* FIXME: This pin in intended to be input, but the TXS0108 fails
	 * to release the device from reset if this floats. */
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ, 
			GPIO_CNF_OUTPUT_PUSHPULL, GPIO7);

	/* Setup heartbeat timer */
	systick_set_clocksource(STK_CTRL_CLKSOURCE_AHB_DIV8); 
	systick_set_reload(900000);	/* Interrupt us at 10 Hz */
	SCB_SHPR(11) &= ~((15 << 4) & 0xff);
	SCB_SHPR(11) |= ((14 << 4) & 0xff);
	systick_interrupt_enable();
	systick_counter_enable();

#ifdef INCLUDE_UART_INTERFACE
	/* On mini hardware, UART and SWD share connector pins.
	 * Don't enable UART if we're being debugged. */
	if ((platform_hwversion() == 0) ||
	    !(SCS_DEMCR & SCS_DEMCR_TRCENA))
		uart_init();
#endif
	if (platform_hwversion() > 0) {
		adc_init();
	} else {
		gpio_clear(GPIOB, GPIO0);
		gpio_set_mode(GPIOB, GPIO_MODE_INPUT, 
				GPIO_CNF_INPUT_PULL_UPDOWN, GPIO0);
	}

	SCB_VTOR = 0x2000;	// Relocate interrupt vector table here

	cdcacm_init();

	jtag_scan();
	
	return 0;
}

void sys_tick_handler(void)
{
	if(running_status) 
		gpio_toggle(LED_PORT, LED_RUN);
	else
		gpio_clear(LED_PORT, LED_RUN);

	if(timeout_counter) 
		timeout_counter--;

	morse_update();
}


/* Morse code patterns and lengths */
static const struct {
	uint16_t code;
	uint8_t bits;
} morse_letter[] = {
	{        0b00011101,  8}, // 'A' .-
	{    0b000101010111, 12}, // 'B' -...
	{  0b00010111010111, 14}, // 'C' -.-.
	{      0b0001010111, 10}, // 'D' -..
	{            0b0001,  4}, // 'E' .
	{    0b000101110101, 12}, // 'F' ..-.
	{    0b000101110111, 12}, // 'G' --.
	{      0b0001010101, 10}, // 'H' ....
	{          0b000101,  6}, // 'I' ..
	{0b0001110111011101, 16}, // 'J' .---
	{    0b000111010111, 12}, // 'K' -.-
	{    0b000101011101, 12}, // 'L' .-..
	{      0b0001110111, 10}, // 'M' --
	{        0b00010111,  8}, // 'N' -.
	{  0b00011101110111, 14}, // 'O' ---
	{  0b00010111011101, 14}, // 'P' .--.
	{0b0001110101110111, 16}, // 'Q' --.-
	{      0b0001011101, 10}, // 'R' .-.
	{        0b00010101,  8}, // 'S' ...
	{          0b000111,  6}, // 'T' -
	{      0b0001110101, 10}, // 'U' ..-
	{    0b000111010101, 12}, // 'V' ...-
	{    0b000111011101, 12}, // 'W' .--
	{  0b00011101010111, 14}, // 'X' -..-
	{0b0001110111010111, 16}, // 'Y' -.--
	{  0b00010101110111, 14}, // 'Z' --..
};


const char *morse_msg;
static const char * volatile morse_ptr;
static char morse_repeat;

void morse(const char *msg, char repeat)
{
	morse_msg = morse_ptr = msg;
	morse_repeat = repeat;
	SET_ERROR_STATE(0);
}

static void morse_update(void)
{
	static uint16_t code;
	static uint8_t bits;

	if(!morse_ptr) return;

	if(!bits) {
		char c = *morse_ptr++;
		if(!c) {
			if(morse_repeat) {
				morse_ptr = morse_msg;
				c = *morse_ptr++;
			} else {
				morse_ptr = 0;
				return;
			}
		}
		if((c >= 'A') && (c <= 'Z')) {
			c -= 'A';
			code = morse_letter[c].code;
			bits = morse_letter[c].bits;
		} else {
			code = 0; bits = 4;
		}
	}
	SET_ERROR_STATE(code & 1);
	code >>= 1; bits--;
}

#ifdef INCLUDE_UART_INTERFACE
static void uart_init(void)
{
	rcc_peripheral_enable_clock(&RCC_APB2ENR, RCC_APB2ENR_USART1EN);

	/* UART1 TX to 'alternate function output push-pull' */
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ,
		      GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO9);

	/* Setup UART parameters. */
	usart_set_baudrate(USART1, 38400);
	usart_set_databits(USART1, 8);
	usart_set_stopbits(USART1, USART_STOPBITS_1);
	usart_set_mode(USART1, USART_MODE_TX_RX);
	usart_set_parity(USART1, USART_PARITY_NONE);
	usart_set_flow_control(USART1, USART_FLOWCONTROL_NONE);

	/* Finally enable the USART. */
	usart_enable(USART1);

	/* Enable interrupts */
	USART1_CR1 |= USART_CR1_RXNEIE;
	nvic_set_priority(NVIC_USART1_IRQ, IRQ_PRI_USART1);
	nvic_enable_irq(NVIC_USART1_IRQ);
}

static uint8_t uart_usb_buf[CDCACM_PACKET_SIZE];
static uint8_t uart_usb_buf_size;

void uart_usb_buf_drain(uint8_t ep)
{
	if (!uart_usb_buf_size) 
		return;

	usbd_ep_write_packet(ep, uart_usb_buf, uart_usb_buf_size);
	uart_usb_buf_size = 0;
}

void usart1_isr(void)
{
	char c = usart_recv(USART1);

	/* Try to send now */
	if (usbd_ep_write_packet(0x83, &c, 1) == 1) 
		return;
		
	/* We failed, so queue for later */
	if (uart_usb_buf_size == CDCACM_PACKET_SIZE) {
		/* Drop if the buffer's full: we have no flow control */
		return;
	}

	uart_usb_buf[uart_usb_buf_size++] = c;
}
#endif

static void adc_init(void)
{
	rcc_peripheral_enable_clock(&RCC_APB2ENR, RCC_APB2ENR_ADC1EN);

	gpio_set_mode(GPIOB, GPIO_MODE_INPUT, 
			GPIO_CNF_INPUT_ANALOG, GPIO0);

	adc_off(ADC1);
	adc_disable_scan_mode(ADC1);
	adc_set_single_conversion_mode(ADC1);
	adc_enable_discontinous_mode_regular(ADC1);
	adc_disable_external_trigger_regular(ADC1);
	adc_set_right_aligned(ADC1);
	adc_set_conversion_time_on_all_channels(ADC1, ADC_SMPR_SMP_28DOT5CYC);

	adc_on(ADC1);

	/* Wait for ADC starting up. */
	for (int i = 0; i < 800000; i++)    /* Wait a bit. */
		__asm__("nop");

	adc_reset_calibration(ADC1);
	adc_calibration(ADC1);
}

const char *platform_target_voltage(void)
{
	if (platform_hwversion() == 0)
		return gpio_get(GPIOB, GPIO0) ? "OK" : "ABSENT!";

	static char ret[] = "0.0V";
	const u8 channel = 8;
	adc_set_regular_sequence(ADC1, 1, (u8*)&channel);

	adc_on(ADC1);

	/* Wait for end of conversion. */
	while (!(ADC_SR(ADC1) & ADC_SR_EOC));

	u32 val = ADC_DR(ADC1) * 99; /* 0-4095 */
	ret[0] = '0' + val / 81910;
	ret[2] = '0' + (val / 8191) % 10;

	return ret;
}
