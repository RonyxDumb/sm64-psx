#include <assert.h>
#include <string.h>
#include <ultra64.h>

#include "game/save_file.h"

struct SaveBuffer gSaveBuffer;
s8 gEepromProbe = 1;
OSMesgQueue gSIEventMesgQueue;
static u8 eeprom[EEPROM_SIZE];

s32 osEepromLongRead(UNUSED OSMesgQueue *queue, u8 address, u8 *buffer, int size) {
    memcpy(buffer, eeprom + address * 8, size);
    return 0;
}

s32 osEepromLongWrite(UNUSED OSMesgQueue *queue, u8 address, u8 *buffer, int size) {
    memcpy(eeprom + address * 8, buffer, size);
    return 0;
}

int main(void) {
    save_file_load_all();
    for (s32 slot = SAVE_FILE_A; slot < SAVE_FILE_D; slot++) {
        assert(!save_file_exists(slot));
    }
    assert(save_file_exists(SAVE_FILE_D));
    assert(save_file_get_total_star_count(SAVE_FILE_D, COURSE_MIN - 1, COURSE_MAX - 1) == 120);
    for (s32 course = COURSE_MIN; course <= COURSE_STAGES_MAX; course++) {
        assert(save_file_get_star_flags(SAVE_FILE_D, course - 1) == 0x7F);
        assert(save_file_get_course_coin_score(SAVE_FILE_D, course - 1) == 100);
    }

    struct SaveFile complete = gSaveBuffer.files[SAVE_FILE_D][0];
    u32 unlocks = SAVE_FLAG_UNLOCKED_UPSTAIRS_DOOR | SAVE_FLAG_UNLOCKED_BASEMENT_DOOR
                  | SAVE_FLAG_MOAT_DRAINED | SAVE_FLAG_HAVE_VANISH_CAP
                  | SAVE_FLAG_HAVE_METAL_CAP | SAVE_FLAG_HAVE_WING_CAP;
    assert((complete.flags & unlocks) == unlocks);
    size_t primary_offset = (u8 *) &gSaveBuffer.files[SAVE_FILE_D][0] - (u8 *) &gSaveBuffer;
    memset(eeprom + primary_offset, 0, sizeof(complete));
    save_file_load_all();
    assert(memcmp(&complete, &gSaveBuffer.files[SAVE_FILE_D][0], sizeof(complete)) == 0);

    save_file_erase(SAVE_FILE_D);
    save_file_set_star_flags(SAVE_FILE_D, COURSE_BOB - 1, 1);
    save_file_do_save(SAVE_FILE_D);
    save_file_load_all();
    assert(save_file_get_total_star_count(SAVE_FILE_D, COURSE_MIN - 1, COURSE_MAX - 1) == 1);
    return 0;
}
