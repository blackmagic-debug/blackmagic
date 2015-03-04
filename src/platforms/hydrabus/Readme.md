Connections
====================
* PA0: User button to force system bootloader entry with reset (enter USB DFU)

* JTAG/SWD
 * PC0: TMS/SWDIO
 * PC1: TCK/SWCLK
 * PC2: TDI
 * PC3: TDO/TRACESWO
 * PC4: SRST (NRST/System Reset)
 * PC5: TRST (optional Test Reset)

* Green Led(ULED/PA4): Indicator that system bootloader is entered via BMP

* USB USART
 * PA9: USART1 TX (usbuart_xxx)
 * PA10: USART1 RX (usbuart_xxx)

How to Build
============
```
cd blackmagic/src
make PROBE_HOST=hydrabus
```

How to Flash the firmware with Windows
========================================
* After build:
 * 1) Force the F4 into system bootloader mode by jumpering "BOOT0" to "3V3" and "PB2/BOOT1" to "GND" and reset (RESET button). System bootloader should appear.
 * 2) Double click on "update_fw_usb_dfu_blackmagic.bat"
