// spindrift.c.inc

struct ObjectHitbox sSpindriftHitbox = {
    /* interactType: */ INTERACT_BOUNCE_TOP,
    /* downOffset: */ 0,
    /* damageOrCoinValue: */ 2,
    /* health: */ 1,
    /* numLootCoins: */ 3,
    /* radius: */ 90,
    /* height: */ 80,
    /* hurtboxRadius: */ 80,
    /* hurtboxHeight: */ 70,
};

void bhv_spindrift_loop(void) {
    o->activeFlags |= ACTIVE_FLAG_UNK10;
    if (cur_obj_set_hitbox_and_die_if_attacked(&sSpindriftHitbox, SOUND_OBJ_DYING_ENEMY1, 0))
        cur_obj_change_action(1);
    cur_obj_update_floor_and_walls();
	f32 forwardVel;
    switch (o->oAction) {
        case 0:
			forwardVel = FFIELD(o, oForwardVel);
            approach_forward_vel(&forwardVel, 4.0f, 1.0f);
			FSETFIELD(o, oForwardVel, forwardVel);
            if (cur_obj_lateral_dist_from_mario_to_home() > 1000.0f)
                o->oAngleToMario = cur_obj_angle_to_home();
            else if (QFIELD(o, oDistanceToMario) > q(300.0))
                o->oAngleToMario = obj_angle_to_object(o, gMarioObject);
            cur_obj_rotate_yaw_toward(o->oAngleToMario, 0x400);
            break;
        case 1:
            o->oFlags &= ~8;
            QSETFIELD(o, oForwardVel, q(-10));
            if (o->oTimer > 20) {
                o->oAction = 0;
                o->oInteractStatus = 0;
                o->oFlags |= 8;
            }
            break;
    }
    cur_obj_move_standard(-60);
}
