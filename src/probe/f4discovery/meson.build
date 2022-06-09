
platform = 'stm32'
mcu_family = 'stm32f4'

probe_includes += include_directories(
	'.',
)

probe_sources += files(
	'platform.c',
)

probe_args += [
	'-DDFU_SERIAL_LENGTH=9',
]

probe_link_args += [
	'-T@0@'.format('f4discovery.ld'),
	'-Wl,-Ttext=0x8004000',
]
