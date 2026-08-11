#include "gdf.h"

ShortMatrix gdf_process_anim(GdfAnimationPlayback* playback) {
	struct AnimDataInfo* anim_info = &playback->bank[playback->current];
	s16 anim_pos[] = {0, 0, 0};
	s16 anim_rot[] = {0, 0, 0};
	s16* anim_data = anim_info->data;
	switch(anim_info->type) {
		case GD_ANIM_POS3S: {
			s16* frame_data = anim_data + playback->frame * 3;
			anim_pos[0] = frame_data[0];
			anim_pos[1] = frame_data[1];
			anim_pos[2] = frame_data[2];
			break;
		}
		case GD_ANIM_ROT3S: {
			s16* frame_data = anim_data + playback->frame * 3;
			anim_rot[0] = GD_TO_GDF_ANIM_ANGLE(frame_data[0]);
			anim_rot[1] = GD_TO_GDF_ANIM_ANGLE(frame_data[1]);
			anim_rot[2] = GD_TO_GDF_ANIM_ANGLE(frame_data[2]);
			break;
		}
		case GD_ANIM_ROT3S_POS3S: {
			s16* frame_data = anim_data + playback->frame * 6;
			anim_rot[0] = GD_TO_GDF_ANIM_ANGLE(frame_data[0]);
			anim_rot[1] = GD_TO_GDF_ANIM_ANGLE(frame_data[1]);
			anim_rot[2] = GD_TO_GDF_ANIM_ANGLE(frame_data[2]);
			anim_pos[0] = frame_data[3];
			anim_pos[1] = frame_data[4];
			anim_pos[2] = frame_data[5];
			break;
		}
		default:
	}
	playback->frame++;
	if(playback->frame >= anim_info->count) {
		playback->frame = 0;
		playback->current++;
		if(!playback->bank[playback->current].data) {
			playback->current = 0;
		}
	}
	return mtx_rotation_zxy_and_translation(anim_pos, anim_rot);
}
