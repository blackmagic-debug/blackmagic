ST-Link V2/3 with original STM firmware as Blackmagic Debug Probes

Recent STM ST-LINK  firmware revision (V3 and V2 >= J32) expose all
functionality that BMP needs. This platform implements blackmagic debug
probe for the STM ST-LINK.
Use at your own risk, but report or better fix problems.

Compile with "make PROBE_HOST=pc-stlinkv2"

Run the resulting blackmagic_stlinkv2 executable to start the gdb server.

You can also use on the command line alone, e.g
- "blackmagic_stlinkv2 -t" to scan and display the results of the scan
- "blackmagic_stlinkv2 <file.bin>" to flash <file.bin> at 0x08000000
- "blackmagic_stlinkv2 -h" for more options

Cross-compling for windows with mingw succeeds.

Drawback:
- JTAG does not work for chains with multiple devices.
- ST-LINKV3 seem to only work on STM32 devices.
- St-LINKV3 needs connect under reset on more devices than V2

ToDo:
- Implement an SWO server