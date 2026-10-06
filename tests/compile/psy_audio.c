/* Compile check: psy_audio.h as a C translation unit with the implementation
 * enabled. CMake and CI build this as C11 with warnings as errors when
 * miniaudio.h is found; psy_audio.cpp builds the same source as C++17. It
 * opens miniaudio's null device, which needs no sound hardware, plays a
 * tone and reads its record, and touches each miniaudio field the strict
 * open reads, so a miniaudio release that moves one fails here. */
#define PSY_AUDIO_IMPLEMENTATION
#include "psy_audio.h"

#if !defined(PSYAU_NO_MINIAUDIO)
static int touch_fields(ma_device* d) {
    int n = (int)d->playback.internalSampleRate + (int)d->playback.internalChannels
          + (int)d->playback.internalFormat + (int)d->playback.converter.isPassthrough
          + (int)d->playback.internalChannelMap[0] + (int)d->playback.internalPeriodSizeInFrames
          + (int)d->playback.internalPeriods;
#if defined(MA_SUPPORT_WASAPI)
    n += d->wasapi.pAudioClientPlayback != NULL;
    n += (int)d->wasapi.actualBufferSizeInFramesPlayback;
#endif
    return n;
}
#endif

int main(void) {
    psyau_audio* au = (psyau_audio*)calloc(1, sizeof(psyau_audio));
    psyau_desc d;
    int n = 0, rc;
    if (!au) return 1;
    memset(&d, 0, sizeof d);
#if defined(PSYAU_NO_MINIAUDIO)
    rc = psyau_open(au, &d) ? 1 : 0;   /* must refuse: no backend */
    free(au);
    return rc == 0 && psyau_params(&n) && n > 0 ? 0 : 2;
#else
    psyau_tone_desc td;
    psyau_buf b;
    psyau_onset r;
    psyau_id id;
    {
        ma_device dummy;
        memset(&dummy, 0, sizeof dummy);
        (void)touch_fields(&dummy);
    }
    d.backend = PSYAU_BACKEND_NULL;
    d.arena_bytes = 1 << 20;
    if (!psyau_open(au, &d)) { fprintf(stderr, "%s\n", psyau_error(au)); free(au); return 3; }
    memset(&td, 0, sizeof td);
    td.hz = 1000; td.dur = PSYAU_MS(20); td.peak = 0.01f;
    b = psyau_tone(au, &td);
    if (!b.frames) { psyau_close(au); free(au); return 4; }
    id = psyau_play_at(au, b, 0);
    if (id <= 0) { psyau_close(au); free(au); return 5; }
    rc = psyau_wait(au, id, 2000000000LL, &r);
    psyau_close(au);
    free(au);
    if (rc != PSYAU_OK || r.tier != PSYAU_TIER_SIM) return 6;
    return psyau_params(&n) && n > 0 ? 0 : 7;
#endif
}
