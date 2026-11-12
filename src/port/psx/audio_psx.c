#ifdef TARGET_PSX

#include <game/print.h>
#include <port/cd.h>
#include <port/gfx/gfx.h>
#include <port/audio_data.h>
#include <game/memory.h>
#include <types.h>
#include <ps1/registers.h>
#include <ps1/gpu.h>
#include <port/psx/cd_psx.h>
#include <audio/external.h>
#include <seq_ids.h>
#include <sounds.h>
#include <assert.h>
#include <ultra64.h>

#ifdef BENCH

void audio_backend_init() {}
void audio_backend_tick() {}
void audio_backend_vblank() {}
void play_sound(UNUSED s32, UNUSED f32*) {}
void play_music(UNUSED u8 player, UNUSED u16 seqArgs, UNUSED u16 fadeTimer) {}

#else

extern u8 _audio_table_segment[];
extern u8 _audio_table_segment_end[];
extern u8 _audio_sample_segment[];
extern u8 _audio_sample_segment_end[];
extern u8 _sfx_data_segment[];
extern u8 _sfx_data_segment_end[];
#define SPU_START_ADDR 4096
#define SFX_SAMPLE_LOOPED 0x8000u

typedef union {
	struct {
		u16 spu_freq; // 0x1000 * (frequency in Hz) / 44100
		u16 spu_addr; // address in SPU memory divided by 8
	};
	u32 as_u32;
} SampleDef;

static u16* table;

static u8* sfx_blob_storage;
static const SfxBlobHeader* sfx_blob_header;
static const SfxDef* sfx_blob_defs;
static const SfxLayerDef* sfx_blob_layers;
static const SfxEvent* sfx_blob_events;

static void sfx_init_hardware_reverb(u32 sample_dma_bytes);
static void sfx_init_extdat_blob(void);

static void sfx_init_extdat_blob(void) {
    u32 blob_bytes = (u32) (_sfx_data_segment_end - _sfx_data_segment);

    if(blob_bytes != sfx_data_blob_size || blob_bytes < sizeof(SfxBlobHeader)) {
        abortf(
            "SFX V4 blob size mismatch: ext=%u generated=%u\n",
            blob_bytes,
            sfx_data_blob_size
        );
    }

    sfx_blob_storage = main_pool_alloc(blob_bytes, MEMORY_POOL_RIGHT);

    bool prev_can_show_screen_message = can_show_screen_message;
    can_show_screen_message = false;
    dma_read(sfx_blob_storage, _sfx_data_segment, _sfx_data_segment_end);
    can_show_screen_message = prev_can_show_screen_message;

    sfx_blob_header = (const SfxBlobHeader*) sfx_blob_storage;
    if(sfx_blob_header->magic != SFX_BLOB_MAGIC) {
        abortf("bad SFX V4 blob magic: %08x\n", sfx_blob_header->magic);
    }

    if(sfx_blob_header->def_count != sfx_total_def_count ||
       sfx_blob_header->layer_count != sfx_total_layer_count ||
       sfx_blob_header->event_count != sfx_total_event_count) {
        abortf("SFX V4 blob table-count mismatch\n");
    }

    u32 defs_end = sfx_blob_header->defs_offset +
                   (u32) sfx_blob_header->def_count * sizeof(SfxDef);
    u32 layers_end = sfx_blob_header->layers_offset +
                     (u32) sfx_blob_header->layer_count * sizeof(SfxLayerDef);
    u32 events_end = sfx_blob_header->events_offset +
                     sfx_blob_header->event_count * sizeof(SfxEvent);

    if(sfx_blob_header->defs_offset < sizeof(SfxBlobHeader) ||
       sfx_blob_header->layers_offset < defs_end ||
       sfx_blob_header->events_offset < layers_end ||
       defs_end > blob_bytes ||
       layers_end > blob_bytes ||
       events_end > blob_bytes) {
        abortf("corrupt SFX V4 blob offsets\n");
    }

    sfx_blob_defs = (const SfxDef*) (sfx_blob_storage + sfx_blob_header->defs_offset);
    sfx_blob_layers = (const SfxLayerDef*) (sfx_blob_storage + sfx_blob_header->layers_offset);
    sfx_blob_events = (const SfxEvent*) (sfx_blob_storage + sfx_blob_header->events_offset);
}

