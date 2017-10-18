#!/usr/bin/env python
#
# stm32_mem.py: STM32 memory access using USB DFU class
# Copyright (C) 2011  Black Sphere Technologies 
# Copyright (C) 2017  Uwe Bonnes (bon@elektron.ikp.physik.tu-darmstadt.de)
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

import argparse
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

def stm32_set_address(dev, addr):
	set_address_cmd = struct.pack("<BL", CMD_SETADDRESSPOINTER, addr)
	dev.download(0, set_address_cmd)
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

def stm32_read(dev):
	data = dev.upload(2, 1024)
	while True:
		status = dev.get_status()
		if status.bState == dfu.STATE_DFU_DOWNLOAD_BUSY:
			sleep(status.bwPollTimeout / 1000.0)
		if status.bState == dfu.STATE_DFU_UPLOAD_IDLE:
			break
	return data

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
def stm32_scan(args, test):
	devs = dfu.finddevs()
	bmp = 0
	if not devs:
                if test == True:
                        return
		print "No DFU devices found!"
		exit(-1)

	for dev in devs:
		dfudev = dfu.dfu_device(*dev)
		man = dfudev.handle.getString(dfudev.dev.iManufacturer, 30)
		if man == "Black Sphere Technologies": bmp = bmp + 1
	if bmp == 0 :
                if test == True:
                        return
		print "No compatible device found\n"
		exit(-1)
	if bmp > 1 and not args.serial_target :
                if test == True:
                        return
		print "Found multiple devices:\n"
		for dev in devs:
			dfudev = dfu.dfu_device(*dev)
			man = dfudev.handle.getString(dfudev.dev.iManufacturer, 30)
			product = dfudev.handle.getString(dfudev.dev.iProduct, 96)
			serial_no = dfudev.handle.getString(dfudev.dev.iSerialNumber, 30)
			print "Device ID:\t %04x:%04x" % (dfudev.dev.idVendor, dfudev.dev.idProduct)
			print "Manufacturer:\t %s" % man
			print "Product:\t %s" % product
			print "Serial:\t\t %s\n" % serial_no
		print "Select device with serial number!"
		exit (-1)

	for dev in devs:
		dfudev = dfu.dfu_device(*dev)
		man = dfudev.handle.getString(dfudev.dev.iManufacturer, 30)
		product = dfudev.handle.getString(dfudev.dev.iProduct, 96)
		serial_no = dfudev.handle.getString(dfudev.dev.iSerialNumber, 30)
		if args.serial_target:
			if man == "Black Sphere Technologies" and serial_no ==	args.serial_target: break
		else:
			if man == "Black Sphere Technologies": break
	print "Device ID:\t %04x:%04x" % (dfudev.dev.idVendor, dfudev.dev.idProduct)
	print "Manufacturer:\t %s" % man
	print "Product:\t %s" % product
	print "Serial:\t\t %s" % serial_no
	if args.serial_target and serial_no !=	args.serial_target:
		print "Serial number doesn't match!\n"
		exit(-2)
	return dfudev

if __name__ == "__main__":
	print
	print "USB Device Firmware Upgrade - Host Utility -- version 1.2"
	print "Copyright (C) 2011  Black Sphere Technologies"
	print "License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>"
	print

	parser = argparse.ArgumentParser()
	parser.add_argument("progfile", help="Binary file to program")
	parser.add_argument("-s", "--serial_target", help="Match Serial Number")
	parser.add_argument("-a", "--address", help="Start address for firmware")
	parser.add_argument("-m", "--manifest", help="Start application, if in DFU mode", action='store_true')
	args = parser.parse_args()
	dfudev = stm32_scan(args, False)
	try:
		state = dfudev.get_state()
	except:
		if args.manifest : exit(0)
		print "Failed to read device state! Assuming APP_IDLE"
		state = dfu.STATE_APP_IDLE
	if state == dfu.STATE_APP_IDLE:
		dfudev.detach()
		dfudev.release()
		print "Invoking DFU Device"
		timeout = 0
		while True :
			sleep(0.5)
			timeout = timeout + 0.5
			dfudev = stm32_scan(args, True)
			if dfudev: break
			if timeout > 5 :
				print "Error: DFU device did not appear"
				exit(-1)
	if args.manifest :
		stm32_manifest(dfudev)
		print "Invoking Application Device"
		exit(0)
	dfudev.make_idle()
	file = open(args.progfile, "rb")
	bin = file.read()

	product = dfudev.handle.getString(dfudev.dev.iProduct, 64)
	if args.address :
		start = int(args.address, 0)
	else :
		if "F4" in product:
			start = 0x8004000
		else:
			start = 0x8002000
	addr = start
	while bin:
		print ("Programming memory at 0x%08X\r" % addr),
		stdout.flush()
		try:
# STM DFU bootloader erases always.
# BPM Bootloader only erases once per sector
# To support the STM DFU bootloader, the interface descriptor must
# get evaluated and erase called only once per sector!
			stm32_erase(dfudev, addr)
		except:
			print "\nErase Timed out\n"
			break
		try:
			stm32_set_address(dfudev, addr)
		except:
			print "\nSet Address Timed out\n"
			break
		stm32_write(dfudev, bin[:1024])
		bin = bin[1024:]
		addr += 1024
	file.seek(0)
	bin = file.read()
	len = len(bin)
	addr = start
        print
        while bin:
		try:
			stm32_set_address(dfudev, addr)
			data = stm32_read(dfudev)
		except:
# Abort silent if bootloader does not support upload
			break
		print ("Verifying memory at   0x%08X\r" % addr),
                stdout.flush()
		if len > 1024 :
			size = 1024
		else :
			size = len
		if bin[:size] != bytearray(data[:size]) :
			print ("\nMitmatch in block at	0x%08X" % addr)
			break;
		bin = bin[1024:]
		addr += 1024
		len -= 1024
		if len <= 0 :
			print "\nVerified!"
	stm32_manifest(dfudev)

	print "All operations complete!\n"
