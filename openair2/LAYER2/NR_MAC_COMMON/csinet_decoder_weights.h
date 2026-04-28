#ifndef CSINET_DECODER_WEIGHTS_H
#define CSINET_DECODER_WEIGHTS_H

/*
 * Bootstrap CSINet-like decoder projection weights.
 * These are fixed placeholders for mode-3 integration bring-up.
 */
static const float csinet_dec4_w[8][6] = {
    {1.0f, 0.0f, 0.0f, 0.0f, 0.7f, 0.0f},
    {0.0f, 1.0f, 0.0f, 0.0f, 0.7f, 0.0f},
    {0.0f, 0.0f, 1.0f, 0.0f, -0.7f, 0.0f},
    {0.0f, 0.0f, 0.0f, 1.0f, -0.7f, 0.0f},
    {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.7f},
    {0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.7f},
    {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, -0.7f},
    {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -0.7f},
};

static const float csinet_dec2_w[4][6] = {
    {2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 2.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 2.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f},
};

#endif /* CSINET_DECODER_WEIGHTS_H */
