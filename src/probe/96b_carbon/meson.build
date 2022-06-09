
platform = 'stm32'
mcu_family = 'stm32f4'

probe_includes += include_directories(
	'.',
)

probe_sources += files(
	'platform.c',
)

probe_args += [
	'-D_96B_CARBON',
	'-DDFU_SERIAL_LENGTH=9',
]

probe_link_args += [
	'-Wl,--defsym,_stack=0x20006000',
	'-T@0@'.format('96b_carbon.ld'),
]
