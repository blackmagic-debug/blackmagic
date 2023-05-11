# Common code for the Black Magic Probe Firmware for WeAct Studio F401CC/F401CE/F411CE boards

This code allows using a [Black Pill F4](https://github.com/WeActStudio/WeActStudio.MiniSTM32F4x1) as a Black Magic Probe.

This directory contains the common code for the following platforms:
- [blackpill-f401cc](./../../blackpill-f401cc/README.md)
- [blackpill-f401ce](./../../blackpill-f401ce/README.md)
- [blackpill-f411ce](./../../blackpill-f411ce/README.md)

## Pinout

| Function        | Pinout | Alternative pinout 1 | Cluster   |
| --------------- | ------ | -------------------- | --------- |
| TDI             | PB6    | PB5                  | JTAG/SWD  |
| TDO/TRACESWO    | PB7    | PB6                  | JTAG/SWD  |
| TCK/SWCLK       | PB8    | PB7                  | JTAG/SWD  |
| TMS/SWDIO       | PB9    | PB8                  | JTAG/SWD  |
| nRST            | PA5    | PB4                  | JTAG/SWD  |
| TRST (optional) | PA6    | PB3                  | JTAG/SWD  |
| UART TX         | PA2    | PA2                  | USB USART |
| UART RX         | PA3    | PA3                  | USB USART |
| Power pin       | PA1    | PB9                  | Power     |
| LED idle run    | PC13   | PC13                 | LED       |
| LED error       | PC14   | PC14                 | LED       |
| LED bootloader  | PC15   | PC15                 | LED       |
| LED UART        | PA4    | PA1                  | LED       |
| User button KEY | PA0    | PA0                  |           |

## How to Build

In the following commands replace `blackpill-f4x1cx` with the platform you are using, e.g. `blackpill-f401cc`.

To build the code using the default pinout, run:

```sh
cd blackmagic
make clean
make PROBE_HOST=blackpill-f4x1cx
```

or, to use alternative pinout 1, run:

```sh
cd blackmagic
make clean
make PROBE_HOST=blackpill-f4x1cx ALTERNATIVE_PINOUT=1
```

## Firmware upgrade with dfu-util

- Install [dfu-util](https://dfu-util.sourceforge.net).
- Connect the Black Pill to the computer via USB. Make sure it is a data cable, not a charging cable.
- Force the F4 into system bootloader mode:
  - Push NRST and BOOT0
  - Wait a moment
  - Release NRST
  - Wait a moment
  - Release BOOT0
- Upload the firmware:
```
./dfu-util -a 0 --dfuse-address 0x08000000:leave -R -D blackmagic.bin
```
- Exit dfu mode: press and release nRST. The newly flashed Black Magic Probe should boot and enumerate.

For other firmware upgrade instructions see the [Firmware Upgrade](https://black-magic.org/upgrade.html) section.

## SWD/JTAG frequency setting

In special cases the SWD/JTAG frequency needs to be adapted. The fequency can be set to `900k`, as this value was found to be helpful in practice, using:

```
mon freq 900k
```

For details see the [pull request](https://github.com/blackmagic-debug/blackmagic/pull/783#issue-529197718) that implemented the `mon freq` command.
