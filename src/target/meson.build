
targets = get_option('targets')

target_includes = include_directories(
	'.',
)

target_sources = files(
	'target.c',
	'adiv5.c',
	'adiv5_swdp.c',
	'adiv5_jtagdp.c',
	'jtag_scan.c',
	'jtag_devs.c',
	'target_probes.c',
	# 'swdptap_generic.c',
	# 'jtagtap_generic.c',
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
		{'files': ['lpc_common.c', 'lpc15xx.c', 'lpc43xx.c', 'lpc546xx.c', 'lpc17xx.c', 'lpc11xx.c'],
		 'depends': ['cortexm']},
	'nrf':
		{'files': ['nrf51.c'],
		 'depends': ['cortexm']},
	'nxp':
		{'files': ['nxpke04.c', 'kinetis.c'],
		 'depends': ['cortexm']},
	'rp':
		{'files': ['rp.c'],
		 'depends': ['cortexm']},
	'sam':
		{'files': ['samx5x.c', 'sam3x.c', 'samd.c', 'sam4l.c'],
		 'depends': ['cortexm']},
	'stm':
		{'files': ['stm32f1.c', 'ch32f1.c', 'stm32g0.c', 'stm32l4.c', 'stm32l0.c', 'stm32f4.c', 'stm32h7.c',],
		 'depends': ['cortexm']},
	'ti':
		{'files': ['lmi.c', 'msp432.c'],
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
