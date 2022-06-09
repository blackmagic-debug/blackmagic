project('Black Magic Firmware', 'c',
	version: 'v1.8',
	license: 'GPL-3.0',
	default_options: [
		'optimization=s',
		'debug=false',
		# 'warning_level=3',
		# 'werror=true',
		],
	meson_version: '>= 0.58.0',
	subproject_dir: 'lib',
)

if not meson.is_cross_build()
	error('Black Magic Firmware must be cross-compiled to the target probe/platform and cannot be built for the host machine')
endif

subdir('src')

# ----------------------------------------------------------------------------------------------------------------------
# Project wide flags
# ----------------------------------------------------------------------------------------------------------------------
c_args = [
  '-std=gnu99',
  '-Wno-char-subscripts',
  '-DPC_HOSTED=0',
]
link_args = []

# ----------------------------------------------------------------------------------------------------------------------
# System binaries
# ----------------------------------------------------------------------------------------------------------------------
size = find_program('size')
objcopy = find_program('objcopy')

# ----------------------------------------------------------------------------------------------------------------------
# Build targets
# ----------------------------------------------------------------------------------------------------------------------
out_name = meson.project_name().to_lower().underscorify()

link_args += [
	f'-Wl,-Map=@out_name@.map'
]

# get version control system tag
version = vcs_tag(
	command: ['git', 'describe', '--always', '--dirty', '--tags'],
	input: 'src/include/version.h.in',
	output: 'version.h',
	fallback: meson.project_version(),
)

elf = executable(f'@out_name@.elf',
	[src_sources, target_sources, platform_sources, probe_sources, version],
	c_args : [c_args, platform_args, probe_args],
	link_args: [link_args, platform_link_args, probe_link_args],
	dependencies : platform_deps,
	include_directories : [src_includes, target_includes, platform_includes, probe_includes],
)

bin = custom_target('bin',
	output: f'@out_name@.bin',
	input: elf,
	command: [objcopy, ['-O', 'binary', '@INPUT@', '@OUTPUT@']],
	depends: elf,
	build_by_default: true,
)
alias_target('bin', bin)

hex = custom_target('hex',
	input: elf,
	output: f'@out_name@.hex',
	command: [objcopy, ['-O', 'ihex', '@INPUT@', '@OUTPUT@']],
	depends: elf)
alias_target('hex', hex)

run_target('size',
	command: [size, elf.full_path(), '-B'],
	depends: elf)

# if probe_dfu_available
# dfu_elf = executable(f'@out_name@_dfu.elf',
# 	[src_dfu_sources, platform_dfu_sources, probe_dfu_sources, version],
# 	c_args : [c_args, platform_args, probe_args],
# 	link_args: [link_args, platform_link_args, probe_dfu_link_args],
# 	dependencies : platform_deps,
# 	include_directories : [src_includes, target_includes, platform_includes, probe_includes],
# 	build_by_default: false,
# )
# endif
