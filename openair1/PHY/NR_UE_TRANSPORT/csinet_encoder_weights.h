#ifndef CSINET_ENCODER_WEIGHTS_H
#define CSINET_ENCODER_WEIGHTS_H

/*
 * Bootstrap CSINet-like encoder projection weights.
 * These are fixed placeholders for mode-3 integration bring-up.
 */
static const float csinet_enc4_w[6][8] = {
    {0.50f, 0.00f, 0.00f, 0.00f, 0.50f, 0.00f, 0.00f, 0.00f},
    {0.00f, 0.50f, 0.00f, 0.00f, 0.00f, 0.50f, 0.00f, 0.00f},
    {0.00f, 0.00f, 0.50f, 0.00f, 0.00f, 0.00f, 0.50f, 0.00f},
    {0.00f, 0.00f, 0.00f, 0.50f, 0.00f, 0.00f, 0.00f, 0.50f},
    {0.35f, 0.35f, -0.35f, -0.35f, 0.00f, 0.00f, 0.00f, 0.00f},
    {0.00f, 0.00f, 0.00f, 0.00f, 0.35f, 0.35f, -0.35f, -0.35f},
};

static const float csinet_enc2_w[6][4] = {
    {0.50f, 0.00f, 0.00f, 0.00f},
    {0.00f, 0.50f, 0.00f, 0.00f},
    {0.00f, 0.00f, 0.50f, 0.00f},
    {0.00f, 0.00f, 0.00f, 0.50f},
    {0.35f, 0.35f, 0.00f, 0.00f},
    {0.00f, 0.00f, 0.35f, 0.35f},
};

#endif /* CSINET_ENCODER_WEIGHTS_H */
