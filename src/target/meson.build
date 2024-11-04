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

# Conditional file compilation based on target selection
# We declare a dependency for each target group with the source files required
# these dependencies are then added to the BMD core, conditinal on the targets option
# NOTE: sourceset module might be an alternative to this method (unexplored)
all_targets = []

arm_coresight = declare_dependency(
	sources: files('arm_coresight_cti.c'),
)

cortex_common = declare_dependency(
	sources: files('cortex.c'),
)

target_cortexar = declare_dependency(
	sources: files('cortexar.c'),
	dependencies: cortex_common,
	compile_args: ['-DCONFIG_CORTEXAR=1'],
	variables: {
		'description': 'Cortex-A/R support'
	}
)
all_targets += target_cortexar

target_cortexm = declare_dependency(
	sources: files('cortexm.c'),
	dependencies: cortex_common,
	compile_args: ['-DCONFIG_CORTEXM=1'],
	variables: {
		'description': 'Cortex-M support'
	}
)
all_targets += target_cortexm

target_cortexa_armv8 = declare_dependency(
	sources: files('cortexa_armv8.c'),
	dependencies: [cortex_common, arm_coresight],
	variables: {
		'description': 'Cortex-A armv8 support'
	}
)
all_targets += target_cortexa_armv8

riscv_jtag_dtm = declare_dependency(
	sources: files(
		'riscv_jtag_dtm.c',
	),
)

riscv_common = declare_dependency(
	sources: files(
		'riscv_debug.c',
		'riscv_adi_dtm.c',
	),
	compile_args: ['-DCONFIG_RISCV=1'],
	dependencies: riscv_jtag_dtm,
)

target_riscv32 = declare_dependency(
	sources: files('riscv32.c'),
	dependencies: riscv_common,
	variables: {
		'description': 'RISC-V 32-bit support'
	},
)
all_targets += target_riscv32

target_riscv64 = declare_dependency(
	sources: files('riscv64.c'),
	dependencies: riscv_common,
	variables: {
		'description': 'RISC-V 64-bit support'
	},
)
all_targets += target_riscv64

target_apollo3 = declare_dependency(
	sources: files('apollo3.c'),
	compile_args: ['-DCONFIG_APOLLO3=1'],
	dependencies: target_cortexm,
	variables: {
		'description': 'Ambiq Apollo3 parts'
	},
)
all_targets += target_apollo3

target_ch579 = declare_dependency(
	sources: files('ch579.c'),
	compile_args: ['-DCONFIG_CH579=1'],
	dependencies: target_cortexm,
	variables: {
		'description': 'WinChipHead CH579'
	},
)
all_targets += target_ch579

target_efm = declare_dependency(
	sources: files(
		'efm32.c'
	) + efm32_stub,
	compile_args: ['-DCONFIG_EFM32=1'],
	dependencies: target_cortexm,
	variables: {
		'description': 'Energy Micro parts'
	},
)
all_targets += target_efm

target_hc32 = declare_dependency(
	sources: files('hc32l110.c'),
	compile_args: ['-DCONFIG_HC32=1'],
	dependencies: target_cortexm,
	variables: {
		'description': 'HC32 parts'
	},
)
all_targets += target_hc32

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
	compile_args: ['-DCONFIG_LPC=1'],
	dependencies: target_cortexm,
	variables: {
		'description': 'LPC series parts'
	},
)
all_targets += target_lpc

target_nrf = declare_dependency(
	sources: files(
		'nrf51.c',
		'nrf54l.c',
		'nrf91.c',
	),
	compile_args: ['-DCONFIG_NRF=1'],
	dependencies: target_cortexm,
	variables: {
		'description': 'nRF series parts'
	},
)
all_targets += target_nrf

target_nxp = declare_dependency(
	sources: files(
		'imxrt.c',
		'kinetis.c',
		'nxpke04.c',
		's32k3xx.c',
	),
	compile_args: ['-DCONFIG_NXP=1'],
	dependencies: target_cortexm,
	variables: {
		'description': 'Kinetis series parts'
	},
)
all_targets += target_nxp

target_puya = declare_dependency(
	sources: files('puya.c'),
	compile_args: ['-DCONFIG_PUYA=1'],
	dependencies: target_cortexm,
	variables: {
		'description': 'Puya PY32 series parts'
	},
)
all_targets += target_puya

target_renesas_ra = declare_dependency(
	sources: files('renesas_ra.c'),
	compile_args: ['-DCONFIG_RA=1'],
	dependencies: target_cortexm,
	variables: {
		'description': 'Renesas RA parts'
	},
)
all_targets += target_renesas_ra

target_renesas_rz = declare_dependency(
	sources: files('renesas_rz.c'),
	compile_args: ['-DCONFIG_RZ=1'],
	dependencies: target_cortexar,
	variables: {
		'description': 'Renesas RZ parts'
	},
)
all_targets += target_renesas_rz

target_renesas = [target_renesas_ra, target_renesas_rz]

