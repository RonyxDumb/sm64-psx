#pragma once
#include <types.h>

void gdm_init(void* pool, u32 size);
void gdm_maketestdl(int _id);
u32* gdm_gettestdl(int _id);
u32 gd_sfx_to_play(void);
#define gdm_setup(...)
#define gd_add_to_heap(...)
#define gd_copy_p1_contpad(...)
#define gd_vblank(...)
