#!/usr/bin/env python
#
# dfu.py: Access USB DFU class devices
# Copyright (C) 2009  Black Sphere Technologies 
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

 
import usb

DFU_DETACH_TIMEOUT = 1000

# DFU Requests 
DFU_DETACH = 0x00
DFU_DNLOAD = 0x01
DFU_UPLOAD = 0x02
DFU_GETSTATUS = 0x03
DFU_CLRSTATUS = 0x04
DFU_GETSTATE = 0x05
DFU_ABORT = 0x06

# DFU States
STATE_APP_IDLE =                 0x00
STATE_APP_DETACH =               0x01
STATE_DFU_IDLE =                 0x02
STATE_DFU_DOWNLOAD_SYNC =        0x03
STATE_DFU_DOWNLOAD_BUSY =        0x04
STATE_DFU_DOWNLOAD_IDLE =        0x05
STATE_DFU_MANIFEST_SYNC =        0x06
STATE_DFU_MANIFEST =             0x07
STATE_DFU_MANIFEST_WAIT_RESET =  0x08
STATE_DFU_UPLOAD_IDLE =          0x09
STATE_DFU_ERROR =                0x0a
DFU_STATUS_OK =                  0x00

# DFU Status cides
DFU_STATUS_ERROR_TARGET =        0x01
DFU_STATUS_ERROR_FILE =          0x02
DFU_STATUS_ERROR_WRITE =         0x03
DFU_STATUS_ERROR_ERASE =         0x04
DFU_STATUS_ERROR_CHECK_ERASED =  0x05
DFU_STATUS_ERROR_PROG =          0x06
DFU_STATUS_ERROR_VERIFY =        0x07
DFU_STATUS_ERROR_ADDRESS =       0x08
DFU_STATUS_ERROR_NOTDONE =       0x09
DFU_STATUS_ERROR_FIRMWARE =      0x0a
DFU_STATUS_ERROR_VENDOR =        0x0b
DFU_STATUS_ERROR_USBR =          0x0c
DFU_STATUS_ERROR_POR =           0x0d
DFU_STATUS_ERROR_UNKNOWN =       0x0e
DFU_STATUS_ERROR_STALLEDPKT =    0x0f

class dfu_status(object):
	def __init__(self, buf):
		self.bStatus = buf[0]
		self.bwPollTimeout = buf[1] + (buf[2]<<8) + (buf[3]<<16)
		self.bState = buf[4]
		self.iString = buf[5]


class dfu_device(object):
	def __init__(self, dev, conf, iface):
		self.dev = dev
		self.conf = conf
		self.iface = iface
		self.handle = self.dev.open()
		try:
			self.handle.setConfiguration(conf)
		except: pass
		self.handle.claimInterface(iface.interfaceNumber)
		if type(self.iface) is usb.Interface:
			self.index = self.iface.interfaceNumber
		else:	self.index = self.iface

	def detach(self, wTimeout=255):
		self.handle.controlMsg(usb.ENDPOINT_OUT | usb.TYPE_CLASS | 
			usb.RECIP_INTERFACE, DFU_DETACH, 
			None, value=wTimeout, index=self.index)

	def download(self, wBlockNum, data):
		self.handle.controlMsg(usb.ENDPOINT_OUT | usb.TYPE_CLASS |
			usb.RECIP_INTERFACE, DFU_DNLOAD,
			data, value=wBlockNum, index=self.index) 

	def upload(self, wBlockNum, length):
		return self.handle.controlMsg(usb.ENDPOINT_IN | 
			usb.TYPE_CLASS | usb.RECIP_INTERFACE, DFU_UPLOAD,
			length, value=wBlockNum, index=self.index) 

	def get_status(self):
		buf = self.handle.controlMsg(usb.ENDPOINT_IN | 
			usb.TYPE_CLASS | usb.RECIP_INTERFACE, DFU_GETSTATUS,
			6, index=self.index) 
		return dfu_status(buf)
		
	def clear_status(self):
		self.handle.controlMsg(usb.ENDPOINT_OUT | usb.TYPE_CLASS | 
			usb.RECIP_INTERFACE, DFU_CLRSTATUS, 
			"", index=0)

	def get_state(self):
		buf = self.handle.controlMsg(usb.ENDPOINT_IN | 
			usb.TYPE_CLASS | usb.RECIP_INTERFACE, DFU_GETSTATE,
			1, index=self.index) 
		return buf[0]

	def abort(self):
		self.handle.controlMsg(usb.ENDPOINT_OUT | usb.TYPE_CLASS | 
			usb.RECIP_INTERFACE, DFU_ABORT, 
			None, index=self.index)


	def make_idle(self):
		retries = 3
		while retries:
			try:
				status = self.get_status()
			except:
				self.clear_status()
				continue

			retries -= 1

			if status.bState == STATE_DFU_IDLE:
				return True

			if ((status.bState == STATE_DFU_DOWNLOAD_SYNC) or 
			    (status.bState == STATE_DFU_DOWNLOAD_IDLE) or
			    (status.bState == STATE_DFU_MANIFEST_SYNC) or
			    (status.bState == STATE_DFU_UPLOAD_IDLE) or
			    (status.bState == STATE_DFU_DOWNLOAD_BUSY) or
			    (status.bState == STATE_DFU_MANIFEST)):
				self.abort()
				continue

			if status.bState == STATE_DFU_ERROR:
				self.clear_status()
				continue

			if status.bState == STATE_APP_IDLE:
				self.detach(DFU_DETACH_TIMEOUT)
				continue

			if ((status.bState == STATE_APP_DETACH) or
			    (status.bState == STATE_DFU_MANIFEST_WAIT_RESET)):
                		usb.reset(self.handle)
				return False

		raise Exception

def finddevs():
	devs = []
	for bus in usb.busses():
		for dev in bus.devices:
			for conf in dev.configurations:
				for ifaces in conf.interfaces:
					for iface in ifaces:
						if ((iface.interfaceClass == 0xFE) and
						    (iface.interfaceSubClass == 0x01)):
							devs.append((dev, conf, iface))
	return devs


if __name__ == "__main__":
	devs = finddevs()
	if not devs:
		print "No devices found!"
		exit(-1)

	for dfu in devs:
		handle = dfu[0].open()
		man = handle.getString(dfu[0].iManufacturer, 30)
		product = handle.getString(dfu[0].iProduct, 30)
		print "Device %s: ID %04x:%04x %s - %s" % (dfu[0].filename, 
			dfu[0].idVendor, dfu[0].idProduct, man, product)
		print "%r, %r" % (dfu[1], dfu[2])



