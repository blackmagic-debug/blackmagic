# Black Magic Debug

The Black Magic Debug suite is a self-contained debugger for microcontrollers which needs no config
files and auto-detects plus configures the connected targets. It is designed to be fast and easy to use.

The project can be built as either firmware for
[Black Magic Probe](https://1bitsquared.com/products/black-magic-probe) -
a debugger-in-a-dongle that provides multi-voltage debug with no other external tools than GDB required -
or as Black Magic Debug App (BMDA) which is the project built for the host machine, more details below.

The project allows debugging of devices connected over JTAG or SWD, and via the companion tool
[bmpflash](https://github.com/blackmagic-debug/bmpflash) the programming of SPI Flash devices.
This includes support for ARM and RISC-V devices, the complete list can be found on the website.

[![Discord](https://img.shields.io/discord/613131135903596547?logo=discord)](https://discord.gg/P7FYThy)
[![Current release](https://img.shields.io/github/v/release/blackmagic-debug/blackmagic.svg?logo=github)](https://github.com/blackmagic-debug/blackmagic/releases)
[![CI flow status](https://github.com/blackmagic-debug/blackmagic/actions/workflows/build-and-upload.yml/badge.svg)](https://github.com/blackmagic-debug/blackmagic/actions/workflows/build-and-upload.yml)

Table of contents:

* [Resources](#resources)
* [Usage](#usage)
* [Build quick-start](#getting-started)
* [Contribution information](#contributing-and-reporting-issues)

## Resources

* [Official website](https://black-magic.org/index.html)
* [Binary builds](https://github.com/blackmagic-debug/blackmagic/releases)

## Usage

There is a more detailed [getting started guide](https://black-magic.org/getting-started.html) on the website,
however below is a brief guide for both the firmware and BMDA.

### Black Magic Debug Firmware

When built as firmware and put on a probe, the project is used like as follows:

```console
> arm-none-eabi-gdb gpio.elf
...<GDB Copyright message>
(gdb) tar ext /dev/ttyBmpGdb
Remote debugging using /dev/ttyBmpGdb
(gdb) mon a
Target voltage: 2.94V
Available Targets:
No. Att Driver
 1      STM32F40x M4
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
(gdb)
```

Note: this presumes on Linux that you have installed the project's udev rules.

### Black Magic Debug App

When built as BMDA, usage differs a bit - principally as instead of using a virtual serial interface,
BMDA makes the GDB server available over TCP/IP on a port that it will print to its console (typically
2000 though). This means replacing `tar ext /dev/ttyBmpGdb` in the firmware example, with `tar ext :2000`.

To bring the GDB server up, you will need a probe it can talk to, the BMDA binary, and an extra console to
the one you want to run GDB from. Verbosity of BMDA can be increased with the `-v` option such as running
`blackmagic -v 5` which sets the server to emit INFO and TARGET diagnostic level information in addition to
ERROR and WARNING output.

Black Magic Debug App can talk to:

* Black Magic Probe firmware probes via the USB-serial port
* ST-Link v2, v2.1 and v3 with recent firmware
* CMSIS-DAP compatible probes
* J-Link probes
* FTDI MPSSE based probes

When connected to a single BMDA-supported probe, starting `blackmagic` w/o any arguments starts the server.
When several BMDA-supported probes are connected, their types, serial numbers and version information is
displayed and the program exits. Add `-s <serial>` to the next invocation to select one.

For the setup from the sample session above:

In another terminal:

```console
> blackmagic
Black Magic Debug App v1.10.2
 for Black Magic Probe, ST-Link v2 and v3, CMSIS-DAP, J-Link and FTDI (MPSSE)
Using 1d50:6018 8BB20A03 Black Magic Debug
 Black Magic Probe v1.10.2
Setting V6ONLY to off for dual stack listening.
Listening on TCP: 2000
```

And in the GDB terminal:

```console
...
(gdb) tar ext :2000
Remote debugging using :2000
(gdb) mon a
...
```

Black Magic Debug App also provides for Flashing, reading and verification of a binary file,
by default starting at lowest Flash address. The `-t` argument displays information about the
connected target. Use `-h`/`--help` to get a list of supported options.

#### OS specific remarks

On *BSD and macOS, you should use `/dev/cu.usbmodemXXX1`. There are unresolved issues with trying to
use the `/dev/tty.usbmodemXXX1` device node involving how it operates under the hood.

## Getting started

### Requirements

When developing this project, the following tools are required:

* [git](https://git-scm.com)
* [meson](https://mesonbuild.com/Getting-meson.html) (version 0.63 or greater)
* [ninja](https://ninja-build.org)

Additionally, depending on if you want to build/work on the firmware or Black Magic Debug App (BMDA), you also need:

#### Black Magic Debug Firmware Requirements

* [`arm-none-eabi-gcc`](https://developer.arm.com/tools-and-software/open-source-software/developer-tools/gnu-toolchain/downloads) (from ARM, self-built and distro-supplied is currently broken)

The project is tested to work on the 12.2.Rel1 and 13.2.Rel1 releases. Newer releases may work but are not guaranteed.
The old `gnu-rm` compiler series is no longer supported as the code generation performs way too badly.

If you have a toolchain from other sources and find problems, check if it's an issue with your toolchain first,
and if not then open an issue, or better, submit a pull request with a fix.

#### Black Magic Debug App Requirements

* [GCC](https://gcc.gnu.org) or Clang
* pkg-config
* libusb1
* libhidapi

On Linux and macOS (+ other non-Windows platforms):

* libftdi1

If you do not install the subsequent dependencies libusb1, libhidapi, and libftdi1, then the build system will
do source builds of the dependencies using the information in `deps/` that will be statically linked into BMDA.

### Building

The project is configured and built using the Meson build system, you will need to create a build
directory using `meson setup`, and configure the build depending on what you want.

The project has a couple of build options you may configure, which affect the resulting binaries.
This is how you will configure the firmware for your respective probe, or enable certain features.

A non-exhaustive list of project options:

* `probe`: Hardware platform where the BMD firmware will run
* `targets`: Targets and architectures enabled for debug and Flashing
* `debug_output`: Enable debug output (for debugging the BMD stack, not debug targets)
* `rtt_support`: Enable RTT (Real Time Transfer) support

You may see all available project options and valid values under `Project options` in the output
of the `meson configure` command.

The following commands are ran from the root of your clone of the `blackmagic` repository:

```sh
cd /path/to/repository/blackmagic
```

#### Building the firmware

To build the firmware you need to configure the probe hardware the firmware will run on, as well as the
cross-compilation toolchain to be used.

For convenience, a cross-file for each supported hardware probe is available which provides a sane default
configuration for it.

The build configuration command for the native probe may look like:

```sh
meson setup build --cross-file cross-file/native.ini
```

Note that even if you are using the pre-configured cross-file, you may still override it's defaults with
`-Doption=value` in the same configuration command, or later as highlighted in
[Changing the build configuration](#changing-the-build-configuration).

Alternatively (for advanced users), if you wish to configure manually, for instance while writing support
for a new probe, or a different toolchain, you can run something similar to this:

```sh
meson setup build --cross-file cross-file/arm-none-eabi.ini -Dprobe=native -Dtargets=cortexm,stm
```

After following one of these two paths, you now should have a `build` directory from where you can build
the firmware. This is also where your binary files will appear.

The command `meson compile` will build the default targets, you may omit `-C build` if you run the command
from within the `build` directory:

```sh
meson compile -C build
```

You should now see the resulting binaries in `build`, in this case:

* `blackmagic_native_firmware.bin`
* `blackmagic_native_firmware.elf`

These are the binary files you will use to flash to your probe.

##### region `rom' overflowed

It may happen, while working with non default configurations or the project's latest version from Git,
that the firmware does not fit in the available space for the configured probe, this could look something like:

```console
arm-none-eabi/bin/ld: region `rom' overflowed by 4088 bytes
Memory region         Used Size  Region Size  %age Used
             rom:      135160 B       128 KB    103.12%
             ram:        5708 B        20 KB     27.87%
collect2: error: ld returned 1 exit status
```

This is not unexpected, as some hardware probe have limited space for firmware. You can get around it by
disabling some features or targets support:

```sh
meson configure build -Dtargets=cortexm,stm -Drtt_support=false
```

#### Building Black Magic Debug App

The Black Magic Debug App (BMDA) is always built by default, even for firmware builds. So long as all its
dependencies are found, you can find the executable under the `build` directory, named simply `blackmagic`.

If you wish to build only BMDA, you can set the hardware `probe` option to an empty string `-Dprobe=''`,
this is the default value:

```sh
meson setup build
```

You now should have a `build` directory from where you can build the app, this is also where your executable
will appear.

The command `meson compile` will build the default targets, you may omit `-C build` if you run the command
from within the `build` directory:

```sh
meson compile -C build
```

You should now see the resulting executable in `build`:

* `blackmagic`

### Changing the build configuration

You may change the configuration at any time after configuring the project, if may also change the default
options while configuring for the first time.

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

NB: Since about Meson v1.2.0, using this second form is the only way to change the active targets list. This is
a known issue in Meson which has yet to be addressed in a release.

Keep in mind that when changing the configured probe, the other default options will not change, and you may end
up with a configuration that does not make sense for it. In such cases it's best to use the *cross-file* for the
probe, not just change the `probe` option. For this you will need to use the `meson setup` command again:

```sh
meson setup build --reconfigure --cross-file=cross-file/bluepill.ini
```

When changing options after configuration, you may omit the argument `build` if you are running the command from
within the `build` directory.

You can have multiple build directories! So if you are regularly building firmware for multiple probes we would
recommend keeping an individual build directory configured for each one.

If you are working with PowerShell you may have some issue while trying to configure some options like the enabled
target list `-Dtargets=cortexm,stm`:

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

### Working with an existing clone

If you are working with an existing clone of the project, you may encounter a build issue with libopencm3 or
one of the other dependencies in `deps/` - if this happens, you have one of two recourses:

The first is to ensure the dependency is fully up to date (Meson manages them as Git clones but due to limitations
is unaware of when the remote is newer than the local when using `revision = head` in the .wrap files) with the
remote by doing a full fetch cycle (`git fetch --all --prune`, `git pull --tags --force`) within the dependency
clone, and making sure the latest version is checked out (`git pull`). This is labour intensive but good if
you have working copy changes

The second, more nuclear option, is to blow away the clone of the dependency, eg `rm -rf deps/libopencm3` and rebuild.
Meson will automatically detect this and re-clone the dependency at the version indicated by the .wrap files.

## Contributing and reporting issues

Take a look at out contribution guidelines in [CONTRIBUTING](CONTRIBUTING.md).
