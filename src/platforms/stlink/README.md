# Blackmagic for STLINK Adapters

STLINK-V3, ST-LINK/V2-1 and /V2 with original, recent ST firmware can can use the hosted branch, running the GDB server on PC.

Running the BMP firmware on ST-LINK/V2 and ST-LINK/V2-1 provides:
- built-in gdb server on the dongle
- VCP on ST-LINK/V2. Access to VCP lines needs additional wires to the STM32F103C8!
- VCP on ST-LINK/V2 clones ([Baite](https://www.mikrocontroller.net/attachment/356780/st-linkV2-mini_LRG.jpg)), accessible on the bare PCB.
- no mass storage device (MSD). A MSD may collide with company policies.

For all commands below, unplug all other BMP/ST-LINK beside the target(*1)

## Upload BMP Firmware:

- Keep the original ST Bootloader.

- Compile firmware with `make PROBE_HOST=stlink ST_BOOTLOADER=1`

- Upload firmware with stlink-tool from [stlink-tool](https://github.com/UweBonnes/stlink-tool/tree/stlinkv21)(*3).

- For ST-LINK/V2, as on older disco boards, un- and replug USB to enter the bootloader.

- Upload BMP firmware with `stlink-tool blackmagic.bin`

- For ST-LINK/V2, after each stlink replug, call either `blackmacic -t` or `stlink-tool` without arguments  or on Linux use some udev rule like

`> cat /etc/udev/rules.d/98-stlink.rules`

 `SUBSYSTEM=="usb", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="3748", ACTION=="add", RUN+="<path-to>/stlink-tool"`

  to enter the BMP firmware

## Reverting to original ST Firmware with running BMP firmware:

- Get STLINK upgrade [firmware](https://www.st.com/content/st_com/en/products/development-tools/software-development-tools/stm32-software-development-tools/stm32-programmers/stsw-link007.html) and unzip. Change to "stsw-link007/AllPlatforms/" in the unzipped directory.

- To enter Bootloader again use `dfu-util -e`, or with ST-LINK/V2 un- and replug.

- For ST-LINK/V2(Standalone) run `java -jar STLinkUpgrade.jar -jtag_swim -force_prog`

- For ST-LINK/V2(embedded) run `java -jar STLinkUpgrade.jar -jtag -force_prog`

- For ST-LINK/V21 run `java -jar STLinkUpgrade.jar -msvcp -force_prog`

- For STLINK-V3 run `java -jar STLinkUpgrade.jar -d8_d32_msc_br -force_prog` (*2)

<br> (*1) Command arguments are available to specify some specific of several devices. Look at the help for blackmagic, stlink-tool and dfu-util if really needed. For STLinkUpgrade.jar, no documentation is known, but `strings ./st/stlinkupgrade/app/a.class` may give a clue.<
<br> (*2) Loading firmware V37 and higher to STLINK-V3 with original bootloader will inhibit further debugging the F723 on the ST-LINK itself. There are patched bootloaders out there that do not set RDP2.
<br> (*3) Pull request to original author pending.

## Versions

### [Standalone ST-LINK/V2](https://www.st.com/content/st_com/en/products/development-tools/hardware-development-tools/development-tool-hardware-for-mcus/debug-hardware-for-mcus/debug-hardware-for-stm32-mcus/st-link-v2.html)
Accessible connectors for JTAG/SWD (20-pin) and SWIM.
ST-LINK/V2/ISOL).
### ST-LINK/V2 clones aka "baite"
JTAG/SWD/SWIM are on a 10-pin connector. CPU SWD pins are accessible on the
board.
### SWIM-only ST-LINK adapters on STM8 Discovery boards
JTAG and target SWIM pins are accessible on connector (footprints). They are handled in the swlink platform.
### SWIM-only ST-LINK adapters on STM8 Nucleo-Stm8 boards
As only a SWIM connector is accessible, they are not usefull as BMP target.
### [SWD only ST-LINK adapter](https://www.st.com/content/ccc/resource/technical/document/technical_note/group0/30/c8/1d/0f/15/62/46/ef/DM00290229/files/DM00290229.pdf/jcr:content/translations/en.DM00290229.pdf) (Stm32 Nucleo Boards, recent Discovery boards)
 SWD, SWO and Reset are accessible on a 6-pin connector row.
 Jumper allow to route SWD to on-board target or off-board.
 Newer variants have UART TX/RX accessible on a connector
 According to on-board target variant, some signals have open (resistor)  jumper between debugger and target.
 Newer variants have transistor for USB reenumeration
 Newer variants may switch onboard target power.
 Newer Variants may have level shifters for some signals to onboard target.
#### ST-LINK/V1
CDCACM USART pins are not accessible. MCO output is used for LED. Use the swlink platform!
#### ST-LINK/V2 and ST-LINK/V2-A
CDCACM USART pins are not accessible. MCO is connected to on board target.
#### ST-LINK/V2-1 and ST-LINK/V2-B
#### [STLINK-V3SET](https://www.st.com/content/st_com/en/products/development-tools/hardware-development-tools/development-tool-hardware-for-mcus/debug-hardware-for-mcus/debug-hardware-for-stm32-mcus/stlink-v3set.html)

## Wiring on Discovery and Nucleo Boards

If there is a 6-pin connector, connect an external target after removing
the 2 jumper shortening the 4-pin connector like this:

1: VCC sense, used only for measurement

2: SWCLK

3: GND

4: SWDIO

5: nSRST (pulled high by on board target. Will reset with on board target
   unpowered.

6: SWO


## BMP version detection and handling
All ST-LINK variants
PC13/14 open -> Standalone ST-LINK/V2 or baite, some STM32 Disco w/o accessible
UART RX/TX

PC13 low -> SWIM internal connection

PC13/PC14 both low -> ST-LINK/V2 on some F4_Diso boards.

## ST-LINK V2.1 force Bootloader entry
On ST-LINK V2/2-1 boards with the original bootloader, you can force
bootloader entry with asserting [NRST](https://www.carminenoviello.com/2016/02/26/restore-st-link-interface-bad-update-2-26-15-firmware/) of the STM32F103CB of the USB powered board. Serveral attempts may be needed.
