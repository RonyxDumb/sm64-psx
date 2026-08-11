#ifndef NDEBUG

.section .text.cold

ubsan_handler:
	addiu $sp, -8
	sw $ra, ($sp)

	jal printf_
	nop
	jal abort
	nop

	lw $ra, ($sp)
	jr $ra
	addiu $sp, 8

.macro define_handler name

.global __ubsan_handle_\name
__ubsan_handle_\name:
	b ubsan_handler
	la $a0, .Lstr_\name

.section .rodata
.Lstr_\name: .asciz "ubsan error: \name\n"

.section .text.cold

.endm

define_handler builtin_unreachable
define_handler type_mismatch_v1
define_handler pointer_overflow
define_handler out_of_bounds

#endif
