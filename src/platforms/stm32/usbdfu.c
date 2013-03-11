/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2013 Gareth McMullin <gareth@blacksphere.co.nz>
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

#include <string.h>
#include <libopencm3/cm3/systick.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#if defined(STM32F1)
#include <libopencm3/stm32/f1/flash.h>
#elif defined(STM32F2)
#include <libopencm3/stm32/f2/flash.h>
#elif defined(STM32F4)
#include <libopencm3/stm32/f4/flash.h>
#else
#warning "Unhandled STM32 family"
#endif
#include <libopencm3/cm3/scb.h>

#define FLASH_OBP_RDP 0x1FFFF800
#define FLASH_OBP_WRP10 0x1FFFF808

#define FLASH_OBP_RDP_KEY 0x5aa5

#if defined (STM32_CAN)
#define FLASHBLOCKSIZE 2048
#else
#define FLASHBLOCKSIZE 1024
#endif

#if defined(DISCOVERY_STLINK)
uint8_t rev;
uint16_t led_idle_run;
u32 led2_state = 0;
int stlink_test_nrst(void) {
/* Test if JRST/NRST is pulled down*/
    int i;
    uint16_t nrst;
    uint16_t pin;

    /* First, get Board revision by pulling PC13/14 up. Read
     *  11 for ST-Link V1, e.g. on VL Discovery, tag as rev 0
     *  10 for ST-Link V2, e.g. on F4 Discovery, tag as rev 1
     */
    rcc_peripheral_enable_clock(&RCC_APB2ENR, RCC_APB2ENR_IOPCEN);
    gpio_set_mode(GPIOC, GPIO_MODE_INPUT,
                  GPIO_CNF_INPUT_PULL_UPDOWN, GPIO14|GPIO13);
    gpio_set(GPIOC, GPIO14|GPIO13);
    for (i=0; i< 100; i++)
        rev = (~(gpio_get(GPIOC, GPIO14|GPIO13))>>13) & 3;

    switch (rev)
    {
    case 0:
        pin = GPIO1;
        led_idle_run= GPIO8;
        break;
    default:
        pin = GPIO0;
        led_idle_run = GPIO9;
    }
    gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ,
                  GPIO_CNF_OUTPUT_PUSHPULL, led_idle_run);
    rcc_peripheral_enable_clock(&RCC_APB2ENR, RCC_APB2ENR_IOPBEN);
    gpio_set_mode(GPIOB, GPIO_MODE_INPUT,
                  GPIO_CNF_INPUT_PULL_UPDOWN, pin);
    gpio_set(GPIOB, pin);
    for (i=0; i< 100; i++)
        nrst = gpio_get(GPIOB, pin);
    return (nrst)?1:0;
}
#endif

static u32 max_address;
#if defined (STM32F4)
#define APP_ADDRESS	0x08010000
static u32 sector_addr[] = {0x8000000, 0x8004000, 0x8008000, 0x800c000,
                            0x8010000, 0x8020000, 0x8040000, 0x8060000,
                            0x8080000, 0x80a0000, 0x80c0000, 0x80e0000,
                            0x8100000, 0};
u16 sector_erase_time[12]= {500, 500, 500, 500,
                            1100,
                            2600, 2600, 2600, 2600, 2600, 2600, 2600};
u8 sector_num = 0xff;
/* Find the sector number for a given address*/
void get_sector_num(u32 addr)
{
	int i = 0;
	while(sector_addr[i+1]) {
		if (addr < sector_addr[i+1])
			break;
		i++;
		}
	if (!sector_addr[i])
		return;
	sector_num = i;
}

void check_and_do_sector_erase(u32 addr)
{
	if(addr == sector_addr[sector_num]) {
		flash_erase_sector((sector_num & 0x1f)<<3, FLASH_PROGRAM_X32);
	}
}
#else
#define APP_ADDRESS	0x08002000
static uint32_t last_erased_page=0xffffffff;
void check_and_do_sector_erase(u32 sector)
{
	sector &= (~(FLASHBLOCKSIZE-1));
	if (sector != last_erased_page) {
		flash_erase_page(sector);
		last_erased_page = sector;
	}
}
#endif

#if defined(BLACKMAGIC)
#	define PRODUCT_STRING	\
		"Black Magic Probe (Upgrade), (Firmware 1.5" VERSION_SUFFIX ", build " BUILDDATE ")"
#elif defined(DISCOVERY_STLINK)
#	define PRODUCT_STRING	\
		"Black Magic (Upgrade) for STLink/Discovery, (Firmware 1.5" VERSION_SUFFIX ", build " BUILDDATE ")"
