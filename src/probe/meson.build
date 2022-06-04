
probe = get_option('probe')

# dynamically get a list of valid probe targets
# only runs on python3, todo skip check on failure
probe_list = run_command(
	['python',
	 '-c',
	 'import os; print(*[os.path.basename(x[0]) for x in os.walk(os.getcwd()) if x[0] != os.getcwd()], sep=\',\', end=\'\')'],
	capture: true, check: false).stdout().split(',')

if probe not in probe_list
    message('Available target probes:', probe_list)
    error(f'The target probe @probe@ is not a valid probe')
endif

probe_includes = []
probe_sources = []
probe_args = []
probe_link_args = []
probe_dfu_link_args = []
probe_deps = []

subdir(probe)

# probe_dfu_available = false
# probe_dfu_sources = []
