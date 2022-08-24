#!/usr/bin/env python
#
# This file is part of the Black Magic Debug project.
#
# Copyright (C) 2022 1BitSquared <info@1bitsquared.com>
# Written by Rafael Silva <perigoso@riseup.net>
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

import sys
import re


class Region:
    def __init__(self, name: str, used: int, total: int):
        self.name = name
        self.used = used
        self.total = total

    @property
    def percentage(self) -> float:
        return self.used/self.total*100

    def __sub__(self, __o: object) -> object:
        return Region(self.name, self.used - __o.used, self.total)

    def __eq__(self, __o: object) -> bool:
        return self.name == __o.name

    def __str__(self):
        return f'{self.name}: {self.used:+0d} / {self.total:d} B ({self.percentage:+0.2f}%)'


def get_regions(string: str) -> list:
    regex = r"^\s+(\S+):\s+(\d+)\s(\S+)\s+(\d+)\s(\S+)\s+([\d.]+)%$"
    # match lines like:
    #   rom:    117572 B    128 KB  89.70%
    # groups: [1: name, 2: used, 3: unit, 4: total, 5: unit, 6: percent]

    UNIT_MULTIPLIER = {'B': 1, 'KB': 1024}

    regions = []
    for match in re.finditer(regex, string, re.MULTILINE):
        regions.append(
            Region(
                match.group(1),
                int(match.group(2)) * UNIT_MULTIPLIER[match.group(3)],
                int(match.group(4)) * UNIT_MULTIPLIER[match.group(5)]))
    return regions


if __name__ == '__main__':
    if len(sys.argv) != 3:
        print(f'Usage: {sys.argv[0]} <base_file> <diff_file>')
        sys.exit(1)

    with open(sys.argv[1], 'r') as base_file:
        base_regions = get_regions(base_file.read())

    with open(sys.argv[2], 'r') as diff_file:
        diff_regions = get_regions(diff_file.read())

    for region in base_regions:
        if region in diff_regions:
            print(diff_regions[diff_regions.index(region)] - region)
