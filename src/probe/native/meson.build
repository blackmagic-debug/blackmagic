
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

probe_common_link_args = [
]

probe_link_args += [
	'-T@0@'.format('blackmagic.ld'),
	'-Wl,-Ttext=0x8002000',
]

# probe_dfu_sources += files(
# 	probe_common_sources,
# 	'usbdfu.c',
# )

# probe_dfu_link_args = [
# 	probe_common_link_args
# ]

# probe_dfu_available = true
