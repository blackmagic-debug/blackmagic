
platform_sources += files(
	'usbuart.c',
	'traceswo.c',
	'gdb_if.c',
)

if mcu_family in ['lm4f']
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
else
	error(f'Unsupported MCU family @mcu_family@ for platform @platform@')
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
	'-Wl,-gc-sections',
	'-L@0@'.format(meson.current_source_dir()),
]

# ugly hack just to get things compiling for now :), libopencm3 likely should be a subproject
compiler = meson.get_compiler('c')
libopencm3_dep = compiler.find_library(f'libopencm3_@0@'.format(mcu_family), dirs: join_paths(meson.project_source_root(), 'libopencm3/lib'))
platform_includes += include_directories('../../../libopencm3/include')
platform_link_args += ['-L@0@'.format(join_paths(meson.project_source_root(), 'libopencm3/lib'))]

platform_deps += [libopencm3_dep]

summary({'family': mcu_family}, section: 'Platform')
