
platform = 'tm4c'
mcu_family = 'lm4f'

probe_includes += include_directories(
	'.',
)

probe_sources += files(
	'platform.c',
)

probe_args += [
	'-DDFU_SERIAL_LENGTH=9',
	'-DSERIAL_NO=1',
	'-DTARGET_IS_BLIZZARD_RB1',
	'-DPART_TM4C123GH6PM',
]

probe_link_args += [
	'-T@0@'.format('tm4c.ld'),
]
