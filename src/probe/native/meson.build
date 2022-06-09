
platform = 'stm32'
mcu_family = 'stm32f1'

probe_includes += include_directories(
	'.',
)

probe_sources += files(
	'platform.c',
)

probe_args += [
	'-DDFU_SERIAL_LENGTH=9',
	'-DBLACKMAGIC',
]

probe_link_args += [
	'-T@0@'.format('blackmagic.ld'),
	'-Wl,-Ttext=0x8002000',
]

summary({'demo': 'just a demo info for probes'}, section: 'Probe')