void audio_backend_init() {
	SPU_CTRL = SPU_CTRL_ENABLE | SPU_CTRL_UNMUTE;
	for(int i = 0; i < 24; i++) {
		/* Fast attack, full sustain. The sequencer controls release. */
		SPU_CH_ADSR1(i) = 0x000F;
		SPU_CH_ADSR2(i) = 0x1FC0;
		SPU_CH_ADSR_VOL(i) = 0;
		SPU_CH_VOL_L(i) = 0x3FFF;
		SPU_CH_VOL_R(i) = 0x3FFF;
	}
	SPU_FLAG_NOISE1 = 0;
	SPU_FLAG_NOISE2 = 0;
	SPU_FLAG_FM1 = 0;
	SPU_FLAG_FM2 = 0;
	SPU_FLAG_REVERB1 = 0;
	SPU_FLAG_REVERB2 = 0;
	SPU_FLAG_OFF1 = 0xFFFF;
	SPU_FLAG_OFF2 = 0xFFFF;
	SPU_MASTER_VOL_L = 0x3FFF;
	SPU_MASTER_VOL_R = 0x3FFF;

	SPU_REVERB_ADDR = 0;
	SPU_REVERB_VOL_L = 0;
	SPU_REVERB_VOL_R = 0;

	u32 sample_bytes = (u32) (_audio_sample_segment_end - _audio_sample_segment);
	u32 sample_dma_bytes = (sample_bytes + 255u) & ~255u;
	u8* buf = main_pool_alloc(sample_dma_bytes, MEMORY_POOL_RIGHT);
	/* sendSPUData() is given the DMA-rounded size, so allocate and clear the
	 * same rounded size instead of reading past the end of the sample buffer. */
	for(u32 i = 0; i < sample_dma_bytes; i++) {
		buf[i] = 0;
	}
	dma_read(buf, _audio_sample_segment, _audio_sample_segment_end);
	sendSPUData(buf, SPU_START_ADDR, sample_dma_bytes);
	main_pool_free(buf);

	/* The reverb work area lives at the top of SPU RAM. Initialization is
	 * refused automatically if the packed sample bank reaches that area. */
	sfx_init_hardware_reverb(sample_dma_bytes);

	table = main_pool_alloc(_audio_table_segment_end - _audio_table_segment, MEMORY_POOL_RIGHT);
	dma_read((u8*) table, _audio_table_segment, _audio_table_segment_end);

	/* V4 keeps the large static sound-player tables out of APP_RAM. Load the
	 * packed EXT.DAT blob once into the dynamic main pool. */
	sfx_init_extdat_blob();

#ifdef SERIAL
	SPU_CDDA_VOL_L = 0;
	SPU_CDDA_VOL_R = 0;
#else
	psx_music_init();
#endif
}

#ifndef BENCH
static bool spu_voice_ended(u32 voice) {
	if(voice < 16) {
		return (SPU_FLAG_STATUS1 & (1u << voice)) != 0;
	} else {
		return (SPU_FLAG_STATUS2 & (1u << (voice - 16))) != 0;
	}
}

static SampleDef find_instrument(u32 sound_bank_id, u32 instrument_id) {
	if(instrument_id == 128) {
		/* 28 samples per period at C5 (523.25 Hz), relative to 44.1 kHz. */
		return (SampleDef) {.spu_freq = SFX_SAMPLE_LOOPED | 1361, .spu_addr = table[11]};
	}
	return (SampleDef) {.as_u32 = *((u32*) table + table[sound_bank_id] + instrument_id)};
}
#endif

// from src/audio/data.c, but stored as u16:
// Frequencies for notes using the standard twelve-tone equal temperament scale.
// For indices 0..116, gNoteFrequencies[k] = 2^((k-39)/12).
// For indices 117..128, gNoteFrequencies[k] = 0.5 * 2^((k-39)/12).
// The 39 in the formula refers to piano key 40 (middle C, at 256 Hz) being
// the reference frequency, which is assigned value 1.
#define UNIT 512
static u16 note_freq_scales[128] = {
	0.105112f * UNIT, 0.111362f * UNIT, 0.117984f * UNIT, 0.125f * UNIT, 0.132433f * UNIT, 0.140308f * UNIT, 0.148651f * UNIT, 0.15749f * UNIT, 0.166855f * UNIT, 0.176777f * UNIT, 0.187288f * UNIT, 0.198425f * UNIT,
	0.210224f * UNIT, 0.222725f * UNIT, 0.235969f * UNIT, 0.25f * UNIT, 0.264866f * UNIT, 0.280616f * UNIT, 0.297302f * UNIT, 0.31498f * UNIT, 0.33371f * UNIT, 0.353553f * UNIT, 0.374577f * UNIT, 0.39685f * UNIT,
	0.420448f * UNIT, 0.445449f * UNIT, 0.471937f * UNIT, 0.5f * UNIT, 0.529732f * UNIT, 0.561231f * UNIT, 0.594604f * UNIT, 0.629961f * UNIT, 0.66742f * UNIT, 0.707107f * UNIT, 0.749154f * UNIT, 0.793701f * UNIT,
	0.840897f * UNIT, 0.890899f * UNIT, 0.943875f * UNIT, 1.0f * UNIT, 1.059463f * UNIT, 1.122462f * UNIT, 1.189207f * UNIT, 1.259921f * UNIT, 1.33484f * UNIT, 1.414214f * UNIT, 1.498307f * UNIT, 1.587401f * UNIT,
	1.681793f * UNIT, 1.781798f * UNIT, 1.887749f * UNIT, 2.0f * UNIT, 2.118926f * UNIT, 2.244924f * UNIT, 2.378414f * UNIT, 2.519842f * UNIT, 2.66968f * UNIT, 2.828428f * UNIT, 2.996615f * UNIT, 3.174803f * UNIT,
	3.363586f * UNIT, 3.563596f * UNIT, 3.775498f * UNIT, 4.0f * UNIT, 4.237853f * UNIT, 4.489849f * UNIT, 4.756829f * UNIT, 5.039685f * UNIT, 5.33936f * UNIT, 5.656855f * UNIT, 5.993229f * UNIT, 6.349606f * UNIT,
	6.727173f * UNIT, 7.127192f * UNIT, 7.550996f * UNIT, 8.0f * UNIT, 8.475705f * UNIT, 8.979697f * UNIT, 9.513658f * UNIT, 10.07937f * UNIT, 10.67872f * UNIT, 11.31371f * UNIT, 11.986459f * UNIT, 12.699211f * UNIT,
	13.454346f * UNIT, 14.254383f * UNIT, 15.101993f * UNIT, 16.0f * UNIT, 16.95141f * UNIT, 17.959394f * UNIT, 19.027315f * UNIT, 20.15874f * UNIT, 21.35744f * UNIT, 22.62742f * UNIT, 23.972918f * UNIT, 25.398422f * UNIT,
	26.908691f * UNIT, 28.508766f * UNIT, 30.203985f * UNIT, 32.0f * UNIT, 33.90282f * UNIT, 35.91879f * UNIT, 38.05463f * UNIT, 40.31748f * UNIT, 42.71488f * UNIT, 45.25484f * UNIT, 47.945835f * UNIT, 50.796844f * UNIT,
	53.817383f * UNIT, 57.017532f * UNIT, 60.40797f * UNIT, 64.0f * UNIT, 67.80564f * UNIT, 71.83758f * UNIT, 76.10926f * UNIT, 80.63496f * UNIT, 85.42976f * UNIT, 45.25484f * UNIT, 47.945835f * UNIT, 50.796844f * UNIT,
	53.817383f * UNIT, 57.017532f * UNIT, 60.40797f * UNIT, 64.0f * UNIT, 67.80564f * UNIT, 71.83758f * UNIT, 76.10926f * UNIT, 80.63496f * UNIT
};

