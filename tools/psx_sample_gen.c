#include <audiofile.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    BLOCK_SAMPLES = 28,
    BLOCK_BYTES = 16,
    LOOP_END = 1,
    LOOP_REPEAT = 2,
    LOOP_START = 4,
    FILTER_RADIUS = 16,
};

static const int predictors[][2] = {
    {0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60},
};

static int clamp(int value, int min, int max) {
    return value < min ? min : value > max ? max : value;
}

/* Pick the predictor and shift by measuring error after SPU reconstruction. */
static void encode_block(uint8_t out[BLOCK_BYTES], const int16_t pcm[BLOCK_SAMPLES],
                         int history[2], unsigned flags) {
    uint64_t best_error = UINT64_MAX;
    int best_recent = 0;
    int best_older = 0;
    unsigned filter_count = (flags & LOOP_START) ? 1 : sizeof(predictors) / sizeof(predictors[0]);

    for (unsigned filter = 0; filter < filter_count; filter++) {
        for (int shift = 0; shift <= 12; shift++) {
            uint8_t candidate[BLOCK_BYTES] = {filter * 16 + shift, flags};
            int step = 1 << (12 - shift);
            int recent = history[0];
            int older = history[1];
            uint64_t error = 0;
            for (int i = 0; i < BLOCK_SAMPLES; i++) {
                int pred = (recent * predictors[filter][0] + older * predictors[filter][1] + 32) >> 6;
                int delta = pcm[i] - pred;
                int code = clamp((delta + (delta < 0 ? -step / 2 : step / 2)) / step, -8, 7);
                older = recent;
                recent = clamp(pred + code * step, INT16_MIN, INT16_MAX);
                int residual = pcm[i] - recent;
                error += (int64_t) residual * residual;
                if (error >= best_error) {
                    break;
                }
                candidate[2 + i / 2] |= (code & 15) << (4 * (i % 2));
            }
            if (error < best_error) {
                best_error = error;
                memcpy(out, candidate, BLOCK_BYTES);
                best_recent = recent;
                best_older = older;
            }
        }
    }
    history[0] = best_recent;
    history[1] = best_older;
}

/* Low-pass before decimation. Read around the original loop seam as a
 * periodic signal so repeated blocks do not introduce a discontinuity. */
static int16_t resample(const int16_t *pcm, int frames, double position, int divisor,
                        int loop_start, int loop_end) {
    double sum = 0;
    double weight_sum = 0;
    int center = (int) position;
    for (int offset = -FILTER_RADIUS; offset <= FILTER_RADIUS; offset++) {
        int index = center + offset;
        double distance = index - position;
        if (fabs(distance) > FILTER_RADIUS) {
            continue;
        }
        double phase = 3.141592653589793 * distance;
        double cutoff = 0.90 / divisor;
        double weight = distance == 0.0 ? cutoff : sin(phase * cutoff) / phase;
        weight *= (1.0 + cos(phase / FILTER_RADIUS)) * 0.5;
        if (loop_end > loop_start) {
            int period = loop_end - loop_start;
            index = loop_start + ((index - loop_start) % period + period) % period;
        }
        sum += pcm[clamp(index, 0, frames - 1)] * weight;
        weight_sum += weight;
    }
    return clamp(lround(sum / weight_sum), INT16_MIN, INT16_MAX);
}

