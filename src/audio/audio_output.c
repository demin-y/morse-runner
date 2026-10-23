// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
#include "audio_output.h"

#include "miniaudio.h"

#include <stdlib.h>

struct AudioOutput {
    ma_context context;
    ma_device device;
    AudioCallback callback;
    void* user;
};

static void data_callback(ma_device* dev, void* output, const void* input, ma_uint32 frames)
{
    (void)input;
    AudioOutput* a = dev->pUserData;
    a->callback(output, (int)frames, a->user);
}

static bool init(AudioOutput* a, const ma_backend* backends, ma_uint32 count, int rate, int period)
{
    if (ma_context_init(backends, count, NULL, &a->context) != MA_SUCCESS) return false;

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = 1;
    cfg.sampleRate = (ma_uint32)rate;
    cfg.periodSizeInFrames = (ma_uint32)period;
    cfg.periods = 3;
    cfg.performanceProfile = ma_performance_profile_low_latency;
    cfg.dataCallback = data_callback;
    cfg.pUserData = a;
    if (ma_device_init(&a->context, &cfg, &a->device) != MA_SUCCESS) {
        ma_context_uninit(&a->context);
        return false;
    }
    return true;
}

AudioOutput* audio_open(int sampleRate, int periodFrames, AudioCallback cb, void* user, bool* silent)
{
    AudioOutput* a = calloc(1, sizeof(AudioOutput));
    if (!a) return NULL;
    a->callback = cb;
    a->user = user;

    if (!init(a, NULL, 0, sampleRate, periodFrames)) {
        ma_backend nullBackend = ma_backend_null;
        if (!init(a, &nullBackend, 1, sampleRate, periodFrames)) {
            free(a);
            return NULL;
        }
    }
    *silent = a->context.backend == ma_backend_null;
    return a;
}

void audio_close(AudioOutput* a)
{
    if (!a) return;
    ma_device_uninit(&a->device);
    ma_context_uninit(&a->context);
    free(a);
}

bool audio_start(AudioOutput* a)
{
    if (!a) return false;
    if (ma_device_is_started(&a->device)) return true;
    return ma_device_start(&a->device) == MA_SUCCESS;
}

void audio_stop(AudioOutput* a)
{
    if (a && ma_device_is_started(&a->device)) ma_device_stop(&a->device);
}
