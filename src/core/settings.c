// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
#include "settings.h"

#include <string.h>

void settings_defaults(Settings* s)
{
    memset(s, 0, sizeof(*s));
    s->wpm = 30;
    s->pitch = 600;
    s->bandwidth = 500;
    s->qsk = true;
    s->activity = 2;
    s->qrn = s->qrm = s->qsb = s->flutter = s->lids = true;
    s->duration = 30;
    s->compDuration = 60;
    s->bufSize = 512;
    s->runMode = RM_STOP;
}
