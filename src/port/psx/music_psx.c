#if defined(TARGET_PSX) && !defined(SERIAL) && !defined(BENCH)

#include <audio/external.h>
#include <game/area.h>
#include <game/camera.h>
#include <game/level_update.h>
#include <port/psx/cd_psx.h>
#include <ps1/registers.h>
#include <seq_ids.h>
#include <sm64.h>

#define XA_NONE 0xFF
#define XA_MISSING 0xFFFFFFFFu
#define XA_INTERVAL 8
#define XA_POLL_FRAMES 2
#define XA_MUSIC_VOLUME 0x5200

enum {
    TRACK_DIRE_DOCKS = 7,
    TRACK_CAVE_DUNGEON = 14,
    TRACK_COUNT = 38,
};

/* Values are WAV slot numbers plus one, so unspecified entries stay silent. */
static const u8 track_for_sequence[SEQ_COUNT] = {
    [SEQ_EVENT_CUTSCENE_COLLECT_STAR] = 25 + 1,
    [SEQ_MENU_TITLE_SCREEN] = 0 + 1,
    [SEQ_LEVEL_GRASS] = 3 + 1,
    [SEQ_LEVEL_INSIDE_CASTLE] = 5 + 1,
    [SEQ_LEVEL_WATER] = TRACK_DIRE_DOCKS + 1,
    [SEQ_LEVEL_HOT] = 10 + 1,
    [SEQ_LEVEL_BOSS_KOOPA] = 31 + 1,
    [SEQ_LEVEL_SNOW] = 11 + 1,
    [SEQ_LEVEL_SLIDE] = 4 + 1,
    [SEQ_LEVEL_SPOOKY] = 12 + 1,
    [SEQ_EVENT_PIRANHA_PLANT] = 17 + 1,
    [SEQ_LEVEL_UNDERGROUND] = TRACK_CAVE_DUNGEON + 1,
    [SEQ_MENU_STAR_SELECT] = 26 + 1,
    [SEQ_EVENT_POWERUP] = 18 + 1,
    [SEQ_EVENT_METAL_CAP] = 19 + 1,
    [SEQ_EVENT_KOOPA_MESSAGE] = 29 + 1,
    [SEQ_LEVEL_KOOPA_ROAD] = 30 + 1,
    [SEQ_EVENT_HIGH_SCORE] = 27 + 1,
    [SEQ_EVENT_MERRY_GO_ROUND] = 13 + 1,
    [SEQ_EVENT_RACE] = 24 + 1,
    [SEQ_EVENT_CUTSCENE_STAR_SPAWN] = 23 + 1,
    [SEQ_EVENT_BOSS] = 28 + 1,
    [SEQ_EVENT_CUTSCENE_COLLECT_KEY] = 32 + 1,
    [SEQ_EVENT_ENDLESS_STAIRS] = 6 + 1,
    [SEQ_LEVEL_BOSS_KOOPA_FINAL] = 33 + 1,
    [SEQ_EVENT_CUTSCENE_CREDITS] = 36 + 1,
    [SEQ_EVENT_SOLVE_PUZZLE] = 21 + 1,
    [SEQ_EVENT_TOAD_MESSAGE] = 22 + 1,
    [SEQ_EVENT_PEACH_MESSAGE] = 1 + 1,
    [SEQ_EVENT_CUTSCENE_INTRO] = 2 + 1,
    [SEQ_EVENT_CUTSCENE_VICTORY] = 34 + 1,
    [SEQ_EVENT_CUTSCENE_ENDING] = 35 + 1,
    [SEQ_MENU_FILE_SELECT] = 20 + 1,
    [SEQ_EVENT_CUTSCENE_LAKITU] = 37 + 1,
};

typedef struct {
    u32 start;
    u32 end;
} XaTrack;

typedef struct {
    u32 lba;
    u16 seq_args;
    u8 track;
    bool loop;
} MusicStream;

static XaTrack tracks[TRACK_COUNT];
static MusicStream level = {.track = XA_NONE};
static MusicStream overlay = {.track = XA_NONE};
static MusicStream jingle = {.track = XA_NONE};
static MusicStream* playing;
static bool ready;
static bool reading;
static u8 poll_countdown;
static u16 volume = XA_MUSIC_VOLUME;
static u16 level_volume = XA_MUSIC_VOLUME;
static u16 fade_from;
static u16 fade_to;
static u16 fade_total;
static u16 fade_left;
static bool stop_at_fade_end;
static bool muted;

