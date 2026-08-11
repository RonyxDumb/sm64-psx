// lll_sinking_rock_block.c.inc

void bhv_lll_sinking_rock_block_loop(void) {
	f32 sinkWhenSteppedOnUnk108 = FFIELD(o, oSinkWhenSteppedOnUnk108);
    lll_octagonal_mesh_find_y_offset(&o->oSinkWhenSteppedOnUnk104, &sinkWhenSteppedOnUnk108, 124, -110);
	FSETFIELD(o, oSinkWhenSteppedOnUnk108, sinkWhenSteppedOnUnk108);
    QSETFIELD(o, oGraphYOffset, 0);
    QSETFIELD(o, oPosY, QFIELD(o, oHomeY) + QFIELD(o, oSinkWhenSteppedOnUnk108));
}
