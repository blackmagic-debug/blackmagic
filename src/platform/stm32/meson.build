
platform_includes += include_directories(
	'.',
)

platform_sources += files(
	'serialno.c',
	'usbuart.c',
	'traceswo.c',
	'traceswodecode.c',
	'traceswoasync.c',
	# 'rtt_if.c',
	'gdb_if.c',
	'timing_stm32.c',
)

# platform_dfu_sources += files(
# 	platform_common_sources,
# 	'dfucore.c',
# )

if mcu_family in ['stm32f1']

# platform_dfu_sources += files(
# 	'dfu_f1.c',
# )
platform_args += [
    '-mcpu=cortex-m3',
]
platform_link_args += [
    '-mcpu=cortex-m3',
]

elif mcu_family in ['stm32f4']

# platform_dfu_sources += files(
# 	'dfu_f4.c',
# )
platform_args += [
    '-mcpu=cortex-m4',
	'-mfloat-abi=hard',
	'-mfpu=fpv4-sp-d16',
]
platform_link_args += [
    '-mcpu=cortex-m4',
	'-mfloat-abi=hard',
	'-mfpu=fpv4-sp-d16',
]

endif

platform_args += [
	'-mthumb',
	'-nostdlib',
    '-nostartfiles',
    '-ffreestanding',
	'-D@0@'.format(mcu_family.to_upper()),
]

platform_link_args += [
	'-lc',
	'-lgcc',
	'-lnosys',
	'-nostdlib',
    '-nostartfiles',
    '-ffreestanding',
    '--specs=nano.specs',
    '--specs=nosys.specs',
	'-Wl,-gc-sections',
	'-L@0@'.format(meson.current_source_dir()),
]

# ugly hack just to get things compiling for now :), libopencm3 likely should be a subproject
compiler = meson.get_compiler('c')
libopencm3_dep = compiler.find_library(f'libopencm3_@0@'.format(mcu_family), dirs: join_paths(meson.project_source_root(), 'libopencm3/lib'))
platform_includes += include_directories('../../../libopencm3/include')
platform_link_args += ['-L@0@'.format(join_paths(meson.project_source_root(), 'libopencm3/lib'))]

platform_deps += [libopencm3_dep]
