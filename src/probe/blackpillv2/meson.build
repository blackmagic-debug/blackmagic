
platform = 'stm32'
mcu_family = 'stm32f4'

probe_includes += include_directories(
	'.',
)

probe_sources += files(
	'platform.c',
)

probe_link_args += [
	'-T@0@'.format('blackpillv2.ld'),
]

if get_option('bmp_bootloader')
	probe_args += [
		'-DDFU_SERIAL_LENGTH=9',
	]
	probe_link_args += [
		'-Wl,-Ttext=0x8004000',
	]
else
	probe_args += [
		'-DDFU_SERIAL_LENGTH=13',
	]
endif

if get_option('bmp_bootloader')
	summary({'bootloader': 'Blackmagic bootloader load at 0x8004000'}, section: 'Probe')
else
	summary({'bootloader': 'None'}, section: 'Probe')
endif
