# This file is part of the Black Magic Debug project.
#
# Copyright (C) 2023 1BitSquared <info@1bitsquared.com>
# Written by Rafael Silva <perigoso@riseup.net>
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

project(
	'Black Magic Debug',
	'c',
	version: '1.10.0',
	license: 'GPL-3.0-or-later OR BSD-3-Clause OR MIT',
	default_options: [
		'c_std=c11',
		'optimization=s',
		'debug=true',
		'warning_level=2',
		'werror=true',
		'b_ndebug=if-release',
	],
	meson_version: '>= 0.58.0',
	subproject_dir: 'deps',
)

# Version from version control system
# fallback to meson project version (f.i. when building from source tarball)
# TODO: when no vcs version is available, we should mark it somehow (-static?) as it may not be 'clean'
version = vcs_tag(
	command: ['git', 'describe', '--always', '--dirty', '--tags'],
	input: 'src/include/version.h.in',
	output: 'version.h',
	fallback: meson.project_version(),
)

## Black Magic Debug (BMD) sources
## _______________________________

# Project wide flags
add_project_arguments('-Wreturn-type', '-Wno-char-subscripts', language: 'c')
if host_machine.system() in ['macos', 'darwin']
	add_project_arguments(
		'-Wmaybe-uninitialized',
		'-Wstringop-overflow',
		'-Wunsafe-loop-optimizations',
		language: 'c',
	)
endif

# Null dependency to allow for checking and conditional compilation later
probe_host = disabler()
probe_bootloader = disabler()

subdir('src')

## Black Magic Firmware (BMF) targets
## __________________________________

message('Adding targets for Black Magic Firmware')

assert(
	probe_host.found(),
	'''Probe host dependency not found, this is not supposed to happen...
If you did not touch the build system this is not your fault, please report it
''',
)

if get_option('print_memory_usage')
	add_project_link_arguments('-Wl,--print-memory-usage', language: 'c')
endif

# System binary utilities
size = find_program('size')
objcopy = find_program('objcopy')

# Base name for output files
bmf_base_name = 'black_magic_firmware_@0@'.format(get_option('probe')).to_lower().underscorify()

# Main firmware elf file
bmf_elf = executable(
	f'@bmf_base_name@.elf',
	dependencies: [bmd_core, probe_host],
)
alias_target('elf', bmf_elf)

# Firmware binary and hex files
bmf_bin = custom_target(
	'bin',
	output: f'@bmf_base_name@.bin',
	input: bmf_elf,
	command: [objcopy, ['-O', 'binary', '@INPUT@', '@OUTPUT@']],
	depends: bmf_elf,
	build_by_default: true,
)
alias_target('bin', bmf_bin)

bmf_hex = custom_target(
	'hex',
	input: bmf_elf,
	output: f'@bmf_base_name@.hex',
	command: [objcopy, ['-O', 'ihex', '@INPUT@', '@OUTPUT@']],
	depends: bmf_elf,
)
alias_target('hex', bmf_hex)

# Firmware size report
run_target(
	'size',
	command: [size, bmf_elf.full_path(), '-B'],
	depends: bmf_elf,
)

## Black Magic Firmware (BMF) Bootloader targets
## _____________________________________________

if probe_bootloader.found() and get_option('bmd_bootloader')
	message('Adding targets for Black Magic Firmware Bootloader')

	# Base name for output files
	bmf_boot_base_name = f'@bmf_base_name@_bootloader'

	# Bootloader elf file
	bmf_boot_elf = executable(
		f'@bmf_boot_base_name@.elf',
		include_directories: bmd_core_includes,
		dependencies: probe_bootloader,
		build_by_default: false,
	)
	alias_target('boot-elf', bmf_boot_elf)

	# Bootloader binary and hex files
	bmf_boot_bin = custom_target(
		'bootloader-bin',
		output: f'@bmf_boot_base_name@.bin',
		input: bmf_boot_elf,
		command: [objcopy, ['-O', 'binary', '@INPUT@', '@OUTPUT@']],
		depends: bmf_boot_elf,
	)
	alias_target('boot-bin', bmf_boot_bin)

	bmf_boot_hex = custom_target(
		'bootloader-hex',
		input: bmf_boot_elf,
		output: f'@bmf_boot_base_name@.hex',
		command: [objcopy, ['-O', 'ihex', '@INPUT@', '@OUTPUT@']],
		depends: bmf_boot_elf,
	)
	alias_target('boot-hex', bmf_boot_hex)
endif

# We report this at the end of the configuration, so it's easier to spot
warning(
	'''The meson build system is experimental and not yet fully supported. Please use the Makefile build system instead.
Please report any issues you find to https://github.com/blackmagic-debug/blackmagic/issues
'''
)