/*
 * SM64 sound-player V4 for PS1 (EXT.DAT-backed event tables).
 *
 * Sequence events advance at 120 BPM with 48 ticks per quarter note. N64
 * synthesis updates (vibrato and release) use a separate, faster clock.
 */
#define SFX_FIRST_VOICE 0
#define SFX_LAST_VOICE  23
#define MAX_ACTIVE_SFX  12
#define MAX_SFX_LAYERS  4
#define NO_SFX_VOICE 0xFF

#define SFX_SEQUENCE_HZ (120 * 48 / 60)
#define SFX_CONTROL_HZ 240
#define SFX_RELEASE_UNITS_PER_FRAME (SFX_CONTROL_HZ / 30)

/*
 * PS1 SPU hardware reverb.
 *
 * The standard Room algorithm uses 0x26C0 bytes at the top of the 512 KiB
 * SPU RAM. sendSPUData() is used in 0x100-byte DMA-sized chunks, therefore
 * the clear range starts 0x40 bytes before the actual Room work area. The
 * sample-overlap guard reserves that padding as well.
 */
#define SFX_SPU_RAM_SIZE          0x80000u
#define SFX_REVERB_WORK_SIZE      0x26C0u
#define SFX_REVERB_WORK_ADDR      (SFX_SPU_RAM_SIZE - SFX_REVERB_WORK_SIZE) /* 0x7D940 */
#define SFX_REVERB_WORK_REG       (SFX_REVERB_WORK_ADDR >> 3)               /* 0xFB28 */
#define SFX_REVERB_CLEAR_ADDR     0x7D900u
#define SFX_REVERB_CLEAR_SIZE     (SFX_SPU_RAM_SIZE - SFX_REVERB_CLEAR_ADDR) /* 0x2700 */
#define SFX_REVERB_DEPTH          0x1800u
#define SFX_SPU_CTRL_REVERB       0x0080u
#define SFX_SPU_REVERB_REG_BASE   ((volatile u16*) 0x1F801DC0u)
#define SFX_SPU_DMA_CTRL_REG      (*(volatile u16*) 0x1F801DACu)

STATIC_ASSERT((SFX_REVERB_WORK_ADDR & 7u) == 0, "SPU reverb area must be 8-byte aligned");
STATIC_ASSERT((SFX_REVERB_CLEAR_ADDR & 0xFFu) == 0, "SPU reverb clear area must be DMA aligned");
STATIC_ASSERT((SFX_REVERB_CLEAR_SIZE & 0xFFu) == 0, "SPU reverb clear size must be DMA aligned");
STATIC_ASSERT(SFX_REVERB_CLEAR_ADDR <= SFX_REVERB_WORK_ADDR, "reverb clear range must cover work area");

/* Sony/libspu-compatible Room preset: dAPF1..vRIN (1F801DC0h..1F801DFEh). */
static const u16 sfx_room_reverb_preset[32] = {
    0x007D, 0x005B, 0x6D80, 0x54B8, 0xBED0, 0x0000, 0x0000, 0xBA80,
    0x5800, 0x5300, 0x04D6, 0x0333, 0x03F0, 0x0227, 0x0374, 0x01EF,
    0x0334, 0x01B5, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x01B4, 0x0136, 0x00B8, 0x005C, 0x8000, 0x8000,
};

static u32 sfx_reverb_voice_mask = 0;
static bool sfx_hw_reverb_available = false;

static void sfx_write_reverb_mask(void) {
    if(!sfx_hw_reverb_available) {
        SPU_FLAG_REVERB1 = 0;
        SPU_FLAG_REVERB2 = 0;
        return;
    }
    SPU_FLAG_REVERB1 = (u16) (sfx_reverb_voice_mask & 0xFFFFu);
    SPU_FLAG_REVERB2 = (u16) ((sfx_reverb_voice_mask >> 16) & 0x00FFu);
}

