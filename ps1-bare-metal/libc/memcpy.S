.set noreorder
.set nomacro
.set noat

// a compact, fast memcpy for PS1

#define dst $a0
#define src $a1
#define size $a2

#define target $a3
#define aligned_target $t0

.section .text.memcpy, "ax", @progbits
.type memcpy, @function
.global memcpy
.balign 16
memcpy:
	// if any of src, dst, or size is not aligned to words, just do the byte version
	or $at, dst, src
	andi $at, 3
	bnez $at, .Lbyte_loop
	addu target, dst, size // branch delay slot

	// early return on size 0
	beqz size, .Lreturn
	srl aligned_target, target, 2 // branch delay slot
	sll aligned_target, 2

	// copy a word at a time
	// since memory access is so slow, no sense in batching more than one word
.Lword_loop:
	lw $at, (src)
	addiu src, 4
	addiu dst, 4
	bne dst, aligned_target, .Lword_loop
	sw $at, -4(dst) // branch delay slot

	// if not done, go to the byte loop
	// this little block fits in an icache line, thus the repetition
	bne dst, target, .Lbyte_loop
	nop // branch delay slot
	jr $ra
	subu $v0, target, size // branch delay slot

	nop // align the last instruction below to the end of a cache line

	// fallback: copy a byte at a time
.Lbyte_loop:
	lb $at, (src)
	addiu src, 1
	addiu dst, 1
	bne dst, target, .Lbyte_loop
	sb $at, -1(dst) // branch delay slot

.Lreturn:
	jr $ra
	subu $v0, target, size // branch delay slot
