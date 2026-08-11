#include <PR/os_cont.h>
#include <lib/src/osContInternal.h>
#include <SDL3/SDL.h>
#include <game/main.h>
#include <port/gfx/gfx.h>

void controller_backend_read(OSContPad* pad, u32 port) {
	pad->button = 0;
	pad->errnum = 0;
	const bool* keys = SDL_GetKeyboardState(NULL);
	if(keys[SDL_SCANCODE_A]) {
		pad->stick_x = -80;
	} else if(keys[SDL_SCANCODE_D]) {
		pad->stick_x = 80;
	} else {
		pad->stick_x = 0;
	}
	if(keys[SDL_SCANCODE_S]) {
		pad->stick_y = -80;
	} else if(keys[SDL_SCANCODE_W]) {
		pad->stick_y = 80;
	} else {
		pad->stick_y = 0;
	}
	if(keys[SDL_SCANCODE_F]) {
		pad->right_stick_x = -80;
	} else if(keys[SDL_SCANCODE_H]) {
		pad->right_stick_x = 80;
	} else {
		pad->right_stick_x = 0;
	}
	if(keys[SDL_SCANCODE_T]) {
		pad->right_stick_y = -80;
	} else if(keys[SDL_SCANCODE_G]) {
		pad->right_stick_y = 80;
	} else {
		pad->right_stick_y = 0;
	}
	if(keys[SDL_SCANCODE_SPACE] || keys[SDL_SCANCODE_K]) pad->button |= A_BUTTON;
	if(keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL] || keys[SDL_SCANCODE_J] || keys[SDL_SCANCODE_L]) pad->button |= B_BUTTON;
	if(keys[SDL_SCANCODE_UP]) pad->button |= U_CBUTTONS;
	if(keys[SDL_SCANCODE_DOWN]) pad->button |= D_CBUTTONS;
	if(keys[SDL_SCANCODE_LEFT]) pad->button |= L_CBUTTONS;
	if(keys[SDL_SCANCODE_RIGHT]) pad->button |= R_CBUTTONS;
	if(keys[SDL_SCANCODE_RETURN]) pad->button |= START_BUTTON;
	if(keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT] || keys[SDL_SCANCODE_Q]) pad->button |= Z_TRIG;
	if(keys[SDL_SCANCODE_E]) pad->button |= R_TRIG;
	if(keys[SDL_SCANCODE_1]) pad->button |= L_TRIG;

	static bool already_backspacing = false;
	if(keys[SDL_SCANCODE_BACKSPACE]) {
		if(!already_backspacing) {
			debug_processed_poly_count = 0;
			gShowDebugText = !gShowDebugText;
			gShowProfiler = !gShowProfiler;
			already_backspacing = true;
		}
	} else {
		already_backspacing = false;
	}
}
