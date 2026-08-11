.set noreorder

// a compact, fast memset for PS1

#define dst $a0
#define value $a1
#define size $a2
#define target $v0
#define aligned_target $v1
#define alignment $t0
#define tmp $t1

.section .text.memset, "ax", @progbits
.type memset, @function
.global memset
memset:
	// if less than 4 bytes, just write in bytes
	bltu size, 4, .Lmemset_byte
	addu target, dst, size // branch delay slot

	// if dst not aligned to words, just write in bytes
	andi alignment, dst, 3
	bnez alignment, .Lmemset_byte_skipcheck // skip the size check because it is already known to be over 4 bytes here

	// repeat the value to fill a word
	sll tmp, value, 8 // branch delay slot
	or value, tmp
	sll tmp, value, 16
	or value, tmp
	// write words
	srl aligned_target, target, 2
	sll aligned_target, 2
	addiu aligned_target, -4
.Lmemset_word_loop:
	sw value, (dst)
	bne dst, aligned_target, .Lmemset_word_loop
	addiu dst, 4 // branch delay slot

	// write any remaining bytes
.Lmemset_byte:
	beq dst, target, .Lmemset_done
.Lmemset_byte_skipcheck:
	addiu target, -1 // potential branch delay slot
.Lmemset_byte_loop:
	sb value, (dst)
	bne dst, target, .Lmemset_byte_loop
	addiu dst, 1 // branch delay slot

.Lmemset_done:
	jr $ra
	move $v0, dst // branch delay slot
