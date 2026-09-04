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
#include <sounds.h>
#include <assert.h>

#ifdef BENCH

void audio_backend_init() {}
void audio_backend_tick() {}
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

#if !defined(SERIAL) && !defined(BENCH)
static u8 track_mapping[] = {
	0,  // SEQ_SOUND_PLAYER,                 // 0x00
	27, // SEQ_EVENT_CUTSCENE_COLLECT_STAR,  // 0x01
	2,  // SEQ_MENU_TITLE_SCREEN,            // 0x02
	5,  // SEQ_LEVEL_GRASS,                  // 0x03
	7,  // SEQ_LEVEL_INSIDE_CASTLE,          // 0x04
	9,  // SEQ_LEVEL_WATER,                  // 0x05
	12, // SEQ_LEVEL_HOT,                    // 0x06
	33, // SEQ_LEVEL_BOSS_KOOPA,             // 0x07
	13, // SEQ_LEVEL_SNOW,                   // 0x08
	6,  // SEQ_LEVEL_SLIDE,                  // 0x09
	14, // SEQ_LEVEL_SPOOKY,                 // 0x0A
	19, // SEQ_EVENT_PIRANHA_PLANT,          // 0x0B
	16, // SEQ_LEVEL_UNDERGROUND,            // 0x0C
	28, // SEQ_MENU_STAR_SELECT,             // 0x0D
	20, // SEQ_EVENT_POWERUP,                // 0x0E
	21, // SEQ_EVENT_METAL_CAP,              // 0x0F
	31, // SEQ_EVENT_KOOPA_MESSAGE,          // 0x10
	32, // SEQ_LEVEL_KOOPA_ROAD,             // 0x11
	29, // SEQ_EVENT_HIGH_SCORE,             // 0x12
	15, // SEQ_EVENT_MERRY_GO_ROUND,         // 0x13
	26, // SEQ_EVENT_RACE,                   // 0x14
	25, // SEQ_EVENT_CUTSCENE_STAR_SPAWN,    // 0x15
	30, // SEQ_EVENT_BOSS,                   // 0x16
	34, // SEQ_EVENT_CUTSCENE_COLLECT_KEY,   // 0x17
	8,  // SEQ_EVENT_ENDLESS_STAIRS,         // 0x18
	35, // SEQ_LEVEL_BOSS_KOOPA_FINAL,       // 0x19
	38, // SEQ_EVENT_CUTSCENE_CREDITS,       // 0x1A
	23, // SEQ_EVENT_SOLVE_PUZZLE,           // 0x1B
	24, // SEQ_EVENT_TOAD_MESSAGE,           // 0x1C
	3,  // SEQ_EVENT_PEACH_MESSAGE,          // 0x1D
	4,  // SEQ_EVENT_CUTSCENE_INTRO,         // 0x1E
	36, // SEQ_EVENT_CUTSCENE_VICTORY,       // 0x1F
	37, // SEQ_EVENT_CUTSCENE_ENDING,        // 0x20
	22, // SEQ_MENU_FILE_SELECT,             // 0x21
	39, // SEQ_EVENT_CUTSCENE_LAKITU,        // 0x22 (not in JP)
};

typedef struct {
	MinSecFrame alignas(u32) start_msf;
	MinSecFrame alignas(u32) end_msf;
} BgmInfo;
static BgmInfo bgm_info[256];
#endif

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
		SPU_CH_ADSR1(i) = 0;
		SPU_CH_ADSR2(i) = 0;
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

#if defined(SERIAL) || defined(BENCH)
	SPU_CDDA_VOL_L = 0;
	SPU_CDDA_VOL_R = 0;
#else
	SPU_CTRL |= SPU_CTRL_CDDA;
	SPU_CDDA_VOL_L = 0x7FFF;
	SPU_CDDA_VOL_R = 0x7FFF;
	psx_cd_run_cmd(CDROM_DEMUTE, NULL, 0, NULL, 0);

	u32 bgm_info_lba = psx_cd_find_file_lba("BGMINFO.DAT;1");
	psx_cd_do_read((u8*) bgm_info, bgm_info_lba, 1, NULL);
	u32 bgm_pack_lba = psx_cd_find_file_lba("BGMPACK.XA;1");
	for(int i = 0; i < 40; i++) {
		bgm_info[i].start_msf = lba_to_msf(bgm_pack_lba + bgm_info[i].start_msf.as_u32);
		bgm_info[i].end_msf = lba_to_msf(bgm_pack_lba + bgm_info[i].end_msf.as_u32);
	}
