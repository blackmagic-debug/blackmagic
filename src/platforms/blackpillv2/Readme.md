# Firmware BMP for STM32F401/stm32f411 MiniF4 aka BlackPillV2 boards

Allows the use of BlackPillV2 as a Black Magic Probe

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
make PROBE_HOST=blackpill
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
https://github.com/blackmagic-debug/blackmagic/pull/783#issue-529197718

`mon freq 900k` helps at most
