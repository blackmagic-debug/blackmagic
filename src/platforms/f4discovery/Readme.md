# Firmware BMP for STM32F407 DISCO boards

Kept for historical reasons to load BMP bootloader to the STM32F103 of the onboard STLINK or external STLINKs. As stlink-tool now allows to load BMP firmware via the original STLINK bootloader is no longer really needed.

## Connections:

PC2: TDI<br>
PC4: TMS/SWDIO<br>
PC5: TCK/SWCLK<br>
PC6: TDO/TRACESWO<br>

PC1: TRST<br>
PC8: SRST<br>

# Alternate build for stm32f401 stm32f411 MiniF4 aka BlackPillV2 boards.

https://github.com/WeActTC/MiniSTM32F4x1

## Connections:

* JTAG/SWD
   * PA1: TDI
   * PA13: TMS/SWDIO
   * PA14: TCK/SWCLK
   * PB3: TDO/TRACESWO
   * PB5: TRST
   * PB4: SRST

* USB USART
   * PB6: USART1 TX (usbuart_xxx)
   * PB7: USART1 RX (usbuart_xxx)

* +3V3.
   * PB8 - turn on IRLML5103 transistor

How to Build
========================================
```
cd blackmagic
make clean
make PROBE_HOST=f4discovery BLACKPILL=1
```

How to Flash with dfu
========================================
* After build:
 * 1) `apt install dfu-util`
 * 2) Force the F4 into system bootloader mode by jumpering "BOOT0" to "3V3" and "PB2/BOOT1" to "GND" and reset (RESET button). System bootloader should appear.
 * 3) `dfu-util -a 0 --dfuse-address 0x08000000 -D blackmagic.bin`

To exit from dfu mode press a "key" and "reset", release reset. BMP firmware should appear


10 pin male from pins
========================================

| PB3/TDO  | PB7/RX      | PB6/TX     | X          | PA1/TDI |
| -------- | ----------- | ---------- | ---------- | ------- |
| PB4/SRST | +3V3/PB8 SW | PA13/SWDIO | PA14/SWCLK | GND     |

SWJ frequency setting
====================================
https://github.com/blacksphere/blackmagic/pull/783#issue-529197718

`mon freq 900k` helps at most
