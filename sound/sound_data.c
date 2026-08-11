unsigned char gSoundDataADSR[] = {
#ifndef NO_AUDIO
#include "sound/sound_data.ctl.inc.c"
#endif
};

unsigned char gSoundDataRaw[] = {
#ifndef NO_AUDIO
#include "sound/sound_data.tbl.inc.c"
#endif
};

unsigned char gMusicData[] = {
#ifndef NO_AUDIO
#include "sound/sequences.bin.inc.c"
#endif
};

#ifndef VERSION_SH
unsigned char gBankSetsData[] = {
#ifndef NO_AUDIO
#include "sound/bank_sets.inc.c"
#endif
};
#endif
