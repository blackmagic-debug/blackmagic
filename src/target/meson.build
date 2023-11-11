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

target_common_includes = include_directories('.')

target_common_sources = files(
	'adiv5.c',
	'adiv5_jtag.c',
	'adiv5_swd.c',
	'gdb_reg.c',
	'jtag_devs.c',
	'jtag_scan.c',
	'sfdp.c',
	'spi.c',
	'target.c',
	'target_flash.c',
	'target_probe.c',
)

# Handle generic routines, used when low-level routines are not available
if get_option('no_own_ll')
	target_common_sources += files(
		'jtagtap_generic.c',
		'swdptap_generic.c',
	)
endif

# Conditional file compilation based on target selection
# We declare a dependency for each target group with the source files required
# these dependencies are then added to the BMD core, conditinal on the targets option
# NOTE: sourceset module might be an alternative to this method (unexplored)
target_cortex = declare_dependency(
	sources: files('cortex.c'),
)

target_cortexar = declare_dependency(
	sources: files('cortexar.c'),
	dependencies: target_cortex,
	compile_args: ['-DENABLE_CORTEXAR=1'],
)

target_cortexm = declare_dependency(
	sources: files('cortexm.c'),
	dependencies: target_cortex,
)

target_riscv = declare_dependency(
	sources: files(
		'riscv_debug.c',
		'riscv_jtag_dtm.c',
	),
	compile_args: ['-DENABLE_RISCV=1'],
)

target_riscv32 = declare_dependency(
	sources: files('riscv32.c'),
	dependencies: target_riscv,
)

target_riscv64 = declare_dependency(
	sources: files('riscv64.c'),
	dependencies: target_riscv,
)

target_efm = declare_dependency(
	sources: files('efm32.c'),
	dependencies: target_cortexm,
)

target_hc32 = declare_dependency(
	sources: files('hc32l110.c'),
	dependencies: target_cortexm,
)

target_lpc = declare_dependency(
	sources: files(
		'lpc11xx.c',
		'lpc15xx.c',
		'lpc17xx.c',
		'lpc40xx.c',
		'lpc43xx.c',
		'lpc546xx.c',
		'lpc55xx.c',
		'lpc_common.c',
	),
	dependencies: target_cortexm,
)

target_nrf = declare_dependency(
	sources: files(
		'nrf51.c',
		'nrf91.c',
	),
	dependencies: target_cortexm,
)

target_nxp = declare_dependency(
	sources: files(
		'imxrt.c',
		'kinetis.c',
		'nxpke04.c',
	),
	dependencies: target_cortexm,
)

target_renesas = declare_dependency(
	sources: files('renesas.c'),
	dependencies: target_cortexm,
)

target_rp = declare_dependency(
	sources: files('rp.c'),
	dependencies: target_cortexm,
)

target_sam = declare_dependency(
	sources: files(
		'sam3x.c',
		'sam4l.c',
		'samd.c',
		'samx5x.c',
	),
	dependencies: target_cortexm,
)

target_stm_deps = [target_cortexm]
# If the 32-bit RISC-V target is not enabled, don't enable GD32VF103 support
if get_option('targets').contains('riscv32')
	target_stm_deps += [target_riscv32]
endif

target_stm = declare_dependency(
	sources: files(
		'at32f43x.c',
		'ch32f1.c',
		'stm32f1.c',
		'stm32f4.c',
		'stm32g0.c',
		'stm32h5.c',
		'stm32h7.c',
		'stm32l0.c',
		'stm32l4.c',
		'stm32mp15.c',
	),
	dependencies: target_stm_deps,
)

target_ti = declare_dependency(
	sources: files(
		'lmi.c',
		'msp432e4.c',
		'msp432p4.c',
	),
	dependencies: target_cortexm,
)

target_xilinx = declare_dependency(
	sources: files(
		'zynq7000.c',
	),
	dependencies: target_cortexar,
)

# Convert targets option list into a list of dependencies
enabled_target_deps = []
foreach target : get_option('targets')
	enabled_target_deps += get_variable(f'target_@target@')
endforeach

# Check that at least one target is enabled
assert(
	enabled_target_deps.length() > 0,
	'''No debug targets enabled, please enable at least one target to build.
See the 'targets' option for a list of available targets.
''',
)

# BMD target dependency
bmd_targets = declare_dependency(
	include_directories: target_common_includes,
	sources: target_common_sources,
	dependencies: enabled_target_deps,
)