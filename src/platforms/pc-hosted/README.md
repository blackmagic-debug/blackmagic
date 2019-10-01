PC Hosted variant

THIS IS INCOMPLETE - ONLY SUPPORTS SWD AT THE MOMENT

This variant will use any BMP probe with recent firmware as a remote
actuator, with the actual probe code running on the PC. The BMP itself
is 'dumb' and doesn't do anything (although any secondary serial port
remains available).

To use it, compile for the pc-hosted target and then connect to your normal
BMP GDB port;

src/blackmagic -s /dev/ttyACM0

...you can then connect your gdb session to localhost:2000 for all your
debugging goodness;

$arm-eabi-none-gdb
(gdb) monitor swdp_scan
Target voltage: not supported
Available Targets:
No. Att Driver
 1      STM32F1 medium density M3/M4
(gdb) attach 1
Attaching to program: Builds/blackmagic/src/blackmagic, Remote target
0x08001978 in ?? ()
(gdb) file src/blackmagic
A program is being debugged already.
Are you sure you want to change the file? (y or n) y
Load new symbol table from "src/blackmagic"? (y or n) y
Reading symbols from src/blackmagic...
(gdb) load
Loading section .text, size 0x1201c lma 0x8002000
Loading section .data, size 0xd8 lma 0x801401c
Start address 0x800d9fc, load size 73972
Transfer rate: 2 KB/sec, 960 bytes/write.
(gdb)

...note that the speed of the probe in this way is about 10 times less than
running native. This build is intended for debug and development only.