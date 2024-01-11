# Black Magic Debug App (BMDA)

## Description

BMDA can be compiled as follows:

```sh
meson setup build
meson compile -C build
```

This results in a `blackmagic` executable for your computer.

When the `blackmagic` executable is launched, it will present a list of available probes.
Alternatively, if there is only one probe attached to the computer, or enough information is given on the
command line to select one of several probes, it will start a GDB server on localhost port 2000
(if the port is already bound, BMDA will try the next 4 port numbers up to 2004).

You can connect to the server as you would connect to the BMP over USB serial, replacing
the device path with `:2000` as in `target extended-remote :2000`. BMDA's GDB functionality
is the same, but the monitor options are a little different and can be viewed with `monitor help`.

The following lists some of the supported additional commmand line options and what they do.
It is not exhaustive, and does not show how they can all be combined.

### Display the complete program usage help on the console

```sh
blackmagic -h
```

### Run "Test Mode" and display any connected targets

For SWD:

```sh
blackmagic -t
```

For JTAG:

```sh
blackmagic -tj
```

### Directly flash a binary file at lowest flash address

**NB: BMDA will treat whatever file you provide here as the raw contents to write to Flash.**
**It does not understand ELF, Intel Hex, or SREC formats.**

```sh
blackmagic <file>
```

or with the -S argument at some other address

```sh
blackmagic -S 0x08002000 <file>
```

### Read flash to binary file

```sh
blackmagic -r <file>
```

### Verify flash against binary file

```sh
blackmagic -V <file>
```

### Show available monitor commands

```sh
blackmagic -M help
```

### Show available monitor commands for the second target found

```sh
blackmagic -n 2 -M help
```

### Monitor commands with multiple arguments - e.g. for STM32 option bytes

```sh
blackmagic -M "option help"
```

## Dependencies for non-`HOSTED_BMP_ONLY=1` builds

BMDA uses the following external libraries to function when built in full:

* libusb1
* libftdi1 (except on Windows)
* hidapi (hidapi-hidraw for Linux)

## Compiling on Windows

To build BMDA on Windows,
[please see the guide on the website](https://black-magic.org/knowledge/compiling-windows.html)

It is possible to build BMDA for Windows under Linux using Clang-cl or a MinGW compiler and combining
in the Windows SDK headers and link libraries aquired using `xwin`, but this is outside the scope of
this guide.

## Supported debuggers

|    Debugger   | Speed | Remarks
| ------------- | ----- | ------
| BMP           |  +++  | Requires recent firmware for decent speed
| ST-Link v3*   | ++++  | Requires recent firmware, Only STM32 devices supported!
| ST-Link v2*   |  +++  | Requires recent firmware, No CDCACM uart! Cortex only!
| ST-Link v2.1* |  +++  | Requires recent firmware, Cortex only!
| CMSIS-DAP*    |  +++  | Speed varies with devuce implementing CMSIS-DAP
| FTDI MPSSE*   |   ++  | Requires a device description
| J-Link*       |    -  | Limited support for hardware prior to v8

The probes in this table marked with a star only apply for a full build.

## Device matching

As other USB dongles already connected to the host PC may use FTDI chips,
cable descriptions must be provided to match with the dongle.
To match the dongle, at least USB VID/PID  that must match.
If a description is given, the USB device must provide that string. If a
serial number string is given on the command line, that number must match
with serial number in the USB descriptor of the device.

## FTDI connection possibilities

| Direct Connection     |
| ----------------------|
| MPSSE_SK --> JTAG_TCK |
| MPSSE_DO --> JTAG_TDI |
| MPSSE_DI <-- JTAG_TDO |
| MPSSE_CS <-> JTAG_TMS |

\+ JTAG and bitbanging SWD is possible\
\- No level translation, no buffering, no isolation

Example: [Flossjtag](https://randomprojects.org/wiki/Floss-JTAG).

| Resistor SWD           |
|------------------------|
| MPSSE_SK ---> JTAG_TCK |
| MPSSE_DO -R-> JTAG_TMS |
| MPSSE_DI <--- JTAG_TMS |

BMP would allow direct MPSSE_DO ->JTAG_TMS connections as BMP tristates DO
when reading. Resistor defeats contentions anyways. R is typical chosen
in the range of 470R

\+ MPSSE SWD possible\
\- No Jtag, no level translation, no buffering, no isolation\

| Direct buffered Connection |
|----------------------------|
| MPSSE_SK -B-> JTAG_TCK     |
| MPSSE_DO -B-> JTAG_TDI     |
| MPSSE_DI <-B- JTAG_TDO     |
| MPSSE_CS -B-> JTAG_TMS     |

\+ Only Jtag, buffered, possible level translation and isolation\
\- No SWD

Example: [Turtelizer](http://www.ethernut.de/en/hardware/turtelizer/index.html)
[schematics](http://www.ethernut.de/pdf/turtelizer20c-schematic.pdf)

The 'r' command line arguments allows to specify an external SWD
resistor connection added to some existing cable. Jtag is not possible
together with the 'r' argument.

### Many variants possible

As FTDI devices have more pins, these pins may be used to control
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
