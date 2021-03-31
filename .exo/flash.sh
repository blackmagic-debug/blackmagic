#!/bin/bash
cd $(dirname $0)
sudo dfu-util -a 0 -s 0x08002000:leave:force -D /i/tom/git/blackmagic/src/blackmagic.bin

