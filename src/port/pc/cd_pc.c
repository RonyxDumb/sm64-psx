#include <port/cd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static u8* ext_files_buffer = NULL;

void cd_read(void* out, u32 pos, u32 size) {
	if(!ext_files_buffer) {
		FILE* h = fopen("build/us_pc/ext_files.dat", "rb");
		assert(h);
		fseek(h, 0, SEEK_END);
		u32 ext_files_size = ftell(h);
		ext_files_buffer = malloc(ext_files_size);
		fseek(h, 0, SEEK_SET);
		fread(ext_files_buffer, ext_files_size, 1, h);
		fclose(h);
	}
	memcpy(out, ext_files_buffer + pos, size);
}
