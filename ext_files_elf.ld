SECTIONS
{
	.data : { *(.data*) *(.rodata*) }
	/DISCARD/ : { *(.text*) *(.bss*) *(.comment*) *(.note*) *(.eh_frame*) *(.rel*) *(.debug*) *(.MIPS.abiflags) }
}