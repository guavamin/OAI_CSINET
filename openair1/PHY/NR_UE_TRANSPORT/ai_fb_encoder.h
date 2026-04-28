#ifndef AI_FB_ENCODER_H
#define AI_FB_ENCODER_H

#include <complex.h>
#include <stdbool.h>
#include <stdint.h>
#include "openair2/LAYER2/NR_MAC_COMMON/ai_fb_common.h"
#include "openair2/LAYER2/NR_MAC_COMMON/ai_fb_model_loader.h"

bool ai_fb_encode_rank1_4p(const double complex v_in[4],
                           ai_fb_impl_mode_t mode,
                           uint8_t out_payload[6]);

bool ai_fb_encode_rank1_2p(const double complex v_in[2],
                           ai_fb_impl_mode_t mode,
                           uint8_t out_payload[6]);

bool ai_fb_encode_angular_delay_features(const float in_feat[AI_FB_AD_IN],
                                         uint8_t out_payload[6]);
bool ai_fb_encode_angular_refinenet_features(const float in_feat[AI_FB_AD_IN],
                                             uint8_t out_payload[6]);

#endif /* AI_FB_ENCODER_H */
