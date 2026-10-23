// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
#ifndef MR_AUDIO_OUTPUT_H
#define MR_AUDIO_OUTPUT_H

#include <stdbool.h>

// fills `frames` mono samples; runs on the audio thread
typedef void (*AudioCallback)(float* out, int frames, void* user);

typedef struct AudioOutput AudioOutput;

// Mono float playback through the default output device. When no sound
// device is available, a silent "null" device driven by a timer keeps the
// simulation running (*silent is set). Returns NULL on failure.
AudioOutput* audio_open(int sampleRate, int periodFrames, AudioCallback cb, void* user, bool* silent);
void audio_close(AudioOutput* a);
bool audio_start(AudioOutput* a);
void audio_stop(AudioOutput* a);

#endif
