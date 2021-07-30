# Firmware BMP for MiniF4 aka BlackPillV2 

stm32f401 stm32f411 Black Magic Probe firmware

https://github.com/WeActTC/MiniSTM32F4x1

## Connections:

* JTAG/SWD
   * PA1: TDI<br>
   * PA13: TMS/SWDIO<br>
   * PA14: TCK/SWCLK<br>
   * PB3: TDO/TRACESWO<br>
   * PB8: TRST<br>
   * PB4: SRST<br>

* USB USART
   * PB6: USART1 TX (usbuart_xxx)
   * PB7: USART1 RX (usbuart_xxx)

* +3V3 
   * PB8 - turn on IRLML5103 transistor



How to Build
========================================
```
cd blackmagic
make clean
make PROBE_HOST=f4mini
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

| PB3/TDO  | PB7/RX      | PB8/TX     | X          | PA1/TDI |
| -------- | ----------- | ---------- | ---------- | ------- |
| PB4/SRST | +3V3/PB8 SW | PA13/SWDIO | PA14/SWCLK | GND     |

SWJ frequency setting
====================================
https://github.com/blacksphere/blackmagic/pull/783#issue-529197718

`mon freq 900k` helps at most