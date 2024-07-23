# This file is part of the Black Magic Debug project.
#
# Copyright (C) 2023 1BitSquared <info@1bitsquared.com>
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

bmda_includes = [include_directories('.')]

bmda_sources = files(
	'platform.c',
	'gdb_if.c',
	'rtt_if.c',
	'cli.c',
	'utils.c',
	'probe_info.c',
	'debug.c',
	'bmp_remote.c',
	'bmp_libusb.c',
	'cmsis_dap.c',
	'dap.c',
	'dap_command.c',
	'dap_swd.c',
	'dap_jtag.c',
	'stlinkv2.c',
	'stlinkv2_jtag.c',
	'stlinkv2_swd.c',
	'ftdi_bmp.c',
	'ftdi_jtag.c',
	'ftdi_swd.c',
	'jlink.c',
	'jlink_jtag.c',
	'jlink_swd.c',
)
subdir('remote')

bmda_args = [
	'-DPC_HOSTED=1',
	'-DHOSTED_BMP_ONLY=0',
	# XXX: These need removing and the warnings they're covering up fixing
	'-Wno-format-nonliteral',
	'-Wno-missing-field-initializers',
]
bmda_link_args = []
bmda_deps = []

cc = is_cross_build ? cc_native : cc_host

# Ensure that MSVC is switched to standards compliant mode
if cc.get_define('_MSC_VER') != ''
	standards_flags = [
		'/Zc:__STDC__',
		'/D_CRT_DECLARE_NONSTDC_NAMES=1'
	]

	bmda_args += cc.get_supported_arguments(standards_flags)
endif

# Determine if we're on a MSYS2 environment of some kind
# If the compiler is MSYS2 GCC or Clang (but not Clang-cl)
if build_machine.system() == 'windows' and cc.get_define('__MINGW32__') == '1'
	# It needs custom location for some MinGWs
	# See https://github.com/msys2/MINGW-packages/issues/10275
	ucrt_check = '''
		#ifdef __MINGW32__
		#include <_mingw.h>
		#endif
		#include <stddef.h>
		#ifndef _UCRT
		#error "_UCRT not defined"
		#endif
	'''

	# Check if we're in a UCRT based environment or not
	if cc.compiles(ucrt_check, name: 'the compiler links against the UCRT')
		# Force linking against the correct C runtime DLL
		if cc.get_id() != 'clang'
			bmda_args += ['-mcrtdll=ucrt']
			bmda_link_args += ['-mcrtdll=ucrt']
		endif
	else
		bmda_args += ['-mcrtdll=msvcrt']
		bmda_link_args += ['-mcrtdll=msvcrt']
	endif
endif

libusb = dependency(
	'libusb-1.0',
	version: '>=1.0.13',
	method: 'pkg-config',
	fallback: 'libusb',
	native: is_cross_build,
	default_options: [
		'default_library=static',
		'install_targets=false',
		'docs=disabled',
		'build_native=true',
	],
	required: not is_cross_build,
	disabler: true,
)

if build_machine.system() in ['windows', 'cygwin']
	subdir('windows')

	# Make sure we build for Windows Vista and above, where the
	# 'SetupDiGetDevicePropertyW()' function is available
	bmda_args += ['-D_WIN32_WINNT=0x600']
	bmda_deps += [
		cc.find_library('ws2_32'),
		cc.find_library('setupapi'),
	]
	bmda_sources += files('serial_win.c')

	if not cc.has_function('getopt_long', prefix: '#include <getopt.h>')
		bmda_deps += [
			libgetopt
		]
	endif
else
	libftdi = dependency(
		'libftdi1',
		method: 'pkg-config',
		fallback: 'libftdi',
		native: is_cross_build,
		default_options: [
			'default_library=static',
			'install_targets=false',
			'tests=disabled',
			'docs=disabled',
			'examples=disabled',
			'ftdi_eeprom=disabled',
			#'python_bindings=disabled',
			'build_native=true',
		],
		required: not is_cross_build,
		disabler: true,
	)
	bmda_sources += files('serial_unix.c')
endif

# Pick the appropriate HIDAPI depending on platform
if build_machine.system() == 'linux'
	hidapi = dependency(
		'hidapi-hidraw',
		method: 'pkg-config',
		fallback: ['hidapi', 'hidapi_hidraw_dep'],
		native: is_cross_build,
		default_options: [
			'c_std=gnu99',
			'default_library=static',
			'install_targets=false',
			'with_libusb=false',
			'build_native=true',
		],
		required: not is_cross_build,
		disabler: true,
	)
else
	hidapi = dependency(
		'hidapi',
		method: 'pkg-config',
		fallback: 'hidapi',
		native: is_cross_build,
		default_options: [
			'c_std=c99',
			'default_library=static',
			'install_targets=false',
			'build_native=true',
		],
		required: not is_cross_build,
		disabler: true,
	)
endif

if build_machine.system() == 'linux'
	libgpiod = dependency(
		'libgpiod',
		version: ['>=1.0.0', '<2.0.0'],
		required: get_option('enable_gpiod'),
		native: is_cross_build,
	)

	if libgpiod.found()
		bmda_sources += files('bmda_gpiod.c')
		bmda_args += ['-DENABLE_GPIOD=1']

		bmda_sources += files(
			'../common/jtagtap.c',
			'../common/swdptap.c'
		)
	endif
else
	libgpiod = declare_dependency()
endif

# Build a dependency object that describes the sources needed to build BMDA for the build platform
bmda_platform = declare_dependency(
	include_directories: bmda_includes,
	sources: bmda_sources,
	compile_args: cc.get_supported_arguments(bmda_args),
	link_args: bmda_link_args,
	dependencies: [libbmd_core, bmda_deps, libftdi, hidapi, libusb, libgpiod],
)