static void sfx_set_voice_reverb(u8 voice, bool enabled) {
    if(voice >= 24) {
        return;
    }
    u32 bit = 1u << voice;
    if(enabled && sfx_hw_reverb_available) {
        sfx_reverb_voice_mask |= bit;
    } else {
        sfx_reverb_voice_mask &= ~bit;
    }
    sfx_write_reverb_mask();
}

static void sfx_disable_hardware_reverb(void) {
    sfx_hw_reverb_available = false;
    sfx_reverb_voice_mask = 0;
    SPU_FLAG_REVERB1 = 0;
    SPU_FLAG_REVERB2 = 0;
    SPU_REVERB_VOL_L = 0;
    SPU_REVERB_VOL_R = 0;
    SPU_REVERB_ADDR = 0xFFFE;
    SPU_CTRL &= (u16) ~SFX_SPU_CTRL_REVERB;
}

static void sfx_init_hardware_reverb(u32 sample_dma_bytes) {
    u32 sample_end = SPU_START_ADDR + sample_dma_bytes;
    if(sample_end > SFX_REVERB_CLEAR_ADDR) {
        /* Never corrupt sample RAM to make room for the effect. */
        sfx_disable_hardware_reverb();
        return;
    }

    SPU_CTRL &= (u16) ~SFX_SPU_CTRL_REVERB;
    SPU_REVERB_VOL_L = 0;
    SPU_REVERB_VOL_R = 0;
    SPU_FLAG_REVERB1 = 0;
    SPU_FLAG_REVERB2 = 0;

    /* Reverb feedback RAM must be zero before the engine is enabled. The
     * 0x2700-byte range is deliberately 0x100-aligned for sendSPUData(). */
    u8* zero = main_pool_alloc(SFX_REVERB_CLEAR_SIZE, MEMORY_POOL_RIGHT);
    for(u32 i = 0; i < SFX_REVERB_CLEAR_SIZE; i++) {
        zero[i] = 0;
    }
    sendSPUData(zero, SFX_REVERB_CLEAR_ADDR, SFX_REVERB_CLEAR_SIZE);
    main_pool_free(zero);

    volatile u16* reverb_regs = SFX_SPU_REVERB_REG_BASE;
    for(u32 i = 0; i < 32; i++) {
        reverb_regs[i] = sfx_room_reverb_preset[i];
    }

    SPU_REVERB_ADDR = (u16) SFX_REVERB_WORK_REG;
    /* 1F801DACh must be initialized after the transfer helper has finished. */
    SFX_SPU_DMA_CTRL_REG = 0x0004;
    SPU_REVERB_VOL_L = (u16) SFX_REVERB_DEPTH;
    SPU_REVERB_VOL_R = (u16) SFX_REVERB_DEPTH;
    sfx_reverb_voice_mask = 0;
    sfx_hw_reverb_available = true;
    SPU_CTRL |= SFX_SPU_CTRL_REVERB;
}

typedef struct {
	bool active;
	u8 voice;
	u16 event_index;
	u32 tick_units;
	const SfxLayerDef* def;
} ActiveSfxLayer;

typedef struct {
	bool active;
	bool discrete;
	bool no_echo;
	u8 channel_id;
	u16 sfx_id;
	u16 priority;
	u8 transpose;
	u32 last_request_frame;
	const f32* source;
	u8 layer_count;
	ActiveSfxLayer layers[MAX_SFX_LAYERS];
} ActiveSfx;

typedef struct {
	bool owned;
	bool keyed;
	bool releasing;
	bool looped;
	u8 owner_instance;
	u8 owner_layer;
	u16 sample_addr;
	u16 priority;
	u32 age;
	u32 start_update;

	u16 gate_left;
	u16 port_start_pitch;
	u16 port_target_pitch;
	u16 port_elapsed;
	u16 port_total;

	u32 vibrato_phase;
	u16 vibrato_delay_left;
	u8 vibrato_rate;
	u8 vibrato_extent;

	u8 release_rate;
	u16 release_units_left;
	u16 release_units_total;
	u16 base_vol_l;
	u16 base_vol_r;
} SfxVoiceState;

static ActiveSfx active_sfx[MAX_ACTIVE_SFX] = {};
static SfxVoiceState sfx_voice_state[24] = {};
static u32 sfx_voice_age_clock = 1;
static u32 sfx_update;
static u32 sfx_game_frame;
static u16 sfx_disabled_banks;
static u8 sfx_sound_mode;

static void key_off_voice_hw(u32 voice) {
	if(voice < 16) {
		SPU_FLAG_OFF1 = 1 << voice;
	} else {
		SPU_FLAG_OFF2 = 1 << (voice - 16);
	}
}

static void key_on_voice_hw(u32 voice) {
	if(voice < 16) {
		SPU_FLAG_ON1 = 1 << voice;
	} else {
		SPU_FLAG_ON2 = 1 << (voice - 16);
	}
}

