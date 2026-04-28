#ifndef AI_FB_DECODER_H
#define AI_FB_DECODER_H

#include <complex.h>
#include <stdbool.h>
#include <stdint.h>
#include "openair2/LAYER2/NR_MAC_COMMON/ai_fb_common.h"

typedef struct {
  double complex vhat[4];
  int best_ll;
  int best_mm;
  int best_class;
  double best_metric;
  uint8_t pmi_x1;
  uint8_t pmi_x2;
  uint8_t ri;
  uint8_t cqi;
  bool low_energy_fallback;
} ai_fb_decode4p_out_t;

typedef struct {
  double complex vhat2[2];
  double best_metric;
  uint8_t pmi_x1;
  uint8_t pmi_x2;
  uint8_t ri;
  uint8_t cqi;
  bool low_energy_fallback;
} ai_fb_decode2p_out_t;

bool ai_fb_decode_rank1_4p(const uint8_t latent[6], ai_fb_impl_mode_t mode, ai_fb_decode4p_out_t *out);
bool ai_fb_decode_rank1_2p(const uint8_t latent[6], ai_fb_impl_mode_t mode, ai_fb_decode2p_out_t *out);

#endif /* AI_FB_DECODER_H */
