Launchpad platform for the Black Magic Probe firmware
=====================================================

The 'launchpad' platform make it possible to use a Tiva (formerly Stellaris)
Launchpad from TI as host for the Black Magic Probe firmware. Since the
Launchpad contains two identical TM4C123G microcontrollers, it is possible to
use either of them as BMP host:

* the 'ICDI' MCU, i.e. the one normally used as debugger on the Launchpad,
  requires an external flashing tool for re-flash

* the 'target' MCU, i.e. the one normally used as development target on the
  Launchpad, can be flashed through USB by means of open-source tools

More information about the Tiva Launchpad, including board schematics, can be
found at http://www.ti.com/tool/EK-TM4C123GXL


# Launchpad ICDI MCU

## Connections

The JTAG/SWD lines on the ICDI MCU are normally connected to the target TM4C123G
MCU; they can also be brought outside through a 2.54"-spaced header (a jumper is
available on-board to cut power going to target MCU when the JTAG port is used
for an external device).

* JTAG/SWD
  * PA3: TMS/SWDIO
  * PA2: TCK/SWCLK
  * PA5: TDI
  * PA4: TDO
  * PA6: SRST (NRST/System Reset)
  * PD6: SWO

* USB UART
  * PA1: UART0 TX
  * PA0: UART0 RX

* LEDs and debug UART are not available

## How to build

```
cd blackmagic
make clean
make PROBE_HOST=launchpad PROBE_HOST_VARIANT=icdi
```

## How to flash

A 10-pin Tag-Connect JTAG port is available for ICDI MCU programming; another
Black Magic Probe (or any JTAG programmer supporting the TM4C123G MCU) can be
used to flash the BMP firmware.


# Launchpad target MCU

## Connections

The JTAG/SWD lines on the target MCU are connected to the 2.54"-spaced headers
used for Booster Packs.

* JTAG/SWD
  * PA3: TMS/SWDIO
  * PA2: TCK/SWCLK
  * PA5: TDI
  * PA4: TDO
  * PA6: SRST (NRST/System Reset)
  * PE0: SWO

* USB UART
  * PE4: UART5 RX
  * PE5: UART5 TX

* Debug UART (BMP trace port)
  * PA0: UART0 TX
  * PA1: UART0 RX

* RGB LED status indicator
  * red LED (PF1): ERROR
  * blue LED (PF2): IDLE
  * green LED (PF3): RUN

## How to build

```
cd blackmagic
make clean
make PROBE_HOST=launchpad PROBE_HOST_VARIANT=target
```

## How to flash

While the ICDI MCU retains the original firmware from TI, the open-source tool
_lm4flash_ can be used to flash the target MCU on the Launchpad. After
connecting the Launchpad to the PC using the micro USB port marked _DEBUG_,
following commands have to be run:

```
cd blackmagic
lm4flash src/blackmagic.bin
```

Instructions on how to get and build the _lm4flash_ tool can be found at
https://github.com/utzig/lm4tools
