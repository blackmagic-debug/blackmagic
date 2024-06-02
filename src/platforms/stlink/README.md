# Blackmagic for ST-Link Adapters

With recent ST firmware, the ST-Link v2, ST-Link v2-1 and v3 can be used with
Black Magic Debug App rather than having to flash this firmware to the adaptor.

Running the BMP firmware on ST-Link v2 and ST-Link v2-1 provides:

* built-in gdb server on the dongle
* VCP on ST-Link v2. Access to VCP lines needs additional wires to the STM32F103C8!
* VCP on ST-Link v2 clones ([Baite](https://www.mikrocontroller.net/attachment/356780/st-linkV2-mini_LRG.jpg)), accessible on the bare PCB.
* no mass storage device (MSD). A MSD may collide with company policies.

For all commands below, unplug all other BMP/ST-Link beside the target(*1)

This build provides a UART interface over USB to the host.
By default, it's USART2 with pins PA2 and PA3 used as TX and RX.
Many clones of the official adaptors provide SWIM pins on the connector and don't provide a UART interface. 
In this case, the firmware can be built with `SWIM_NRST_AS_UART=1` flag to repurpose the SWIM pins as the UART.
In particular, USART1 will be provided to the host and pins PB6 and PB7 will be used as TX and RX. 

Note: on some clones PB6 is used as nRST pin, so with `SWIM_NRST_AS_UART=1` the UART will take up both SWIM (PB7) and RST (PB6) pins.
In this case nRST function is moved to PB0 pin and, if this pin is not on the connector, reset functionality will be lost.
On other devices where nRST pin is PB0, reset feature will remain functional.

Note: on some clones, SWIM is strongly pulled up by a 680 Ohm resistor.

Some of the clones are not detected correctly by the firmware
(`version` output will say e.g. `Hardware Version 1` instead of
`Hardware Version 257`) because of differences in internal
connections. In this case you can build the firmware with
`STLINK_FORCE_CLONE=1` to force the firmware to use the clone pinmap
(nRST on PB6).


## External connections

| Function  | Normal Pin | Alt Pin |
| --------- | ---------- | ------- |
| SWCLK/TCK |  PA5       |    -    |
| TDO       |  PA6       |    -    |
| TDI       |  PA7       |    -    |
| SWDIO/TMS |  PB14      |    -    |
| SWO       |  PA10      |    -    |
| nRST      |  PB0       | PB6     |

The alternative pinout uses the same pins as the normal unless noted otherwise for a function.
This second pinout is used on some clone boards - if you are using a clone, check the schematic.

NB: SWDIO/TMS is on P**B**14, not P**A**14.

## Upload BMP Firmware

* Keep the original ST Bootloader.
* Compile firmware with the option `-Dbmd_bootloader=false`
* Upload firmware with stlink-tool from [stlink-tool](https://github.com/blackmagic-debug/stlink-tool)(*3).
* For ST-Link v2, as on older disco boards, un- and replug USB to enter the bootloader.
* Upload BMP firmware with `stlink-tool blackmagic_stlink_firmware.bin`
* For ST-Link v2, after each stlink replug, call either `blackmacic -t` or `stlink-tool` without arguments  or on Linux use some udev rule like the one shown below to enter the BMP firmware

```sh
> cat >/etc/udev/rules.d/98-stlink.rules <<EOF
SUBSYSTEM=="usb", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="3748", ACTION=="add", RUN+="<path-to>/stlink-tool"
EOF
```

## Reverting to original ST Firmware with running BMP firmware

* Get ST-Link upgrade [firmware](https://www.st.com/content/st_com/en/products/development-tools/software-development-tools/stm32-software-development-tools/stm32-programmers/stsw-link007.html) and unzip. Change to "stsw-link007/AllPlatforms/" in the unzipped directory.
* To enter Bootloader again use `dfu-util -e`, or with ST-Link v2 un- and replug.
* For ST-Link v2(Standalone) run `java -jar STLinkUpgrade.jar -jtag_swim -force_prog`
* For ST-Link v2(embedded) run `java -jar STLinkUpgrade.jar -jtag -force_prog`
* For ST-Link v2-1 run `java -jar STLinkUpgrade.jar -msvcp -force_prog`
* For ST-Link v3 run `java -jar STLinkUpgrade.jar -d8_d32_msc_br -force_prog` (*2)

 (*1) Command arguments are available to specify some specific of several devices. Look at the help for blackmagic, stlink-tool and dfu-util if really needed. For STLinkUpgrade.jar, no documentation is known, but `strings ./st/stlinkupgrade/app/a.class` may give a clue.

 (*2) Loading firmware V37 and higher to ST-Link v3 with original bootloader will inhibit further debugging the F723 on the ST-Link itself. There are patched bootloaders out there that do not set RDP2.

 (*3) Pull request to original author pending.

## Versions

### [Standalone ST-Link v2](https://www.st.com/content/st_com/en/products/development-tools/hardware-development-tools/development-tool-hardware-for-mcus/debug-hardware-for-mcus/debug-hardware-for-stm32-mcus/st-link-v2.html)

Accessible connectors for JTAG/SWD (20-pin) and SWIM.
ST-Link v2/ISOL).

### ST-Link v2 clones aka "baite"

JTAG/SWD/SWIM are on a 10-pin connector. CPU SWD pins are accessible on the
board.

### SWIM-only ST-Link adapters on STM8 Discovery boards

JTAG and target SWIM pins are accessible on connector (footprints). They are handled in the swlink platform.

### SWIM-only ST-Link adapters on STM8 Nucleo-Stm8 boards

As only a SWIM connector is accessible, they are not useful as BMP target.

### [SWD only ST-Link adapter](https://www.st.com/content/ccc/resource/technical/document/technical_note/group0/30/c8/1d/0f/15/62/46/ef/DM00290229/files/DM00290229.pdf/jcr:content/translations/en.DM00290229.pdf) (Stm32 Nucleo Boards, recent Discovery boards)

 SWD, SWO and Reset are accessible on a 6-pin connector row.
 Jumper allow to route SWD to on-board target or off-board.
 Newer variants have UART TX/RX accessible on a connector
 According to on-board target variant, some signals have open (resistor)  jumper between debugger and target.
 Newer variants have transistor for USB reenumeration
 Newer variants may switch onboard target power.
 Newer Variants may have level shifters for some signals to onboard target.

#### ST-Link v1

CDC ACM USART pins are not accessible. MCO output is used for LED. Use the swlink platform!

#### ST-Link v2 and ST-Link v2-A

CDC ACM USART pins are not accessible. MCO is connected to on board target.

#### ST-Link v2-1 and ST-Link v2-B

#### [ST-Link v3SET](https://www.st.com/content/st_com/en/products/development-tools/hardware-development-tools/development-tool-hardware-for-mcus/debug-hardware-for-mcus/debug-hardware-for-stm32-mcus/stlink-v3set.html)

## Wiring on Discovery and Nucleo Boards

If there is a 6-pin connector, connect an external target after removing
the 2 jumper shortening the 4-pin connector like this:

1. VCC sense, used only for measurement
2. SWCLK
3. GND
4. SWDIO
5. nRST (pulled high by on board target. Will reset with on board target unpowered.
6. SWO

## BMP version detection and handling

All ST-Link variants:

* PC13/14 open -> Standalone ST-Link v2 or baite, some STM32 Disco w/o accessible
UART RX/TX
* PC13 low -> SWIM internal connection
* PC13/PC14 both low -> ST-Link v2 on some F4_Diso boards.

## ST-Link v2.1 force Bootloader entry

On ST-Link v2/2-1 boards with the original bootloader, you can force
bootloader entry with asserting [NRST](https://www.carminenoviello.com/2016/02/26/restore-st-link-interface-bad-update-2-26-15-firmware/) of the STM32F103CB of the USB powered board. Several attempts may be needed.
