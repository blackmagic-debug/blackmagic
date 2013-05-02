#!/usr/bin/env python

# hexprog.py: Python application to flash a target with an Intel hex file
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

import gdb
import struct
import time

# Microcode sequence to erase option bytes
stub_opterase = '\nH\x0bIA`\x0bIA`\tI\x81`\tI\x81`\x01iA\xf0 \x01\x01aA\xf0@\x01\x01a\xc4hO\xf0\x01\x064B\xfa\xd1\x00\xbe\x00 \x02@#\x01gE\xab\x89\xef\xcd'
# Microcode sequence to program option bytes
stub_optprog = '\tJ\nKS`\nKS`\x08K\x93`\x08K\x93`\x13iC\xf0\x10\x03\x13a\x01\x80\xd4hO\xf0\x01\x064B\xfa\xd1\x00\xbe\x00 \x02@#\x01gE\xab\x89\xef\xcd'

def flash_write_hex(target, hexfile, progress_cb=None):
	target.flash_probe()
	f = open(hexfile)
	addrhi = 0
	for line in f:
		if line[0] != ':': raise Exception("Error in hex file")
		reclen = int(line[1:3], 16)
		addrlo = int(line[3:7], 16)
		rectype = int(line[7:9], 16);
		if sum(ord(x) for x in gdb.unhexify(line[1:11+reclen*2])) & 0xff != 0:
			raise Exception("Checksum error in hex file") 
		if rectype == 0: # Data record
			addr = (addrhi << 16) + addrlo
			data = gdb.unhexify(line[9:9+reclen*2])
			target.flash_write_prepare(addr, data)
			pass
		elif rectype == 4: # High address record
			addrhi = int(line[9:13], 16)
			pass
		elif rectype == 5: # Entry record
			pass
		elif rectype == 1: # End of file record
			break 
		else:
			raise Exception("Invalid record in hex file")

	try:
		target.flash_commit(progress_cb)
	except:
		print "Flash write failed! Is device protected?\n"
		exit(-1)
	

if __name__ == "__main__":
	from serial import Serial, SerialException
	from sys import argv, platform, stdout
	from getopt import getopt

	if platform == "linux2":
		print ("\x1b\x5b\x48\x1b\x5b\x32\x4a") # clear terminal screen
	print("Black Magic Probe -- Target Production Programming Tool -- version 1.0")
	print "Copyright (C) 2011  Black Sphere Technologies"
	print "License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>"
	print("")

	dev = "COM1" if platform == "win32" else "/dev/ttyACM0"
	baud = 115200
	scan = "jtag_scan"
	targetno = 1
	unprot = False; prot = False

	try:
		opts, args = getopt(argv[1:], "sd:b:t:rR")
		for opt in opts:
			if opt[0] == "-s": scan = "swdp_scan"
			elif opt[0] == "-b": baud = int(opt[1])
			elif opt[0] == "-d": dev = opt[1]
			elif opt[0] == "-t": targetno = int(opt[1])
			elif opt[0] == "-r": unprot = True
			elif opt[0] == "-R": prot = True
			else: raise Exception()

		hexfile = args[0]
	except:
		print("Usage %s [-s] [-d <dev>] [-b <baudrate>] [-t <n>] <filename>" % argv[0])
		print("\t-s : Use SW-DP instead of JTAG-DP")
		print("\t-d : Use target on interface <dev> (default: %s)" % dev)
		print("\t-b : Set device baudrate (default: %d)" % baud)
		print("\t-t : Connect to target #n (default: %d)" % targetno)
		print("\t-r : Clear flash read protection before programming")
		print("\t-R : Enable flash read protection after programming (requires power-on reset)")
		print("")
		exit(-1)

	try:
		s = Serial(dev, baud, timeout=3)
		s.setDTR(1)
		while s.read(1024):
			pass

		target = gdb.Target(s)
		
	except SerialException, e:
		print("FATAL: Failed to open serial device!\n%s\n" % e[0])
		exit(-1)

	try:
		r = target.monitor("version")
		for s in r: print s,
	except SerialException, e:
		print("FATAL: Serial communication failure!\n%s\n" % e[0])
		exit(-1)
	except: pass

	print "Target device scan:"
	targetlist = None
	r = target.monitor(scan)
	for s in r: 
		print s,
	print

	r = target.monitor("targets")
	for s in r: 
		if s.startswith("No. Att Driver"): targetlist = []
		try:
			if type(targetlist) is list: 
				targetlist.append(int(s[:2]))
		except: pass

	#if not targetlist:
	#	print("FATAL: No usable targets found!\n")
	#	exit(-1)

	if targetlist and (targetno not in targetlist):
		print("WARNING: Selected target %d not available, using %d" % (targetno, targetlist[0]))
		targetno = targetlist[0]

	print("Attaching to target %d." % targetno)
	target.attach(targetno)
	time.sleep(0.1)

	if unprot:
		print("Removing device protection.")
		# Save option bytes for later
		#optbytes = struct.unpack("8H", target.read_mem(0x1FFFF800, 16))
		# Remove protection
		target.run_stub(stub_opterase, 0x20000000)
		target.run_stub(stub_optprog, 0x20000000, 0x1FFFF800, 0x5aa5)
		target.reset()
		time.sleep(0.1)

	for m in target.flash_probe():
		print("FLASH memory -- Offset: 0x%X  BlockSize:0x%X\n" % (m.offset, m.blocksize))

	def progress(percent):
		print ("Progress: %d%%\r" % percent),
		stdout.flush()

	print("Programming target")
	flash_write_hex(target, hexfile, progress)

	print("Resetting target")
	target.reset()

	if prot:
		print("Enabling device protection.")
		target.run_stub(stub_opterase, 0x20000000)
		target.run_stub(stub_optprog, 0x20000000, 0x1FFFF800, 0x00ff)
		target.reset()

	target.detach()

	print("\nAll operations complete!\n")

