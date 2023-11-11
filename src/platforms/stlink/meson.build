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

probe_stlink_includes = include_directories('.')

probe_stlink_sources = files(
	'platform.c',
	'stlink_common.c',
)

probe_stlink_dfu_sources = files('usbdfu.c')

probe_stlink_args = []

probe_stlink_link_args = [
	'-L@0@'.format(meson.current_source_dir()),
	'-T@0@'.format('stlink.ld'),
]

probe_stlink_args += [
	'-DDFU_SERIAL_LENGTH=9',
]
probe_stlink_link_args += [
	'-Wl,-Ttext=0x8002000',
]

# TODO: support bootloader choice
# if get_option('bmd_bootloader')
# else
# 	probe_stlink_args += [
# 		'-DDFU_SERIAL_LENGTH=25',
# 	]
# 	probe_stlink_link_args += [
# 		'-Wl,-Ttext=0x8004000',
# 	]
# endif

if probe == 'bluepill'
	probe_stlink_args += ['-DBLUEPILL=1']
endif

probe_stlink_dependencies = [platform_common, platform_stm32f1]

if probe == 'stlink' and get_option('stlink_swim_as_uart')
	probe_stlink_args += ['-DSWIM_AS_UART=1']
else
	probe_stlink_dependencies += fixme_platform_stm32_traceswoasync
endif

probe_host = declare_dependency(
	include_directories: probe_stlink_includes,
	sources: probe_stlink_sources,
	compile_args: probe_stlink_args,
	link_args: probe_stlink_link_args,
	dependencies: probe_stlink_dependencies,
)
