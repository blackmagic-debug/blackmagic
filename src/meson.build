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

bmd_core_includes = include_directories(
	'.',
	'include',
)

# Define sources used in both the firwmare and when built as a library
libbmd_core_sources = files(
	'command.c',
	'crc32.c',
	'exception.c',
	'gdb_main.c',
	'gdb_packet.c',
	'hex_utils.c',
	'main.c',
	'maths_utils.c',
	'morse.c',
	'remote.c',
	'timing.c',
)

# Define sources used only by the firmware
bmd_core_sources = [
	libbmd_core_sources,
]

bmd_core_args = []
libbmd_core_args = [
	'-DENABLE_DEBUG=1',
]

# Debug output handling
debug_output = get_option('debug_output')
if debug_output
	bmd_core_args += ['-DENABLE_DEBUG=1']
endif

# RTT support handling
rtt_support = get_option('rtt_support')
libbmd_core_sources += files('rtt.c')
libbmd_core_args += ['-DENABLE_RTT=1']
if rtt_support
	bmd_core_sources += files('rtt.c')
	bmd_core_args += ['-DENABLE_RTT=1']

	rtt_ident = get_option('rtt_ident')
	if rtt_ident != ''
		bmd_core_args += [f'-DRTT_IDENT=@rtt_ident@']
		libbmd_core_args += [f'-DRTT_IDENT=@rtt_ident@']
	endif
endif

# Advertise QStartNoAckMode
advertise_noackmode = get_option('advertise_noackmode')
if advertise_noackmode
	bmd_core_args += ['-DADVERTISE_NOACKMODE=1']
	libbmd_core_args += ['-DADVERTISE_NOACKMODE=1']
endif

# Get BMD targets dependency
subdir('target')

if is_firmware_build
	# Core BMD "library" (a dependency with all sources and flags to build the BMD core)
	bmd_core = declare_dependency(
		compile_args: bmd_core_args,
		include_directories: bmd_core_includes,
		sources: [bmd_core_sources, version],
		dependencies: bmd_targets,
	)
endif

# libbmd core "library" dependency
libbmd_core = declare_dependency(
	compile_args: libbmd_core_args,
	include_directories: [bmd_core_includes, include_directories('platforms/hosted')],
	sources: [libbmd_core_sources, version],
	dependencies: libbmd_targets,
)

if is_firmware_build
	# Get probe host and platform dependencies
	subdir('platforms')
endif
subdir('platforms/hosted')

summary(
	{
		'Debug output': debug_output,
		'RTT support': rtt_support,
		'Advertise QStartNoAckMode': advertise_noackmode,
	},
	bool_yn: true,
	section: 'Black Magic Debug',
)
