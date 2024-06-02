# Black Magic Probe

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
makes [SWO viewing as simple as connecting to a serial port](https://black-magic.org/usage/swo.html).

## Resources

* [Official website](https://black-magic.org/index.html)
* [Binary builds](https://github.com/blackmagic-debug/blackmagic/releases)

[![Discord](https://img.shields.io/discord/613131135903596547?logo=discord)](https://discord.gg/P7FYThy)

## Usage

### Black Magic Debug Firmware

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

### Black Magic Debug App

You can also build the Black Magic Debug suite as a PC program called Black Magic Debug App (BMDA).

This builds the same GDB server that is running on the Black Magic Probe.
While connection to the Black Magic Probe GDB server is via serial line,
connection to the Black Magic Debug App is via TCP port 2000 for the first
GDB server and higher for more invocations. Use "tar(get) ext(ented) :2000"
to connect.

Black Magic Debug App can talk to

* Black Magic Probe firmware probes via the USB-serial port
* ST-Link v2, v2.1 and v3 with recent firmware
* CMSIS-DAP compatible probes
* J-Link probes
* FTDI MPSSE based probes

When connected to a single BMP supported probe, starting `blackmagic` w/o any
arguments starts the server. When several BMP supported probes are connected,
their types, position and serial number is displayed and the program exits.
Add `-P (position)` to the next invocation to select one.
For the setup from the sample session above:

In another terminal:

```console
> blackmagic
Black Magic Debug App v1.9.2
 for Black Magic Probe, ST-Link v2 and v3, CMSIS-DAP, J-Link and FTDI (MPSSE)
Using 1d50:6018 8BB20A03 Black Magic Debug
 Black Magic Probe  v1.9.2
Listening on TCP: 2000
```

And in the GDB terminal:

```console
(gdb) tar ext :2000
Remote debugging using :2000
(gdb) mon s
...
```

Black Magic Debug App also provides for Flashing, reading and verification of a binary file,
by default starting at lowest flash address. The `-t` argument displays information about the
connected target. Use `-h`/`--help` to get a list of supported options.

#### OS specific remarks

On *BSD and macOS, you should use `/dev/cu.usbmodemXXX`. There are unresolved issues with trying to
use the `/dev/tty.usbmodemXXX` device node involving how it operates under the hood.

## Getting started

### Requirements

When developing this project, the following tools are required:

* [git](https://git-scm.com)
* [meson](https://mesonbuild.com/Getting-meson.html) (version 0.61 or greater)
* [ninja](https://ninja-build.org)

Additionally, depending on if you want to build/work on the firmware or Black Magic Debug App (BMDA), you also need:

#### Black Magic Debug Firmware

* [`arm-none-eabi-gcc`](https://developer.arm.com/tools-and-software/open-source-software/developer-tools/gnu-toolchain/downloads) (from ARM, self-built and distro-supplied is currently broken)

If you wish to use the older [gnu-rm](https://developer.arm.com/downloads/-/gnu-rm) ARM toolchain,
this is fine and works well.

If you have a toolchain from other sources and find problems, check if it's an issue with your toolchain first,
and if not then open an issue, or better, submit a pull request with a fix.

#### Black Magic Debug App

* [GCC](https://gcc.gnu.org) or Clang (Clang is not officially supported)

If you wish to enable support for 3rd party probes, and not only the official native hardware, you will also need:

* libusb1
* libftdi1
* libhidapi

### Building

The project is configured and built using the Meson build system, you will need to create a build directory
and then configure the build depending on what you want.

The project has a couple of build options you may configure, which affect the resulting binaries.
This is how you will configure the firmware for your respective probe, or enable certain features.

A non-exhaustive list of project options:

* `probe`: Hardware platform where the BMD firmware will run
* `targets`: Enabled debug targets
* `debug_output`: Enable debug output (for debugging the BMD stack, not debug targets)
* `rtt_support`: Enable RTT (Real Time Transfer) support

You may see all available project options and valid values under  `Project options` in the output of the `meson configure` command.

The following commands are ran from the root of your clone of the `blackmagic` repository:

```sh
cd /path/to/repository/blackmagic
```

#### Building Black Magic Debug Firmware

To build the firmware you need to configure the probe hardware the firmware will run on, as well as the cross-compilation toolchain to be used.

For convenience, a cross-file for each supported hardware probe is available which provides a sane default configuration for it.

The build configuration command for the native probe may look like:

```sh
meson setup build --cross-file cross-file/native.ini
```

Note that even if you are using the pre-configured cross-file, you may still override it's defaults with `-Doption=value` in the same configuration command, or later as highlighted in [Changing the build configuration](#changing-the-build-configuration).

Alternatively (for advanced users), if you wish to configure manually, for instance while writing support for a new probe, or a different toolchain, you can run:

```sh
meson setup build --cross-file cross-file/arm-none-eabi.ini -Dprobe=native -Dtargets=cortexm,stm
```

You now should have a `build` directory from where you can build the firmware, this is also where your binary files will appear.

The command `meson compile` will build the default targets, you may omit `-C build` if you run the command from within the `build` directory:

```sh
meson compile -C build
```

You should now see the resulting binaries in `build`, in this case:

* `blackmagic_native_firmware.bin`
* `blackmagic_native_firmware.elf`

These are the binary files you will use to flash to your probe.

##### region `rom' overflowed

It may happen, while working with non default configurations or the project's latest version from Git, that the firmware does not fit in the available space for the configured probe, this could look something like:

```console
arm-none-eabi/bin/ld: region `rom' overflowed by 4088 bytes
Memory region         Used Size  Region Size  %age Used
             rom:      135160 B       128 KB    103.12%
             ram:        5708 B        20 KB     27.87%
collect2: error: ld returned 1 exit status
```

This is not unexpected, as some hardware probe have limited space for firmware. You can get around it by disabling some features or targets support:

```sh
meson configure build -Dtargets=cortexm,stm -Drtt_support=false
```

#### Building Black Magic Debug App

The Black Magic Debug App (BMDA) is always built by default, even for firmware builds. So long as all its dependencies are found, you can find the executable under the `build` directory, named simply `blackmagic`.

If you wish to build only BMDA, you can set the hardware `probe` option to an empty string `-Dprobe=''`, this is the default value:

```sh
meson setup build
```

You now should have a `build` directory from where you can build the app, this is also where your executable will appear.

The command `meson compile` will build the default targets, you may omit `-C build` if you run the command from within the `build` directory:

```sh
meson compile -C build
```

You should now see the resulting executable in `build`:

* `blackmagic`

### Changing the build configuration

You may change the configuration at any time after configuring the project, if may also change the default options while configuring for the first time.

Changing options at configure time:

```sh
meson setup build -Ddebug_output=true
```

Changing options after configuration:

```sh
meson configure build -Dtargets=cortexm,stm
```

Alternatively, you can use the equivalent but more verbose `meson setup` command:

```sh
meson setup build --reconfigure -Dtargets=cortexm,stm
```

Keep in mind that when changing the configured probe, the other default options will not change, and you may end up with a configuration that does not make sense for it. In such cases it's best to use the *cross-file* for the probe, not just change the `probe` option. For this you will need to use the `meson setup` command again:

```sh
meson setup build --reconfigure --cross-file=cross-file/bluepill.ini
```

When changing options after configuration, you may omit the argument `build` if you are running the command from within the `build` directory.

You can have multiple build directories! So if you are regularly building firmware for multiple probes we would recommend keeping an individual build directory configured for each one.

If you are working with PowerShell you may have some issue while trying to configure some options like the enabled target list `-Dtargets=cortexm,stm`:

``` console
PS C:\...\blackmagic\build> meson configure build -Dtargets=cortexm,stm
ParserError:
Line | 1 | meson configure build -Dtargets=cortexm,stm
     | ~ | Missing argument in parameter list.
PS D:\...\blackmagic\build>
```

To get around this you may wrap the options with double quotes `"`, in this example:

```sh
meson configure build "-Dtargets=cortexm,stm"
```

### Working with an existing clone (used before the new meson build system was introduced)

If you are working with an existing clone of the project where you used the old `make` build system,
or simply initialized the submodules, you may encounter issues while trying the new Meson workflow,
this may look something like:

```console
fatal: Fetched in submodule path 'deps/libopencm3', but it did not contain bed4a785eecb6c9e77e7491e196565feb96c617b. Direct fetching of that commit failed.
```

To get around this try running the following commands:

```sh
cd deps/libopencm3
git remote set-url origin https://github.com/blackmagic-debug/libopencm3
cd ../..
git submodule update
```

Alternatively, the nuclear option
(**BEWARE, THIS WILL ERASE ANY CHANGES THAT HAVE NOT BEEN PUSHED UPSTREAM**)

```sh
git submodule deinit --force --all
meson subprojects purge --confirm
```

## Contributing and reporting issues

Take a look at out contribution guidelines in [CONTRIBUTING](CONTRIBUTING.md).