static u16 pitch_for_note(SampleDef sample, u32 note) {
	if(note > 127) {
		note = 127;
	}
	u32 pitch = (u32) (sample.spu_freq & ~SFX_SAMPLE_LOOPED) * note_freq_scales[note] / UNIT;
	if(pitch < 1) {
		pitch = 1;
	} else if(pitch > 0x3FFF) {
		pitch = 0x3FFF;
	}
	return (u16) pitch;
}

static void volume_for_pan(u8 velocity, u8 pan, u16* out_l, u16* out_r) {
	u32 vol = (0x3000u * velocity) / 127u;
	u32 left;
	u32 right;

	if(sfx_sound_mode == SOUND_MODE_MONO) {
		left = right = vol;
	} else if(pan <= 64) {
		left = vol;
		right = vol * pan / 64u;
	} else {
		right = vol;
		left = vol * (127u - pan) / 63u;
	}

	*out_l = (u16) left;
	*out_r = (u16) right;
}

static void detach_voice_from_old_layer(u8 voice) {
	SfxVoiceState* v = &sfx_voice_state[voice];
	if(v->owner_instance < MAX_ACTIVE_SFX && v->owner_layer < MAX_SFX_LAYERS) {
		ActiveSfx* instance = &active_sfx[v->owner_instance];
		if(instance->active && v->owner_layer < instance->layer_count) {
			ActiveSfxLayer* layer = &instance->layers[v->owner_layer];
			if(layer->active && layer->voice == voice) {
				layer->voice = NO_SFX_VOICE;
			}
		}
	}
	v->owner_instance = NO_SFX_VOICE;
	v->owner_layer = NO_SFX_VOICE;
}

static void hard_stop_voice(u8 voice) {
	detach_voice_from_old_layer(voice);
	sfx_set_voice_reverb(voice, false);
	key_off_voice_hw(voice);
	SPU_CH_VOL_L(voice) = 0;
	SPU_CH_VOL_R(voice) = 0;
	sfx_voice_state[voice].owned = false;
	sfx_voice_state[voice].keyed = false;
	sfx_voice_state[voice].releasing = false;
}

static s32 alloc_sfx_voice(u8 instance_idx, u8 layer_idx) {
	s32 best_voice = -1;
	u16 priority = active_sfx[instance_idx].priority;
	for(s32 voice = SFX_FIRST_VOICE; voice <= SFX_LAST_VOICE; voice++) {
		SfxVoiceState* v = &sfx_voice_state[voice];
		if(!v->owned) {
			best_voice = voice;
			break;
		}
		if(v->priority > priority) {
			continue;
		}
		if(best_voice < 0 || v->priority < sfx_voice_state[best_voice].priority ||
		   (v->priority == sfx_voice_state[best_voice].priority &&
		    v->age < sfx_voice_state[best_voice].age)) {
			best_voice = voice;
		}
	}
	if(best_voice < 0) {
		return -1;
	}
	SfxVoiceState* v = &sfx_voice_state[best_voice];
	if(v->owned) {
		hard_stop_voice((u8) best_voice);
	}
	v->owned = true;
	v->owner_instance = instance_idx;
	v->owner_layer = layer_idx;
	v->priority = priority;
	return best_voice;
}

static u8 release_frames_for_rate(u8 release_rate) {
	if(release_rate == 0) {
		return 1;
	}
	u32 frames = (256u + release_rate - 1u) / release_rate;
	if(frames < 1) {
		frames = 1;
	} else if(frames > 32) {
		frames = 32;
	}
	return (u8) frames;
}

static void begin_voice_release(u8 voice) {
	SfxVoiceState* v = &sfx_voice_state[voice];
	if(!v->keyed || v->releasing) {
		return;
	}
	v->releasing = true;
	v->release_units_total = release_frames_for_rate(v->release_rate) * SFX_RELEASE_UNITS_PER_FRAME;
	v->release_units_left = v->release_units_total;
}

static u16 voice_portamento_pitch(const SfxVoiceState* v) {
	if(v->port_total == 0 || v->port_elapsed >= v->port_total) {
		return v->port_target_pitch;
	}
	s32 diff = (s32) v->port_target_pitch - (s32) v->port_start_pitch;
	s32 pitch = (s32) v->port_start_pitch + diff * v->port_elapsed / v->port_total;
	if(pitch < 1) {
		pitch = 1;
	} else if(pitch > 0x3FFF) {
		pitch = 0x3FFF;
	}
	return (u16) pitch;
}

static s32 vibrato_triangle(u32 phase) {
	u32 p = (phase >> 10) & 63u;
	if(p < 16) {
		return (s32) p * 8;
	}
	if(p < 32) {
		return 127 - (s32) (p - 16) * 8;
	}
	if(p < 48) {
		return -(s32) (p - 32) * 8;
	}
	return -127 + (s32) (p - 48) * 8;
}

static u16 voice_effective_pitch(SfxVoiceState* v) {
	s32 pitch = voice_portamento_pitch(v);
	if(v->vibrato_extent != 0 && v->vibrato_delay_left == 0) {
		s32 wave = vibrato_triangle(v->vibrato_phase);
		/* About one semitone at extent 127, scaled by the N64 extent argument. */
		s32 semitone_delta = pitch * 61 / 1024;
		s32 delta = semitone_delta * wave * v->vibrato_extent / (127 * 127);
		pitch += delta;
	}
	if(pitch < 1) {
		pitch = 1;
	} else if(pitch > 0x3FFF) {
		pitch = 0x3FFF;
	}
	return (u16) pitch;
}

