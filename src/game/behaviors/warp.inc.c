// warp.c.inc

void bhv_warp_loop(void) {
    u16 sp6;
    if (o->oTimer == 0) {
        sp6 = (o->oBehParams >> 24) & 0xFF;
        if (sp6 == 0)
            o->hitboxRadius_s16 = 50;
        else if (sp6 == 0xFF)
            o->hitboxRadius_s16 = 10000;
        else
            o->hitboxRadius_s16 = sp6 * 10;
        o->hitboxHeight_s16 = 50;
    }
    o->oInteractStatus = 0;
}

void bhv_fading_warp_loop() // identical to the above function except for o->hitboxRadius
{
    u16 sp6;
    if (o->oTimer == 0) {
        sp6 = (o->oBehParams >> 24) & 0xFF;
        if (sp6 == 0)
            o->hitboxRadius_s16 = 85;
        else if (sp6 == 0xFF)
            o->hitboxRadius_s16 = 10000;
        else
            o->hitboxRadius_s16 = sp6 * 10;
        o->hitboxHeight_s16 = 50;
    }
    o->oInteractStatus = 0;
}