static bool has_track(u8 track) {
    return track < TRACK_COUNT && tracks[track].start != XA_MISSING;
}

void psx_music_init(void) {
    /* CD reads transfer a whole 2048-byte sector. */
    u32 info[2048 / sizeof(u32)];
    psx_cd_do_read((u8*) info, psx_cd_find_file_lba("BGMINFO.DAT;1"), 1, NULL);
    u32 pack_lba = psx_cd_find_file_lba("BGMPACK.XA;1");
    for(u8 track = 0; track < TRACK_COUNT; track++) {
        u32 start = info[2 * track];
        u32 end = info[2 * track + 1];
        tracks[track].start = start == XA_MISSING ? XA_MISSING : pack_lba + start;
        tracks[track].end = end == XA_MISSING ? XA_MISSING : pack_lba + end;
    }
    SPU_CDDA_VOL_L = volume;
    SPU_CDDA_VOL_R = volume;
    SPU_CTRL |= SPU_CTRL_CDDA;
    psx_cd_run_cmd(CDROM_DEMUTE, NULL, 0, NULL, 0);
    ready = true;
}

static bool read_position(u32* lba) {
    /* GetlocP works during seek as well; avoid a preceding NOP command
     * while ReadS is streaming XA sectors on the shared drive. */
    return psx_cd_get_audio_lba(lba);
}

static void cache_resume_position(u32 lba) {
    if(!playing || playing->track == XA_NONE) {
        return;
    }
    XaTrack track = tracks[playing->track];
    if(lba < track.start || lba >= track.end) {
        return;
    }
    u32 next = track.start + ((lba - track.start) / XA_INTERVAL + 1) * XA_INTERVAL;
    playing->lba = next < track.end ? next : track.end - XA_INTERVAL;
}

static void remember_position(void) {
    u32 lba;
    if(reading && read_position(&lba)) {
        cache_resume_position(lba);
    }
}

static void start_read(void) {
    MinSecFrame msf = lba_to_msf(playing->lba);
    psx_cd_run_cmd(CDROM_SETMODE, (const u8[]) {MODE_XA_ADPCM | MODE_XA_SECTOR_FILTER | MODE_2X_SPEED}, 1, NULL, 0);
    psx_cd_run_cmd(CDROM_SETFILTER, (const u8[]) {playing->track, 0}, 2, NULL, 0);
    psx_cd_run_cmd(CDROM_SETLOC, msf.bytes, 3, NULL, 0);
    psx_cd_run_cmd(CDROM_READS, NULL, 0, NULL, 0);
    reading = true;
    poll_countdown = XA_POLL_FRAMES - 1;
}

static void pause_read(void) {
    if(reading) {
        psx_cd_run_cmd(CDROM_PAUSE, NULL, 0, NULL, 0);
        psx_cd_await_interrupt(IRQ_BLOCKING_CMD_DONE, NULL, 0);
        reading = false;
    }
}

void psx_music_suspend_cd(void) {
    /* A level load must stop XA before issuing any further CD commands.
     * The music tick already checkpoints playback position, so querying
     * GetlocP here (while a loading screen is on display) is unnecessary
     * and risks stalling the handoff if an audio/data IRQ is pending. */
    pause_read();
}

static void set_volume(u16 value) {
    volume = value;
    if(muted) {
        value /= 3;
    }
    SPU_CDDA_VOL_L = value;
    SPU_CDDA_VOL_R = value;
}

void set_audio_muted(u8 value) {
    muted = value != 0;
    set_volume(volume);
}

static void fade_volume(u16 target, u16 duration) {
    /* The original fades use the audio update clock, about four per game frame. */
    fade_from = volume;
    fade_to = target;
    fade_total = fade_left = (duration + 3) / 4;
    if(fade_left == 0) {
        set_volume(target);
    }
}

static void select_stream(bool restart) {
    MusicStream* next = jingle.track != XA_NONE ? &jingle :
                        overlay.track != XA_NONE ? &overlay :
                        level.track != XA_NONE ? &level : NULL;
    if(next == playing && !restart) {
        return;
    }
    pause_read();
    playing = next;
    if(playing && !stop_at_fade_end) {
        fade_left = 0;
        set_volume(playing == &level ? level_volume : XA_MUSIC_VOLUME);
    }
}

static void stop_all_music(void) {
    level.track = overlay.track = jingle.track = XA_NONE;
    stop_at_fade_end = false;
    fade_left = 0;
    select_stream(false);
}

