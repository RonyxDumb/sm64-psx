#include <port/psx/retail_diagnostics.h>

#ifdef RETAIL_DIAGNOSTICS
#include <stdint.h>

#define STACK_CANARY 0x51ACCAFE

extern char _stackBottom[], _stackTop[];

u32 retail_stack_peak_used;
u32 retail_stack_remaining;
u32 retail_stack_size;

static uintptr_t get_stack_pointer(void) {
	uintptr_t stack_pointer;
	__asm__ volatile("move %0, $sp" : "=r"(stack_pointer));
	return stack_pointer;
}

void retail_stack_diagnostics_sample(void) {
	uintptr_t stack_bottom = (uintptr_t) _stackBottom;
	uintptr_t stack_top = (uintptr_t) _stackTop;
	uintptr_t scan = stack_bottom;

	while(scan < stack_top && *(volatile u32*) scan == STACK_CANARY) {
		scan += sizeof(u32);
	}

	u32 used = stack_top - scan;
	if(used > retail_stack_peak_used) {
		retail_stack_peak_used = used;
		retail_stack_remaining = retail_stack_size - used;
	}
}

void retail_stack_diagnostics_init(void) {
	uintptr_t stack_bottom = (uintptr_t) _stackBottom;
	uintptr_t stack_pointer = get_stack_pointer() & ~(uintptr_t) (sizeof(u32) - 1);

	retail_stack_size = (uintptr_t) _stackTop - stack_bottom;
	for(uintptr_t addr = stack_bottom; addr < stack_pointer; addr += sizeof(u32)) {
		*(volatile u32*) addr = STACK_CANARY;
	}

	retail_stack_diagnostics_sample();
}
#endif
