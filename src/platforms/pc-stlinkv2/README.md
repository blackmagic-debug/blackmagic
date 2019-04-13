Stlink V2/3 with original STM firmware as Blackmagic Debug Probes

Recent STM Stlink firmware revision (V3 and V2 >= J32) expose nearly all
functionality that BMP needs. This branch implements blackmagic debug probe
for the STM Stlink as a proof of concept.
Use at your own risk, but report or better fix problems.

Run the resulting blackmagic_stlinkv2 executabel to start the gdb server

CrosscCompling for windows with mingw succeeds.

Drawback: JTAG does not work for chains with multiple devices.

This branch may get forced push. In case of problems:
- git reset --hard master
- git rebase
