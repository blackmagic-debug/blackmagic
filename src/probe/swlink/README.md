# Blackmagic for STM8S Discovery and STM32F103 Minimum System Development Board

## External connections

|  Function   | PIN   | STM8S-DISCO | BLUEPILL    |
| ----------- | ----- | ----------- | ----------- |
|  JTMS/SWDIO |  PA13 |   CN5/5     |  P2/2       |
|  JTCK/SWCLK |  PA14 |   CN5/4     |  P2/3       |
|  JTDI       |  PA15 |    CN5/6    |  P4/11 (38) |
|  JTDO       |  PB3  |   CN5/3     |  P4/10 (39) |
|  nRST       |  PB4  |   CN5/8     |  P4/9  (40) |
|  UART1_TX   |  PB6  |   CN7/4     |  P4/7  (42) |
|  UART1_RX   |  PB7  |   CN7/2     |  P4/6  (43) |
|  SWO/RX2    |  PA3  |   NA(*1)    |  P3/8  (13) |

*1: Wire JTDO/PB3  (U2/39) to USART2_RX/PA3 (U2/13) to expose SWO for ST-Link
on STM8S-Disco on CN5/3

### Force Bootloader Entry

    STM8S Discovery: Jumper CN7/4 to CN7/3 to read PB6 low.
    Bluepill: Jumper Boot1 to '1' to read PB2 high.

### References

[STM8S UM0817 User manual
    ](https://www.st.com/resource/en/user_manual/cd00250600.pdf)

[Blue Pill Schematics 1
    ](https://jeelabs.org/img/2016/STM32F103C8T6-DEV-BOARD-SCH.pdf) :
    Use first number!

[Blue Pill Schematics 2
    ](https://stm32duinoforum.com/forum/images/a/ae/wiki_subdomain/Bluepillpinout.gif) :
    Use second number!

Distinguish boards by checking the SWIM_IN connection PB9/PB10 seen on
STM8S Discovery.

## STM8S Discovery

The board is a ST-Link V1 Board, but with access to JTAG pins accessible
on CN5. This allows easy reprogramming and reuse of the JTAG header.
Programmatical it seems indistinguishable from a e.g. STM32VL
Discovery. So here a variant that uses CN5 for JTAG/SWD and CN7 for
UART.

Force Bootloader entry is done with shorting CN7 Pin3/4 so PB6 read low while
pulled up momentary by PB6. As PB6 is USBUART TX, this pin is idle
high. Setting the jumper while BMP is running means shorting the GPIO with
output high to ground. Do not do that for extended periods. Un- and repower
soon after setting the jump. Best is to short only when unplugged.

Reuse SWIM Pins for Uart (USART1)
   RX: CN7 Pin2 ->SWIM_IN (PB7)/USART1_RX / SWIM_IN(PB9)
   TX: CN7 Pin4 -> SWIM_RST_IN(PB6)/USART1_TX

## STM32F103 Minimum System Development Board (aka Blue Pill)

This board has the SWD pins of the onboard F103 accessible on one side.
Reuse these pins. There are also jumpers for BOOT0 and BOOT1(PB2). Reuse
Boot1 as "Force Bootloader entry" jumpered high when booting. Boot1
has 100 k Ohm between MCU and header pin and can not be used as output.

All other port pins are have header access with headers not yet soldered.

This platform can be used for any STM32F103x[8|B] board when JTAG/SWD are
accessible, with the LED depending on actual board layout routed to some
wrong pin and force boot not working.

## Other STM32F103x[8|B] boards

If the needed JTAG connections are accessible, you can use this swlink variant.
Depending on board layout, LED and force bootloader entry may be routed to
wrong pins.
