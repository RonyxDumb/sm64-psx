#pragma once

#include <types.h>

#ifdef RETAIL_DIAGNOSTICS
void retail_stack_diagnostics_init(void);
void retail_stack_diagnostics_sample(void);

extern u32 retail_stack_peak_used;
extern u32 retail_stack_remaining;
extern u32 retail_stack_size;
#endif
