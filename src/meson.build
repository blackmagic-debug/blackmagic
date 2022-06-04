
src_includes = include_directories(
	'.',
	'include',
)

src_sources = files(
	'command.c',
	'hex_utils.c',
	'main.c',
	'crc32.c',
	'remote.c',
	'gdb_hostio.c',
	'rtt.c',
	'exception.c',
	'gdb_packet.c',
	'gdb_main.c',
	'morse.c',
	'timing.c',
)

subdir('target')

subdir('probe')

subdir('platform')
