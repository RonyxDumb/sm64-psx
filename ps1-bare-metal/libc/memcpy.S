.set noreorder

// a compact, fast memcpy for PS1

#define dst $a0
#define src $a1
#define size $a2
#define target $v0
#define aligned_target $v1
#define alignment $a3
#define tmp0 $t0
#define tmp1 $t1
#define tmp2 $t2
#define tmp3 $t3

.section .text.memcpy, "ax", @progbits
.type memcpy, @function
.global memcpy
memcpy:
	// if less than 16 bytes, just copy in bytes
	bltu size, 16, .Lmemcpy_byte
	addu target, dst, size // branch delay slot

	// if src or dst not aligned to words, just copy in bytes
	or alignment, dst, src
	andi alignment, 3
	bnez alignment, .Lmemcpy_byte_loop // skip the size check because it is already known to be over 4 bytes here

	// copy word-aligned chunks of 16 bytes
	srl tmp0, size, 4 // branch delay slot
	sll tmp0, 4
	addu aligned_target, dst, tmp0
.Lmemcpy_chunked_loop:
	lw tmp0, (src)
	lw tmp1, 4(src)
	lw tmp2, 8(src)
	lw tmp3, 12(src)
	addiu src, 16
	addiu dst, 16
	sw tmp0, -16(dst)
	sw tmp1, -12(dst)
	sw tmp2, -8(dst)
	bne dst, aligned_target, .Lmemcpy_chunked_loop
	sw tmp3, -4(dst) // branch delay slot

	// copy any remaining bytes
.Lmemcpy_byte:
	beq dst, target, .Lmemcpy_done
	nop // branch delay slot
.Lmemcpy_byte_loop:
	lb tmp0, (src)
	addiu src, 1
	addiu dst, 1
	bne dst, target, .Lmemcpy_byte_loop
	sb tmp0, -1(dst) // branch delay slot

.Lmemcpy_done:
	jr $ra
	move $v0, dst // branch delay slot