static bool loops(u8 seq) {
    switch(seq) {
        case SEQ_EVENT_CUTSCENE_COLLECT_STAR:
        case SEQ_MENU_STAR_SELECT:
        case SEQ_EVENT_KOOPA_MESSAGE:
        case SEQ_EVENT_HIGH_SCORE:
        case SEQ_EVENT_RACE:
        case SEQ_EVENT_CUTSCENE_STAR_SPAWN:
        case SEQ_EVENT_CUTSCENE_COLLECT_KEY:
        case SEQ_EVENT_CUTSCENE_CREDITS:
        case SEQ_EVENT_SOLVE_PUZZLE:
        case SEQ_EVENT_TOAD_MESSAGE:
        case SEQ_EVENT_PEACH_MESSAGE:
        case SEQ_EVENT_CUTSCENE_INTRO:
        case SEQ_EVENT_CUTSCENE_VICTORY:
        case SEQ_EVENT_CUTSCENE_ENDING:
        case SEQ_EVENT_CUTSCENE_LAKITU:
            return false;
        default:
            return true;
    }
}

/* Match the layer selections in the original audio/external.c. The three
 * restored mixes of each song share a timeline. */
static u8 track_variant(u8 seq_id) {
    u8 seq = seq_id & SEQ_BASE_ID;
    if(seq == SEQ_LEVEL_WATER) {
        s16 x = gMarioState->pos[0];
        s16 y = gMarioState->pos[1];
        s16 z = gMarioState->pos[2];
        if(gCurrLevelNum == LEVEL_DDD) {
            if(gCurrAreaIndex == (AREA_DDD_WHIRLPOOL & 15) && (x < -800 || (x < 470 && y >= -2000))) {
                return TRACK_DIRE_DOCKS;
            }
            return TRACK_DIRE_DOCKS + (gCurrAreaIndex == (AREA_DDD_SUB & 15) && y >= 100 ? 2 : 1);
        }
        if(gCurrLevelNum == LEVEL_JRB) {
            if(gCurrAreaIndex == (AREA_JRB_SHIP & 15) || y >= 1000 || (y >= 945 && x < -5260)) {
                return TRACK_DIRE_DOCKS;
            }
            return TRACK_DIRE_DOCKS + (y >= -3100 && z < -900 ? 2 : 1);
        }
    } else if(seq == SEQ_LEVEL_UNDERGROUND) {
        if(seq_id & SEQ_VARIATION) {
            return TRACK_CAVE_DUNGEON + 2;
        }
        s16 x = gMarioState->pos[0];
        s16 y = gMarioState->pos[1];
        if((gCurrLevelNum == LEVEL_HMC && y < (x >= 0 ? -203 : -2400)) ||
           (gCurrLevelNum == LEVEL_WDW && (gCurrAreaIndex == (AREA_WDW_TOWN & 15) ||
            (gCurrAreaIndex == (AREA_WDW_MAIN & 15) && y < -670)))) {
            return TRACK_CAVE_DUNGEON + 1;
        }
    }
    return track_for_sequence[seq] - 1;
}

void play_music(u8 player, u16 seqArgs, u16 fadeTimer) {
    u8 seq = seqArgs & SEQ_BASE_ID;
    if(!ready || player == SEQ_PLAYER_SFX || seq >= SEQ_COUNT) {
        return;
    }
    if(seq == SEQ_SOUND_PLAYER) {
        if(player == SEQ_PLAYER_LEVEL) {
            stop_all_music();
        }
        return;
    }
    MusicStream* slot = &level;
    if(seq == SEQ_EVENT_POWERUP || seq == SEQ_EVENT_METAL_CAP || seq == SEQ_EVENT_ENDLESS_STAIRS) {
        slot = &overlay;
    } else if(player == SEQ_PLAYER_ENV || seq == SEQ_EVENT_CUTSCENE_LAKITU) {
        slot = &jingle;
    }
    u8 track = track_variant(seqArgs);
    if(!has_track(track)) {
        track = track_for_sequence[seq] - 1;
    }
    if(!has_track(track)) {
        if(slot == &level) {
            stop_all_music();
        }
        return;
    }
    if(slot->track == track && slot->seq_args == seqArgs && slot->loop && !stop_at_fade_end) {
        return;
    }

    remember_position();
    if(slot == &level) {
        overlay.track = XA_NONE;
        level_volume = XA_MUSIC_VOLUME;
    }
    *slot = (MusicStream) {tracks[track].start, seqArgs, track, slot != &jingle && loops(seq)};
    stop_at_fade_end = false;
    select_stream(playing == slot);
    if(playing == slot) {
        set_volume(fadeTimer ? 0 : XA_MUSIC_VOLUME);
        fade_volume(XA_MUSIC_VOLUME, fadeTimer);
    }
}

