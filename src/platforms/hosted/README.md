# PC-Hosted BMP
Compile in src with "make PROBE_HOST=hosted". This needs minimal external
support.  "make PROBE_HOST=hosted HOSTED_BMP_ONLY=0" will compile support for FTDI,
STLink, CMSIS-DAP and JLINK probes, but requires external libraries.

## Description
PC-hosted BMP run on the PC and compiles as "blackmagic". When started,
it either presents a list of available probes or starts the BMP process
if either only one probe is attached to the PC or enough information is
given on the command line to select one of several probes.

When started without any other argument beside the probe selection, a
GDB server is started on port 2000 and up. Connect to the server as you would
connect to the BMP with the CDCACM GDB serial server. GDB functionality
is the same, monitor option may vary.

More arguments allow to
### Print information on the connected target
```
blackmagic -t
```
### Directly flash a binary file at lowest flash address
```
blackmagic <file.bin>
```
or with the -S argument at some other address
```
blackmagic -S 0x08002000 <file.bin>
```

### Read flash to binary file
```
blackmagic -r <file>.bin
```
### Verify flash against binary file
```
blackmagic -V <file>.bin
```
### Show more options
```
blackmagic -h
```
### Show available monitor commands
```
blackmagic -M help
```
### Show available monitor commands on second target
```
blackmagic -n 2 -M help
```
### Monitor commands with multiple arguments, e.g.Stm32F1:
```
blackmagic -M "option help"
```
## Used shared libraries:
### libusb
### libftdi, for FTDI support

## Other used libraries:
### hidapi-libusb, for CMSIS-DAP support

## Compiling on windows

You can crosscompile blackmagic for windows with mingw or on windows
with cygwin. For support of other probes beside BMP, headers for libftdi1 and
libusb-1.0 are needed. For running, libftdi1.dll and libusb-1.0.dll are needed
and the executable must be able to find them. Mingw on cygwin does not provide
a libftdi package yet.

PC-hosted BMP for windows can also be built with [MSYS2](https://www.msys2.org/),
in windows. Make sure to use the `mingw64` shell from msys2, otherwise,
you may get compilation errors. You will need to install the libusb
and libftdi libraries, and have the correct mingw compiler.
You can use these commands to install dependencies, and build PC-hosted BMP
from a mingw64 shell, from within the `src` directory:
```
pacman -S mingw-w64-x86_64-libusb --needed
pacman -S mingw-w64-x86_64-libftdi --needed
pacman -S mingw-w64-x86_64-gcc --needed
PROBE_HOST=hosted make
```

For support of other probes beside BMP, libusb access is needed. To prepare
libusb access to the ftdi/stlink/jlink/cmsis-dap devices, run zadig
https://zadig.akeo.ie/. Choose WinUSB(libusb-1.0).

Running cygwin/blackmagic in a cygwin console, the program does not react
on ^C. In another console, run "ps ax" to find the WINPID of the process
and then "taskkill /F ?PID (WINPID)".

## Supported debuggers
REMOTE_BMP is a "normal" BMP usb connected

|   Debugger   | Speed | Remarks
| ------------ | ----- | ------
| REMOTE_BMP   |  +++  | Requires recent firmware for decent speed
Probes below only when compiled with HOSTED_BMP_ONLY=0
| ST-Link V3   | ++++  | Requires recent firmware, Only STM32 devices supported!
| ST-Link V2   |  +++  | Requires recent firmware, No CDCACM uart! Cortex only!
| ST-Link V2/1 |  +++  | Requires recent firmware, Cortex only!
| CMSIS-DAP    |  +++  | Speed varies with MCU implementing CMSIS-DAP
| FTDI MPSSE   |   ++  | Requires a device description
| JLINK        |    -  | Useful to add BMP support for MCUs with built-in JLINK

## Device matching
As other USB dongles already connected to the host PC may use FTDI chips,
cable descriptions must be provided to match with the dongle.
To match the dongle, at least USB VID/PID  that must match.
If a description is given, the USB device must provide that string. If a
serial number string is given on the command line, that number must match
with serial number in the USB descriptor of the device.

## FTDI connection possibilities:

| Direct Connection     |
| ----------------------|
| MPSSE_SK --> JTAG_TCK |
| MPSSE_DO --> JTAG_TDI |
| MPSSE_DI <-- JTAG_TDO |
| MPSSE_CS <-> JTAG_TMS |

\+ JTAG and bitbanging SWD is possible<br>
\- No level translation, no buffering, no isolation<br>
Example: [Flossjtag](https://randomprojects.org/wiki/Floss-JTAG).

| Resistor SWD           |
|------------------------|
| MPSSE_SK ---> JTAG_TCK |
| MPSSE_DO -R-> JTAG_TMS |
| MPSSE_DI <--- JTAG_TMS |

BMP would allow direct MPSSE_DO ->JTAG_TMS connections as BMP tristates DO
when reading. Resistor defeats contentions anyways. R is typical chosen
in the range of 470R

\+ MPSSE SWD possible<br>
\- No Jtag, no level translation, no buffering, no isolation<br>

|Direct buffered Connection|
|--------------------------|
| MPSSE_SK -B-> JTAG_TCK   |
| MPSSE_DO -B-> JTAG_TDI   |
| MPSSE_DI <-B- JTAG_TDO   |
| MPSSE_CS -B-> JTAG_TMS   |

\+ Only Jtag, buffered, possible level translation and isolation<br>
\- No SWD<br>
Example: [Turtelizer]http://www.ethernut.de/en/hardware/turtelizer/index.html)
[schematics](http://www.ethernut.de/pdf/turtelizer20c-schematic.pdf)

The 'r' command line arguments allows to specify an external SWD
resistor connection added to some existing cable. Jtag is not possible
together with the 'r' argument.

### Many variants possible
As the FTDI has more pins, these pins may be used to control
enables of buffers and multiplexer selection in many variants.

### FTDI SWD speed
SWD read needs two USB round trip, one for the acknowledge and one
round-trip after the data phase, while JTAG only needs one round-trip.
For that, SWD read speed is about half the JTAG read speed.

### Reset, Target voltage readback etc
The additional pins may also control Reset functionality, provide
information if target voltage is applied. etc.

### Cable descriptions
Please help to verify the cable description and give feedback on the
cables already listed and propose other cable. A link to the schematics
is welcome.

## Feedback
### Issues and Pull request on https://github.com/blackmagic-debug/blackmagic/
### Discussions on Discord.
You can find the Discord link here: https://1bitsquared.com/pages/chat
### Blackmagic mailing list http://sourceforge.net/mail/?group_id=407419
