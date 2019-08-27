# Blackmagic for ST -Link Adapters

For STlinkV3 and StlinkV2/1, as found on all Nucleo and recent Discovery
boards, use the pc-stlinkv2 branch, running on the PC and with original,
recent ST firmware.

Only if you have a Stlinkv2 with STM32F103C8 versus the STM32F103CB on V2/1
and you want to rewire and use the UART, consider reflashing the the Stlink
firmware.

## Versions

### [Standalone ST-LINKV2
](https://www.st.com/content/st_com/en/products/development-tools/hardware-development-tools/development-tool-hardware-for-mcus/debug-hardware-for-mcus/debug-hardware-for-stm32-mcus/st-link-v2.html)
Accessible connectors for JTAG/SWD (20-pin) and SWIM.
ST-LINKV2/ISOL).
### ST-LINKV2 clones aka "baite"
JTAG/SWD/SWIM are on a 10-pin connector. CPU SWD pins are accessible on the
board.
### SWIM-only ST-LINK adapters on STM8 Discovery boards
JTAG and target SWIM pins are accessible on connector (footprints). They are handled in the swlink branch.
### SWIM-only ST-LINK adapters on STM8 Nucleo-Stm8 boards
As only a SWIM connector is accessible, they are not usefull as BMP target.
### [SWD only ST-LINK adapter
](https://www.st.com/content/ccc/resource/technical/document/technical_note/group0/30/c8/1d/0f/15/62/46/ef/DM00290229/files/DM00290229.pdf/jcr:content/translations/en.DM00290229.pdf)
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
### [STLINK-V3SET
](https://www.st.com/content/st_com/en/products/development-tools/hardware-development-tools/development-tool-hardware-for-mcus/debug-hardware-for-mcus/debug-hardware-for-stm32-mcus/stlink-v3set.html)

## BMP version detection and handling
All stlink variants
PC13/14 open -> Standalone ST-LINKV2 or baite, some STM32 Disco w/o accessible
UART RX/TX

PC13 low -> SWIM internal connection

