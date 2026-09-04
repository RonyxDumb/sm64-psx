#!/bin/bash
if [[ "$1" == -* ]]; then
	mipsel-none-elf-objdump -Mreg_names=eabi32 build/us_psx/sm64.elf $@
else
	mipsel-none-elf-objdump -Mreg_names=eabi32 build/us_psx/sm64.elf --disassemble=$@
fi