static void tick_sfx_voice(u8 voice, u32 sequence_ticks, u32 control_ticks) {
	SfxVoiceState* v = &sfx_voice_state[voice];
	if(!v->keyed || v->start_update == sfx_update) {
		return;
	}
	if(!v->looped && spu_voice_ended(voice)) {
		hard_stop_voice(voice);
		return;
	}

	if(!v->releasing) {
		if(v->gate_left <= sequence_ticks) {
			begin_voice_release(voice);
			return;
		}
		v->gate_left -= sequence_ticks;

		if(v->port_total != 0 && v->port_elapsed < v->port_total) {
			u32 elapsed = v->port_elapsed + sequence_ticks;
			v->port_elapsed = (u16) (elapsed > v->port_total ? v->port_total : elapsed);
		}

		u32 delay = v->vibrato_delay_left < control_ticks ? v->vibrato_delay_left : control_ticks;
		v->vibrato_delay_left -= delay;
		if(v->vibrato_extent != 0) {
			v->vibrato_phase += (u32) v->vibrato_rate * 32u * (control_ticks - delay);
		}

		SPU_CH_FREQ(voice) = voice_effective_pitch(v);
	} else if(v->release_units_left <= control_ticks) {
		hard_stop_voice(voice);
	} else {
		v->release_units_left -= control_ticks;
		SPU_CH_VOL_L(voice) = (u32) v->base_vol_l * v->release_units_left / v->release_units_total;
		SPU_CH_VOL_R(voice) = (u32) v->base_vol_r * v->release_units_left / v->release_units_total;
	}
}

static void start_event_on_voice(u8 voice, SampleDef sample, const SfxEvent* event, const ActiveSfx* instance) {
	SfxVoiceState* v = &sfx_voice_state[voice];
	u16 note_pitch = pitch_for_note(sample, event->note + instance->transpose);
	u16 target_pitch = pitch_for_note(sample, event->portamento_target + instance->transpose);
	bool continuous = (event->pan_flags & SFX_EVENT_CONTINUOUS) != 0;
	bool can_reuse = continuous && v->keyed && !v->releasing &&
	                 v->sample_addr == sample.spu_addr &&
	                 (v->looped || v->start_update == sfx_update || !spu_voice_ended(voice));

	volume_for_pan(event->velocity, event->pan_flags & SFX_EVENT_PAN_MASK, &v->base_vol_l, &v->base_vol_r);
	v->release_rate = event->release_rate;
	v->gate_left = event->gate_ticks != 0 ? event->gate_ticks : 1;
	v->vibrato_rate = event->vibrato_rate;
	v->vibrato_extent = event->vibrato_extent;
	v->vibrato_delay_left = event->vibrato_delay_ticks;
	if(!can_reuse) {
		v->vibrato_phase = 0;
	}
	v->port_elapsed = 0;
	v->port_total = event->portamento_ticks;

	u8 port_mode = event->portamento_mode & 0x7F;
	if(v->port_total != 0 && (port_mode == 1 || port_mode == 3 || port_mode == 5)) {
		v->port_start_pitch = target_pitch;
		v->port_target_pitch = note_pitch;
	} else if(v->port_total != 0 && (port_mode == 2 || port_mode == 4)) {
		v->port_start_pitch = note_pitch;
		v->port_target_pitch = target_pitch;
	} else {
		v->port_start_pitch = note_pitch;
		v->port_target_pitch = note_pitch;
		v->port_total = 0;
	}
	v->age = sfx_voice_age_clock++;
	v->start_update = sfx_update;
	v->releasing = false;

	/* The PS1 SPU exposes a binary per-voice reverb send (EON). Keep the
	 * sequence's zero/nonzero intent and honor SOUND_NO_ECHO. Set the route
	 * before KEY-ON so the first samples of a fresh voice use the right path. */
	sfx_set_voice_reverb(voice, !instance->no_echo && event->reverb != 0);

	if(!can_reuse) {
		key_off_voice_hw(voice);
		v->sample_addr = sample.spu_addr;
		v->looped = (sample.spu_freq & SFX_SAMPLE_LOOPED) != 0;
		SPU_CH_ADDR(voice) = sample.spu_addr;
		SPU_CH_LOOP_ADDR(voice) = sample.spu_addr;
		v->keyed = true;
	}
	SPU_CH_FREQ(voice) = voice_effective_pitch(v);
	SPU_CH_VOL_L(voice) = v->base_vol_l;
	SPU_CH_VOL_R(voice) = v->base_vol_r;
	if(!can_reuse) {
		key_on_voice_hw(voice);
	}
}

static void finish_sfx_layer(u8 instance_idx, u8 layer_idx) {
	ActiveSfxLayer* layer = &active_sfx[instance_idx].layers[layer_idx];
	if(!layer->active) {
		return;
	}
	if(layer->voice != NO_SFX_VOICE) {
		SfxVoiceState* v = &sfx_voice_state[layer->voice];
		if(v->owner_instance == instance_idx && v->owner_layer == layer_idx) {
			/* Keep ownership while the final gate/release tail is still alive, but
			 * detach it from the reusable ActiveSfx slot. */
			v->owner_instance = NO_SFX_VOICE;
			v->owner_layer = NO_SFX_VOICE;
		}
	}
	layer->voice = NO_SFX_VOICE;
	layer->active = false;
}

