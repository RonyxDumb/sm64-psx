#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct Entry {
	unsigned long hash;
	unsigned long len;
	unsigned char name[128];
};

#define ALIGNMENT 2048
#define STRINGIFY(x) #x

int main(int argc, const char** argv) {
	if(argc < 3 || argc > 4) {
		printf(
			"usage: makextfiles <input list> <output binary> [output for defsym args]\
			makes a compressed binary blob from a list of file sections, outputting a list of -Wl,--defsym flags to be used with gcc\
			the binary data starts at 0 in the file, and each entry is padded to " STRINGIFY(ALIGNMENT) " bytes\
			each defsym flag is offset by (4096 + (compressed_size + " STRINGIFY(ALIGNMENT) " - 1) / " STRINGIFY(ALIGNMENT) ")\
			_*End symbols can be subtracted with the _*Start symbols to find the final decompressed size\
			for the in-file compressed size, subtract the _*Start symbol by 4096 and modulo it by " STRINGIFY(ALIGNMENT) "\
			for the in-file start of a segment's data, subtract the _*Start symbol by 4096 and round it down to " STRINGIFY(ALIGNMENT) "\n"
		);
		return 1;
	}
	FILE* input = fopen(argv[1], "r");
	if(!input) {
		fprintf(stderr, "could not open input '%s'\n", argv[1]);
		return 1;
	}
	FILE* output_bin = fopen(argv[2], "wb");
	if(!output_bin) {
		fprintf(stderr, "could not open output binary '%s'\n", argv[2]);
		return 1;
	}
	FILE* output_defsym = NULL;
	if(argc >= 4) {
		output_defsym = fopen(argv[3], "w");
		if(!output_defsym) {
			fprintf(stderr, "could not open defsym output '%s'\n", argv[3]);
			return 1;
		}
	}
	unsigned capacity = 256;
	struct Entry* entries = malloc(capacity * sizeof(struct Entry));
	int entry_count = 0;
	char src_filename[128];
	unsigned long data_start = 0;
	unsigned long segment_size = 0;
	fscanf(input, " ");
	unsigned long entry_pos = ftell(input);
	while(fscanf(input, "%127[^:]:%x:%x", src_filename, &data_start, &segment_size) == 3) {
		struct Entry* entry = &entries[entry_count];
		entry->hash = 0;
		entry->len = 0;
		for(; src_filename[entry->len]; entry->len++) {
			entry->hash = ((entry->hash << 8) | (entry->hash >> (sizeof(unsigned long) * 8 - 8))) ^ src_filename[entry->len];
		}
		for(int i = 0; i < entry_count - 1; i++) {
			if(entries[i].len == entry->len && entries[i].hash == entry->hash && !strcmp(entries[i].name, src_filename)) {
				entry = NULL;
				entry_count--;
				fscanf(input, "%*s ");
				break;
			}
		}
		if(!entry) continue;
		strcpy(entry->name, src_filename);

		FILE* src = fopen(src_filename, "rb");
		if(!src) {
			fprintf(stderr, "could not open listed source file '%s'\n", src_filename);
			fclose(input);
			fclose(output_bin);
			remove(argv[2]);
			if(output_defsym) {
				fclose(output_defsym);
				remove(argv[3]);
			}
			return 1;
		}
		fseek(src, data_start, SEEK_SET);
		unsigned aligned_segment_size = (segment_size + 2047) / 2048 * 2048;
		unsigned char* segment_buf = malloc(aligned_segment_size);
		fread(segment_buf, segment_size, 1, src);
		if(aligned_segment_size != segment_size) {
			memset(segment_buf + segment_size, 0, aligned_segment_size - segment_size);
		}

		unsigned long start_in_blob = ftell(output_bin);
		fwrite(segment_buf, aligned_segment_size, 1, output_bin);

		if(output_defsym) {
			char start_sym[128];
			char end_sym[128];
			if(fscanf(input, "!%127[^ :]:%127[^ :]", start_sym, end_sym) != 2) {
				fprintf(stderr, "defsym mode is on, but start and end symbol names were not specified for '%s'\n", src_filename);
				fclose(input);
				fclose(output_bin);
				remove(argv[2]);
				if(output_defsym) {
					fclose(output_defsym);
					remove(argv[3]);
				}
				return 1;
			}
			unsigned encoded_compressed_size = aligned_segment_size / ALIGNMENT;
			fprintf(output_defsym, "-Wl,--defsym=%s=0x%lx -Wl,--defsym=%s=0x%lx ", start_sym, 4096 + start_in_blob, end_sym, 4096 + start_in_blob + segment_size);
		}
		fclose(src);
		if(++entry_count >= capacity) {
			capacity *= 2;
			entries = realloc(entries, capacity * sizeof(struct Entry));
		}
		fscanf(input, " ");
		free(segment_buf);
	}
	free(entries);
	if(!feof(input)) {
		fprintf(stderr, "invalid data in list file\n");
		fclose(input);
		fclose(output_bin);
		remove(argv[2]);
		if(output_defsym) {
			fclose(output_defsym);
			remove(argv[3]);
		}
		return 1;
	}
	if(output_defsym) {
		fclose(output_defsym);
	}
	fclose(output_bin);
	fclose(input);
	return 0;
}
