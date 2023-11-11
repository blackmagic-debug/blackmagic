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

platform_stm32_includes = include_directories('.')

platform_stm32_sources = files(
	'gdb_if.c',
	'serialno.c',
	'timing_stm32.c',
	'traceswodecode.c',
)

# TODO: add proper traceswoasync support
# This is a temporary hack to allow selecting traceswoasync vs traceswo implementation
fixme_platform_stm32_traceswo = declare_dependency(sources: files('traceswo.c'))
fixme_platform_stm32_traceswoasync = declare_dependency(sources: files('traceswoasync.c'))
fixme_platform_stm32f7_traceswoasync = declare_dependency(sources: files('traceswoasync_f723.c'))

# RTT support handling
if get_option('rtt_support')
	platform_stm32_sources += files('rtt_if.c')
endif

platform_stm32_dfu_sources = files(
	'dfucore.c',
	'serialno.c',
)

platform_stm32_args = [
	'-mthumb',
	'-nostartfiles',
	'-DPC_HOSTED=0',
]

platform_stm32_link_args = [
	'-mthumb',
	'-lc',
	'-nostartfiles',
	'--specs=nano.specs',
	get_option('debug_output') ? '--specs=rdimon.specs' : '--specs=nosys.specs',
	'-Wl,-gc-sections',
]

platform_stm32_common = declare_dependency(
	include_directories: platform_stm32_includes,
	sources: platform_stm32_sources,
	compile_args: platform_stm32_args,
	link_args: platform_stm32_link_args,
)

platform_stm32_dfu_common = declare_dependency(
	include_directories: platform_stm32_includes,
	sources: platform_stm32_dfu_sources,
	compile_args: platform_stm32_args,
	link_args: platform_stm32_link_args,
)

## STM32F0 Platform
## ________________

platform_stm32f0_args = [
	'-mcpu=cortex-m0',
	'-DSTM32F0',
]

platform_stm32f0_link_args = [
	'-mcpu=cortex-m0',
]

platform_stm32f0 = declare_dependency(
	compile_args: platform_stm32f0_args,
	link_args: platform_stm32f0_link_args,
	dependencies: [platform_stm32_common, dependency('opencm3_stm32f0')],
)

## STM32F1 Platform
## ________________

platform_stm32f1_dfu_sources = files('dfu_f1.c')

platform_stm32f1_args = [
	'-mcpu=cortex-m3',
	'-DSTM32F1',
]

platform_stm32f1_link_args = [
	'-mcpu=cortex-m3',
]

platform_stm32f1 = declare_dependency(
	compile_args: platform_stm32f1_args,
	link_args: platform_stm32f1_link_args,
	dependencies: [platform_stm32_common, dependency('opencm3_stm32f1')],
)

platform_stm32f1_dfu = declare_dependency(
	sources: platform_stm32f1_dfu_sources,
	compile_args: platform_stm32f1_args,
	link_args: platform_stm32f1_link_args,
	dependencies: [platform_stm32_dfu_common, dependency('opencm3_stm32f1')],
)

## STM32F3 Platform
## ________________

platform_stm32f3_args = [
	'-mcpu=cortex-m4',
	'-mfloat-abi=hard',
	'-mfpu=fpv4-sp-d16',
	'-DSTM32F3',
]

platform_stm32f3_link_args = [
	'-mcpu=cortex-m4',
	'-mfloat-abi=hard',
	'-mfpu=fpv4-sp-d16',
]

platform_stm32f3 = declare_dependency(
	compile_args: platform_stm32f3_args,
	link_args: platform_stm32f3_link_args,
	dependencies: [platform_stm32_common, dependency('opencm3_stm32f3')],
)

## STM32F4 Platform
## ________________

platform_stm32f4_dfu_sources = files('dfu_f4.c')

platform_stm32f4_args = [
	'-mcpu=cortex-m4',
	'-mfloat-abi=hard',
	'-mfpu=fpv4-sp-d16',
	'-DSTM32F4',
]

platform_stm32f4_link_args = [
	'-mcpu=cortex-m4',
	'-mfloat-abi=hard',
	'-mfpu=fpv4-sp-d16',
]

platform_stm32f4 = declare_dependency(
	compile_args: platform_stm32f4_args,
	link_args: platform_stm32f4_link_args,
	dependencies: [platform_stm32_common, dependency('opencm3_stm32f4')],
)

platform_stm32f4_dfu = declare_dependency(
	sources: platform_stm32f4_dfu_sources,
	compile_args: platform_stm32f4_args,
	link_args: platform_stm32f4_link_args,
	dependencies: [platform_stm32_dfu_common, dependency('opencm3_stm32f4')],
)

## STM32F7 Platform
## ________________

platform_stm32f7_args = [
	'-mcpu=cortex-m7',
	'-mfloat-abi=hard',
	'-mfpu=fpv5-sp-d16',
	'-DSTM32F7',
]

platform_stm32f7_link_args = [
	'-mcpu=cortex-m7',
	'-mfloat-abi=hard',
	'-mfpu=fpv5-sp-d16',
]

platform_stm32f7 = declare_dependency(
	compile_args: platform_stm32f7_args,
	link_args: platform_stm32f7_link_args,
	dependencies: [platform_stm32_common, dependency('opencm3_stm32f7')],
)

platform_stm32f7_dfu = declare_dependency(
	sources: platform_stm32f4_dfu_sources,
	compile_args: platform_stm32f7_args,
	link_args: platform_stm32f7_link_args,
	dependencies: [platform_stm32_dfu_common, dependency('opencm3_stm32f7')],
)
