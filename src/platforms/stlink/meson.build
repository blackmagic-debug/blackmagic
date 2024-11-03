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

probe_stlink_dfu_sources = files(
	'stlink_common.c',
	'usbdfu.c',
)

stlink_v2_isol = get_option('stlink_v2_isol')

bmd_bootloader = get_option('bmd_bootloader')

probe_stlink_dfu_serial_length = bmd_bootloader ? '9' : '25'
probe_stlink_load_address = bmd_bootloader ? '0x8002000' : '0x8004000'

probe_stlink_args = [
	f'-DDFU_SERIAL_LENGTH=@probe_stlink_dfu_serial_length@',
]
if stlink_v2_isol
	probe_stlink_args += ['-DSTLINK_V2_ISOL']
endif
if not bmd_bootloader
	probe_stlink_args += ['-DST_BOOTLOADER']
endif

probe_stlink_common_link_args = [
	'-L@0@'.format(meson.current_source_dir()),
	'-T@0@'.format('stlink.ld'),
]

probe_stlink_link_args = [
	f'-Wl,-Ttext=@probe_stlink_load_address@',
]

if probe == 'bluepill'
	probe_stlink_args += ['-DBLUEPILL=1']
endif

probe_stlink_dependencies = [platform_common, platform_stm32f1, platform_stm32_swo]

stlink_swim_nrst_as_uart = get_option('stlink_swim_nrst_as_uart')

trace_protocol = get_option('trace_protocol')
if probe == 'stlink' and stlink_swim_nrst_as_uart
	probe_stlink_args += ['-DSWIM_NRST_AS_UART=1']
	probe_stlink_dependencies += platform_stm32_swo_manchester
	probe_stlink_args += ['-DSWO_ENCODING=1']
else
	probe_stlink_args += [f'-DSWO_ENCODING=@trace_protocol@']
	if trace_protocol in ['1', '3']
		probe_stlink_dependencies += platform_stm32_swo_manchester
	endif
	if trace_protocol in ['2', '3']
		probe_stlink_dependencies += platform_stm32_swo_uart
	endif
endif

probe_host = declare_dependency(
	include_directories: probe_stlink_includes,
	sources: probe_stlink_sources,
	compile_args: probe_stlink_args,
	link_args: probe_stlink_common_link_args + probe_stlink_link_args,
	dependencies: probe_stlink_dependencies,
)

probe_bootloader = declare_dependency(
	include_directories: [platform_common_includes, probe_stlink_includes],
	sources: probe_stlink_dfu_sources,
	compile_args: probe_stlink_args,
	link_args: probe_stlink_common_link_args,
	dependencies: platform_stm32f1_dfu,
)

summary(
	{
		'Name': probe == 'stlink' ? 'ST-Link' : 'BluePill',
		'Platform': 'STM32F1',
		'Bootloader': bmd_bootloader ? 'Black Magic Debug Bootloader' : 'OEM ST Bootloader',
		'Load Address': probe_stlink_load_address,
		'SWIM as UART': probe == 'stlink' and stlink_swim_nrst_as_uart,
	},
	bool_yn: true,
	section: 'Probe',
)
