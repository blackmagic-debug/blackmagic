#!/bin/sh
#
# Convert the output of objdump to an array of half-words that can be
# included into C code to represent the stub.
#
# Invoke with something like this:
#
#   objdump -z -d FILE.o | dump-to-array.sh > FILE.stub
#

sed -E "/^[ ][ ]*[0-9a-fA-F]+:/!d; s/([0-9a-fA-F]+):[ \t]+([0-9a-fA-F]+).*/[0x\1\/2] = 0x\2,/ ; s/0x(....)(....),/0x\2, 0x\1,/"
