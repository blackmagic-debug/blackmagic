Compiling on windows

You can crosscompile blackmagic for windows with mingw or on windows
with cygwin. For compilation, headers for libftdi1 and libusb-1.0 are
needed. For running, libftdi1.dll and libusb-1.0.dll are needed and
the executable must be able to find them. Mingw on cygwin does not provide
a libftdi package yet.

To prepare libusb access to the ftdi device, run zadig https://zadig.akeo.ie/.
Choose WinUSB(libusb-1.0) for the BMP Ftdi device.

Running cygwin/blackmagic in a cygwin console, the program does not react
on ^C. In another console, run "ps ax" to find the WINPID of the process
and then "taskkill /F ?PID (WINPID)".
