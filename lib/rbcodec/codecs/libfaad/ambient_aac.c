/***************************************************************************
 * Small glue between the main firmware's "ambient" audio layer and libfaad.
 * It is compiled with the codec flags (so the libfaad headers work) and
 * exposes a plain C interface that needs none of the libfaad headers.
 * libfaad uses static buffers only, so no allocator is needed.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 ****************************************************************************/

#include "common.h"
#include "structs.h"
#include "decoder.h"

static NeAACDecHandle amb_dec;

/* Returns the number of bytes of header to skip (>= 0), or -1 if the data is
 * not a raw ADTS/ADIF AAC stream. */
int ambient_aac_open(const unsigned char *buf, unsigned int size,
                     unsigned int *rate, unsigned int *channels)
{
    uint32_t sr = 0;
    uint8_t ch = 0;
    int32_t skip;

    amb_dec = NeAACDecOpen();
    if (!amb_dec)
        return -1;

    NeAACDecConfigurationPtr conf = NeAACDecGetCurrentConfiguration(amb_dec);
    conf->outputFormat = FAAD_FMT_24BIT; /* irrelevant, no conversion done */
    NeAACDecSetConfiguration(amb_dec, conf);

    skip = NeAACDecInit(amb_dec, (uint8_t *)buf, size, &sr, &ch);
    if (skip < 0)
        return -1;
    if (!amb_dec->adts_header_present && !amb_dec->adif_header_present)
        return -1;

    *rate = sr;
    *channels = ch;
    return (int)skip;
}

/* Decodes one frame. Returns -1 on a decode error. The returned sample
 * pointers stay valid until the next call; samples have 29 fraction bits. */
int ambient_aac_decode(unsigned char *buf, unsigned int size,
                       unsigned int *consumed, unsigned int *rate,
                       unsigned int *channels, unsigned int *frames,
                       const int32_t **left, const int32_t **right)
{
    NeAACDecFrameInfo info;
    void *ret;

    *frames = 0;
    ret = NeAACDecDecode(amb_dec, &info, buf, size);
    *consumed = info.bytesconsumed;

    if (info.error > 0)
        return -1;
    if (ret == NULL)
        return 0;

    unsigned int ch = info.channels ? info.channels : 2;
    *channels = ch;
    *rate = info.samplerate;
    *frames = info.samples / ch;
    *left = (const int32_t *)amb_dec->time_out[0];
    *right = ch > 1 ? (const int32_t *)amb_dec->time_out[1] : NULL;
    return 0;
}
