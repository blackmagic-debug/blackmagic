#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
from argparse import ArgumentParser
from pathlib import Path
from subprocess import run
from concurrent.futures import ThreadPoolExecutor
from sys import exit

parser = ArgumentParser(
	description = 'Light-weight wrapper around clang-tidy to enable `make clang-tidy` or ' +
		'`ninja clang-tidy` to function properly',
	allow_abbrev = False
)
parser.add_argument('-s', required = True, type = str, metavar = 'sourcePath',
	dest = 'sourcePath', help = 'Path to the source directory to run clang-tidy in')
parser.add_argument('-p', type = str, metavar = 'buildPath',
	dest = 'buildPath', help = 'Path to the build directory containing a compile_commands.json')
parser.add_argument('-I', type = str, action = 'append', metavar = 'includePaths',
	dest = 'includePaths', default = [], help = 'Additional include paths to use')
args = parser.parse_args()

def globFiles():
	srcDir = Path(args.sourcePath)
	paths = set(('src', 'upgrade'))
	suffixes = set(('c', 'h'))
	for path in paths:
		for suffix in suffixes:
			yield srcDir.glob('{}/**/*.{}'.format(path, suffix))

def gatherFiles():
	for fileGlob in globFiles():
		for file in fileGlob:
			yield file

extraArgs = [
	'-Isrc/target', '-Isrc', '-Isrc/include', '-Isrc/platforms/common',
	'-Isrc/platforms/native', '-Ilibopencm3/include', '-Isrc/platforms/stm32'
] + args.includePaths

for i, arg in enumerate(extraArgs):
	extraArgs[i] = f'--extra-arg={arg}'

if args.buildPath is not None:
	print(f'Adding build path "{args.buildPath}" to extraArgs')
	extraArgs += ['-p', args.buildPath]

futures = []
returncode = 0
with ThreadPoolExecutor() as pool:
	for file in gatherFiles():
		futures.append(pool.submit(run, ['clang-tidy'] + extraArgs + [str(file)]))
	returncode = max((future.result().returncode for future in futures))
exit(returncode)
