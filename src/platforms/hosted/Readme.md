# PC-Hosted BMP
Compile in src with "make PROBE_HOST=hosted"

## Description
PC-hosted BMP run on the PC and compiles as "blackmagic". When started,
it either presents a list of available probes or starts the BMP process
if either only one probe is attached to the PC or enough information is
given on the command line to select one of several probes.

When started without any other argument beside the probe selection, a
GDB server is started as port 2000 and up. Connect to the server as you would
connect to the BMP with the CDCACM GDB serial server. GDB functionality
is the same, monitor option may vary.

More arguments allow to
### print information on the connected target "blackmagiv -t"
### directly flash a binary file at 0x0800000  "blackmagic <file.bin>"
or with the -S argument at some other address
### read flash to binary file "blackmagic -r <file>.bin
### verify flash against binary file "blackmagic -V <file>.bin

Use "blackmagic -h" to see all options.

## Used libraries:
### libusb
### libftdi, for FTDI support
### hidapi-libusb, for CMSIS-DAP support

## Supported debuggers
REMOTE_BMP is a "normal" BMP usb connected

|   Debugger   | Speed | Remarks
| ------------ | ----- | ------
| REMOTE_BMP   |  +++  | Requires recent firmware for decent speed
| ST-Link V3   | ++++  | Requires recent firmware, Only STM32 devices supported!
| ST-Link V2   |  +++  | Requires recent firmware, No CDCACM uart! Cortex only!
| ST-Link V2/1 |  +++  | Requires recent firmware, Cortex only!
| CMSIS-DAP    |  +++  | Speed varies with MCU implementing CMSIS-DAP
| FTDI MPSSE   |   ++  | Requires a device descrition
| JLINK        |    -  | Usefull to add BMP support for MCUs with built-in JLINK

## Feedback
### Issues and Pull request on https://github.com/blacksphere/blackmagic/
### Discussions on Discord.
You can find the Discord link here: https://1bitsquared.com/pages/chat
### Blackmagic mailing list http://sourceforge.net/mail/?group_id=407419

## Known deficiencies
### For REMOTE_BMP
#### On windows, the device node must be given on the command line
Finding the device from USB VID/PID/Serial in not yet implemented
### FTDI MPSSE
#### No auto detection
Cable description must be given on the command line
