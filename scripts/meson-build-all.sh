#!/bin/bash
#
# This file is part of the Black Magic Debug project.
#
# Copyright (C) 2024 1BitSquared <info@1bitsquared.com>
# Written by Piotr Esden-Tempski <piotr@1bitsquared.com>
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this
#    list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
#
# 3. Neither the name of the copyright holder nor the names of its
#    contributors may be used to endorse or promote products derived from
#    this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

set -e

build () {
	cross_file=$1
	name=$(basename -s .ini "${cross_file}")
	echo "Configuring ${name}..."
	meson setup build-${name} --cross-file ${cross_file}
	echo "Compiling ${name}..."
	meson compile -C build-${name}
}

echo "Welcome to the meson build-all script!"
echo "This script will attempt to compile all the configurations found in the cross-file directory."
echo "Each configuration will be built in the respective \`build-NAME\` subdirectory."

if [[ ! -d cross-file ]]; then
	echo "Could not find the \`cross-file\` directory. Make sure to run the script from the blackmagic firware root directory!"
	exit 1
fi

for i in cross-file/*.ini; do
	build $i
done
