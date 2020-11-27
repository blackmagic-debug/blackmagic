Black Magic Probe
=================

[![Build Status](https://travis-ci.org/blacksphere/blackmagic.svg?branch=master)](https://travis-ci.org/blacksphere/blackmagic)
[![Discord](https://img.shields.io/discord/613131135903596547?logo=discord)](https://discord.gg/P7FYThy)
[![Donate](https://img.shields.io/badge/paypal-donate-blue.svg)](https://www.paypal.com/cgi-bin/webscr?cmd=_s-xclick&hosted_button_id=N84QYNAM2JJQG)
[![Kickstarter](https://img.shields.io/badge/kickstarter-back%20us-14e16e.svg)](https://www.kickstarter.com/projects/esden/1bitsy-and-black-magic-probe-demystifying-arm-prog)

Firmware for the Black Magic Debug Probe.

The Black Magic Probe is a modern, in-application debugging tool for
embedded microprocessors. It allows you see what is going on 'inside' an
application running on an embedded microprocessor while it executes. It is
able to control and examine the state of the target microprocessor using a
JTAG or Serial Wire Debugging (SWD) port and on-chip debug logic provided
by the microprocessor. The probe connects to a host computer using a
standard USB interface. The user is able to control exactly what happens
using the GNU source level debugging software, GDB.
Serial Wire Output (SWO) allows the target to write tracing and logging to the host
without using usb or serial port. Decoding SWO in the probe itself
makes [SWO viewing as simple as connecting to a serial port](https://github.com/blacksphere/blackmagic/wiki/Serial-Wire-Output).

See online documentation at https://github.com/blacksphere/blackmagic/wiki

Binaries from the latest automated build are at http://builds.blacksphere.co.nz/blackmagic

Toolchain specific remarks
==========================
Most firmware building is done with the most recent suite from https://developer.arm.com/tools-and-software/open-source-software/developer-tools/gnu-toolchain/gnu-rm.
If you have a toolchain from other sources and find problems, check if it is a failure of your toolchain and if not open an issue or better provide a pull request with a fix.

OS specific remarks for BMP-Hosted
==================================
Most hosted building is done on and for Linux. BMP-hosted for windows can also be build with Mingw on Linux.
On BSD/Macos, using dev/tty.usbmodemXXX should work but unresolved discussions indicate a hanging open() call on the second invocation. If that happens, try with cu.usbmodemXXX.

Reporting problems
==================
Before reporting issues, check against the latest git version. If possible, test against another target /and/or debug probe. Consider broken USB cables and connectors. Try to reproduce with bmp-hosted with at least debug bit 1 set (blackmagic -v 1 ...), as debug messages will be dumped to the starting console. When reporting issues, be as specific as possible!

Sample Session
=============
```console
> arm-none-eabi-gdb gpio.elf
...<GDB Copyright message>
(gdb) tar ext /dev/ttyACM0
Remote debugging using /dev/ttyACM0
(gdb) mon s
Target voltage: 2.94V
Available Targets:
No. Att Driver
 1      STM32F40x M3/M4
(gdb) att 1
Attaching to program: /devel/en_apps/gpio/f4_discovery/gpio.elf, Remote target
0x08002298 in UsartIOCtl ()
(gdb) load
Loading section .text, size 0x5868 lma 0x8000000
Loading section .data, size 0x9e0 lma 0x8005868
Loading section .rodata, size 0x254 lma 0x8006248
Start address 0x800007c, load size 25756
Transfer rate: 31 KB/sec, 919 bytes/write.
(gdb) b main
Breakpoint 1 at 0x80000e8: file /devel/en_apps/gpio/f4_discovery/../gpio.c, line 70.
(gdb) r
Starting program: /devel/en_apps/gpio/f4_discovery/gpio.elf 
Note: automatically using hardware breakpoints for read-only addresses.

Breakpoint 1, main () at /devel/en_apps/gpio/f4_discovery/../gpio.c:70
70      {
```

BLACKMAGIC
==========

You can also build blackmagic as a PC hosted application
"make PROBE_HOST=hosted"

This builds the same GDB server, that is running on the Black Magic Probe.
While connection to the Black Magic Probe GDB server is via serial line,
connection to the PC-Hosted GDB server is via TCP port 2000 for the first
GDB server and higher for more invokations. Use "tar(get) ext(ented) :2000"
to connect.
PC-hosted BMP GDB server can talk to
- Black Magic Probe firmware probes via the USB-serial port
- ST-LinkV2 and V3 with recent firmware
- CMSIS-DAP compatible probes
- JLINK probes
- FTDI MPSSE based probe.

When connected to a single BMP supported probe, starting "blackmagic" w/o any
arguments starts the server. When several BMP supported probes are connected,
their types, position and serial number is displayed and the program exits.
Add "-P (position)" to the next invocation to select one.
For the setup from the sample session above:
In another terminal:
```console
> blackmagic 
Using 1d50:6018 E2E489E7 Black Sphere Technologies Black Magic Probe (STLINK), (Firmware v1.6.1-477-g70bb131-dirty)
Remote is Black Magic Probe (STLINK), (Firmware v1.6.1-477-g70bb131-dirty) v1.6.1-477-g70bb131-dirty
Listening on TCP: 2000
And in the GDB terminal:
(gdb) target ext :2000
Remote debugging using :2000
(gdb) mon s
...
```

PC hosted BMP also allows to flash, read and verify a binary file, by default
starting at lowest flash address. The "-t" argument displays information about the
connected target. Use "-h " to get a list of supported options.