#endif
}

#ifndef BENCH
static bool is_voice_playing(u32 voice) {
	if(voice < 16) {
		return (SPU_FLAG_STATUS1 & (1 << voice)) == 0;
	} else {
		return (SPU_FLAG_STATUS2 & (1u << (voice - 16))) == 0;
	}
}

static SampleDef find_instrument(u32 sound_bank_id, u32 instrument_id) {
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
 * V3 keeps the V2 absolute-timeline sequencer and adds faithful PS1-side
 * controller effects, hardware reverb routing, and safer voice/sample handling.
 * Events stay on the original absolute 240 Hz timeline and are quantized only
 * at the 30 Hz backend boundary, avoiding cumulative short-note timing error.
 */
#define SFX_FIRST_VOICE 2
#define SFX_LAST_VOICE  23
#define MAX_ACTIVE_SFX  12
#define MAX_SFX_LAYERS  4
#define MAX_SOUND_CHANNELS 16
#define NO_SFX_VOICE 0xFF

#define SFX_SEQUENCE_HZ 240
#define SFX_BACKEND_HZ  30

#define SFX_TICKS_PER_BACKEND (SFX_SEQUENCE_HZ / SFX_BACKEND_HZ)
STATIC_ASSERT(SFX_SEQUENCE_HZ % SFX_BACKEND_HZ == 0, "SFX tick ratio must be integral");

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
	u8 layer_count;
	ActiveSfxLayer layers[MAX_SFX_LAYERS];
} ActiveSfx;

typedef struct {
	bool owned;
	bool keyed;
	bool releasing;
	u8 owner_instance;
	u8 owner_layer;
	u16 sample_addr;
	u32 age;

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
	u8 release_frames_left;
	u8 release_frames_total;
	u16 base_vol_l;
	u16 base_vol_r;
} SfxVoiceState;

static ActiveSfx active_sfx[MAX_ACTIVE_SFX] = {};
static SfxVoiceState sfx_voice_state[24] = {};
static u32 sfx_voice_age_clock = 1;
static bool last_non_discrete_voice_valid[MAX_SOUND_CHANNELS] = {};
static u8 last_non_discrete_voice[MAX_SOUND_CHANNELS] = {};

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
	u32 pitch = (u32) sample.spu_freq * note_freq_scales[note] / UNIT;
	if(pitch < 1) {
		pitch = 1;
	} else if(pitch > 0x3FFF) {
		pitch = 0x3FFF;
	}
	return (u16) pitch;
}

static void volume_for_pan(u8 velocity, u8 pan, u16* out_l, u16* out_r) {
	u32 vol = (0x3FFFu * velocity) / 127u;
	u32 left;
	u32 right;

	if(pan <= 64) {
		left = vol;
		right = vol * pan / 64u;
	} else {
		right = vol;
		left = vol * (127u - pan) / 63u;
	}

	*out_l = (u16) left;
	*out_r = (u16) right;
}

static void clear_voice_runtime(u8 voice, bool clear_owner) {
	SfxVoiceState* v = &sfx_voice_state[voice];
	v->keyed = false;
	v->releasing = false;
	v->gate_left = 0;
	v->port_elapsed = 0;
	v->port_total = 0;
	v->vibrato_delay_left = 0;
	v->release_frames_left = 0;
	v->release_frames_total = 0;
	if(clear_owner) {
		v->owned = false;
		v->owner_instance = NO_SFX_VOICE;
		v->owner_layer = NO_SFX_VOICE;
	}
}

