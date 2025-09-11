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

subdir('flashstub')

target_common_includes = include_directories('.')

target_common_sources = files(
	'adi.c',
	'adiv5.c',
	'adiv5_jtag.c',
	'adiv5_swd.c',
	'adiv6.c',
	'gdb_reg.c',
	'jtag_devs.c',
	'jtag_scan.c',
	'onboard_flash.c',
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

arm_coresight = declare_dependency(
	sources: files('arm_coresight_cti.c'),
)

cortex_common = declare_dependency(
	sources: files('cortex.c'),
)

targets = {
	'cortexar': declare_dependency(
		sources: files('cortexar.c'),
		dependencies: cortex_common,
		compile_args: ['-DCONFIG_CORTEXAR=1'],
		variables: {
			'description': 'Cortex-A/R support'
		}
	),
	'cortexm': declare_dependency(
		sources: files('cortexm.c'),
		dependencies: cortex_common,
		compile_args: ['-DCONFIG_CORTEXM=1'],
		variables: {
			'description': 'Cortex-M support'
		}
	),
	'cortexa_armv8': declare_dependency(
		sources: files('cortexa_armv8.c'),
		dependencies: [cortex_common, arm_coresight],
		variables: {
			'description': 'Cortex-A armv8 support'
		}
	)
}

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

targets += {
	'riscv32': declare_dependency(
		sources: files('riscv32.c'),
		dependencies: riscv_common,
		variables: {
			'description': 'RISC-V 32-bit support'
		},
	)
}

targets += {
	'riscv64': declare_dependency(
		sources: files('riscv64.c'),
		dependencies: riscv_common,
		variables: {
			'description': 'RISC-V 64-bit support'
		},
	)
}

targets += {
	'apollo3': declare_dependency(
		sources: files('apollo3.c'),
		compile_args: ['-DCONFIG_APOLLO3=1'],
		dependencies: targets['cortexm'],
		variables: {
			'description': 'Ambiq Apollo3 parts'
		},
	)
}

targets += {
	'ch579': declare_dependency(
		sources: files('ch579.c'),
		compile_args: ['-DCONFIG_CH579=1'],
		dependencies: targets['cortexm'],
		variables: {
			'description': 'WinChipHead CH579'
		},
	)
}

targets += {
	'efm': declare_dependency(
		sources: files(
			'efm32.c'
		) + efm32_stub,
		compile_args: ['-DCONFIG_EFM32=1'],
		dependencies: targets['cortexm'],
		variables: {
			'description': 'Energy Micro parts'
		},
	)
}

targets += {
	'hc32': declare_dependency(
		sources: files('hc32l110.c'),
		compile_args: ['-DCONFIG_HC32=1'],
		dependencies: targets['cortexm'],
		variables: {
			'description': 'HC32 parts'
		},
	)
}

targets += {
	'lpc': declare_dependency(
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
		dependencies: targets['cortexm'],
		variables: {
			'description': 'LPC series parts'
		},
	)
}

targets += {
	'nrf': declare_dependency(
		sources: files(
			'nrf51.c',
			'nrf54l.c',
			'nrf91.c',
		),
		compile_args: ['-DCONFIG_NRF=1'],
		dependencies: targets['cortexm'],
		variables: {
			'description': 'nRF series parts'
		},
	)
}

targets += {
	'nxp': declare_dependency(
		sources: files(
			'imxrt.c',
			'kinetis.c',
			'nxpke04.c',
			's32k3xx.c',
		),
		compile_args: ['-DCONFIG_NXP=1'],
		dependencies: targets['cortexm'],
		variables: {
			'description': 'Kinetis series parts'
		},
	)
}

targets += {
	'puya': declare_dependency(
		sources: files('puya.c'),
		compile_args: ['-DCONFIG_PUYA=1'],
		dependencies: targets['cortexm'],
		variables: {
			'description': 'Puya PY32 series parts'
		},
	)
}

targets += {
	'renesas_ra': declare_dependency(
		sources: files('renesas_ra.c'),
		compile_args: ['-DCONFIG_RA=1'],
		dependencies: targets['cortexm'],
		variables: {
			'description': 'Renesas RA parts'
		},
	),
	'renesas_rz': declare_dependency(
		sources: files('renesas_rz.c'),
		compile_args: ['-DCONFIG_RZ=1'],
		dependencies: targets['cortexar'],
		variables: {
			'description': 'Renesas RZ parts'
		},
	)
}
targets += {
	'renesas': declare_dependency(
		dependencies: [targets['renesas_ra'], targets['renesas_rz']],
		variables: {
			'description': 'Renesas parts (RA + RZ)'
		},
	)
}

targets += {
	'rp': declare_dependency(
		sources: files(
			'rp2040.c',
			'rp2350.c',
		) + rp2040_stub,
		compile_args: ['-DCONFIG_RP=1'],
		dependencies: targets['cortexm'],
		variables: {
			'description': 'Raspberry Pi MCUs (RP2040, RP2350)'
		},
	)
}

targets += {
	'sam': declare_dependency(
		sources: files(
			'sam3x.c',
			'sam4l.c',
			'samd.c',
			'samx5x.c',
		),
		compile_args: ['-DCONFIG_SAM=1'],
		dependencies: targets['cortexm'],
		variables: {
			'description': 'ATSAM series parts'
		},
	)
}

stm_common = declare_dependency(
	sources: files(
		'stm32_common.c',
	)
)

targets += {
	'stm32f1': declare_dependency(
		sources: files(
			'stm32f1.c',
		),
		dependencies: [targets['cortexm'], stm_common],
		variables: {
			'description': 'STM32F1 parts'
		},
	),
	'stm32f4': declare_dependency(
		sources: files(
			'stm32f4.c',
		),
		dependencies: [targets['cortexm'], stm_common],
		variables: {
			'description': 'STM32F4 parts'
		},
	)
}

targets += {
	'ch32': declare_dependency(
		sources: files(
			'ch32f1.c',
		),
		compile_args: ['-DCONFIG_CH32=1'],
		dependencies: targets['stm32f1'],
		variables: {
			'description': 'WinChipHead CH32 ARM Cortex parts'
		},
	),
	'ch32v': declare_dependency(
		sources: files(
			'ch32vx.c',
		),
		dependencies: targets['riscv32'],
		variables: {
			'description': 'WinChipHead CH32 RISC-V parts'
		},
	)
}

targets += {
	'stm': declare_dependency(
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
		dependencies: [targets['cortexm'], stm_common, targets['stm32f1'], targets['stm32f4']],
		variables: {
			'description': 'STM32 parts'
		},
	)
}

targets += {
	'gd32': declare_dependency(
		dependencies: [targets['stm32f1'], targets['stm32f4']],
		compile_args: ['-DCONFIG_GD32=1'],
		variables: {
			'description': 'GigaDevice parts'
		},
	),
	'gd32_rv': declare_dependency(
		dependencies: [targets['riscv32'], targets['stm32f1'], targets['stm32f4']],
		compile_args: ['-DCONFIG_GD32=1'],
		variables: {
			'description': 'GigaDevice Risc-V parts'
		},
	)
}

targets += {
	'mm32': declare_dependency(
		dependencies: targets['stm32f1'],
		compile_args: ['-DCONFIG_MM32=1'],
		variables: {
			'description': 'MindMotion parts'
		},
	)
}

targets += {
	'at32f4': declare_dependency(
		sources: files(
			'at32f43x.c',
		),
		compile_args: ['-DCONFIG_AT32=1'],
		dependencies: [targets['cortexm'], stm_common],
		variables: {
			'description': 'Arterytek parts'
		},
	)
}

targets += {
	'ti': declare_dependency(
		sources: files(
			'lmi.c',
			'msp432e4.c',
			'msp432p4.c',
			'mspm0.c'
		) + lmi_stub,
		compile_args: ['-DCONFIG_TI=1'],
		dependencies: targets['cortexm'],
		variables: {
			'description': 'Texas Instruments parts'
		},
	)
}

targets += {
	'xilinx': declare_dependency(
		sources: files(
			'zynq7000.c',
		),
		dependencies: targets['cortexar'],
		compile_args: ['-DCONFIG_XILINX=1'],
		variables: {
			'description': 'Xilinx parts'
		},
	)
}

if is_firmware_build
	# Convert targets option list into a list of dependencies
	enabled_bmd_targets = []
	foreach target_name : get_option('targets')
		enabled_bmd_targets += targets[target_name]
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
all_targets = []
foreach target_name, target_dep : targets
	all_targets += target_dep
endforeach
libbmd_targets = declare_dependency(
	include_directories: target_common_includes,
	sources: target_common_sources,
	dependencies: all_targets,
)

if is_firmware_build
	# Build a dictionary of the targets that have been selected for the firmware build
	target_summary = {}
	foreach target_name, target_dep : targets
		# Get the target description and add the enabled status to the summary
		target_description = target_dep.get_variable('description')
		target_summary += {target_description: target_dep in enabled_bmd_targets}
	endforeach

	# Include the target enable list in the summary output
	summary(
		target_summary,
		bool_yn: true,
		section: 'Targets',
	)
endif
