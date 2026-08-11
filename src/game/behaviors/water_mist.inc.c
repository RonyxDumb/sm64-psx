// water_mist.c.inc
// TODO: there is confusion with the earlier mist file. Clarify?

void bhv_water_mist_2_loop(void) {
    FSETFIELD(o, oPosY, qtof(find_water_levelq(QFIELD(o, oHomeX), QFIELD(o, oHomeZ)) + q(20)));
#define PRECISION 64 // cannot overflow s16
    FSETFIELD(o, oPosX, qtof(QFIELD(o, oHomeX) + q(random_s16_around_zero(150 * PRECISION)) / PRECISION));
    FSETFIELD(o, oPosZ, qtof(QFIELD(o, oHomeZ) + q(random_s16_around_zero(150 * PRECISION)) / PRECISION));
    o->oOpacity = qtof(random_u16() % (50 * PRECISION) + 200 * PRECISION) / PRECISION;
#undef PRECISION
}
