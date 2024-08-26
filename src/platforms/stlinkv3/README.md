# Blackmagic firmware for STLINK-V3 Adapters

Besides using STLINK-V3 as a debugger for the hosted build of the blackmagic,
it is also possible to run the BMP firmware directly on the STLINK-V3
hardware.

## Attention: Irreversible read protection activation starting with firmware V36 and up!
If updated with ST firmware version V36 or higher, the STLINK-V3 enters
read-out protection (RDP) level 2, which irreversibly makes SWD access
to the chip impossible. The BMP firmware may still be flashed to the device,
even though in a somewhat not-straightforward manner. Some unprotected bootloader
on the net is available that is said to not set RDP2 with V36 and above.

## STLINK-V3 features
The STLINK-V3 is built upon the STM32F723 device, which features a high-speed
USB connection, 384 kBytes of flash space above the ST bootloader,
256 kBytes of RAM and lots of connections. STLINK-V3SET comes with
a case and offering multiple connectors for the other functions. STLINK-V3MINI has
a 14-pin 1.27mm JTAG/SWD/UART connector, compatible with the 10 pin BMP-Native
connector when used with a 14-pin connector. The rest of the connection are
castellated holes. The STLINK-V3MINI is now obsolete,
but has been replaced with the STLINK-V3MINIE which is similar to the STLINK-V3MINI
but replaces the micro USB port with a USB type-C port and does not have any
castellated holes. STLINK-V3MODS remove the 14-pin connector and only has castellated
holes, meant to be soldered on board.

The ST firmware checks the Romtable and only allows access to STM32 devices. In
some situations, Romtable access may also fail on STM32 device and so a debugger
warm plug will fail. Cold plug should work with any STM32 device.

## Performance
(UM2448) Stock firmware has three settings: High-performance (192 MHz), Standard frequency (96 MHz), Low-consumption (48 MHz).
SWD up to 24000 kHz (then 8000 kHz, 3333, ...); JTAG up to 21333 kHz (both likely via SPI5, SPI3), has a return clock pin, does not support daisy chaining of targets.

SWO up to 16 Mbaud, VCP 732-16000000 baud.
See 14.2 Baud rate computing: 192000000/N where 16<=N<=65535 (OVER8: 2*192000000/N where 24<=N<=31). Substitute 96 MHz or 48 MHz correspondingly.

(RN0093) "On STLINK-V3 boards in high-performance mode, the minimum baud rate for Virtual COM port is 2931 bauds."
2931*65535~=192 MHz, or rather 192000000/65535~=2929.7, which also confirms the first system frequency.

(DS11853 F723) Suffix 7 devices are rated for -40..+125 deg C but at 200 MHz frequency maximum.
Other mentioned limits from datasheet are 144M, 168M, 180M for scales 3-2-1 and overdrive on/off.

This firmware, in contrast, always runs at 216 MHz (suffix 6). AHB clock = 216 MHz, APB1 = 54 MHz, APB2 = 108 MHz.
SWD can be around 12-18 MHz (no SPI) when size-optimized, JTAG is slower (no SPI). CRC32 offloading enabled.

USART1, USART6 on APB2 (108 MHz Pclk) are 6.75 Mbaud, others (including UART5) on APB1, 3375000 baud.
SWO capture is implemented as UART5 Rx DMA in particular.

Recent versions of firmware switch `uart_ker_ck` to sysclk, bumping them to 13.5Mbaud, and enable OVER8 as needed, allowing 216000000/8 = 27000000 baud in theory.

Only USB High-Speed capable ports are supported with current driver stack for Internal USBOTG_HS PHY and firmware descriptors. USB Full-Speed only ports, hubs, isolators are not supported.

## Building.

As simple as
```sh
meson setup build --cross-file=cross-file/stlinkv3.ini
meson compile -C build
```

If you are building for bootloader you need to add option `-Dbmd_bootloader=true`
```sh
meson setup build --cross-file=cross-file/stlinkv3.ini -Dbmd_bootloader=true
meson compile -C build
```


## Flashing
Easiest is using the BMP bootloader. Load the BMP firmware easiest with
scripts/stm32mem.py  or dfu-utils. BMP bootloader must be flashed with SWD
access. Use CN2 on the [STLINK-V3SET](https://www.st.com/resource/en/data_brief/stlink-v3set.pdf).
For [STLINK-V3MINI](https://www.st.com/resource/en/data_brief/stlink-v3mini.pdf)
and [STLINK-V3MODS](https://www.st.com/resource/en/data_brief/stlink-v3mods.pdf)
use soldered connections to CN3. For [STLINK-V3MINI](https://www.st.com/resource/en/data_brief/stlink-v3minie.pdf) use soldered connections to CN5.

It is a good idea to keep a full image of the original flash content as backup!

If you want to keep the original bootloader or access via SWD is disabled, clone
https://github.com/blackmagic-debug/stlink-tool
make and use like
`stlink-tool blackmagic_stlinkv3_firmware.bin`
Revert to original ST firmware with
`java -jar STLinkUpgrade.jar`
Try to use old version that do not disable SWD access. Expect newer ST firmware even to be more restrictive.

If you did not build with bootloader option enabled flashing this firmware will "soft" brick stlink. While the stlink v3 is plugged into usb
use tweezers to short CN4 (2 pins next to the USB connector) this will force bootloader mode. On STLINK-V3MINIE version its TP1 and TP2 on the bottom.

## What remains to be done?

- Improve and document LED indication
- ...and more, e.g. additional implement CAN, I2C, ... in the firmware
