#!/usr/bin/env python
#
# gdb.py: Python module for low level GDB protocol implementation
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


# Used to parse XML memory map from target
from xml.dom.minidom import parseString
import struct
import time

def hexify(s):
	"""Convert a binary string into hex representation"""
	ret = ''
	for c in s:
		ret += "%02X" % ord(c)
	return ret

def unhexify(s):
	"""Convert a hex string into binary representation"""
	ret = ''
	for i in range(0, len(s), 2):
		ret += chr(int(s[i:i+2], 16))
	return ret

class FakeSocket:
	"""Emulate socket functions send and recv on a file object"""
	def __init__(self, file):
		self.file = file
	
	def send(self, data):
		self.file.write(data)

	def recv(self, bufsize):
		return self.file.read(bufsize)

class Target:
	def __init__(self, sock):
		if "send" in dir(sock):
			self.sock = sock
		else:	
			self.sock = FakeSocket(sock)

	def getpacket(self):
		"""Return the first correctly received packet from GDB target"""
		while True:
			while self.sock.recv(1) != '$': pass
			csum = 0
			packet = ''

			while True:
				c = self.sock.recv(1)
				if c == '#': break

				if c == '$':
					packet = ''
					csum = 0
					continue

				if c == '}':
					c = self.sock.recv(1)
					csum += ord(c) + ord('}')
					packet += chr(ord(c) ^ 0x20)
					continue	

				packet += c
				csum += ord(c)

			if (csum & 0xFF) == int(self.sock.recv(2),16): break

			self.sock.send('-')
		
		self.sock.send('+')
		return packet


	def putpacket(self, packet):
		"""Send packet to GDB target and wait for acknowledge"""
		while True:
			self.sock.send('$')
			csum = 0
			for c in packet:
				if (c == '$') or (c == '#') or (c == '}'):
					self.sock.send('}')
					self.sock.send(chr(ord(c) ^ 0x20))
					csum += (ord(c) ^ 0x20) + ord('}')
				else:
					self.sock.send(c)
					csum += ord(c)
			self.sock.send('#')
			self.sock.send("%02X" % (csum & 0xFF))
			if self.sock.recv(1) == '+': break

	def monitor(self, cmd):
		"""Send gdb "monitor" command to target"""
		ret = []
		self.putpacket("qRcmd," + hexify(cmd))

		while True:
			s = self.getpacket()
			if s == '': return None
			if s == 'OK': return ret
			if s.startswith('O'): ret.append(unhexify(s[1:]))
			else:
				raise Exception('Invalid GDB stub response')

	def attach(self, pid):
		"""Attach to target process (gdb "attach" command)"""
		self.putpacket("vAttach;%08X" % pid)
		reply = self.getpacket()
		if (len(reply) == 0) or (reply[0] == 'E'): 
			raise Exception('Failed to attach to remote pid %d' % pid)

	def detach(self):
		"""Detach from target process (gdb "detach" command)"""
		self.putpacket("D")
		if self.getpacket() != 'OK': 
			raise Exception("Failed to detach from remote process")

	def reset(self):
		"""Reset the target system"""
		self.putpacket("r")

	def read_mem(self, addr, length):
		"""Read length bytes from target at address addr"""
		self.putpacket("m%08X,%08X" % (addr, length))
		reply = self.getpacket()
		if (len(reply) == 0) or (reply[0] == 'E'): 
			raise Exception('Error reading memory at 0x%08X' % addr)
		try:
			data = unhexify(reply)
		except Excpetion:
			raise Exception('Invalid response to memory read packet: %r' % reply)
		return data
		
	def write_mem(self, addr, data):
		"""Write data to target at address addr"""
		self.putpacket("X%08X,%08X:%s" % (addr, len(data), data))
		if self.getpacket() != 'OK': 
			raise Exception('Error writing to memory at 0x%08X' % addr)

	def read_regs(self):
		"""Read target core registers"""
		self.putpacket("g")
		reply = self.getpacket()
		if (len(reply) == 0) or (reply[0] == 'E'): 
			raise Exception('Error reading memory at 0x%08X' % addr)
		try:
			data = unhexify(reply)
		except Excpetion:
			raise Exception('Invalid response to memory read packet: %r' % reply)
		return struct.unpack("=20L", data)

	def write_regs(self, *regs):
		"""Write target core registers"""
		data = struct.pack("=%dL" % len(regs), *regs)
		self.putpacket("G" + hexify(data))
		if self.getpacket() != 'OK': 
			raise Exception('Error writing to target core registers')

	def memmap_read(self):
		"""Read the XML memory map from target"""
		offset = 0
		ret = ''
		while True:
			self.putpacket("qXfer:memory-map:read::%08X,%08X" % (offset, 512))
			reply = self.getpacket()
			if (reply[0] == 'm') or (reply[0] == 'l'):
				offset += len(reply) - 1
				ret += reply[1:]
			else:
				raise Exception("Invalid GDB stub response")

			if reply[0] == 'l': return ret
			
	def resume(self):
		"""Resume target execution"""
		self.putpacket("c")

	def interrupt(self):
		"""Interrupt target execution"""
		self.sock.send("\x03")

	def run_stub(self, stub, address, *args):
		"""Execute a binary stub at address, passing args in core registers."""
		self.reset() # Ensure processor is in sane state
		time.sleep(0.1)
		self.write_mem(address, stub)
		regs = list(self.read_regs())
		regs[:len(args)] = args
		regs[15] = address
		self.write_regs(*regs)
		self.resume()
		reply = self.getpacket()
		while not reply:
			reply = self.getpacket()
		if not reply.startswith("T05"):
			raise Exception("Invalid stop response: %r" % reply)

	class FlashMemory:
		def __init__(self, target, offset, length, blocksize):
			self.target = target
			self.offset = offset
			self.length = length
			self.blocksize = blocksize
			self.blocks = list(None for i in range(length / blocksize))

		def prog(self, offset, data):
			assert ((offset >= self.offset) and 
				(offset + len(data) <= self.offset + self.length))

			while data:
				index = (offset - self.offset) / self.blocksize
				bloffset = (offset - self.offset) % self.blocksize
				bldata = data[:self.blocksize-bloffset]
				data = data[len(bldata):]; offset += len(bldata)
				if self.blocks[index] is None: # Initialize a clear block
					self.blocks[index] = "".join(chr(0xff) for i in range(self.blocksize))
				self.blocks[index] = (self.blocks[index][:bloffset] + bldata + 
						self.blocks[index][bloffset+len(bldata):])

		def commit(self, progress_cb=None):
			totalblocks = 0
			for b in self.blocks:
				if b is not None: totalblocks += 1
				
			block = 0
			for i in range(len(self.blocks)):
				block += 1
				if callable(progress_cb):
					progress_cb(block*100/totalblocks)

				# Erase the block
				data = self.blocks[i]
				addr = self.offset + self.blocksize * i
				if data is None: continue
				#print "Erasing flash at 0x%X" % (self.offset + self.blocksize*i)
				self.target.putpacket("vFlashErase:%08X,%08X" % 
					(self.offset + self.blocksize*i, self.blocksize))
				if self.target.getpacket() != 'OK': 
					raise Exception("Failed to erase flash")

				while data:
					d = data[0:980]
					data = data[len(d):]
					#print "Writing %d bytes at 0x%X" % (len(d), addr)
					self.target.putpacket("vFlashWrite:%08X:%s" % (addr, d))
					addr += len(d)
					if self.target.getpacket() != 'OK': 
						raise Exception("Failed to write flash")
					
				self.target.putpacket("vFlashDone")
				if self.target.getpacket() != 'OK': 
					raise Exception("Failed to commit")
			
			self.blocks = list(None for i in range(self.length / self.blocksize))
			

	def flash_probe(self):
		self.mem = []
		xmldom = parseString(self.memmap_read())
		
		for memrange in xmldom.getElementsByTagName("memory"):
			if memrange.getAttribute("type") != "flash": continue
			offset = eval(memrange.getAttribute("start"))
			length = eval(memrange.getAttribute("length"))
			for property in memrange.getElementsByTagName("property"):
				if property.getAttribute("name") == "blocksize":
					blocksize = eval(property.firstChild.data)
					break
			mem = Target.FlashMemory(self, offset, length, blocksize)
			self.mem.append(mem)

		xmldom.unlink()

		return self.mem

	def flash_write_prepare(self, address, data):
		for m in self.mem:
			if (address >= m.offset) and (address + len(data) <= m.offset + m.length):
				m.prog(address, data)

	def flash_commit(self, progress_cb=None):
		for m in self.mem:
			m.commit(progress_cb)


