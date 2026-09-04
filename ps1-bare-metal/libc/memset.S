.set noreorder
.set nomacro
.set noat

// a compact, fast memset for PS1

#define dst $a0
#define value $a1
#define size $a2

#define target $a3

.section .text.memset, "ax", @progbits
.type memset, @function
.global memset
.balign 16
memset:
	// if either dst or size is misaligned, just do the byte version
	or $at, dst, size
	andi $at, 3
	bnez $at, .Lbyte_loop
	addu target, dst, size // branch delay slot

	// early return on size 0
	beqz size, .Lreturn

	// repeat the byte value into a word
	sll $at, value, 8 // branch delay slot
	or value, $at
	sll $at, value, 16
	or value, $at

	// store a word at a time
	// since memory access is so slow, no sense in batching more than one word
.Lword_loop:
	addiu dst, 4
	bne dst, target, .Lword_loop
	sw value, -4(dst) // branch delay slot

	jr $ra
	subu $v0, target, size // branch delay slot

	nop // align the last instruction below to the end of a cache line

	// fallback: store a byte at a time
.Lbyte_loop:
	addiu dst, 1
	bne dst, target, .Lbyte_loop
	sb value, -1(dst) // branch delay slot

.Lreturn:
	jr $ra
	subu $v0, target, size // branch delay slot
