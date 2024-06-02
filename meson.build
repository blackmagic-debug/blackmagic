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
	# license_files: ['COPYING', 'COPYING-BSD', 'COPYING-MIT'], # Only available in meson 1.1.0
	default_options: [
		'c_std=c11',
		'optimization=s',
		'debug=true',
		# 'warning_level=3', # TODO: Enable by default when all warnings are fixed
		'warning_level=2',
		'werror=false',
		'b_ndebug=if-release',
	],
	meson_version: '>= 0.61.0',
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

# Import the filesystem module
fs = import('fs')

# Grab if we're in cross-compilation mode or not
is_cross_build = meson.is_cross_build()

# Grab the target probe from project options
probe = get_option('probe')

# Determine if we are building the firmware or just the native library
is_firmware_build = probe != ''
if is_firmware_build
	# Ensure we are cross-compiling and not building for the build host
	assert(
		is_cross_build,
		'''Black Magic Firmware must be cross-compiled to the probe
Try adding the option `--cross-file @0@` to the configuration'''.format(
			meson.project_source_root() / 'cross-file' / probe + '.ini'
		),
	)
endif

## Black Magic Debug (BMD) sources
## _______________________________

cc_host = meson.get_compiler('c')
if is_cross_build
	cc_native = meson.get_compiler('c', native: true)
endif

# Project wide flags
extended_warnings = [
	'-Warith-conversion',
	'-w24244', # 'conversion' conversion from 'type1' to 'type2', possible loss of data (integer version)
	'-Wbad-function-cast',
	# '-Wcast-align=strict',
	'-Wcast-function-type',
	# '-Wcast-qual',
	# '-Wconversion',
	'-Wdangling-else',
	'-Wdouble-promotion',
	'-Wduplicated-branches',
	'-w24754', # Conversion rules for arithmetic operations in a comparison mean that one branch cannot be executed.
	'-Wfloat-conversion',
	# '-Wformat-overflow=2',
	# '-Wformat-signedness',
	'-Wformat-truncation',
	'-Wformat=2',
	'-w24774', # ‘<function>’ : format string expected in argument <position> is not a string literal
	'-w24777', #‘<function>’ : format string ‘<format-string>’ requires an argument of type ‘<type>’, but variadic argument <position> has type ‘<type>’ 
	'-Wimplicit-fallthrough',
	'-Wmaybe-uninitialized',
	'-w24701', # Potentially uninitialized local variable 'name' used
	'-w24703', # Potentially uninitialized local pointer variable 'name' used
	'-Wmissing-attributes',
	'-Wmissing-braces',
	'-Wno-char-subscripts',
	'-Wnull-dereference',
	# '-Wpacked',
	'-Wredundant-decls',
	'-Wreturn-type',
	'-w24013', # 'function' undefined; assuming extern returning int
	'-Wsequence-point',
	'-Wshadow=local',
	'-w24456', # declaration of 'identifier' hides previous local declaration
	'-w24457', # declaration of 'identifier' hides function parameter
	# '-Wsign-conversion',
	# '-Wstack-protector',
	'-Wstrict-aliasing',
	'-Wstrict-overflow=3',
	'-Wstring-compare',
	'-Wstringop-overflow',
	'-Wunknown-pragmas', # MSVC's C4081 and it's a level 1 warning
	'-Wunsafe-loop-optimizations',
	'-Wunsuffixed-float-constants',
	'-Wunused-const-variable=2',
	'-w24189', # 'identifier' : local variable is initialized but not referenced
	'-Wunused-local-typedefs',
	'-Wunused',
	'-w24101', # 'identifier' : unreferenced local variable
	'-Wvla-parameter',
	'-Wvla',
]
add_project_arguments(
	cc_host.get_supported_arguments(extended_warnings),
	language: 'c',
)
if is_cross_build
	add_project_arguments(
		cc_native.get_supported_arguments(extended_warnings),
		language: 'c',
		native: true,
	)
endif

# Null dependency to allow for checking and conditional compilation later
probe_host = disabler()
probe_bootloader = disabler()

