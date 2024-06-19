# Firmware BMP for STM32F407 DISCO boards

Allows the use of the STM32F407 Discovery board main cpu as a Black Magic Probe. Historically it was used to program the on board built in debugger before ST-Link bootloader use was possible.

## Connections
* PD9: UART RX
* PD8: UART TX
* PC2: TDI
* PC4: TMS/SWDIO
* PC5: TCK/SWCLK
* PC6: TDO/TRACESWO
* PC1: TRST
* PC8: nRST

## How to Flash with DFU

After build:

1) `apt install dfu-util`
2) Force the F4 into system bootloader mode by jumpering "BOOT0" to "3V3" and "PB2/BOOT1" to
   "GND" and reset (RESET button). System bootloader should appear.
3) `dfu-util -a 0 --dfuse-address 0x08000000 -D blackmagic.bin`

To exit from DFU mode press a "key" and "reset", release reset. BMP firmware should appear

## 10 pin male from pins

| PB3/TDO  | PB7/RX      | PB6/TX     | X          | PA1/TDI |
| -------- | ----------- | ---------- | ---------- | ------- |
| PB4/nRST | +3V3/PB8 SW | PA13/SWDIO | PA14/SWCLK | GND     |

## SWD/JTAG frequency setting

https://github.com/blackmagic-debug/blackmagic/pull/783#issue-529197718

`mon freq 900k` helps at most
