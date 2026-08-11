#ifndef TARGET_PSX

#include <types.h>

void audio_backend_init() {}
void play_sound(s32 soundBits, f32 *pos) {}
bool cd_playing_audio = false;
void play_music(u8 player, u16 seqArgs, u16 fadeTimer) {}
void audio_backend_tick() {}

#endif