target_rp = declare_dependency(
	sources: files(
		'rp2040.c',
		'rp2350.c',
	) + rp2040_stub,
	compile_args: ['-DCONFIG_RP=1'],
	dependencies: target_cortexm,
	variables: {
		'description': 'Raspberry Pi MCUs (RP2040, RP2350)'
	},
)
all_targets += target_rp

target_sam = declare_dependency(
	sources: files(
		'sam3x.c',
		'sam4l.c',
		'samd.c',
		'samx5x.c',
	),
	compile_args: ['-DCONFIG_SAM=1'],
	dependencies: target_cortexm,
	variables: {
		'description': 'ATSAM series parts'
	},
)
all_targets += target_sam

stm_common = declare_dependency(
	sources: files(
		'stm32_common.c',
	)
)

target_stm32f1 = declare_dependency(
	sources: files(
		'stm32f1.c',
	),
	dependencies: [target_cortexm, stm_common],
	variables: {
		'description': 'STM32F1 parts'
	},
)
all_targets += target_stm32f1

target_stm32f4 = declare_dependency(
	sources: files(
		'stm32f4.c',
	),
	dependencies: [target_cortexm, stm_common],
	variables: {
		'description': 'STM32F4 parts'
	},
)
all_targets += target_stm32f4

target_ch32 = declare_dependency(
	sources: files(
		'ch32f1.c',
	),
	compile_args: ['-DCONFIG_CH32=1'],
	dependencies: target_stm32f1,
	variables: {
		'description': 'WinChipHead CH32 ARM Cortex parts'
	},
)
all_targets += target_ch32

target_ch32v = declare_dependency(
	sources: files(
		'ch32vx.c',
	),
	dependencies: target_riscv32,
	variables: {
		'description': 'WinChipHead CH32 RISC-V parts'
	},
)
all_targets += target_ch32v

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
	compile_args: ['-DCONFIG_STM=1'],
	dependencies: [target_cortexm, stm_common, target_stm32f1, target_stm32f4],
	variables: {
		'description': 'STM32 parts'
	},
)
all_targets += target_stm

target_gd32 = declare_dependency(
	dependencies: [target_stm32f1, target_stm32f4],
	compile_args: ['-DCONFIG_GD32=1'],
	variables: {
		'description': 'GigaDevice parts'
	},
)
all_targets += target_gd32

target_gd32_rv = declare_dependency(
	dependencies: [target_gd32, target_riscv32],
	compile_args: ['-DCONFIG_GD32=1'],
	variables: {
		'description': 'GigaDevice Risc-V parts'
	},
)
all_targets += target_gd32_rv

target_mm32 = declare_dependency(
	dependencies: target_stm32f1,
	compile_args: ['-DCONFIG_MM32=1'],
	variables: {
		'description': 'MindMotion parts'
	},
)
all_targets += target_mm32

target_at32f4 = declare_dependency(
	sources: files(
		'at32f43x.c',
	),
	compile_args: ['-DCONFIG_AT32=1'],
	dependencies: [target_cortexm, stm_common],
	variables: {
		'description': 'Arterytek parts'
	},
)
all_targets += target_at32f4

target_ti = declare_dependency(
	sources: files(
		'lmi.c',
		'msp432e4.c',
		'msp432p4.c',
		'mspm0.c'
	) + lmi_stub,
	compile_args: ['-DCONFIG_TI=1'],
	dependencies: target_cortexm,
	variables: {
		'description': 'Texas Instruments parts'
	},
)
all_targets += target_ti

target_xilinx = declare_dependency(
	sources: files(
		'zynq7000.c',
	),
	dependencies: target_cortexar,
	compile_args: ['-DCONFIG_XILINX=1'],
	variables: {
		'description': 'Xilinx parts'
	},
)
all_targets += target_xilinx

if is_firmware_build
	# Convert targets option list into a list of dependencies
	enabled_bmd_targets = []
	foreach target : get_option('targets')
		enabled_bmd_targets += get_variable(f'target_@target@')
	endforeach

	# Check that at least one target is enabled
	assert(
		enabled_bmd_targets.length() > 0,
		'''No debug targets enabled, please enable at least one target to build.
	See the 'targets' option for a list of available targets.
	''',
	)

	# BMD target dependency
	bmd_targets = declare_dependency(
		include_directories: target_common_includes,
		sources: target_common_sources,
		dependencies: enabled_bmd_targets,
	)
endif

# Define the libbmd target dependency, enable all architectures and targets
libbmd_targets = declare_dependency(
	include_directories: target_common_includes,
	sources: target_common_sources,
	dependencies: all_targets,
)

if is_firmware_build
	# Build a dictionary of the targets that have been selected for the firmware build
	target_summary = {}
	foreach target : all_targets
		# Get the target description
		target_description = target.get_variable('description')

		# Add the target enabled status to the summary
		target_summary += {target_description: target in enabled_bmd_targets}
	endforeach

	# Include the target enable list in the summary output
	summary(
		target_summary,
		bool_yn: true,
		section: 'Targets',
	)
endif