void stop_background_music(u16 seqId) {
    MusicStream* slot = overlay.track != XA_NONE && (u8) overlay.seq_args == (u8) seqId ? &overlay : &level;
    if(slot->track != XA_NONE && (u8) slot->seq_args == (u8) seqId) {
        slot->track = XA_NONE;
        select_stream(false);
    }
}

u16 get_current_background_music(void) {
    if(overlay.track != XA_NONE && (overlay.seq_args & SEQ_BASE_ID) != SEQ_EVENT_ENDLESS_STAIRS) {
        return overlay.seq_args;
    }
    return level.track == XA_NONE ? 0xFFFF : level.seq_args;
}

void seq_player_fade_out(u8 player, u16 fadeDuration) {
    if(player == SEQ_PLAYER_LEVEL && playing) {
        if(fadeDuration == 0) {
            stop_all_music();
        } else {
            stop_at_fade_end = true;
            fade_volume(0, fadeDuration);
        }
    }
}

void fadeout_background_music(u16 seqId, u16 fadeOut) {
    if(overlay.track != XA_NONE && (u8) overlay.seq_args == (u8) seqId) {
        /* Release cap/shell music back to the interrupted area track. */
        stop_background_music(seqId);
    } else if(level.track != XA_NONE && (u8) level.seq_args == (u8) seqId) {
        seq_player_fade_out(SEQ_PLAYER_LEVEL, fadeOut);
    }
}

void fade_volume_scale(u8 player, u8 targetScale, u16 fadeDuration) {
    if(player == SEQ_PLAYER_LEVEL) {
        if(targetScale > 127) {
            targetScale = 127;
        }
        level_volume = XA_MUSIC_VOLUME * targetScale / 127;
        if(playing == &level && !stop_at_fade_end) {
            fade_volume(level_volume, fadeDuration);
        }
    }
}

void seq_player_lower_volume(u8 player, u16 fadeDuration, u8 percentage) {
    fade_volume_scale(player, percentage, fadeDuration);
}

void seq_player_unlower_volume(u8 player, u16 fadeDuration) {
    fade_volume_scale(player, 127, fadeDuration);
}

void play_secondary_music(u8 seqId, UNUSED u8 bgMusicVolume, UNUSED u8 volumeScale, u16 fadeTimer) {
    play_music(SEQ_PLAYER_ENV, seqId, fadeTimer);
}

void func_80321080(UNUSED u16 fadeTimer) {
    stop_background_music(SEQ_EVENT_ENDLESS_STAIRS);
}

void func_803210D4(u16 fadeDuration) {
    seq_player_fade_out(SEQ_PLAYER_LEVEL, fadeDuration);
}

void psx_music_tick(void) {
    if(fade_left != 0) {
        fade_left--;
        set_volume(fade_to + ((s32) fade_from - fade_to) * fade_left / fade_total);
        if(fade_left == 0 && stop_at_fade_end) {
            stop_all_music();
        }
    }
    if(!playing) {
        return;
    }
    if(!reading) {
        /* Texture and overlay reads often come in bursts. Resume XA once
         * the frame has finished using the disc, from the saved sector. */
        start_read();
        return;
    }
    if(poll_countdown != 0) {
        poll_countdown--;
        return;
    }
    poll_countdown = XA_POLL_FRAMES - 1;
    u32 lba;
    if(!read_position(&lba)) {
        return;
    }
    XaTrack track = tracks[playing->track];
    if(lba < track.start) {
        return;
    }
    if(lba >= track.end) {
        if(playing->loop) {
            playing->lba = track.start;
            start_read();
        } else {
            playing->track = XA_NONE;
            select_stream(false);
        }
    } else if(playing == &level) {
        u8 variant = track_variant(level.seq_args);
        if(variant != level.track && has_track(variant)) {
            XaTrack new_track = tracks[variant];
            u32 offset = (lba - track.start) / XA_INTERVAL * XA_INTERVAL;
            level.track = variant;
            level.lba = new_track.start + offset;
            if(level.lba >= new_track.end) {
                level.lba = new_track.start;
            }
            start_read();
            return;
        }
    }
    /* Checkpoint the last XA position during normal audio ticks rather
     * than synchronously querying the drive on the data-loading path. */
    cache_resume_position(lba);
}

#endif
