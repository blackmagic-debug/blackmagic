# This file is part of the Black Magic Debug project.
#
# Copyright (C) 2024 1BitSquared <info@1bitsquared.com>
# Written by Rachel Mant <git@dragonmux.network>
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this
#    list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
#
# 3. Neither the name of the copyright holder nor the names of its
#    contributors may be used to endorse or promote products derived from
#    this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# Default definitions of the resulting stubs for all this that are harmless for, eg, BMDA
lmi_stub = []
efm32_stub = []
rp2040_stub = []

# If we're doing a firmware build, type to find hexdump
if is_firmware_build
	hexdump = find_program('hexdump', required: false)
else
	hexdump = disabler()
endif
# If we cannot or we're not a firmware build, we're done
if not hexdump.found()
	subdir_done()
endif

# We found hexdump, so build the stubs and their conversions to text files containing suitably formatted
# hexadecimal of their code, ready to go onto a target.

# Default options for all stub builds
stub_build_args = [
	'-mthumb',
	'-nostartfiles',
	'-nodefaultlibs',
	'-nostdlib',
	'-ffreestanding',
]

# Flash stub for Stellaris/Tiva-C parts
lmi_stub_elf = executable(
	'lmi_stub',
	'lmi.c',
	c_args: [
		'-mcpu=cortex-m3',
		stub_build_args,
	],
	link_args: [
		'-mcpu=cortex-m3',
		stub_build_args,
		'-T', '@0@/lmi.ld'.format(meson.current_source_dir()),
	],
	link_depends: files('lmi.ld'),
	pie: false,
	install: false,
)

lmi_stub = custom_target(
	'lmi_stub-hex',
	command: [
		hexdump,
		'-v',
		'-e', '/2 "0x%04X, "',
		'@INPUT@'
	],
	input: lmi_stub_elf,
	output: 'lmi.stub',
	capture: true,
)

# Flash stub for EFM32 parts
efm32_stub_elf = executable(
	'efm32_stub',
	'efm32.c',
	c_args: [
		'-mcpu=cortex-m0plus',
		stub_build_args,
	],
	link_args: [
		'-mcpu=cortex-m0plus',
		stub_build_args,
		'-T', '@0@/efm32.ld'.format(meson.current_source_dir()),
	],
	link_depends: files('efm32.ld'),
	pie: false,
	install: false,
)

efm32_stub = custom_target(
	'efm32_stub-hex',
	command: [
		hexdump,
		'-v',
		'-e', '/2 "0x%04X, "',
		'@INPUT@'
	],
	input: efm32_stub_elf,
	output: 'efm32.stub',
	capture: true,
)

# Flash stub for the RP2040
rp2040_stub_elf = executable(
	'rp_stub',
	'rp.c',
	c_args: [
		'-mcpu=cortex-m0plus',
		stub_build_args
	],
	link_args: [
		'-mcpu=cortex-m0plus',
		stub_build_args,
		'-T', '@0@/rp.ld'.format(meson.current_source_dir()),
	],
	link_depends: files('rp.ld'),
	pie: false,
	install: false,
)

rp2040_stub = custom_target(
	'rp_stub-hex',
	command: [
		hexdump,
		'-v',
		'-e', '/2 "0x%04X, "',
		'@INPUT@'
	],
	input: rp2040_stub_elf,
	output: 'rp.stub',
	capture: true,
)
