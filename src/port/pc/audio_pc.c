#include <SDL3/SDL.h>
#include <stdio.h>

static SDL_AudioDeviceID dev;

static bool audio_sdl_init(void) {
	if(SDL_Init(SDL_INIT_AUDIO) != 0) {
		fprintf(stderr, "SDL init error: %s\n", SDL_GetError());
		return false;
	}
	SDL_AudioSpec want, have;
	SDL_zero(want);
	want.freq = 32000;
	want.format = SDL_AUDIO_S16LE;
	want.channels = 2;
	dev = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, 0);
	if(dev == 0) {
		fprintf(stderr, "SDL_OpenAudio error: %s\n", SDL_GetError());
		return false;
	}
	SDL_PauseAudioDevice(dev);
	return true;
}

static void audio_sdl_play(const uint8_t *buf, size_t len) {
}