static void hard_stop_voice(u8 voice, bool clear_owner) {
	sfx_set_voice_reverb(voice, false);
	key_off_voice_hw(voice);
	SPU_CH_VOL_L(voice) = 0;
	SPU_CH_VOL_R(voice) = 0;
	clear_voice_runtime(voice, clear_owner);
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

static s32 alloc_sfx_voice(u8 instance_idx, u8 layer_idx) {
	for(s32 voice = SFX_FIRST_VOICE; voice <= SFX_LAST_VOICE; voice++) {
		SfxVoiceState* v = &sfx_voice_state[voice];
		if(!v->owned && !is_voice_playing((u32) voice)) {
			v->owned = true;
			v->owner_instance = instance_idx;
			v->owner_layer = layer_idx;
			return voice;
		}
	}

	for(s32 voice = SFX_FIRST_VOICE; voice <= SFX_LAST_VOICE; voice++) {
		SfxVoiceState* v = &sfx_voice_state[voice];
		if(!v->owned) {
			hard_stop_voice((u8) voice, false);
			v->owned = true;
			v->owner_instance = instance_idx;
			v->owner_layer = layer_idx;
			return voice;
		}
	}

	/* Prefer reclaiming a release tail or a voice the SPU has already stopped
	 * before cutting off an actively sounding note. */
	for(s32 voice = SFX_FIRST_VOICE; voice <= SFX_LAST_VOICE; voice++) {
		SfxVoiceState* v = &sfx_voice_state[voice];
		if(v->owned && (v->releasing || !is_voice_playing((u32) voice))) {
			detach_voice_from_old_layer((u8) voice);
			hard_stop_voice((u8) voice, false);
			v->owned = true;
			v->owner_instance = instance_idx;
			v->owner_layer = layer_idx;
			return voice;
		}
	}

	/* All SFX voices are actively owned: steal the least recently triggered one. */
	s32 best_voice = SFX_FIRST_VOICE;
	u32 best_age = sfx_voice_state[best_voice].age;
	for(s32 voice = SFX_FIRST_VOICE + 1; voice <= SFX_LAST_VOICE; voice++) {
		if(sfx_voice_state[voice].age < best_age) {
			best_age = sfx_voice_state[voice].age;
			best_voice = voice;
		}
	}

	detach_voice_from_old_layer((u8) best_voice);
	hard_stop_voice((u8) best_voice, false);
	sfx_voice_state[best_voice].owned = true;
	sfx_voice_state[best_voice].owner_instance = instance_idx;
	sfx_voice_state[best_voice].owner_layer = layer_idx;
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
	v->release_frames_total = release_frames_for_rate(v->release_rate);
	v->release_frames_left = v->release_frames_total;
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

static void tick_sfx_voice(u8 voice) {
	SfxVoiceState* v = &sfx_voice_state[voice];
	if(!v->owned && !v->keyed && !v->releasing) {
		return;
	}

	if(v->keyed && !v->releasing) {
		if(v->gate_left != 0) {
			if(v->gate_left <= SFX_TICKS_PER_BACKEND) {
				v->gate_left = 0;
				begin_voice_release(voice);
			} else {
				v->gate_left -= SFX_TICKS_PER_BACKEND;
			}
		}

		if(v->port_total != 0 && v->port_elapsed < v->port_total) {
			u32 elapsed = v->port_elapsed + SFX_TICKS_PER_BACKEND;
			v->port_elapsed = (u16) (elapsed > v->port_total ? v->port_total : elapsed);
		}

		if(v->vibrato_delay_left != 0) {
			if(v->vibrato_delay_left <= SFX_TICKS_PER_BACKEND) {
				v->vibrato_delay_left = 0;
			} else {
				v->vibrato_delay_left -= SFX_TICKS_PER_BACKEND;
			}
		} else if(v->vibrato_extent != 0) {
			/* N64 rate arg -> rate*32 per ~240 Hz update; advance 8 updates here. */
			v->vibrato_phase += (u32) v->vibrato_rate * 256u;
		}

		SPU_CH_FREQ(voice) = voice_effective_pitch(v);
	}

	if(v->releasing) {
		if(v->release_frames_left > 0) {
			v->release_frames_left--;
			u32 left = (u32) v->base_vol_l * v->release_frames_left / v->release_frames_total;
			u32 right = (u32) v->base_vol_r * v->release_frames_left / v->release_frames_total;
			SPU_CH_VOL_L(voice) = (u16) left;
			SPU_CH_VOL_R(voice) = (u16) right;
		}
		if(v->release_frames_left == 0) {
			hard_stop_voice(voice, true);
		}
	}
}

static void start_event_on_voice(u8 voice, const SfxEvent* event, bool no_echo) {
	SfxVoiceState* v = &sfx_voice_state[voice];
	SampleDef sample = find_instrument(event->bank_id, event->sample_id);
	u16 note_pitch = pitch_for_note(sample, event->note);
	u16 target_pitch = pitch_for_note(sample, event->portamento_target);
	bool continuous = (event->pan_flags & SFX_EVENT_CONTINUOUS) != 0;
	bool can_reuse = continuous && v->keyed && !v->releasing &&
	                 v->sample_addr == sample.spu_addr && is_voice_playing(voice);

	volume_for_pan(event->velocity, event->pan_flags & SFX_EVENT_PAN_MASK, &v->base_vol_l, &v->base_vol_r);
	v->release_rate = event->release_rate;
	v->gate_left = event->gate_ticks != 0 ? event->gate_ticks : 1;
	v->vibrato_rate = event->vibrato_rate;
	v->vibrato_extent = event->vibrato_extent;
	v->vibrato_delay_left = event->vibrato_delay_ticks;
	v->vibrato_phase = 0;
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

	/* The PS1 SPU exposes a binary per-voice reverb send (EON). Keep the
	 * sequence's zero/nonzero intent and honor SOUND_NO_ECHO. Set the route
	 * before KEY-ON so the first samples of a fresh voice use the right path. */
	sfx_set_voice_reverb(voice, !no_echo && event->reverb != 0);

	if(!can_reuse) {
		key_off_voice_hw(voice);
		v->sample_addr = sample.spu_addr;
		SPU_CH_ADDR(voice) = sample.spu_addr;
		SPU_CH_FREQ(voice) = voice_effective_pitch(v);
		SPU_CH_VOL_L(voice) = v->base_vol_l;
		SPU_CH_VOL_R(voice) = v->base_vol_r;
		v->keyed = true;
		v->releasing = false;
		key_on_voice_hw(voice);
	} else {
		v->releasing = false;
		v->release_frames_left = 0;
		v->release_frames_total = 0;
		SPU_CH_FREQ(voice) = voice_effective_pitch(v);
		SPU_CH_VOL_L(voice) = v->base_vol_l;
		SPU_CH_VOL_R(voice) = v->base_vol_r;
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

		if(layer->voice == NO_SFX_VOICE) {
			s32 voice = alloc_sfx_voice(instance_idx, layer_idx);
			if(voice < 0) {
				return;
			}
			layer->voice = (u8) voice;
		}

		SfxVoiceState* v = &sfx_voice_state[layer->voice];
		v->owned = true;
		v->owner_instance = instance_idx;
		v->owner_layer = layer_idx;
		start_event_on_voice(layer->voice, event, instance->no_echo);

		if(!instance->discrete && instance->channel_id < MAX_SOUND_CHANNELS && layer_idx == 0) {
			last_non_discrete_voice_valid[instance->channel_id] = true;
			last_non_discrete_voice[instance->channel_id] = layer->voice;
		}

		layer->event_index++;
	}
}

static ActiveSfx* alloc_sfx_instance(void) {
	for(int i = 0; i < MAX_ACTIVE_SFX; i++) {
		if(!active_sfx[i].active) {
			return &active_sfx[i];
		}
	}
	return NULL;
}

static bool same_sfx_is_active(u32 channel_id, u32 sfx_id) {
	for(int i = 0; i < MAX_ACTIVE_SFX; i++) {
		if(active_sfx[i].active &&
		   active_sfx[i].channel_id == channel_id &&
		   active_sfx[i].sfx_id == sfx_id) {
			return true;
		}
	}
	return false;
}

void play_sound(s32 soundBits, UNUSED f32* pos) {
	u32 channel_id = ((u32) soundBits & SOUNDARGS_MASK_BANK) >> SOUNDARGS_SHIFT_BANK;
	u32 sfx_id = ((u32) soundBits & SOUNDARGS_MASK_SOUNDID) >> SOUNDARGS_SHIFT_SOUNDID;

	if(channel_id >= sfx_channel_count || sfx_id >= sfx_count_per_channel[channel_id]) {
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
	if(!discrete) {
		if(same_sfx_is_active(channel_id, sfx_id)) {
			return;
		}

		if(channel_id < MAX_SOUND_CHANNELS && last_non_discrete_voice_valid[channel_id]) {
			u8 voice = last_non_discrete_voice[channel_id];
			const SfxLayerDef* first_layer = &sfx_blob_layers[sfx_def->first_layer];
			if(voice >= SFX_FIRST_VOICE && voice <= SFX_LAST_VOICE &&
			   first_layer->event_count != 0) {
				const SfxEvent* first_event = &sfx_blob_events[first_layer->first_event];
				SampleDef first_sample = find_instrument(first_event->bank_id, first_event->sample_id);
				if(is_voice_playing(voice) && SPU_CH_ADDR(voice) == first_sample.spu_addr) {
					return;
				}
			}
		}
	}

	ActiveSfx* instance = alloc_sfx_instance();
	if(instance == NULL) {
		return;
	}
	u8 instance_idx = (u8) (instance - active_sfx);

	instance->active = true;
	instance->discrete = discrete;
	instance->no_echo = no_echo;
	instance->channel_id = (u8) channel_id;
	instance->sfx_id = (u16) sfx_id;
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

static void sfx_backend_tick(void) {
	for(u8 voice = SFX_FIRST_VOICE; voice <= SFX_LAST_VOICE; voice++) {
		tick_sfx_voice(voice);
	}

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

			layer->tick_units += SFX_TICKS_PER_BACKEND;
			dispatch_sfx_layer(instance_idx, layer_idx);
			if(layer->active) {
				any_layer_active = true;
			}
		}

		if(!any_layer_active) {
			instance->active = false;
		}
	}
}


bool cd_playing_audio = false;
#if !defined(SERIAL) && !defined(BENCH)
static u8 cur_song_idx;
#endif

void play_music(UNUSED u8 player, UNUSED u16 seqArgs, UNUSED u16 fadeTimer) {
#if !defined(SERIAL) && !defined(BENCH)
	if((seqArgs >> 8) == SEQ_PLAYER_LEVEL) {
		u8 track = track_mapping[seqArgs & 0xFF];
		if(track == 0) {
			if(cd_playing_audio) {
				psx_cd_run_cmd(CDROM_STOP, NULL, 0, NULL, 0);
				cd_playing_audio = false;
			}
		} else {
			cur_song_idx = track - 2;
			psx_cd_run_cmd(CDROM_SETMODE, (const u8[]) {MODE_XA_ADPCM | MODE_XA_SECTOR_FILTER | MODE_2X_SPEED}, 1, NULL, 0);
			psx_cd_run_cmd(CDROM_SETFILTER, (const u8[]) {cur_song_idx, 0}, 2, NULL, 0);
			psx_cd_run_cmd(CDROM_SETLOC, (u8*) bgm_info[cur_song_idx].start_msf.bytes, 3, NULL, 0);
			psx_cd_run_cmd(CDROM_READS, NULL, 0, NULL, 0);
			cd_playing_audio = true;
		}
	}
#endif
}

void audio_backend_tick() {
	sfx_backend_tick();
#if !defined(SERIAL) && !defined(BENCH)
	if(cd_playing_audio) {
		MinSecFrame playing_msf;
		do {
			psx_cd_run_cmd(CDROM_NOP, NULL, 0, NULL, 0);
		} while(CDROM_RESULT & CDROM_STAT_SEEK);
		psx_cd_run_cmd(CDROM_GETLOCL, NULL, 0, playing_msf.bytes, 3);
		MinSecFrame song_end_msf = bgm_info[cur_song_idx].end_msf;
		if(((u32) playing_msf.min << 16 | (u32) playing_msf.sec << 8 | playing_msf.frame) > ((u32) song_end_msf.min << 16 | (u32) song_end_msf.sec << 8 | song_end_msf.frame)) {
			psx_cd_run_cmd(CDROM_SETLOC, (u8*) bgm_info[cur_song_idx].start_msf.bytes, 3, NULL, 0);
			psx_cd_run_cmd(CDROM_READS, NULL, 0, NULL, 0);
		}
	}
#endif
}

#endif

#endif
