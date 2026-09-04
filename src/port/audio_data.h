#pragma once
#include <types.h>

/*
 * PS1 representation of SM64's 00_sound_player sequence.
 * V4 stores large event tables in EXT.DAT and keeps only a tiny index in the ELF.
 * Timing values are in original sound-player ticks (nominally 240 Hz).
 */
#define SFX_EVENT_PAN_MASK   0x7F
#define SFX_EVENT_CONTINUOUS 0x80

typedef struct {
    u16 start_tick;
    u16 gate_ticks;
    u16 portamento_ticks;
    u16 vibrato_delay_ticks;

    u8 bank_id;
    u8 sample_id;
    u8 note;
    u8 velocity;
    u8 pan_flags;
    u8 release_rate;
    u8 vibrato_rate;
    u8 vibrato_extent;
    u8 portamento_mode;
    u8 portamento_target;
    u8 reverb; /* N64 send level; PS1 backend maps zero/nonzero to SPU EON. */
} SfxEvent;
STATIC_ASSERT(sizeof(SfxEvent) == 20, "SfxEvent V3 layout must stay 20 bytes");

typedef struct {
    u32 first_event;
    u16 event_count;
    u16 end_tick;
} SfxLayerDef;
STATIC_ASSERT(sizeof(SfxLayerDef) == 8, "SfxLayerDef V4 must stay 8 bytes");

typedef struct {
    u16 first_layer;
    u8 layer_count;
    u8 _pad;
} SfxDef;
STATIC_ASSERT(sizeof(SfxDef) == 4, "SfxDef V4 must stay 4 bytes");

typedef struct {
    u32 magic;
    u16 def_count;
    u16 layer_count;
    u32 defs_offset;
    u32 layers_offset;
    u32 events_offset;
    u32 event_count;
} SfxBlobHeader;
STATIC_ASSERT(sizeof(SfxBlobHeader) == 24, "SfxBlobHeader V4 must stay 24 bytes");

#define SFX_BLOB_MAGIC 0x34584653u /* little-endian bytes: SFX4 */

extern const u16 sfx_first_def_per_channel[];
extern const u16 sfx_count_per_channel[];
extern const u8 sfx_channel_count;
extern const u32 sfx_data_blob_size;
extern const u16 sfx_total_def_count;
extern const u16 sfx_total_layer_count;
extern const u32 sfx_total_event_count;
