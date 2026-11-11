/*
 * ps1-bare-metal - (C) 2023 spicyjpeg
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <ps1/registers.h>
#include <ps1/cop0.h>

#define _align(x, n) (((x) + ((n) - 1)) & ~((n) - 1))

typedef void (*Function)(void);

/* Linker symbols */

// These are defined by the linker script. Note that these are not variables,
// they are virtual symbols whose location matches their value. The simplest way
// to turn them into pointers is to declare them as arrays.
extern char _bssStart[], _bssEnd[], _heapStart[], _stackBottom[];

extern const Function _preinitArrayStart[], _preinitArrayEnd[];
extern const Function _initArrayStart[], _initArrayEnd[];
extern const Function _finiArrayStart[], _finiArrayEnd[];

/* Heap API (used by malloc) */

static uintptr_t _heapEnd = (uintptr_t) _heapStart;

void *sbrk(ptrdiff_t incr) {
	uintptr_t currentEnd = _heapEnd;
	uintptr_t newEnd     = _align(currentEnd + incr, 8);

	if (newEnd < (uintptr_t) _heapStart || newEnd > (uintptr_t) _stackBottom)
		return 0;

	_heapEnd = newEnd;
	return (void *) currentEnd;
}

/* Program entry point */

[[gnu::noinline]] int main(int argc, const char **argv);

__asm__(
	".pushsection .text.startup._start,\"ax\",@progbits\n"
	".set push\n"
	".set noreorder\n"
	".globl _start\n"
	"_start:\n"
	"la $sp, _stackTop\n"
	"la $gp, _gp\n"
	"j psx_start\n"
	"nop\n"
	".set pop\n"
	".popsection\n"
);

[[gnu::used, gnu::externally_visible]] int psx_start(int argc, const char **argv) {
	// Disable interrupts at the COP0 side.
	cop0_setReg(COP0_SR, COP0_SR_CU0 | COP0_SR_CU2);

	// Clear all pending IRQ flags and prevent the interrupt controller from
	// generating further IRQs.
	IRQ_MASK = 0;
	IRQ_STAT = 0;
	DMA_DPCR = 0;
	DMA_DICR = DMA_DICR_CH_STAT_BITMASK;

	// allow the pcsx-redux functions to not crash
#ifndef IDE
	asm volatile(
		"lui $v0, 9\n"
		"sw $v0, 0x1F80101C\n"
		::: "$v0"
	);
#endif

	// clear the scratchpad
	memset((void*) 0x1F800000, 0, 1024);

	// Set all uninitialized variables to zero by clearing the BSS section.
	memset(_bssStart, 0, _bssEnd - _bssStart);

	// Invoke all global constructors if any, then main() and finally all global
	// destructors.
	for (const Function *ctor = _preinitArrayStart; ctor < _preinitArrayEnd; ctor++)
		(*ctor)();
	for (const Function *ctor = _initArrayStart; ctor < _initArrayEnd; ctor++)
		(*ctor)();

	int returnValue = main(argc, argv);

	for (const Function *dtor = _finiArrayStart; dtor < _finiArrayEnd; dtor++)
		(*dtor)();

	return returnValue;
}
