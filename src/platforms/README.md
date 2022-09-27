# Platforms and platform support files

This directory contains the implementation of platforms and support file
used by (multiple) platforms.

## Implementation directories

* native: Firmware for [Black Magic Probe](https://1bitsquared.com/products/black-magic-probe)
* stlink: Firmware for ST-Link v2 and ST-Link v2.1
* swlink:  Firmware for ST-Link v1 and Bluepill
* blackpillv2: Firmware for the WeAct Blackpill v2
* hydrabus:  Firmware for [hydrabus](https://hydrabus.com/)
* f4discovery: Firmware for STM32F407DISCO
* f3: Firmware for the STM32F3
* f072: Firmware for the STM32F072
* 96b_carbon: Firmware for [96Boards' Carbon](https://www.96boards.org/product/carbon/)
* launchpad-icdi: Firmware for the TI Launchpad ICDI processor
* hosted: Black Magic Debug App - able to talk to:
  * Black Magic Probe
  * ST-Link v2, v2.1, and v3
  * FTDI MPSSE probes
  * CMSIS-DAP probes and
  * JLink probes

## Support directories

* common: common platform support for all platforms except hosted (BMDA)
* stm32: STM32 specific libopencm3 common platform support
* tm4c: Tiva-C specific libopencm3 common platform support
