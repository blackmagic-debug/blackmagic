# BMP firmware for STM32F303

## System vs BMP Bootloader

For simpicity, the ST system bootloader is used. This saves additional 4 kB for the BMP firmware.

## Connections

* PA2: UART RX
* PS3: UART TX
* PA0: TDI
* PA1: TMS/SWDIO
* PA7: TCK/SWCLK
* PA6: TDO/TRACESWO
* PA5: TRST
* PB5: LED green
* PB6: LED yellow
* PB7: LED red
* PB0: VTARGET
* PB1: VUSB

## Loading/updating BMP firmware

Get into ST bootloader mode with reset or repower and BOOT pulled high. If BMP firmware is already loaded and running, dfu-util can also invoke the bootloader.

```
dfu-util -d 1d50:6018 -e
```

List the available devices

```
dfu-util -l
```

dfu-util should now list "[0483:df11]" "@Internal Flash  /0x08000000/064*0002Kg"
. Compile the firmware with

```
 make PROBE_HOST=f3 clean && make PROBE_HOST=f3
 ```

Load firmware

```
 dfu-util -d 0483:df11 -a 0 -s 0x08000000:leave -D blackmagic.bin

```

Multiple BMP devices or STM devices on the USB bus may require additional dfu-util arguments for device selection.
