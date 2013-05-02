#!/usr/bin/env python
#
# bootprog.py: STM32 SystemMemory Production Programmer -- version 1.1
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

import serial
import struct
from time import sleep

class stm32_boot:
	def __init__(self, port, baud=115200):
		self.serial = serial.Serial(port, baud, 8, 'E', 1, 
				timeout=1)

		# Turn on target device in SystemMemory boot mode
		self.serial.setDTR(1)
		sleep(0.1);

		self._sync()

	def _sync(self):
		# Send sync byte
		#print "sending sync byte"
		self.serial.write("\x7F")
		self._checkack()

	def _sendcmd(self, cmd):
		if type(cmd) == int:
			cmd = chr(cmd)
		cmd += chr(ord(cmd) ^ 0xff)
		#print "sendcmd:", repr(cmd)
		self.serial.write(cmd)

	def _send(self, data):
		csum = 0
		for c in data: csum ^= ord(c)
		data = data + chr(csum)
		#print "sending:", repr(data)
		self.serial.write(data)

	def _checkack(self):
		ACK = "\x79"
		b = self.serial.read(1)
		if b != ACK: raise Exception("Invalid ack: %r" % b)
		#print "got ack!"



	def get(self):
		self._sendcmd("\x00")
		self._checkack()
		num = ord(self.serial.read(1))
		data = self.serial.read(num+1)
		self._checkack()
		return data

	def eraseall(self):
		# Send erase cmd
		self._sendcmd("\x43")
		self._checkack()
		# Global erase
		self._sendcmd("\xff")
		self._checkack()

	def read(self, addr, len):
		# Send read cmd
		self._sendcmd("\x11")
		self._checkack()
		# Send address
		self._send(struct.pack(">L", addr))
		self._checkack()
		# Send length
		self._sendcmd(chr(len-1))
		self._checkack()
		return self.serial.read(len)

	def write(self, addr, data):
		# Send write cmd
		self._sendcmd("\x31")
		self._checkack()
		# Send address
		self._send(struct.pack(">L", addr))
		self._checkack()
		# Send data
		self._send(chr(len(data)-1) + data)
		self._checkack()
	

	def write_protect(self, sectors):
		# Send WP cmd
		self._sendcmd("\x63")
		self._checkack()
		# Send sector list
		self._send(chr(len(sectors)-1) + "".join(chr(i) for i in sectors))
		self._checkack()
		# Resync after system reset
		self._sync()

	def write_unprotect(self):
		self._sendcmd("\x73")
		self._checkack()
		self._checkack()
		self._sync()

	def read_protect(self):
		self._sendcmd("\x82")
		self._checkack()
		self._checkack()
		self._sync()

	def read_unprotect(self):
		self._sendcmd("\x92")
		self._checkack()
		self._checkack()
		self._sync()


if __name__ == "__main__":
	from sys import stdout, argv, platform
	from getopt import getopt

	if platform == "linux2":
		print  "\x1b\x5b\x48\x1b\x5b\x32\x4a" # clear terminal screen
	print "STM32 SystemMemory Production Programmer -- version 1.1"
	print "Copyright (C) 2011  Black Sphere Technologies"
	print "License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>"
	print

	dev = "COM1" if platform == "win32" else "/dev/ttyUSB0"
	baud = 115200
	addr = 0x8000000
	try:
		opts, args = getopt(argv[1:], "b:d:a:")
		for opt in opts:
			if opt[0] == "-b": baud = int(opt[1])
			elif opt[0] == "-d": dev = opt[1]
			else: raise Exception()

		progfile = args[0]
	except:
		print "Usage %s [-d <dev>] [-b <baudrate>] [-a <address>] <filename>" % argv[0]
		print "\t-d : Use target on interface <dev> (default: %s)" % dev
		print "\t-b : Set device baudrate (default: %d)" % baud
		print "\t-a : Set programming address (default: 0x%X)" % addr
		print
		exit(-1)

	prog = open(progfile, "rb").read()
	boot = stm32_boot(dev, baud)

	cmds = boot.get()
	print "Target bootloader version: %d.%d\n" % (ord(cmds[0]) >> 4, ord(cmds[0]) % 0xf)

	print "Removing device protection..."
	boot.read_unprotect()
	boot.write_unprotect()
	print "Erasing target device..."
	boot.eraseall()
	addr = 0x8000000
	while prog:
		print ("Programming address 0x%08X..0x%08X...\r" % (addr, addr + min(len(prog), 255))),
		stdout.flush();
		boot.write(addr, prog[:256])
		addr += 256
		prog = prog[256:]

	print
	print "Enabling device protection..."
	boot.write_protect(range(0,2))
	#boot.read_protect()

	print "All operations completed."
	print


