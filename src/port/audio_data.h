#pragma once
#include <types.h>

typedef struct {
	u8 bank_id;
	u8 sample_id;
	u8 note;
	u8 gate;
	s8 velocity;
} SfxDef;

extern SfxDef* sfx_defs_per_channel[];
