#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

// no compression is actually done right now... but it COULD be done maybe...
// note that it would have to be reducing the animation data itself, NOT just data compression, because i tried that and it sucked

//#define IMPLEMENT_COMPRESS_LZSS
//#define IMPLEMENT_COMPRESS_ANIM
//#include <anim_compression.h>

struct OffsetSizePair {
	uint32_t offset;
	uint32_t size;
};

struct DmaTable {
	uint32_t count;
	uint32_t srcAddr;
	struct OffsetSizePair anim[];
};

uint32_t compress_anims(unsigned char* src, unsigned char* dst, uint32_t size) {
	struct DmaTable* og_table = (struct DmaTable*) src;
	uint32_t table_size = sizeof(struct DmaTable) + og_table->count * sizeof(struct OffsetSizePair);
	struct DmaTable* new_table = (struct DmaTable*) dst;
	new_table->count = og_table->count;
	uint32_t dst_off = table_size;
	for(uint32_t i = 0; i < og_table->count; i++) {
		memcpy(dst + dst_off, src + og_table->anim[i].offset, og_table->anim[i].size);
		new_table->anim[i].offset = dst_off;
		new_table->anim[i].size = og_table->anim[i].size;
		dst_off += og_table->anim[i].size;
	}
	return dst_off;
}

int main(int argc, const char** argv) {
	if(argc != 5) {
		fprintf(stderr, "usage: compress_mario_anims <input file> <output binary> <input start byte> <input size in bytes>\n");
		return 1;
	}
	FILE* input = fopen(argv[1], "r");
	if(!input) {
		fprintf(stderr, "could not open input '%s'\n", argv[1]);
		return 1;
	}
	uint32_t input_start = 0;
	input_start = strtol(argv[3], NULL, 0);
	uint32_t input_size;
	input_size = strtol(argv[4], NULL, 0);
	if(!input_size) {
		fprintf(stderr, "invalid input size provided (input_start: %u, input_size: %u)\n", input_start, input_size);
		return 1;
	}
	fseek(input, input_start, SEEK_SET);
	uint8_t input_buf[input_size];
	fread(input_buf, input_size, 1, input);
	fclose(input);
	/*uint8_t output_buf[input_size * 4];
	uint32_t output_size = compress_anims(input_buf, output_buf, input_size);
	if(!output_size) {
		fprintf(stderr, "could not compress some animation\n");
		return 1;
	}
	fprintf(stderr, "omaga!! input: %d table size %d, output %d table size %d\n", input_size, *(uint32_t*) input_buf * 8 + 8, output_size, *(uint32_t*) output_buf * 8 + 8);
	if(output_size > input_size) {
		return 1;
	}*/
	FILE* output = fopen(argv[2], "wb");
	if(!output) {
		fprintf(stderr, "could not open output binary '%s'\n", argv[2]);
		return 1;
	}
	printf("full anim segment size: %d anim table size: %d\n", input_size, *(uint32_t*) input_buf * 8 + 8);
	//fwrite(output_buf, output_size, 1, output);
	fwrite(input_buf, input_size, 1, output);
	fclose(output);
	return 0;
}
