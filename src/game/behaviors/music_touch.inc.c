// music_touch.c.inc

void bhv_play_music_track_when_touched_loop(void) {
    if (o->oAction == 0) {
        if (QFIELD(o, oDistanceToMario) < q(200)) {
            play_puzzle_jingle();
            o->oAction++;
        }
    }
}
