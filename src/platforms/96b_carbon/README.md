# 96b Carbon

## Connections

The pinout for the programmer is concentrated within a single part of
the Low Speed connector. The target control pins at the even pins 2
through 16 with UART on LS-03 and LS-05.

The pinout for the programmer allows a Carbon to program another Carbon
(either the STM32 or the nRF51) with adjacent pins from LS-06 to LS-12.
The order matches that of the SWD pins for easy hook up.

### JTAG/SWD

* LS-02 (PB12): TDO/TRACESWO
* LS-04 (PB15): TDI
* LS-06 (PB14): TMS/SWDIO
* LS-08 (PB13): TCK/SWCLK
* LS-10       : GND
* LS-12       : Vcc
* LS-14 (PC3) : TRST (optional Test Reset)
* LS-16 (PC5) : nRST (nRST / System Reset)

### LEDs

* USR1 (green): Debug activity indicator
* USR2 (green): UART activity indicator
* BT (blue)   : Error indicator

### UART

* LS-03 (PA2): UART TX
* LS-05 (PA3): UART RX

## How to Build

    cd blackmagic
    make clean
    make PROBE_HOST=96b_carbon

## Flashing using dfu-util

Connect to the USB OTG socket on the Carbon and force the device into
system bootloader mode by holding down the BOOT0 switch whilst pressing
and releasing the RST switch. To program the device try:

    sudo dfu-util -d [0483:df11] -a 0 -D src/blackmagic.bin -s 0x08000000

## Self-programming

A Carbon is capable of self-programming its own nRF51 by connecting two
jumper wires from LS-06 to BLE_SWD-4 (DIO) and LS-08 to BLE_SWD-3 (CLK).

    +------------------------------------------------------------------+
    |  +-2--4--6--8-10-12-14-16-18-20-22-24-26-28-30-+                 |
    |  | .  .  .-+.-+.  .  .  .  .  .  .  .  .  .  . |                 |
    |  | .  .  . |. |.  .  .  .  .  .  .  .  .  .  . |                 |
    |  +-1--3--5-|7-|9-11-13-15-17-19-21-23-25-27-29-+                 |
    |            |  |                                                  |
    |            +--+-----------------------------+                    |
    |               |                             |                    |
    |               +--------------------------+  |                    |
    |                                          |  |                    |
    |                                    .  .  .  .  .                 |
    |                              .  .  .  .  .  .  .  .  .           |
    +------------------------------------------------------------------+
