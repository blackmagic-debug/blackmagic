
platform = 'stm32'
mcu_family = 'stm32f1'

probe_includes += include_directories(
	'.',
)

probe_sources += files(
	'platform.c',
	'platform_common.c',
)

probe_args += [
	'-DDFU_SERIAL_LENGTH=9',
]

probe_link_args += [
	'-T@0@'.format('stlink.ld'),
	'-Wl,-Ttext=0x8002000',
]
