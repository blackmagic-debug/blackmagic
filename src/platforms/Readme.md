# Platforms and platform support files

This directory contains the implementation of platforms and support file
used by (multiple) platforms.

## Implementation directories

native : Firmware for original Black Magic Probe<br>
stlink : Firmware for STLINK-V2 and V21<br>
swlink :  Firmware for STLINK-V1 and Bluepill<br>
hydrabus :  Firmware https://hydrabus.com/ <br>
f4discovery    : Firmware for STM32F407DISCO<br>
launchpad-icdi :<br>
tm4c: <br>
hosted: PC-hosted BMP running as PC application talking to firmware BMPs,
        STLINK-V2/21/3, FTDI MPSSE probes, CMSIS-DAP and JLINK


## Support directories

common: libopencm3 based support for firmware BMPs<br>
stm32:  STM32 specific libopencm3 based support  for firmware BMPs<br>
pc: Support for PC-hosted BMPs.<br>
