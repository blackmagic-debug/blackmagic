# Blackmagic for ST -Link Adapters

For ST-LINK V3 and ST-LINKV 2/1, as found on all Nucleo and recent Discovery
boards, use the pc-stlinkv2 branch, running on the PC and with original,
recent ST firmware.

Only if you have a ST-LINK V22 with STM32F103C8 versus the STM32F103CB on V2/1
and you want to rewire and use the UART, consider reflashing the the Stlink
firmware.

On ST-LINK V2, the original ST Bootloader can also be used with

- Compile firmware with "make PROBE_HOST=stlink ST_BOOTLOADER=1"

- Upload firmware with stlink-tool from [stlink-tool](https://github.com/jeanthom/stlink-tool.git).
  Before upload, replug the stlink to enter the bootloader.

- After each stlink replug, call "stlink-tool" without arguments
  to enter BMP or on Linux use some udev rule like

`> cat /etc/udev/rules.d/98-stlink.rules`

 `SUBSYSTEM=="usb", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="3748", ACTION=="add", RUN+="<path-to>/stlink-tool"`

- To enter Bootloader again either replug or use "dfu-util -e".
  With more than one DFU device connected, you need to specify
  the needed device

## Versions

### [Standalone ST-LINKV2](https://www.st.com/content/st_com/en/products/development-tools/hardware-development-tools/development-tool-hardware-for-mcus/debug-hardware-for-mcus/debug-hardware-for-stm32-mcus/st-link-v2.html)
Accessible connectors for JTAG/SWD (20-pin) and SWIM.
ST-LINKV2/ISOL).
### ST-LINKV2 clones aka "baite"
JTAG/SWD/SWIM are on a 10-pin connector. CPU SWD pins are accessible on the
board.
### SWIM-only ST-LINK adapters on STM8 Discovery boards
JTAG and target SWIM pins are accessible on connector (footprints). They are handled in the swlink branch.
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
#### ST-Link/V1
CDCACM USART pins are not accessible. MCO output is used for LED.
#### ST-Link/V2 and ST-Link/V2-A
CDCACM USART pins are not accessible. MCO is connected to on board target.
#### ST-Link/V2-1 and ST-Link/V2-B
### [STLINK-V3SET](https://www.st.com/content/st_com/en/products/development-tools/hardware-development-tools/development-tool-hardware-for-mcus/debug-hardware-for-mcus/debug-hardware-for-stm32-mcus/stlink-v3set.html)

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
PC13/14 open -> Standalone ST-LINKV2 or baite, some STM32 Disco w/o accessible
UART RX/TX

PC13 low -> SWIM internal connection

PC13/PC14 both low -> ST-LinkV2 on some F4_Diso boards.

## Reflashing BMP back to ST-LINK V2 original firmware

If you built the firmware to use ST-Link v2's bootloader (with `ST_BOOTLOADER=1`), you should be able to reflash back to original firmware using the [STLinkUpgrade utility](https://www.st.com/en/development-tools/stsw-link007.html). Do not check the "Change type" checkbox unless you know what you are doing, as it will change the USB VID:PID and change how it operates.

Replug ST-LINK before flashing if the utility doesn't detect it.
##ST-LINK V2.1 Force Bootloader entry
On ST-LINK V2/2-1 boards with the original bootloader, you can force
bootloader entry with asserting NRST of the STM32F103CB of the USB
powered board. Serveral attempts may be needed.
https://www.carminenoviello.com/2016/02/26/restore-st-link-interface-bad-update-2-26-15-firmware/
