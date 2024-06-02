Hydrabus
========

Connections
-----------

* PA0: User button to force system bootloader entry with reset (enter USB DFU)

* JTAG/SWD
  * PC0: TMS/SWDIO
  * PC1: TCK/SWCLK
  * PC2: TDI
  * PC3: TDO/TRACESWO
  * PC4: NRST (NRST / System Reset)
  * PC5: TRST (optional Test Reset)

* Green Led(ULED/PA4): Indicator that system bootloader is entered via BMP

* USB USART
  * PA9: USART1 TX (usbuart_xxx)
  * PA10: USART1 RX (usbuart_xxx)

How to Build
------------

```sh
meson setup build --cross-file=cross-file/hydrabus.ini
meson compile -C build
```

How to Flash the firmware with Windows
--------------------------------------

*THIS SECTION IS OUT OF DATE AND THE WORKFLOW IS KNOWN TO BE BROKEN*

After build:
  1. Download files from https://github.com/hydrabus/hydrafw/tree/master/utils/windows_dfu_util
  2. Run the command: `python3 ../scripts/dfu-convert.py -i blackmagic_hydrabus_firmware.bin blackmagic_hydrabus_firmware.dfu`
  3. Force the F4 into system bootloader mode by jumpering "BOOT0" to "3V3" and "PB2/BOOT1" to "GND" and reset (RESET button). System bootloader should appear.
  4. Run the command: `DfuSeCommand.exe -c --de 0 -d --fn blackmagic_hydrabus_firmware.dfu`
