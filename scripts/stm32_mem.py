#!/usr/bin/python
#
# stm32_mem.py: STM32 memory access using USB DFU class
# Copyright (C) 2011  Black Sphere Technologies 
# Written by Gareth McMullin <gareth@blacksphere.co.nz>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

from time import sleep
import struct
from sys import stdout, argv

import usb
import dfu

CMD_GETCOMMANDS =            0x00
CMD_SETADDRESSPOINTER =      0x21
CMD_ERASE =                  0x41

def stm32_erase(dev, addr):
	erase_cmd = struct.pack("<BL", CMD_ERASE, addr)
	dev.download(0, erase_cmd)
	while True:
		status = dev.get_status()
		if status.bState == dfu.STATE_DFU_DOWNLOAD_BUSY:
			sleep(status.bwPollTimeout / 1000.0)
		if status.bState == dfu.STATE_DFU_DOWNLOAD_IDLE:
			break

def stm32_write(dev, data):
	dev.download(2, data)
	while True:
		status = dev.get_status()
		if status.bState == dfu.STATE_DFU_DOWNLOAD_BUSY:
			sleep(status.bwPollTimeout / 1000.0)
		if status.bState == dfu.STATE_DFU_DOWNLOAD_IDLE:
			break
	
def stm32_manifest(dev):
	dev.download(0, "")
	while True:
		try:
			status = dev.get_status()
		except:
			return
		sleep(status.bwPollTimeout / 1000.0)
		if status.bState == dfu.STATE_DFU_MANIFEST:
			break

if __name__ == "__main__":
	print
	print "USB Device Firmware Upgrade - Host Utility -- version 1.1"
	print "Copyright (C) 2011  Black Sphere Technologies"
	print "License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>"
	print

	devs = dfu.finddevs()
	if not devs:
		print "No devices found!"
		exit(-1)

	for dev in devs:
		dfudev = dfu.dfu_device(*dev)
		man = dfudev.handle.getString(dfudev.dev.iManufacturer, 30)
		product = dfudev.handle.getString(dfudev.dev.iProduct, 30)
		if man == "Black Sphere Technologies": break
		if man == "STMicroelectronics": break

	print "Device %s: ID %04x:%04x %s - %s" % (dfudev.dev.filename, 
		dfudev.dev.idVendor, dfudev.dev.idProduct, man, product)

	try:
		state = dfudev.get_state()
	except:
		print "Failed to read device state! Assuming APP_IDLE"
		state = dfu.STATE_APP_IDLE
	if state == dfu.STATE_APP_IDLE:
		dfudev.detach()
		print "Run again to upgrade firmware."
		exit(0)
	
	dfudev.make_idle()

	bin = open(argv[1], "rb").read()

	addr = 0x8002000
	while bin:
		print ("Programming memory at 0x%08X\r" % addr),
		stdout.flush()
		stm32_erase(dfudev, addr)
		stm32_write(dfudev, bin[:1024])

		bin = bin[1024:]
		addr += 1024

	stm32_manifest(dfudev)

	print "\nAll operations complete!\n"
