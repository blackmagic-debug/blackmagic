# This file is part of the Black Magic Debug project.
#
# Copyright (C) 2025 1BitSquared <info@1bitsquared.com>
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

probe_bmp_includes = include_directories('.')

probe_bmp_sources = files('platform.c')

probe_bmp_dfu_sources = files('bootloader.c')

probe_bmp_args = [
	'-DDFU_SERIAL_LENGTH=9',
	'-DBLACKMAGICPROBE_V3',
]

trace_protocol = get_option('trace_protocol')
probe_bmp_args += [f'-DSWO_ENCODING=@trace_protocol@']
probe_bmp_dependencies = [platform_stm32_swo]
if trace_protocol in ['1', '3']
	probe_bmp_dependencies += platform_stm32_swo_manchester
endif
if trace_protocol in ['2', '3']
	probe_bmp_dependencies += platform_stm32_swo_uart
endif

probe_bmp_common_link_args = [
	'-L@0@'.format(meson.current_source_dir()),
	'-T@0@'.format('bmp-v3.ld'),
]

probe_bmp_link_args = [
	# Reserve two pages for the bootloader
	'-Wl,-Ttext=0x8004000',
]

probe_host = declare_dependency(
	include_directories: probe_bmp_includes,
	sources: probe_bmp_sources,
	compile_args: probe_bmp_args,
	link_args: probe_bmp_common_link_args + probe_bmp_link_args,
	dependencies: [platform_common, platform_stm32u5, probe_bmp_dependencies],
)

probe_bootloader = declare_dependency(
	include_directories: [platform_common_includes, probe_bmp_includes],
	sources: probe_bmp_dfu_sources,
	compile_args: probe_bmp_args,
	link_args: probe_bmp_common_link_args,
	dependencies: platform_stm32u5_dfu,
)

summary(
	{
		'Name': 'Black Magic Probe v3',
		'Platform': 'STM32U5',
		'Bootloader': 'Black Magic Debug Bootloader',
		'Load Address': '0x8002000',
	},
	section: 'Probe',
)
