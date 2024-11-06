# Firmware BMP for ctxLink Wireless Probe

Allows the use of ctxLink board as a Black Magic Probe.

## LED assignments
 * PB2 -> LED0 -> Green - Running LED
 * PC6 -> LED1 -> Orange - Idle LED
 * PC8 -> LED2 -> Red - Error
 * PC9 -> LED3 -> Green - ctxLink Mode

## Connections

* PA3: TDI
* PA4: TMS/SWDIO
* PA5: TCK/SWCLK
* PC7: TDO/TRACESWO
* PA2: nRST

## 10 pin male from pins

ctxLink uses the common 2 x 5 0.05" pin-header.

+-----------+-------------+--------------+------------+-----------+
|      1    |       3     |       5      |      7     |     9     |
| --------- | ----------- | ------------ | ---------- | --------- |
| Tgt Power |     GND     |      GND     |     N/C    |   GND     |
+-----------+-------------+--------------+------------+-----------+
|      2    |       4     |       6      |      8     |    10     |
| --------- | ----------- | ------------ | ---------- | --------- |
| SWDIO/TMS |  SWCLK/TCK  | TRACESWO/TDO |     TDI    |   RESET   |
+-----------+-------------+--------------+------------+-----------+


## Compiling the ctxLink firmware

1. Clone the Blackmagic Debug Repository

```sh
git clone https://github.com/blackmagic-debug/blackmagic.git
cd blackmagic
```

2. Create the build configuration for the ctxLink firmware

```sh
meson setup build --cross-file=cross-file/ctxlink.ini
```

3. Compile the ctxLink firmware
   note: ctxLink uses the ST System Bootloader and does not have a bootloader itself.

```sh
ninja -C build
```

## How to Flash with DFU

After build:

4) Install dfu-util
5) Force ctxLink into system bootloader mode by holding down the Mode switch while
   pressing reset. Release reset followed by Mode. System bootloader should appear.
6) Flash the firmware

```sh
dfu-util -a 0 --dfuse-address 0x08000000:leave -D blackmagic_ctxlink_firmware.bin
```

## SWD/JTAG frequency setting

https://github.com/blackmagic-debug/blackmagic/pull/783#issue-529197718