static bool write_sample(FILE *output, const int16_t *pcm, int frames, double rate,
                         int divisor, int loop_start, int loop_end) {
    unsigned pitch = lround(4096.0 * rate / (44100 * divisor));
    bool loop = loop_end > loop_start;
    uint8_t header[] = {pitch & 255, pitch >> 8, loop, 0};
    if (fwrite(header, 1, sizeof(header), output) != sizeof(header)) {
        return false;
    }

    int alignment = divisor * BLOCK_SAMPLES;
    int intro_blocks = loop ? (loop_start + alignment / 2) / alignment : 0;
    int loop_blocks = loop ? (loop_end - loop_start + alignment / 2) / alignment : 0;
    if (loop && loop_blocks == 0) {
        loop_blocks = 1;
    }
    int blocks = loop ? intro_blocks + loop_blocks : (frames + alignment - 1) / alignment;
    int history[2] = {0, 0};

    for (int block = 0; block < blocks; block++) {
        bool in_loop = loop && block >= intro_blocks;
        int16_t samples[BLOCK_SAMPLES] = {0};
        for (int i = 0; i < BLOCK_SAMPLES; i++) {
            int sample = block * BLOCK_SAMPLES + i;
            if (in_loop) {
                int loop_sample = sample - intro_blocks * BLOCK_SAMPLES;
                double position = loop_start + (double) loop_sample * (loop_end - loop_start)
                                             / (loop_blocks * BLOCK_SAMPLES);
                samples[i] = resample(pcm, frames, position, divisor, loop_start, loop_end);
            } else if (loop || sample * divisor < frames) {
                double position = loop ? (double) sample * loop_start / (intro_blocks * BLOCK_SAMPLES)
                                       : sample * divisor;
                samples[i] = resample(pcm, frames, position, divisor, 0, 0);
            }
        }
        unsigned flags = loop && block == intro_blocks ? LOOP_START : 0;
        if (loop && block == blocks - 1) {
            flags |= LOOP_END | LOOP_REPEAT;
        }
        uint8_t encoded[BLOCK_BYTES];
        encode_block(encoded, samples, history, flags);
        if (fwrite(encoded, 1, sizeof(encoded), output) != sizeof(encoded)) {
            return false;
        }
    }
    if (!loop) {
        const uint8_t stop[BLOCK_BYTES] = {0, LOOP_END};
        return fwrite(stop, 1, sizeof(stop), output) == sizeof(stop);
    }
    return true;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <input.aiff> <output.samplebin>\n", argv[0]);
        return 1;
    }
    AFfilehandle input = afOpenFile(argv[1], "rb", NULL);
    if (!input) {
        fprintf(stderr, "could not open %s\n", argv[1]);
        return 1;
    }
    int frames = afGetFrameCount(input, AF_DEFAULT_TRACK);
    double rate = afGetRate(input, AF_DEFAULT_TRACK);
    if (frames <= 0 || afGetChannels(input, AF_DEFAULT_TRACK) != 1 ||
        afGetFrameSize(input, AF_DEFAULT_TRACK, 1) != sizeof(int16_t)) {
        fprintf(stderr, "expected nonempty 16-bit mono PCM in %s\n", argv[1]);
        afCloseFile(input);
        return 1;
    }

    int loop_start = 0;
    int loop_end = 0;
    if (afGetInstIDs(input, NULL) && afGetLoopMode(input, AF_DEFAULT_INST, 1) == AF_LOOP_MODE_FORW) {
        loop_start = afGetLoopStartFrame(input, AF_DEFAULT_INST, 1);
        loop_end = afGetLoopEndFrame(input, AF_DEFAULT_INST, 1);
        if (loop_start < 0 || loop_end <= loop_start || loop_end > frames) {
            fprintf(stderr, "invalid loop points in %s\n", argv[1]);
            afCloseFile(input);
            return 1;
        }
    }

    int16_t *pcm = malloc((size_t) frames * sizeof(*pcm));
    bool read_ok = pcm && afReadFrames(input, AF_DEFAULT_TRACK, pcm, frames) == frames;
    afCloseFile(input);
    if (!read_ok) {
        fprintf(stderr, "could not read PCM from %s\n", argv[1]);
        free(pcm);
        return 1;
    }

    /* Give speech more bandwidth within the shared 512 KiB SPU budget. */
    int divisor = strstr(argv[1], "/sfx_mario/") || strstr(argv[1], "/sfx_mario_peach/") ? 2 : 4;
    FILE *output = fopen(argv[2], "wb");
    bool written = output && write_sample(output, pcm, frames, rate, divisor, loop_start, loop_end);
    if (output && fclose(output) != 0) {
        written = false;
    }
    free(pcm);
    if (!written) {
        fprintf(stderr, "could not write %s\n", argv[2]);
        remove(argv[2]);
        return 1;
    }
    return 0;
}
