#ifndef PLATFORM_DISPLACEMENT_H
#define PLATFORM_DISPLACEMENT_H

#include <PR/ultratypes.h>

#include "types.h"

void update_mario_platform(void);
void get_mario_posi(s16 *xi, s16 *yi, s16 *zi);
void set_mario_posi(s16 xi, s16 yi, s16 zi);
void apply_platform_displacement(u32 isMario, struct Object *platform);
void apply_mario_platform_displacement(void);
#ifndef VERSION_JP
void clear_mario_platform(void);
#endif

#endif // PLATFORM_DISPLACEMENT_H