static void dispatch_sfx_layer(u8 instance_idx, u8 layer_idx) {
	ActiveSfx* instance = &active_sfx[instance_idx];
	ActiveSfxLayer* layer = &instance->layers[layer_idx];

	for(int safety = 0; safety < 64 && layer->active; safety++) {
		if(layer->event_index >= layer->def->event_count) {
			if(layer->tick_units >= layer->def->end_tick) {
				finish_sfx_layer(instance_idx, layer_idx);
			}
			return;
		}

		const SfxEvent* event = &sfx_blob_events[layer->def->first_event + layer->event_index];
		if(event->start_tick > layer->tick_units) {
			return;
		}
		layer->event_index++;
		SampleDef sample = find_instrument(event->bank_id, event->sample_id);
		if(sample.spu_addr == 0) {
			continue;
		}

		if(layer->voice == NO_SFX_VOICE) {
			s32 voice = alloc_sfx_voice(instance_idx, layer_idx);
			if(voice < 0) {
				continue;
			}
			layer->voice = (u8) voice;
		}

		SfxVoiceState* v = &sfx_voice_state[layer->voice];
		v->owned = true;
		v->owner_instance = instance_idx;
		v->owner_layer = layer_idx;
		start_event_on_voice(layer->voice, sample, event, instance);
	}
}

static void stop_sfx_instance(ActiveSfx* instance) {
	for(u8 i = 0; i < instance->layer_count; i++) {
		u8 voice = instance->layers[i].voice;
		if(voice != NO_SFX_VOICE) {
			hard_stop_voice(voice);
		}
	}
	instance->active = false;
}

static ActiveSfx* alloc_sfx_instance(u16 priority) {
	ActiveSfx* lowest = &active_sfx[0];
	for(int i = 0; i < MAX_ACTIVE_SFX; i++) {
		ActiveSfx* instance = &active_sfx[i];
		if(!instance->active) {
			return instance;
		}
		if(instance->priority < lowest->priority) {
			lowest = instance;
		}
	}
	if(lowest->priority > priority) {
		return NULL;
	}
	stop_sfx_instance(lowest);
	return lowest;
}

static ActiveSfx* find_sfx_instance(u32 channel_id, u32 sfx_id, const f32* pos) {
	for(int i = 0; i < MAX_ACTIVE_SFX; i++) {
		if(active_sfx[i].active &&
		   active_sfx[i].channel_id == channel_id &&
		   active_sfx[i].sfx_id == sfx_id && active_sfx[i].source == pos) {
			return &active_sfx[i];
		}
	}
	return NULL;
}

void stop_sound(u32 soundBits, f32* pos) {
	u32 channel_id = (soundBits & SOUNDARGS_MASK_BANK) >> SOUNDARGS_SHIFT_BANK;
	u32 sfx_id = (soundBits & SOUNDARGS_MASK_SOUNDID) >> SOUNDARGS_SHIFT_SOUNDID;
	ActiveSfx* instance = find_sfx_instance(channel_id, sfx_id, pos);
	if(instance != NULL) {
		stop_sfx_instance(instance);
	}
}

void stop_sounds_from_source(f32* pos) {
	for(int i = 0; i < MAX_ACTIVE_SFX; i++) {
		if(active_sfx[i].active && active_sfx[i].source == pos) {
			stop_sfx_instance(&active_sfx[i]);
		}
	}
}

void stop_sounds_in_continuous_banks(void) {
	for(int i = 0; i < MAX_ACTIVE_SFX; i++) {
		if(active_sfx[i].active && !active_sfx[i].discrete) {
			stop_sfx_instance(&active_sfx[i]);
		}
	}
}

void sound_banks_disable(u8 player, u16 bankMask) {
	if(player == SEQ_PLAYER_SFX) {
		sfx_disabled_banks |= bankMask;
		for(int i = 0; i < MAX_ACTIVE_SFX; i++) {
			if(active_sfx[i].active && (bankMask & (1u << active_sfx[i].channel_id))) {
				stop_sfx_instance(&active_sfx[i]);
			}
		}
	}
}

void sound_banks_enable(u8 player, u16 bankMask) {
	if(player == SEQ_PLAYER_SFX) {
		sfx_disabled_banks &= ~bankMask;
	}
}

void audio_set_sound_mode(u8 soundMode) {
	sfx_sound_mode = soundMode;
}

void sound_reset(UNUSED u8 presetId) {
	sfx_disabled_banks = 0;
	stop_sounds_in_continuous_banks();
}

