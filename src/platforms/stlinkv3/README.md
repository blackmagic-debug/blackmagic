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

## Building.

As simple as
```
make PROBE_HOST=stlinkv3 clean
make PROBE_HOST=stlinkv3
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
https://github.com/UweBonnes/stlink-tool/tree/stlinkv21
make and use like
`stlink-tool blackmagic.bin`
Revert to original ST firmware with
`java -jar STLinkUpgrade.jar`
Try to use old version that do not disable SWD access. Expect newer ST firmware even to be more restrictive.

## What remains to be done?

- Improve and document LED indication
- ...and more, e.g. additional implement CAN, I2C, ... in the firmware
