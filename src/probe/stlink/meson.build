
platform = 'stm32'
mcu_family = 'stm32f1'

probe_includes += include_directories(
	'.',
)

probe_sources += files(
	'platform.c',
	'stlink_common.c',
)

probe_link_args += [
	'-T@0@'.format('stlink.ld'),
]

if get_option('bmp_bootloader')
	probe_args += [
		'-DDFU_SERIAL_LENGTH=9',
	]
	probe_link_args += [
		'-Wl,-Ttext=0x8002000',
	]
else
	probe_args += [
		'-DDFU_SERIAL_LENGTH=25',
	]
	probe_link_args += [
		'-Wl,-Ttext=0x8004000',
	]
endif

if get_option('bmp_bootloader')
	summary({'bootloader': 'Blackmagic bootloader load at 0x8002000'}, section: 'Probe')
else
	summary({'bootloader': 'Stock ST bootloader load at 0x8004000'}, section: 'Probe')
endif