void play_sound(s32 soundBits, f32* pos) {
	u32 channel_id = ((u32) soundBits & SOUNDARGS_MASK_BANK) >> SOUNDARGS_SHIFT_BANK;
	u32 sfx_id = ((u32) soundBits & SOUNDARGS_MASK_SOUNDID) >> SOUNDARGS_SHIFT_SOUNDID;

	if(!sfx_blob_header || channel_id >= sfx_channel_count ||
	   (sfx_disabled_banks & (1u << channel_id)) || sfx_id >= sfx_count_per_channel[channel_id]) {
		return;
	}

	u32 def_index = (u32) sfx_first_def_per_channel[channel_id] + sfx_id;
	if(def_index >= sfx_blob_header->def_count) {
		return;
	}

	const SfxDef* sfx_def = &sfx_blob_defs[def_index];
	if(sfx_def->layer_count == 0) {
		return;
	}

	bool discrete = (soundBits & SOUND_DISCRETE) != 0;
	bool no_echo = (soundBits & SOUND_NO_ECHO) != 0;
	ActiveSfx* instance = find_sfx_instance(channel_id, sfx_id, pos);
	if(instance != NULL) {
		if(!discrete) {
			instance->last_request_frame = sfx_game_frame;
			return;
		}
		stop_sfx_instance(instance);
	}

	u16 priority = ((u32) soundBits & SOUNDARGS_MASK_PRIORITY) >> SOUNDARGS_SHIFT_PRIORITY;
	if(channel_id == SOUND_BANK_VOICE || channel_id == SOUND_BANK_MENU) {
		priority += 256;
	}
	instance = alloc_sfx_instance(priority);
	if(instance == NULL) {
		return;
	}
	u8 instance_idx = (u8) (instance - active_sfx);

	instance->active = true;
	instance->discrete = discrete;
	instance->no_echo = no_echo;
	instance->channel_id = (u8) channel_id;
	instance->sfx_id = (u16) sfx_id;
	instance->priority = priority;
	instance->transpose = 0;
	u8 red_coin_start = (SOUND_MENU_COLLECT_RED_COIN & SOUNDARGS_MASK_SOUNDID) >> SOUNDARGS_SHIFT_SOUNDID;
	if(channel_id == SOUND_BANK_MENU && sfx_id - red_coin_start < 8) {
		static const u8 major_scale[] = {0, 2, 4, 5, 7, 9, 11, 12};
		instance->transpose = major_scale[sfx_id - red_coin_start];
	}
	instance->last_request_frame = sfx_game_frame;
	instance->source = pos;
	instance->layer_count = 0;

	u8 layers_to_start = sfx_def->layer_count;
	if(layers_to_start > MAX_SFX_LAYERS) {
		layers_to_start = MAX_SFX_LAYERS;
	}

	for(u8 i = 0; i < layers_to_start; i++) {
		u32 layer_index = (u32) sfx_def->first_layer + i;
		if(layer_index >= sfx_blob_header->layer_count) {
			break;
		}

		const SfxLayerDef* layer_def = &sfx_blob_layers[layer_index];
		if(layer_def->event_count == 0) {
			continue;
		}

		u8 layer_idx = instance->layer_count++;
		ActiveSfxLayer* layer = &instance->layers[layer_idx];
		layer->active = true;
		layer->voice = NO_SFX_VOICE;
		layer->event_index = 0;
		layer->tick_units = 0;
		layer->def = layer_def;

		dispatch_sfx_layer(instance_idx, layer_idx);
	}

	if(instance->layer_count == 0) {
		instance->active = false;
	}
}

void audio_backend_vblank(void) {
	static OSTime last_update_time;
	static u32 sequence_phase;
	static u32 control_phase;
	OSTime now = osGetTime();
	u32 elapsed_us = last_update_time != 0 ? (u32) (now - last_update_time) : 16667;
	last_update_time = now;
	/* Large loads may span many blanks. Bound catch-up work on returning. */
	if(elapsed_us > 250000) {
		elapsed_us = 250000;
	}
	sequence_phase += elapsed_us * SFX_SEQUENCE_HZ;
	control_phase += elapsed_us * SFX_CONTROL_HZ;
	u32 sequence_ticks = sequence_phase / 1000000u;
	u32 control_ticks = control_phase / 1000000u;
	sequence_phase %= 1000000u;
	control_phase %= 1000000u;
	sfx_update++;

	for(u8 instance_idx = 0; instance_idx < MAX_ACTIVE_SFX; instance_idx++) {
		ActiveSfx* instance = &active_sfx[instance_idx];
		if(!instance->active) {
			continue;
		}

		bool any_layer_active = false;
		for(u8 layer_idx = 0; layer_idx < instance->layer_count; layer_idx++) {
			ActiveSfxLayer* layer = &instance->layers[layer_idx];
			if(!layer->active) {
				continue;
			}

			layer->tick_units += sequence_ticks;
			dispatch_sfx_layer(instance_idx, layer_idx);
			if(layer->active) {
				any_layer_active = true;
			}
		}

		if(!any_layer_active) {
			instance->active = false;
		}
	}
	for(u8 voice = SFX_FIRST_VOICE; voice <= SFX_LAST_VOICE; voice++) {
		tick_sfx_voice(voice, sequence_ticks, control_ticks);
	}
}


#ifdef SERIAL
void play_music(UNUSED u8 player, UNUSED u16 seqArgs, UNUSED u16 fadeTimer) {}
#endif

void audio_backend_tick() {
	sfx_game_frame++;
	for(int i = 0; i < MAX_ACTIVE_SFX; i++) {
		if(active_sfx[i].active && !active_sfx[i].discrete &&
		   sfx_game_frame - active_sfx[i].last_request_frame > 2) {
			stop_sfx_instance(&active_sfx[i]);
		}
	}
#ifndef SERIAL
	psx_music_tick();
#endif
}

#endif

#endif
