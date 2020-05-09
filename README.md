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

BLACKMAGIC
==========

You can also build blackmagic as a PC hosted application
"make PROBE_HOST=hosted"

This builds the same GDB server, that is running on the Black Magic Probe.
While connection to the Black Magic Probe GDB server is via serial line,
connection to the PC-Hosted GDB server is via TCP port 2000 for the first
GDB server and higher for more invokations. Use "tar(get) ext(ented) :2000"
to connect.
PC-hosted BMP GDB server can talk to the Black Magic Probe itself,
ST-LinkV2 and V3, CMSIS-DAP, JLINK and FTDI MPSSE based debuggers.

When connected to a single BMP supported probe, starting "blackmagic" w/o any
arguments starts the server. When several BMP supported probes are connected,
their types, position and serial number is displayed and the program exits.
Add "-P (position)" to the next invokation to select one.

PC hosted BMP also allows to flash, read and verify a binary file, by default
starting at 0x08000000. The "-t" argument displays information about the
connected target. Use "-h " to get a list of supported options.
