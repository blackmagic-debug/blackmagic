
targets = get_option('targets')

target_includes = include_directories(
	'.',
)

target_sources = files(
	'adiv5_jtagdp.c',
	'adiv5_swdp.c',
	'adiv5.c',
	'gdb_reg.c',
	'jtag_devs.c',
	'jtag_scan.c',
	'jtagtap_generic.c',
	'sfdp.c',
	'swdptap_generic.c',
	'target_flash.c',
	'target_probe.c',
	'target.c',
)

# targets specific files dictionary
target_dicts = {
	'cortexa':
		{'files': ['cortexa.c']},
	'cortexm':
		{'files': ['cortexm.c']},
	'efm':
		{'files': ['efm32.c'],
		 'depends': ['cortexm']},
	'lpc':
		{'files': ['lpc_common.c', 'lpc11xx.c', 'lpc15xx.c', 'lpc17xx.c', 'lpc40xx.c', 'lpc43xx.c', 'lpc546xx.c', 'lpc55xx.c'],
		 'depends': ['cortexm']},
	'nrf':
		{'files': ['nrf51.c'],
		 'depends': ['cortexm']},
	'nxp':
		{'files': ['imxrt.c', 'kinetis.c', 'nxpke04.c'],
		 'depends': ['cortexm']},
	'rp':
		{'files': ['rp.c'],
		 'depends': ['cortexm']},
	'sam':
		{'files': ['sam3x.c', 'sam4l.c', 'samd.c', 'samx5x.c'],
		 'depends': ['cortexm']},
	'stm':
		{'files': ['ch32f1.c', 'stm32f1.c', 'stm32f4.c', 'stm32g0.c', 'stm32h7.c', 'stm32l0.c', 'stm32l4.c'],
		 'depends': ['cortexm']},
	'ti':
		{'files': ['lmi.c', 'msp432.c'],
		 'depends': ['cortexm']},
	'c':
		{'files': ['renesas.c'],
		 'depends': ['cortexm']},
}

# loop through selected targets and add respective sources, check for dependencies if present
foreach target: targets
	# check for valid target
	if target not in target_dicts.keys()
   		error('The target "@0@" is not a valid target: "@1@"'.format(target, ', '.join(target_dicts.keys())))
	endif

	# get 'sub' dictionary
	target_dict = target_dicts[target]

	# check for dependencies
	if target_dict.has_key('depends')
		foreach depend: target_dict['depends']
			if depend not in targets
				# it's funky to add to a list we're iterating over, but it works
				targets += depend
				message('The target "@0@" depends on "@1@", but this target is not enabled, enabling'.format(target, depend))
			endif
		endforeach
	endif

	# add target sources
	target_sources += files(target_dict['files'])
endforeach

# check for disabled targets and add to list for user feedback
disabled_targets = []
foreach target: target_dicts.keys()
	if target not in targets
		disabled_targets += target
	endif
endforeach

summary({'enabled': targets, 'disabled': disabled_targets}, section: 'Targets')
