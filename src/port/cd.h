#pragma once
#include <types.h>

#define SECTOR_SIZE 2048

void cd_read(void* out, u32 pos, u32 size);
