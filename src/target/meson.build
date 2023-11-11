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

# !TODO: selective include
target_sources = files(
	'adiv5.c',
	'adiv5_jtag.c',
	'adiv5_swd.c',
	'at32f43x.c',
	'ch32f1.c',
	'cortex.c',
	'cortexar.c',
	'cortexm.c',
	'efm32.c',
	'gdb_reg.c',
	'hc32l110.c',
	'imxrt.c',
	'jtag_devs.c',
	'jtag_scan.c',
	'kinetis.c',
	'lmi.c',
	'lpc11xx.c',
	'lpc15xx.c',
	'lpc17xx.c',
	'lpc40xx.c',
	'lpc43xx.c',
	'lpc546xx.c',
	'lpc55xx.c',
	'lpc_common.c',
	'msp432e4.c',
	'msp432p4.c',
	'nrf51.c',
	'nrf91.c',
	'nxpke04.c',
	'renesas.c',
	'riscv32.c',
	'riscv64.c',
	'riscv_debug.c',
	'riscv_jtag_dtm.c',
	'rp.c',
	'sam3x.c',
	'sam4l.c',
	'samd.c',
	'samx5x.c',
	'sfdp.c',
	'spi.c',
	'stm32f1.c',
	'stm32f4.c',
	'stm32g0.c',
	'stm32h5.c',
	'stm32h7.c',
	'stm32l0.c',
	'stm32l4.c',
	'stm32mp15.c',
	'zynq7000.c',
	'target.c',
	'target_flash.c',
	'target_probe.c',
)

# Handle generic routines, used when low-level routines are not available
if (get_option('no_own_ll'))
	target_sources += files(
		'jtagtap_generic.c',
		'swdptap_generic.c',
	)
endif

# BMD target dependency
bmd_targets = declare_dependency(
	include_directories: target_common_includes,
	sources: target_sources,
)