#elif defined(STM32_CAN)
#	define PRODUCT_STRING	\
		"Black Magic (Upgrade) for STM32_CAN, (Firmware 1.5" VERSION_SUFFIX ", build " BUILDDATE ")"
#elif defined(F4DISCOVERY)
#	define PRODUCT_STRING	\
		"Black Magic (Upgrade) for F4Discovery, (Firmware 1.5" VERSION_SUFFIX ", build " BUILDDATE ")"
#elif defined(USPS_F407)
#	define PRODUCT_STRING	\
		"Black Magic (Upgrade) for USPS_F407, (Firmware 1.5" VERSION_SUFFIX ", build " BUILDDATE ")"
#else
#	warning "Unhandled board"
#endif

	/* This string is used by ST Microelectronics' DfuSe utility */
#if defined(BLACKMAGIC)
#	define IFACE_STRING	"@Internal Flash   /0x08000000/8*001Ka,120*001Kg"
#elif defined(DISCOVERY_STLINK)
#	define IFACE_STRING	"@Internal Flash   /0x08000000/8*001Ka,56*001Kg"
#elif defined(STM32_CAN)
#	define IFACE_STRING	"@Internal Flash   /0x08000000/4*002Ka,124*002Kg"
#elif defined(F4DISCOVERY) || defined(USPS_F407)
#	define IFACE_STRING	"@Internal Flash   /0x08000000/1*016Ka,3*016Kg,1*064Kg,7*128Kg"
#else
#	warning "Unhandled board"
#endif

static u32 poll_timeout(uint8_t cmd, uint32_t addr, uint16_t blocknum)
{
#if defined(STM32F4)
	/* Erase for big pages on STM2/4 needs "long" time
	   Try not to hit USB timeouts*/
	if ((blocknum == 0) && (cmd == CMD_ERASE)) {
		get_sector_num(addr);
		if(addr == sector_addr[sector_num])
			return sector_erase_time[sector_num];
	}

	/* Programming 256 word with 100 us(max) per word*/
	return 26;
#else
	(void)cmd;
	(void)addr;
	(void)blocknum;
	return 100;
#endif
}

static void flash_program_buffer(uint32_t baseaddr, void *buf, int len)
{
	int i;
#if defined (STM32F4)
	for(i = 0; i < len; i += 4)
		flash_program_word(baseaddr + i,
			*(u32*)(buf+i),
			FLASH_PROGRAM_X32);
#else
	for(i = 0; i < len; i += 2)
		flash_program_half_word(baseaddr + i,
				*(u16*)(buf+i));
#endif
}

static void detach(void)
{
#if defined (DISCOVERY_STLINK)
	/* Disconnect USB cable by resetting USB Device
	   and pulling USB_DP low*/
	rcc_peripheral_reset(&RCC_APB1RSTR, RCC_APB1ENR_USBEN);
	rcc_peripheral_clear_reset(&RCC_APB1RSTR, RCC_APB1ENR_USBEN);
	rcc_peripheral_enable_clock(&RCC_APB1ENR, RCC_APB1ENR_USBEN);
	rcc_peripheral_enable_clock(&RCC_APB2ENR, RCC_APB2ENR_IOPAEN);
	gpio_clear(GPIOA, GPIO12);
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ,
		GPIO_CNF_OUTPUT_OPENDRAIN, GPIO12);
#else
        /* USB device must detach, we just reset... */
#endif
	scb_reset_system();
}

#include "dfucore.c"

