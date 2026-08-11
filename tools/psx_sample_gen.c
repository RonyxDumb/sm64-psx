#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <audiofile.h>

typedef int16_t sample_t;
#define SAMPLE_MIN (-0x8000)
#define STEP 4 // divide the original sample rates by this

static uint8_t* buf_start;
static uint8_t* buf;
static sample_t queued_samples[28];
static int queued_sample_count = 0;

int fits_in_4_bits(int32_t value) {
	return ((int32_t) (value << 28) >> 28) == value;
}

void finish_chunk(void) {
	for(int i = queued_sample_count; i < 28; i++) {
		queued_samples[i] = 0;
	}
	// how much do we need to shift right (compress) for all samples to fit in 4 bits?
	int shr = 0;
	for(int i = 0; i < 28; i++) {
		while(!fits_in_4_bits(queued_samples[i] >> shr)) {
			shr++;
		}
	}
	*(buf++) = 12 - shr; // the compressed samples will be shifted left by 12 and then shifted right by this amount
	*(buf++) = 0; // flag bits
	for(int i = 0; i < 28; i += 2) {
		*(buf++) = (queued_samples[i] >> shr & 0x0F) | (queued_samples[i + 1] >> shr << 4 & 0xF0);
	}
	queued_sample_count = 0;
}

int main(int argc, const char** argv) {
	if(argc != 3) {
		printf("usage: %s <input.aiff> <output.samplebin>\n", argv[0]);
		return 1;
	}
	AFfilehandle handle = afOpenFile(argv[1], "rb", NULL);
	if(!handle) {
		printf("could not open audio file '%s'\n", argv[1]);
		return 1;
	}
	int frame_count = afGetFrameCount(handle, AF_DEFAULT_TRACK);
	int frame_size = afGetFrameSize(handle, AF_DEFAULT_TRACK, 1);
	if(frame_size != 2) {
		printf("expected frame size of 2, got %d\n", frame_size);
		return 1;
	}
	int channel_count = afGetChannels(handle, AF_DEFAULT_TRACK);
	if(channel_count != 1) {
		printf("expected 1 channel, got %d\n", channel_count);
		return 1;
	}

	sample_t* input_samples = malloc(frame_count * frame_size);
	buf_start = malloc(frame_count + 128); // just to be safe
	buf = buf_start;

	// write the sample rate as a small header
	unsigned sample_rate_psx = 0x1000 * afGetRate(handle, AF_DEFAULT_TRACK) / (44100 * STEP);
	*(buf++) = sample_rate_psx & 0xFF;
	*(buf++) = sample_rate_psx >> 8 & 0xFF;
	// align to 4
	buf += 2;

	afReadFrames(handle, AF_DEFAULT_TRACK, input_samples, frame_count);
	for(int i = 0; i < frame_count; i += STEP) {
		queued_samples[queued_sample_count++] = input_samples[i];
		if(queued_sample_count == 28) {
			finish_chunk();
		}
	}
	finish_chunk();
	// add a dummy silent block that gets looped forever
	*(buf++) = 0;
	*(buf++) = 5;
	for(int i = 0; i < 14; i++) {
		*(buf++) = 0;
	}

	FILE* out_handle = fopen(argv[2], "wb");
	if(!out_handle) {
		printf("could not open output file '%s'\n", argv[2]);
		return 1;
	}
	fwrite(buf_start, buf - buf_start, 1, out_handle);
	fclose(out_handle);
	return 0;
}
