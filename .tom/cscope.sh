#!/bin/bash
set -e
cd $(dirname $0)
TOP=$(readlink -f ..)
rm -rf .cscope
mkdir .cscope
FILES=$(readlink -f .cscope/cscope.files)
(find $TOP -name '*.[ch]' >$FILES)
(cd .cscope ; cscope -kbq)
