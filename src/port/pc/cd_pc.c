#include <port/cd.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static u8* ext_files_buffer = NULL;

#ifndef PC_EXT_FILES_PATH
#define PC_EXT_FILES_PATH "build/us_pc/ext_files.dat"
#endif

void cd_read(void* out, u32 pos, u32 size) {
    if (!ext_files_buffer) {
        FILE* h = fopen(PC_EXT_FILES_PATH, "rb");

        if (!h) {
            h = fopen("ext_files.dat", "rb");
        }

        assert(h);

        fseek(h, 0, SEEK_END);
        u32 ext_files_size = (u32)ftell(h);

        ext_files_buffer = malloc(ext_files_size);
        assert(ext_files_buffer);

        fseek(h, 0, SEEK_SET);

        size_t read_count = fread(ext_files_buffer, 1, ext_files_size, h);
        fclose(h);

        assert(read_count == ext_files_size);
    }

    memcpy(out, ext_files_buffer + pos, size);
}