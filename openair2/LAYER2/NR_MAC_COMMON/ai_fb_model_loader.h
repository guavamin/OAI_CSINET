#ifndef AI_FB_MODEL_LOADER_H
#define AI_FB_MODEL_LOADER_H

#include <stdbool.h>
#include "openair2/LAYER2/NR_MAC_COMMON/ai_fb_common.h"
#include "openair2/LAYER2/NR_MAC_COMMON/ai_fb_mlp_weights.h"

#define AI_FB_AD_ROWS 24
#define AI_FB_AD_PORTS 4
#define AI_FB_AD_IN (2 * AI_FB_AD_ROWS * AI_FB_AD_PORTS)
#define AI_FB_AD_H 24
#define AI_FB_AD_OUT 6
#define AI_FB_RN_C 8
#define AI_FB_RN_K 3
#define AI_FB_RN_LATENT 6
#define AI_FB_RN_CH 2
#define AI_FB_RN_H AI_FB_AD_ROWS
#define AI_FB_RN_W AI_FB_AD_PORTS
#define AI_FB_RN_FC (AI_FB_RN_C * AI_FB_RN_H * AI_FB_RN_W)

typedef struct {
  float enc4_w1[AI_FB_MLP_ENC4_H][AI_FB_MLP_ENC4_IN];
  float enc4_b1[AI_FB_MLP_ENC4_H];
  float enc4_w2[AI_FB_MLP_ENC4_OUT][AI_FB_MLP_ENC4_H];
  float enc4_b2[AI_FB_MLP_ENC4_OUT];
  float dec4_w1[AI_FB_MLP_DEC4_H][AI_FB_MLP_DEC4_IN];
  float dec4_b1[AI_FB_MLP_DEC4_H];
  float dec4_w2[AI_FB_MLP_DEC4_OUT][AI_FB_MLP_DEC4_H];
  float dec4_b2[AI_FB_MLP_DEC4_OUT];

  float enc2_w1[AI_FB_MLP_ENC2_H][AI_FB_MLP_ENC2_IN];
  float enc2_b1[AI_FB_MLP_ENC2_H];
  float enc2_w2[AI_FB_MLP_ENC2_OUT][AI_FB_MLP_ENC2_H];
  float enc2_b2[AI_FB_MLP_ENC2_OUT];
  float dec2_w1[AI_FB_MLP_DEC2_H][AI_FB_MLP_DEC2_IN];
  float dec2_b1[AI_FB_MLP_DEC2_H];
  float dec2_w2[AI_FB_MLP_DEC2_OUT][AI_FB_MLP_DEC2_H];
  float dec2_b2[AI_FB_MLP_DEC2_OUT];

  float encad_w1[AI_FB_AD_H][AI_FB_AD_IN];
  float encad_b1[AI_FB_AD_H];
  float encad_w2[AI_FB_AD_OUT][AI_FB_AD_H];
  float encad_b2[AI_FB_AD_OUT];
  float decad_w1[AI_FB_AD_H][AI_FB_AD_OUT];
  float decad_b1[AI_FB_AD_H];
  float decad_w2[AI_FB_AD_IN][AI_FB_AD_H];
  float decad_b2[AI_FB_AD_IN];

  float rn_enc_conv_w[AI_FB_RN_C][AI_FB_RN_CH][AI_FB_RN_K][AI_FB_RN_K];
  float rn_enc_conv_b[AI_FB_RN_C];
  float rn_enc_bn_g[AI_FB_RN_C];
  float rn_enc_bn_b[AI_FB_RN_C];
  float rn_enc_bn_m[AI_FB_RN_C];
  float rn_enc_bn_v[AI_FB_RN_C];
  float rn_enc_fc_w[AI_FB_RN_LATENT][AI_FB_RN_FC];
  float rn_enc_fc_b[AI_FB_RN_LATENT];
  float rn_dec_fc_w[AI_FB_RN_FC][AI_FB_RN_LATENT];
  float rn_dec_fc_b[AI_FB_RN_FC];
  float rn_ref1_conv1_w[AI_FB_RN_C][AI_FB_RN_C][AI_FB_RN_K][AI_FB_RN_K];
  float rn_ref1_conv1_b[AI_FB_RN_C];
  float rn_ref1_bn1_g[AI_FB_RN_C];
  float rn_ref1_bn1_b[AI_FB_RN_C];
  float rn_ref1_bn1_m[AI_FB_RN_C];
  float rn_ref1_bn1_v[AI_FB_RN_C];
  float rn_ref1_conv2_w[AI_FB_RN_C][AI_FB_RN_C][AI_FB_RN_K][AI_FB_RN_K];
  float rn_ref1_conv2_b[AI_FB_RN_C];
  float rn_ref1_bn2_g[AI_FB_RN_C];
  float rn_ref1_bn2_b[AI_FB_RN_C];
  float rn_ref1_bn2_m[AI_FB_RN_C];
  float rn_ref1_bn2_v[AI_FB_RN_C];
  float rn_ref2_conv1_w[AI_FB_RN_C][AI_FB_RN_C][AI_FB_RN_K][AI_FB_RN_K];
  float rn_ref2_conv1_b[AI_FB_RN_C];
  float rn_ref2_bn1_g[AI_FB_RN_C];
  float rn_ref2_bn1_b[AI_FB_RN_C];
  float rn_ref2_bn1_m[AI_FB_RN_C];
  float rn_ref2_bn1_v[AI_FB_RN_C];
  float rn_ref2_conv2_w[AI_FB_RN_C][AI_FB_RN_C][AI_FB_RN_K][AI_FB_RN_K];
  float rn_ref2_conv2_b[AI_FB_RN_C];
  float rn_ref2_bn2_g[AI_FB_RN_C];
  float rn_ref2_bn2_b[AI_FB_RN_C];
  float rn_ref2_bn2_m[AI_FB_RN_C];
  float rn_ref2_bn2_v[AI_FB_RN_C];
  float rn_dec_out_w[AI_FB_RN_CH][AI_FB_RN_C][AI_FB_RN_K][AI_FB_RN_K];
  float rn_dec_out_b[AI_FB_RN_CH];
} ai_fb_model_t;

const ai_fb_model_t *ai_fb_get_model(ai_fb_impl_mode_t mode);
bool ai_fb_model_available(ai_fb_impl_mode_t mode);
int ai_fb_model_backend(void);

#endif /* AI_FB_MODEL_LOADER_H */
