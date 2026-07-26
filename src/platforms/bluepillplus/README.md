# Black Magic Debug Probe Firmware for WeAct Studio BluePill-Plus boards

This platform allows using various BluePill-Plus boards as a Black Magic Probe.

[BluePill-Plus](https://github.com/WeActStudio/BluePill-Plus) is based on STM32F103CB or GD32F303CC;

[BluePill-Plus-GD32](https://github.com/WeActStudio/WeActStudio.BluePill-Plus-GD32) is based on GD32F103CB. SoC runs at up to 108 MHz, but because of USB FS device clock restrictions 96 MHz is used.

[BluePill-Plus-APM32](https://github.com/WeActStudio/WeActStudio.BluePill-Plus-APM32) is based on APM32F103CB (untested).

[BluePill-Plus-CH32](https://github.com/WeActStudio/WeActStudio.BluePill-Plus-CH32) is based on CH32F103C8 (CM3) or CH32V103C8 or CH32V203C8 (RISC-V) and unsupported (flash too small, non-Cortex-M).

| SoC         | Core       |Clock, MHz|SRAM, KiB|Flash, KiB|
|-------------|------------|----------|---------|----------|
| STM32F103CB | Cortex-M3  | 72       | 20      | 128 (2WS)|
| GD32F103CB  | Cortex-M3  | 96       | 20      | 128 (0WS)|
| GD32F303CC  | Cortex-M4F | 120      | 48      | 256 (0WS)|
| APM32F103CB | Cortex-M3  | 96       | 20      | 128 (0WS)|

## Pinout

| Function        | Pinout | Cluster   |
| --------------- | ------ | --------- |
| TDI             | PB6    | JTAG/SWD  |
| TDO/TRACESWO    | PB7    | JTAG/SWD  |
| TCK/SWCLK       | PB8    | JTAG/SWD  |
| TMS/SWDIO       | PB9    | JTAG/SWD  |
| nRST            | PA5    | JTAG/SWD  |
| TRST (optional) | PA6    | JTAG/SWD  |
| UART TX         | PA2    | USB USART |
| UART RX         | PA3    | USB USART |
| LED idle run    | PB2    | LED       |
| LED error       | PB10   | LED       |
| LED UART        | PB11   | LED       |
| User button KEY | PA0    |           |

USB Full-Speed PHY is on PA11/PA12 with a fixed 1.5 kOhm external pull-up resistor.

SWJ-DP is on PA13/14/15/PB3/4, those pins are kept in default function (Inception debug possible).

TODO: add support for onboard SPI flash on PA4/5/6/7 (SPI1) and PB12/13/14/15 (SPI2).

## Instructions for build system

0. Clone the repo and libopencm3 submodule, install toolchains, meson, etc.

```sh
git clone https://github.com/blackmagic-debug/blackmagic.git --depth=2000
cd blackmagic
```

1. Create a build configuration for specific platform (WeActStudio BluePill-Plus) with specific options

```sh
meson setup build --cross-file=cross-file/bluepillplus.ini
```

2. Compile the firmware and bootloader

```sh
ninja -C build
ninja -C build boot-bin
```

3. Flash the BMD bootloader into first 8 KiB of memory if it's empty, using USART1, or only flash the BMD Firmware at an offset, using USB DFU.

For other firmware upgrade instructions see the [Firmware Upgrade](https://black-magic.org/upgrade.html) section.

### Using the BMD Bootloader
If you flashed the bootloader using the above instructions, it may be invoked using the following:
- Force the F1 into BMD bootloader mode:
  - Hold down KEY
  - Tap NRST
  - Release KEY

The BMD bootloader also recognizes RCC Reset reason when nRST is pressed, and `dfu-util --detach` (implemented via magic flags in main SRAM).

Once activated the BMD bootloader may be used to flash the device using 'bmputil,' available [here](https://github.com/blackmagic-debug/bmputil).

## Performance

System clock is set to 72 MHz, expecting a 8 MHz HSE crystal. On GD32 chips, 96 and 120 MHz are also an option.

Because of low expectations to signal integrity or quality wiring, default SWD frequency is reduced to 2 MHz, but in practice 6 MHz is possible, JTAG is slower.

Aux serial runs via USART2 DMA on APB1 (Pclk = 36 MHz) at 549..2250000 baud.

TraceSWO Async capture runs via USART1 DMA on APB2 (Pclk = 72 MHz) at 1098..4500000 baud.

SPI ports are set to Pclk/8 each (use with `bmpflash`). As SPI1 pins may conflict with certain functions, and platform code does not bother restoring them, please soft-reset the probe when done with SPI operations.

## More details

* ST MaskROM is the read-only System Memory bootloader which starts up per BOOT0-triggered SYSCFG mem-remap. It talks USART AN2606 so you can use stm32flash etc. However, it does not implement USB DFU, which is why a BMD bootloader is provided here.
* BMD bootloader is the port of BMP bootloader which
    a) has a fixed compatible PLL config;
    b) understands buttons, drives LED, does not touch other GPIOs, talks USB DfuSe, ~~has MS OS descriptors for automatic driver installation on Windows~~, uses same libopencm3 code so you can verify hardware config via a smaller binary;
    c) erases and writes to internal Flash ~2x faster than MaskROM USART at 460800 baud (verify?) without an external USB-UART bridge dongle.
