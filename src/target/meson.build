# This file is part of the Black Magic Debug project.
#
# Copyright (C) 2023-2024 1BitSquared <info@1bitsquared.com>
# Written by Rafael Silva <perigoso@riseup.net>
# Modified by Rachel Mant <git@dragonmux.network>
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

subdir('flashstub')

target_common_sources = files(
	'adi.c',
	'adiv5.c',
	'adiv5_jtag.c',
	'adiv5_swd.c',
	'adiv6.c',
	'gdb_reg.c',
	'jtag_devs.c',
	'jtag_scan.c',
	'semihosting.c',
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

if is_firmware_build
	enabled_targets = get_option('targets')

	# Check that at least one target is enabled
	assert(
		enabled_targets.length() > 0,
		'''No debug targets enabled, please enable at least one target to build.
	See the 'targets' option for a list of available targets.
	''',
	)

	# Build a dictionary mapping all target short names to proper textual naming
	target_names = {
		'cortexar': 'Cortex-A/R support',
		'cortexm': 'Cortex-M support',
		'riscv32': 'RISC-V 32-bit support',
		'riscv64': 'RISC-V 64-bit support',
		'at32f4': 'Arterytek parts',
		'apollo3': 'Ambiq Apollo3 parts',
		'ch32': 'WinChipHead CH32 parts',
		'ch579': 'WinChipHead CH579 parts',
		'efm': 'Energy Micro parts',
		'gd32': 'GigaDevice parts',
		'hc32': 'HC32 parts',
		'lpc': 'LPC series parts',
		'mm32': 'MindMotion parts',
		'nrf': 'nRF series parts',
		'nxp': 'Kinetis series parts',
		'puya': 'Puya PY32 series parts',
		'renesas': 'Renesas parts',
		'rp': 'Raspberry Pi MCUs (RP2040, RP2350)',
		'sam': 'ATSAM series parts',
		'stm': 'STM32 (and clones) parts',
		'ti': 'Texas Instruments parts',
		'xilinx': 'Xilinx parts',
	}

	# Check to see if the set of enabled targets is all of them and the
	# firmware is to be built for a STM32F1 based probe (requires a restriction)
	if probe in ['bluepill', 'native', 'stlink', 'swlink'] and enabled_targets.length() == target_names.keys().length()
		warning('Disabling some targets as your build will not fit the target probe otherwise')
		enabled_targets = ['cortexm', 'lpc', 'nrf', 'nxp', 'renesas', 'rp', 'sam', 'stm', 'ti']
	elif probe in ['f072'] and enabled_targets.length() == target_names.keys().length()
		warning('Disabling some targets as your build will not fit the target probe otherwise')
		enabled_targets = [
			'cortexm',
			'riscv32',
			'riscv64',
			'lpc',
			'nrf',
			'nxp',
			'renesas',
			'rp',
			'sam',
			'stm',
			'ti'
		]
	endif
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
	compile_args: ['-DCONFIG_CORTEXAR=1'],
)

target_cortexm = declare_dependency(
	sources: files('cortexm.c'),
	dependencies: target_cortex,
)

riscv_jtag_dtm = declare_dependency(
	sources: files(
		'riscv_jtag_dtm.c',
	),
)

target_riscv = declare_dependency(
	sources: files(
		'riscv_debug.c',
		'riscv_adi_dtm.c',
	),
	compile_args: ['-DCONFIG_RISCV=1'],
	dependencies: riscv_jtag_dtm,
)

target_riscv32 = declare_dependency(
	sources: files('riscv32.c'),
	dependencies: target_riscv,
)

target_riscv64 = declare_dependency(
	sources: files('riscv64.c'),
	dependencies: target_riscv,
)

target_apollo3 = declare_dependency(
	sources: files('apollo3.c'),
	dependencies: target_cortexm,
)

target_ch579 = declare_dependency(
	sources: files('ch579.c'),
	dependencies: target_cortexm,
)

target_efm = declare_dependency(
	sources: files(
		'efm32.c'
	) + efm32_stub,
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
		'nrf54l.c',
		'nrf91.c',
	),
	dependencies: target_cortexm,
)

target_nxp = declare_dependency(
	sources: files(
		'imxrt.c',
		'kinetis.c',
		'nxpke04.c',
		's32k3xx.c',
	),
	dependencies: target_cortexm,
)

target_puya = declare_dependency(
	sources: files('puya.c'),
	dependencies: target_cortexm,
)

target_renesas_ra = declare_dependency(
	sources: files('renesas_ra.c'),
	dependencies: target_cortexm,
)

target_renesas_rz = declare_dependency(
	sources: files('renesas_rz.c'),
	dependencies: target_cortexar,
)

# If the Cortex-A/R target is not enabled, don't enable Renesas RZ part support
if not is_firmware_build or enabled_targets.contains('cortexar')
target_renesas = [
	target_renesas_ra,
	target_renesas_rz,
]
else
target_renesas = target_renesas_ra
endif

