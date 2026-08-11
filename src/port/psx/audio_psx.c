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
#define SPU_START_ADDR 4096

typedef union {
	struct {
		u16 spu_freq; // 0x1000 * (frequency in Hz) / 44100
		u16 spu_addr; // address in SPU memory divided by 8
	};
	u32 as_u32;
} SampleDef;

static u16* table;

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
	ALIGNED4 MinSecFrame start_msf;
	ALIGNED4 MinSecFrame end_msf;
} BgmInfo;
static BgmInfo bgm_info[256];
#endif

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

	u8* buf = main_pool_alloc(_audio_sample_segment_end - _audio_sample_segment, MEMORY_POOL_RIGHT);
	dma_read(buf, _audio_sample_segment, _audio_sample_segment_end);
	sendSPUData(buf, SPU_START_ADDR, (u32) (_audio_sample_segment_end - _audio_sample_segment + 255) / 256 * 256);
	main_pool_free(buf);

	table = main_pool_alloc(_audio_table_segment_end - _audio_table_segment, MEMORY_POOL_RIGHT);
	dma_read((u8*) table, _audio_table_segment, _audio_table_segment_end);

#if defined(SERIAL) || defined(BENCH)
	SPU_CDDA_VOL_L = 0;
	SPU_CDDA_VOL_R = 0;
#else
	SPU_CTRL |= SPU_CTRL_CDDA;
	SPU_CDDA_VOL_L = 0x7FFF;
	SPU_CDDA_VOL_R = 0x7FFF;
	psx_cd_run_cmd(CDROM_DEMUTE, NULL, 0);

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
		return (SPU_FLAG_STATUS2 & (1 << voice >> 16)) == 0;
	}
}

static SampleDef find_instrument(u32 sound_bank_id, u32 instrument_id) {
	return (SampleDef) {.as_u32 = *((u32*) table + table[sound_bank_id] + instrument_id)};
}
#endif

//static SoundDef find_sound(u32 channel_id, u32 sound_id) {
//	u32 sound_bank_id = sound_defs[channel_id][sound_id * 2];
//	u32 instrument_id = sound_defs[channel_id][sound_id * 2 + 1];
//	SampleDef sample = (SampleDef) {.as_u32 = *((u32*) table + table[sound_bank_id] + instrument_id)};
//	Sound
//}

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

static u8 queued_offs[24] = {};

static void play_note(u32 voice, SampleDef sample, u32 note, u32 gate, s8 velocity) {
	SPU_CH_FREQ(voice) = (u32) sample.spu_freq * note_freq_scales[note] / UNIT;
	SPU_CH_ADDR(voice) = sample.spu_addr;
	s16 vol = 0x4000 / 127 * velocity;
	SPU_CH_VOL_L(voice) = vol;
	SPU_CH_VOL_R(voice) = vol;
	if(voice < 16) {
		SPU_FLAG_ON1 = 1 << voice;
	} else {
		SPU_FLAG_ON2 = 1 << voice >> 16;
	}
	if(gate != 0) {
		queued_offs[voice] = gate * 30 / 240;
	} else {
		queued_offs[voice] = 240;
	}
}

void play_sound(s32 soundBits, UNUSED f32* pos) {
	u32 channel_id = ((u32) soundBits & SOUNDARGS_MASK_BANK) >> SOUNDARGS_SHIFT_BANK;
	u32 sfx_id = ((u32) soundBits & SOUNDARGS_MASK_SOUNDID) >> SOUNDARGS_SHIFT_SOUNDID;
	u32 voice = channel_id + 2;
	SfxDef sfx_def = sfx_defs_per_channel[channel_id][sfx_id];
	SampleDef instrument = find_instrument(sfx_def.bank_id, sfx_def.sample_id);
	if(soundBits & SOUND_DISCRETE) {
		play_note(voice, instrument, sfx_def.note, sfx_def.gate, sfx_def.velocity);
	} else {
		if(!is_voice_playing(voice) || SPU_CH_ADDR(voice) != instrument.spu_addr) {
			play_note(voice, instrument, sfx_def.note, sfx_def.gate, sfx_def.velocity);
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
				psx_cd_run_cmd(CDROM_STOP, NULL, 0);
				cd_playing_audio = false;
			}
		} else {
			cur_song_idx = track - 2;
			psx_cd_run_cmd(CDROM_SETMODE, (const u8[]) {MODE_XA_ADPCM | MODE_XA_SECTOR_FILTER | MODE_2X_SPEED}, 1);
			psx_cd_run_cmd(CDROM_SETFILTER, (const u8[]) {cur_song_idx, 0}, 2);
			psx_cd_run_cmd(CDROM_SETLOC, (u8*) &bgm_info[cur_song_idx].start_msf, 3);
			psx_cd_run_cmd(CDROM_READS, NULL, 0);
			cd_playing_audio = true;
		}
	}
#endif
}

void audio_backend_tick() {
	for(int voice = 0; voice < 24; voice++) {
		if(queued_offs[voice] != 0) {
			queued_offs[voice]--;
			if(queued_offs[voice] == 0) {
				if(voice < 16) {
					SPU_FLAG_OFF1 = 1 << voice;
				} else {
					SPU_FLAG_OFF2 = 1 << voice >> 16;
				}
			}
		}
	}
#if !defined(SERIAL) && !defined(BENCH)
	if(cd_playing_audio) {
		MinSecFrame playing_msf;
		do {
			psx_cd_run_cmd(CDROM_NOP, NULL, 0);
		} while(CDROM_RESULT & CDROM_STAT_SEEK);
		psx_cd_run_cmd(CDROM_GETLOCL, NULL, 0);
		playing_msf.min = CDROM_RESULT;
		playing_msf.sec = CDROM_RESULT;
		playing_msf.frame = CDROM_RESULT;
		MinSecFrame song_end_msf = bgm_info[cur_song_idx].end_msf;
		if(((u32) playing_msf.min << 16 | (u32) playing_msf.sec << 8 | playing_msf.frame) > ((u32) song_end_msf.min << 16 | (u32) song_end_msf.sec << 8 | song_end_msf.frame)) {
			psx_cd_run_cmd(CDROM_SETLOC, (u8*) &bgm_info[cur_song_idx].start_msf, 3);
			psx_cd_run_cmd(CDROM_READS, NULL, 0);
		}
	}
#endif
}

#endif

#endif
