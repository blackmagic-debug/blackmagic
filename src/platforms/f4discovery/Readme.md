# Firmware BMP for STM32F407 DISCO boards

Allows the use of the STM32F407 Discovery board main cpu as a Black Magic Probe. Historically it was used to program the on board built in debugger before ST-Link bootloader use was possible.

## Connections:

* PC2: TDI
* PC4: TMS/SWDIO
* PC5: TCK/SWCLK
* PC6: TDO/TRACESWO
* PC1: TRST
* PC8: nRST

How to Flash with dfu

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