int main(void)
{
    /* Check the force bootloader pin*/
#if defined (DISCOVERY_STLINK)
	rcc_peripheral_enable_clock(&RCC_APB2ENR, RCC_APB2ENR_IOPAEN);
/* Check value of GPIOA1 configuration. This pin is unconnected on
 * STLink V1 and V2. If we have a value other than the reset value (0x4),
 * we have a warm start and request Bootloader entry
 */
	if(((GPIOA_CRL & 0x40) == 0x40) && stlink_test_nrst()) {
#elif defined (STM32_CAN)
	rcc_peripheral_enable_clock(&RCC_APB2ENR, RCC_APB2ENR_IOPAEN);
	if(!gpio_get(GPIOA, GPIO0)) {
#elif defined (F4DISCOVERY)
	rcc_peripheral_enable_clock(&RCC_AHB1ENR, RCC_AHB1ENR_IOPAEN);
	if(!gpio_get(GPIOA, GPIO0)) {
#elif defined (USPS_F407)
	rcc_peripheral_enable_clock(&RCC_AHB1ENR, RCC_AHB1ENR_IOPBEN);
        /* Pull up an look if external pulled low or if we restart with PB1 low*/
        GPIOB_PUPDR |= 4;
        {
            int i;
            for(i=0; i<100000; i++) __asm__("NOP");
        }
	if(gpio_get(GPIOB, GPIO1)) {
#else
	rcc_peripheral_enable_clock(&RCC_APB2ENR, RCC_APB2ENR_IOPBEN);
	if(gpio_get(GPIOB, GPIO12)) {
#endif
		/* Boot the application if it's valid */
#if defined (STM32F4)
		/* Vector table may be anywhere in 128 kByte RAM
                   CCM not handled*/
		if((*(volatile u32*)APP_ADDRESS & 0x2FFC0000) == 0x20000000) {
#else
		if((*(volatile u32*)APP_ADDRESS & 0x2FFE0000) == 0x20000000) {
#endif
			/* Set vector table base address */
			SCB_VTOR = APP_ADDRESS & 0x1FFFFF; /* Max 2 MByte Flash*/
			/* Initialise master stack pointer */
			asm volatile ("msr msp, %0"::"g"
					(*(volatile u32*)APP_ADDRESS));
			/* Jump to application */
			(*(void(**)())(APP_ADDRESS + 4))();
		}
	}

#if defined (STM32F4)
	if ((FLASH_OPTCR & 0x10000) != 0) {
		flash_program_option_bytes(FLASH_OPTCR & ~0x10000);
		flash_lock_option_bytes();
	}
#else
	if ((FLASH_WRPR & 0x03) != 0x00) {
		flash_unlock();
		FLASH_CR = 0;
		flash_erase_option_bytes();
		flash_program_option_bytes(FLASH_OBP_RDP, FLASH_OBP_RDP_KEY);
		/* CL Device: Protect 2 bits with (2 * 2k pages each)*/
		/* MD Device: Protect 2 bits with (4 * 1k pages each)*/
		flash_program_option_bytes(FLASH_OBP_WRP10, 0x03FC);
	}
#endif

        /* Set up clock*/
#if defined (F4DISCOVERY) || defined(USPS_F407)
        rcc_clock_setup_hse_3v3(&hse_8mhz_3v3[CLOCK_3V3_168MHZ]);
	systick_set_clocksource(STK_CTRL_CLKSOURCE_AHB_DIV8);
	systick_set_reload(2100000);
#else
	rcc_clock_setup_in_hse_8mhz_out_72mhz();
	systick_set_clocksource(STK_CTRL_CLKSOURCE_AHB_DIV8);
	systick_set_reload(900000);
#endif

        /* Handle USB disconnect/connect */
#if defined(DISCOVERY_STLINK)
	/* Just in case: Disconnect USB cable by resetting USB Device
         * and pulling USB_DP low
         * Device will reconnect automatically as Pull-Up is hard wired*/
	rcc_peripheral_reset(&RCC_APB1RSTR, RCC_APB1ENR_USBEN);
	rcc_peripheral_clear_reset(&RCC_APB1RSTR, RCC_APB1ENR_USBEN);
	rcc_peripheral_enable_clock(&RCC_APB1ENR, RCC_APB1ENR_USBEN);
	rcc_peripheral_enable_clock(&RCC_APB2ENR, RCC_APB2ENR_IOPAEN);
	gpio_clear(GPIOA, GPIO12);
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ,
		GPIO_CNF_OUTPUT_OPENDRAIN, GPIO12);
#elif defined(F4DISCOVERY)
#elif defined(USPS_F407)
#elif defined(STM32_CAN)
#else
	rcc_peripheral_enable_clock(&RCC_APB2ENR, RCC_APB2ENR_IOPAEN);
        rcc_peripheral_enable_clock(&RCC_APB1ENR, RCC_APB1ENR_USBEN);
	gpio_set_mode(GPIOA, GPIO_MODE_INPUT, 0, GPIO8);

#endif
	systick_interrupt_enable();
	systick_counter_enable();
	get_dev_unique_id(serial_no);

/* Handle LEDs */
#if defined(F4DISCOVERY)
	rcc_peripheral_enable_clock(&RCC_AHB1ENR, RCC_AHB1ENR_IOPDEN);
	gpio_clear(GPIOD, GPIO12 | GPIO13 | GPIO14 |GPIO15);
	gpio_mode_setup(GPIOD, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE,
			GPIO12 | GPIO13 | GPIO14 |GPIO15);
#elif defined(USPS_F407)
	rcc_peripheral_enable_clock(&RCC_AHB1ENR, RCC_AHB1ENR_IOPBEN);
	gpio_clear(GPIOB, GPIO2);
	gpio_mode_setup(GPIOB, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE,
			GPIO2);
#elif defined (STM32_CAN)
	gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_2_MHZ,
			GPIO_CNF_OUTPUT_PUSHPULL, GPIO0);
#else
	gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_2_MHZ,
			GPIO_CNF_OUTPUT_PUSHPULL, GPIO11);
	gpio_set_mode(GPIOB, GPIO_MODE_INPUT,
			GPIO_CNF_INPUT_FLOAT, GPIO2 | GPIO10);

#endif

/* Set up USB*/
#if defined(STM32_CAN)
	rcc_peripheral_enable_clock(&RCC_APB2ENR, RCC_APB2ENR_IOPAEN);
	rcc_peripheral_enable_clock(&RCC_AHBENR, RCC_AHBENR_OTGFSEN);
	rcc_peripheral_enable_clock(&RCC_APB2ENR, RCC_APB2ENR_IOPBEN);
	usbdev = usbd_init(&stm32f107_usb_driver,
				&dev, &config, usb_strings, 4);
#elif defined(STM32F2)||defined(STM32F4)
	rcc_peripheral_enable_clock(&RCC_AHB1ENR, RCC_AHB1ENR_IOPAEN);
	rcc_peripheral_enable_clock(&RCC_AHB2ENR, RCC_AHB2ENR_OTGFSEN);

	/* Set up USB Pins and alternate function*/
	gpio_mode_setup(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE,
		GPIO9 | GPIO10 | GPIO11 | GPIO12);
	gpio_set_af(GPIOA, GPIO_AF10, GPIO9 | GPIO10| GPIO11 | GPIO12);
	usbdev = usbd_init(&stm32f107_usb_driver,
				&dev, &config, usb_strings, 4);
#else
	usbdev = usbd_init(&stm32f103_usb_driver,
				&dev, &config, usb_strings, 4);
#endif
	usbd_set_control_buffer_size(usbdev, sizeof(usbd_control_buffer));
	usbd_register_control_callback(usbdev,
				USB_REQ_TYPE_CLASS | USB_REQ_TYPE_INTERFACE,
				USB_REQ_TYPE_TYPE | USB_REQ_TYPE_RECIPIENT,
				usbdfu_control_request);

#if defined(BLACKMAGIC)
	gpio_set(GPIOA, GPIO8);
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ,
			GPIO_CNF_OUTPUT_PUSHPULL, GPIO8);
#endif

	while (1)
		usbd_poll(usbdev);
}

static char *get_dev_unique_id(char *s)
{
#if defined(STM32F4) || defined(STM32F2)
#define UNIQUE_SERIAL_R 0x1FFF7A10
#define FLASH_SIZE_R    0x1fff7A22
#elif defined(STM32F3)
#define UNIQUE_SERIAL_R 0x1FFFF7AC
#define FLASH_SIZE_R    0x1fff77cc
#elif defined(STM32L1)
#define UNIQUE_SERIAL_R 0x1ff80050
#define FLASH_SIZE_R    0x1FF8004C
#else
#define UNIQUE_SERIAL_R 0x1FFFF7E8;
#define FLASH_SIZE_R    0x1ffff7e0
#endif
        volatile uint32_t *unique_id_p = (volatile uint32_t *)UNIQUE_SERIAL_R;
	uint32_t unique_id = *unique_id_p +
			*(unique_id_p + 1) +
			*(unique_id_p + 2);
        int i;

        /* Calculated the upper flash limit from the exported data
           in theparameter block*/
        max_address = (*(u32 *) FLASH_SIZE_R) <<10;
        /* Fetch serial number from chip's unique ID */
        for(i = 0; i < 8; i++) {
                s[7-i] = ((unique_id >> (4*i)) & 0xF) + '0';
        }
        for(i = 0; i < 8; i++)
                if(s[i] > '9')
                        s[i] += 'A' - '9' - 1;
	s[8] = 0;

	return s;
}

void sys_tick_handler()
{
#if defined(DISCOVERY_STLINK)
	if (rev == 0)
		gpio_toggle(GPIOA, led_idle_run);
	else
	{
		if (led2_state & 1)
			gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ,
				GPIO_CNF_OUTPUT_PUSHPULL, led_idle_run);
		else
			gpio_set_mode(GPIOA, GPIO_MODE_INPUT,
				GPIO_CNF_INPUT_ANALOG, led_idle_run);
		led2_state++;
	}
#elif defined (F4DISCOVERY)
	gpio_toggle(GPIOD, GPIO12);  /* Green LED on/off */
#elif defined (USPS_F407)
	gpio_toggle(GPIOB, GPIO2);  /* Green LED on/off */
#elif defined(STM32_CAN)
	gpio_toggle(GPIOB, GPIO0);  /* LED2 on/off */
#else
	gpio_toggle(GPIOB, GPIO11); /* LED2 on/off */
#endif
}

