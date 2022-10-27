# Using RTT

When debugging ARM processors, there are multiple ways for the target to print debug messages on
the host: a dedicated UART port (USB to Serial) aka. AUX Serial, Semihosting, Serial Wire Output SWO, and Real-Time Transfer RTT.

To enable RTT in [Black Magic Debug](https://black-magic.org) (BMD) Firmware the `ENABLE_RTT=1` flag has to be set
during compilation. For more details refer to the [Build Instructions](#build-instructions) section.

The RTT solution implemented in BMD is using a novel way to detect RTT automatically, making
the use fast and convenient.

## Usage

This example uses Linux as operating system. For Windows and MacOS see the *Operating Systems*
section.

In one window open a terminal emulator (minicom, putty) and connect to the USB
UART:
```
$ minicom -c on -D /dev/ttyBmpTarg
```

In another window open a debugger:
```
$ gdb
(gdb) target extended-remote /dev/ttyBmpGdb
(gdb) monitor swdp_scan
(gdb) attach 1
(gdb) monitor rtt
(gdb) run
^C
(gdb) monitor rtt status
rtt: on found: yes ident: off halt: off channels: auto 0 1 3
max poll ms: 256 min poll ms: 8 max errs: 10
```

The terminal emulator displays RTT output from the target, and characters typed
in the terminal emulator are sent via RTT to the target.

It is possible to use RTT and the built-in serial port at the same time. If both RTT and the built-in serial port are used at the same time, the terminal will show both RTT and serial port output, interleaved.

Characters typed in at the terminal go to RTT if RTT is enabled, or to the serial port if RTT is not enabled.

- ``monitor rtt``: If RTT is enabled, characters typed in at the terminal are sent to target RTT.
- ``monitor rtt disable``: if RTT is disabled (default), characters typed in at the terminal are sent to the built-in serial port.

## GDB Commands

The following new gdb commands are available:

- ``monitor rtt``

	switch rtt on

- ``monitor rtt enable``

	switch rtt on

- ``monitor rtt disable``

	switch rtt off

- ``monitor rtt poll `` max_poll_ms min_poll_ms max_errs

	sets maximum time between polls, minimum time between polls, and the maximum number of
        errors before RTT disconnects from the target. Times in milliseconds. It is best if
        max_poll_ms/min_poll_ms is a power of two. As an example, if you wish to check for RTT
        output between once per second to eight times per second: ``monitor rtt poll 1000 125 10``.

- ``monitor rtt status``

	show status.

rtt|found|state
---|---|---
rtt: off|found: no|rtt inactive
rtt: on|found: no|searching for rtt control block
rtt: on|found: yes|rtt active
rtt: off|found: yes|corrupt rtt control block, or target memory access error

A status of `rtt: on found: no` indicates bmp is still searching for the rtt control block in
target ram, but has not found anything yet. A status of  `rtt: on found: yes` indicates the
control block has been found and rtt is active.

- ``monitor rtt channel``

	enables the first two output channels, and the first input channel. (default)

- ``monitor rtt channel number...``

	enables the given RTT channel numbers. Channels are numbers from 0 to 15, inclusive.
        Eg. ``monitor rtt channel 0 1 4`` to enable channels 0, 1, and 4.

- ``monitor rtt ram``

	rtt scans all of target memory for the rtt control block. (default)

- ``monitor rtt ram startaddress endaddress``

	limit rtt scan of target memory to address range given. Values in hex. Use if scanning memory hangs the debugger. E.g. to limit rtt scans to the first 8kbyte of ram, beginning at 0x20000000:

```
(gdb) mon rtt ram 0x20000000 0x20002000
(gdb) mon rtt status
rtt: off found: no ident: off halt: off channels: auto ram: 0x20000000 0x20002000
max poll ms: 256 min poll ms: 8 max errs: 10
```

If automatic detection fails, please take the linker map of your firmware, and search for a symbol that contains the word RTT somewhere at the beginning of ram. Look for a block with size a multiple of 24 decimal, word-aligned. For instance:

```
grep 2000 HelloWorld.ino.map | grep RTT
```

 For bmp to find the rtt control block, the rtt control block has to exist, be within the address range of `(gdb)info mem`, or if `mon rtt ram` has been specified, within the address range of `mon rtt ram`.

- ``monitor rtt ident string``

	sets RTT ident to *string*. If *string* contains a space, replace the space with an
        underscore _. Setting ident string is optional, RTT works fine without.

- ``monitor rtt ident``

	clears ident string. (default)

- ``monitor rtt cblock``

	shows rtt control block data, and which channels are enabled. This is an example
        control block:

```
(gdb) mon rtt cb
cbaddr: 0x2000071c
ch ena i/o buffer@      size   head   tail flag
 0   y out 0x200000ac   1024     14     14    0
 1   y out 0x00000000      0      0      0    0
 2   n out 0x00000000      0      0      0    0
 3   y in  0x2000009c     16      0      0    0
 4   n in  0x00000000      0      0      0    0
 5   n in  0x00000000      0      0      0    0
```

Channels are listed, one channel per line. The columns are: channel, enabled, input/output, buffer address, buffer size, head pointer, tail pointer, flag. Each channel is a circular buffer with head and tail pointer. The column 'flag' is the action taken when the buffer is full.

Channels the user wants to see are marked yes `y` in the column enabled `ena`. The user can
change which channels are shown with the `monitor rtt channel` command.

``monitor rtt enable`` forces searching the control block next time the program runs.

## Identifier String

It is possible to set an RTT identifier string.
As an example, if the RTT identifier is "IDENT STR":
```
$ gdb
(gdb) target extended-remote /dev/ttyBmpGdb
(gdb) monitor swdp_scan
(gdb) attach 1
(gdb) monitor rtt ident IDENT_STR
(gdb) monitor rtt
(gdb) run
^C
(gdb) monitor rtt status
rtt: on found: yes ident: "IDENT STR" halt: off channels: auto 0 1 3
max poll ms: 256 min poll ms: 8 max errs: 10
```
Note replacing space with underscore _ in *monitor rtt ident*.

Setting an identifier string is optional. RTT gives the same output at the same speed, with or
without specifying identifier string.

## Operating Systems

[Configuration](https://black-magic.org/getting-started.html) instructions for Windows, Linux and MacOS.

### Windows

After configuration, Black Magic Probe shows up in Windows as two _USB Serial (CDC)_ ports.

Connect arm-none-eabi-gdb, the gnu debugger for arm processors, to the lower numbered of the
two COM ports. Connect an ansi terminal emulator to the higher numbered of the two COM ports.

Sample gdb session:
```
(gdb) target extended-remote COM3
(gdb) monitor swdp_scan
(gdb) attach 1
(gdb) monitor rtt
(gdb) run
```

For COM port COM10 and higher, add the prefix `\\.\`, e.g.
```
target extended-remote \\.\COM10
```

Target RTT output will appear in the terminal, and what you type in the terminal will be sent
to the RTT input of the target.

### Linux

On Linux, install udev rules as described in the [driver
documentation](https://github.com/blackmagic-debug/blackmagic/blob/main/driver/README.md).
Disconnect and re-connect the BMP. Check the device shows up in /dev/ :
```
$ ls -l /dev/ttyBmp*
lrwxrwxrwx 1 root root 7 Dec 13 07:29 /dev/ttyBmpGdb -> ttyACM0
lrwxrwxrwx 1 root root 7 Dec 13 07:29 /dev/ttyBmpTarg -> ttyACM2
```
Connect terminal emulator to /dev/ttyBmpTarg and gdb to /dev/ttyBmpGdb .

In one window:
```
minicom -c on -D /dev/ttyBmpTarg
```
In another window :
```
gdb
(gdb) target extended-remote /dev/ttyBmpGdb
(gdb) monitor swdp_scan
(gdb) attach 1
(gdb) monitor rtt
(gdb) run
```

### MacOS

On MacOS the tty devices have different names than on linux. On connecting blackmagic to the
computer 4 devices are created, 2 'tty' and 2 'cu' devices. Gdb connects to the first cu device
(e.g.: `target extended-remote /dev/cu.usbmodemDDCEC9EC1`), while RTT is connected to the
second tty device (`minicom -c on -D /dev/tty.usbmodemDDCEC9EC3`). In full:

In one Terminal window, connect a terminal emulator to /dev/tty.usbmodemDDCEC9EC3 :

```
minicom -c on -D /dev/tty.usbmodemDDCEC9EC3
```
In another Terminal window, connect gdb to /dev/cu.usbmodemDDCEC9EC1 :
```
gdb
(gdb) target extended-remote /dev/cu.usbmodemDDCEC9EC1
(gdb) monitor swdp_scan
(gdb) attach 1
(gdb) monitor rtt
(gdb) run
```
RTT input/output is in the window running _minicom_.

## Notes

- Design goal was smallest, simplest implementation that has good practical use.

- RTT code size is ~3.5 kbytes.

- Because RTT is implemented as a serial port device, there is no need to write and maintain
  software for different host operating systems. A serial port works everywhere - Linux,
Windows and MacOS. You can even use an Android mobile phone as RTT terminal.

- Because polling occurs between debugger probe and target, the load on the host is small.
  There is no constant usb traffic, there are no real-time requirements on the host.

- RTT polling frequency is adaptive and goes up and down with RTT activity. Use *monitor rtt
  poll* to balance response speed and target load for your use.

- Detects RTT automatically, very convenient.

- When using RTT as a terminal, sending data from host to target, you may need to change local
  echo, carriage return and/or line feed settings in your terminal emulator.

- Architectures such as risc-v may not allow the debugger access to target memory while the
  target is running. As a workaround, on these architectures RTT briefly halts the target
during polling. If the target is halted during polling, `monitor rtt status` shows `halt: on`.

- Measured RTT speed.

| debugger                  | char/s |
| ------------------------- | ------ |
| bmp stm32f723 stlinkv3    | 49811  |
| bmp stm32f411 black pill  | 50073  |
| bmp stm32f103 blue pill   | 50142  |

This is the speed at which characters can be sent from target to debugger probe, in reasonable
circumstances. Test target is an stm32f103 blue pill running an [Arduino
sketch](https://github.com/koendv/Arduino-RTTStream/blob/main/examples/SpeedTest/SpeedTest.ino).
Default *monitor rtt poll* settings on debugger. Default RTT buffer size in target and
debugger. Overhead for printf() calls included.

## Build instructions

To compile with RTT support, add *ENABLE_RTT=1*.

Eg. for Black Magic Probe (native) hardware:
```
make clean
make ENABLE_RTT=1
```

Setting an ident string is optional. But if you wish, you can set the default RTT ident at
compile time.

Eg. for Black Magic Probe (native) hardware:
```
make clean
make ENABLE_RTT=1 "RTT_IDENT=IDENT\ STR"
```
Note the backslash \\ before the space.

When compiling for other targets like bluepill, blackpill or stlink, the `PROBE_HOST` variable
has to be also set appropriately, selecting the correct host target.

## Links
 - [OpenOCD](https://openocd.org/doc/html/General-Commands.html#Real-Time-Transfer-_0028RTT_0029)
 - [probe-rs](https://probe.rs/) and [rtt-target](https://github.com/mvirkkunen/rtt-target) for the _rust_ programming language.
 - [RTT Stream](https://github.com/koendv/Arduino-RTTStream) for Arduino on arm processors