subdir('src')

## Black Magic Firmware (BMF) targets
## __________________________________

if is_firmware_build
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
	bmf_base_name = 'blackmagic_@0@'.format(probe).to_lower().underscorify()

	# Main firmware elf file
	bmf_elf = executable(
		f'@bmf_base_name@_firmware',
		name_suffix: 'elf',
		dependencies: [bmd_core, probe_host],
	)
	alias_target('elf', bmf_elf)

	# Firmware binary and hex files
	bmf_bin = custom_target(
		output: fs.replace_suffix(bmf_elf.name(), '.bin'),
		input: bmf_elf,
		command: [objcopy, ['-O', 'binary', '@INPUT@', '@OUTPUT@']],
		depends: bmf_elf,
		build_by_default: true,
	)
	alias_target('bin', bmf_bin)

	bmf_hex = custom_target(
		output: fs.replace_suffix(bmf_elf.name(), '.hex'),
		input: bmf_elf,
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

		# Bootloader elf file
		bmf_boot_elf = executable(
			f'@bmf_base_name@_bootloader',
			name_suffix: 'elf',
			include_directories: bmd_core_includes,
			dependencies: probe_bootloader,
			build_by_default: false,
		)
		alias_target('boot-elf', bmf_boot_elf)

		# Bootloader binary and hex files
		bmf_boot_bin = custom_target(
			output: fs.replace_suffix(bmf_boot_elf.name(), '.bin'),
			input: bmf_boot_elf,
			command: [objcopy, ['-O', 'binary', '@INPUT@', '@OUTPUT@']],
			depends: bmf_boot_elf,
		)
		alias_target('boot-bin', bmf_boot_bin)

		bmf_boot_hex = custom_target(
			output: fs.replace_suffix(bmf_boot_elf.name(), '.hex'),
			input: bmf_boot_elf,
			command: [objcopy, ['-O', 'ihex', '@INPUT@', '@OUTPUT@']],
			depends: bmf_boot_elf,
		)
		alias_target('boot-hex', bmf_boot_hex)
	endif

	## Utility targets
	## _______________

	# Black Magic Probe Firmware Manager
	bmputil = find_program('bmputil', required: false)
	if bmputil.found()
		message('Adding target for firmware update')

		# Firmware update target
		run_target(
			'flash',
			command: [bmputil, 'flash', bmf_elf.full_path()],
			depends: bmf_elf,
		)
	endif
endif

if meson.is_subproject()
	# libbmd library (static or dynamic, based on the user's choice)
	libbmd = static_library(
		'blackmagicdebug',
		c_args: [
			'-DPC_HOSTED=1',
			'-DHOSTED_BMP_ONLY=1',
		],
		dependencies: [libbmd_core],
		native: is_cross_build,
	)

	libbmd_dep = declare_dependency(
		include_directories: bmd_core_includes,
		link_with: libbmd,
	)
# Build BMDA only if all the dependencies are satisfied etc
elif bmda_platform.found()
	# If we're on windows, copy the FTD2xx DLL into the output directory where it's needed
	if build_machine.system() in ['windows', 'cygwin']
		configure_file(
			input: '3rdparty' / 'ftdi' / processor / 'ftd2xx.dll',
			output: 'ftd2xx.dll',
			copy: true,
		)
	endif

	# BMDA executable
	bmda = executable(
		'blackmagic',
		dependencies: [libbmd_core, bmda_platform],
		native: is_cross_build,
	)
	alias_target('bmda', bmda)
elif not is_firmware_build
	error('''
One or more dependencies for BMDA were not found, and you are not building the firmware.
You would build nothing in this configuration, so aborting.

Dependencies:
	libftdi: @0@
	hidapi: @1@
	libusb: @2@
'''.format(libftdi.found(), hidapi.found(), libusb.found()))
endif

summary(
	{
		'Building Firmware': is_firmware_build,
		'Building BMDA': is_variable('bmda'),
	},
	bool_yn: true,
	section: 'Black Magic Debug',
)