target_rp = declare_dependency(
	sources: files(
		'rp2040.c',
		'rp2350.c',
	) + rp2040_stub,
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

target_stm_common = declare_dependency(
	sources: files(
		'stm32_common.c',
	)
)

target_stm32f1_deps = [target_cortexm, target_stm_common]
# If the 32-bit RISC-V target is not enabled, don't enable GD32VF103 support
if not is_firmware_build or enabled_targets.contains('riscv32')
	target_stm32f1_deps += [target_riscv32]
endif

target_stm32f1 = declare_dependency(
	sources: files(
		'stm32f1.c',
	),
	dependencies: target_stm32f1_deps,
)

target_stm32f4 = declare_dependency(
	sources: files(
		'stm32f4.c',
	),
	dependencies: [target_cortexm, target_stm_common],
)

target_ch32 = declare_dependency(
	sources: files(
		'ch32f1.c',
	),
	dependencies: target_stm32f1,
)

target_stm = declare_dependency(
	sources: files(
		'stm32g0.c',
		'stm32h5.c',
		'stm32h7.c',
		'stm32l0.c',
		'stm32l4.c',
		'stm32mp15.c',
		'stm32wb0.c',
	),
	dependencies: [target_cortexm, target_stm_common, target_stm32f1, target_stm32f4],
)

target_gd32 = declare_dependency(
	dependencies: [target_stm32f1, target_stm32f4],
	compile_args: ['-DCONFIG_GD32=1'],
)

target_mm32 = declare_dependency(
	dependencies: target_stm32f1,
	compile_args: ['-DCONFIG_MM32=1'],
)

target_at32f4 = declare_dependency(
	sources: files(
		'at32f43x.c',
	),
	dependencies: [target_cortexm, target_stm_common]
)

target_ti_cortexm = declare_dependency(
	sources: files(
		'lmi.c',
		'msp432e4.c',
		'msp432p4.c',
		'mspm0.c'
	) + lmi_stub,
	dependencies: target_cortexm,
)

target_ti_cortexa = declare_dependency(
	sources: files(
		'am335x.c',
		'icepick.c'
	),
	dependencies: target_cortexar,
	compile_args: ['-DENABLE_TI_SITARA=1'],
)
# If the Cortex-A/R target is not enabled, don't enable TI AM335x part support
if enabled_targets.contains('cortexar')
	target_ti = [
		target_ti_cortexm,
		target_ti_cortexa,
	]
else
	target_ti = target_ti_cortexm
endif

target_xilinx = declare_dependency(
	sources: files(
		'zynq7000.c',
	),
	dependencies: target_cortexar,
	compile_args: ['-DCONFIG_XILINX=1'],
)

if is_firmware_build
	# Convert targets option list into a list of dependencies
	enabled_target_deps = []
	foreach target : enabled_targets
		enabled_target_deps += get_variable(f'target_@target@')
	endforeach

	# Check if we should enable the RISC-V Debug remote protocol acceleration and do so if required
	enable_riscv_accel = get_option('enable_riscv_accel')
	if enable_riscv_accel
		enabled_target_deps += declare_dependency(
			compile_args: ['-DCONFIG_RISCV_ACCEL=1'],
			dependencies: riscv_jtag_dtm,
		)
	endif

	summary(
		'RISC-V acceleration',
		enable_riscv_accel,
		bool_yn: true,
		section: 'Remote Protocol',
	)

	# BMD target dependency
	bmd_targets = declare_dependency(
		include_directories: target_common_includes,
		sources: target_common_sources,
		dependencies: enabled_target_deps,
	)
endif

libbmd_target_deps = [
	# Enable all architectures for libbmd
	target_cortexar,
	target_cortexm,
	target_riscv32,
	target_riscv64,
	# Enable all targets for libbmd
	target_apollo3,
	target_at32f4,
	target_ch32,
	target_ch579,
	target_efm,
	target_gd32,
	target_hc32,
	target_lpc,
	target_mm32,
	target_nrf,
	target_nxp,
	target_puya,
	target_renesas_ra,
	target_renesas_rz,
	target_rp,
	target_sam,
	target_stm,
	target_ti_cortexm,
	target_ti_cortexa,
	target_xilinx,
]

# Define the libbmd target dependency
libbmd_targets = declare_dependency(
	include_directories: target_common_includes,
	sources: target_common_sources,
	dependencies: libbmd_target_deps,
)

if is_firmware_build
	# Build a dictionary of the targets that have been selected for the firmware build
	target_states = {}
	foreach target_id, target_name : target_names
		target_states += {target_name: target_id in enabled_targets}
	endforeach

	# Include the target enable list in the summary output
	summary(
		target_states,
		bool_yn: true,
		section: 'Targets',
	)
endif
