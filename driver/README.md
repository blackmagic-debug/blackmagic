# Driver and access files for BMP

This directory contains a few different important files described in the sections below. These are:

* [99-blackmagic-plugdev.rules](#99-blackmagic-plugdevrules)
* [99-blackmagic-uucp.rules](#99-blackmagic-uucprules)
* [blackmagic.inf](#blackmagicinf)
* [blackmagic_upgrade.inf](#blackmagic_upgradeinf)

## udev rules installation

Depending on the specific distribution you can choose either the `plugdev` or `uucp` versions
of udev rules. For more information about the distributions and what the differences are refer
to the following sections.

Independent of which udev rule file is used, the selected file has to be copied into
`/etc/udev/rules.d` directory. The udev rules can then be reloaded with `sudo udevadm control
--reload-rules`. After that the Black Magic Probe can be connected to the system and the new
rules should apply to the connected device.

## 99-blackmagic-plugdev.rules

This file contains [udev](https://www.freedesktop.org/wiki/Software/systemd) rules for Linux
systems that use 'plugdev' as the access group for plug-in devices. This allows GDB, dfu-util,
and other utilities access to your Black Magic Probe hardware without needing `sudo` / root on
the computer you're trying to access the hardware from.

Distros that use 'plugdev' for this purpose include:

* Debian
* Ubuntu
* Linux Mint

## 99-blackmagic-uucp.rules

This file contains [udev](https://www.freedesktop.org/wiki/Software/systemd) rules for Linux
systems that use 'uucp' as the access group for plug-in devices. This allows GDB, dfu-util, and
other utilities access to your Black Magic Probe hardware without needing `sudo` / root on the
computer you're trying to access the hardware from.

Distros that use 'uucp' for this purpose include:

* Arch
* Manjaro

## Notes about udev rules

Our udev rules include use of the 'uaccess' tag which means either rules file should just work
on any modern distro that uses systemd. The specific user access group only matters when not
using systemd, or on older distros that predate the user access tag.

## blackmagic.inf

This is a windows driver "installation" (actually binding) information file which when
right-clicked and after clicking Install will name the serial ports Black Magic Probe provides
properly and provide them their proper vendor names.

## blackmagic_upgrade.inf

This is a windows driver "installation" (binding) information file which when right-clicked and
after clicking Install will bind suitable drivers and names to the DFU (firmware upgrade) and
Trace/Capture interfaces that Black Magic Probe provides + give them their proper vendor names.
