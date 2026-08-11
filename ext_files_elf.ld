SECTIONS
{
	.data : { *(.data*) *(.rodata*) }
	/DISCARD/ : { *(.*) }
}