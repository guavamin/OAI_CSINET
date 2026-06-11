#ifndef AI_FB_COMMON_H
#define AI_FB_COMMON_H

#include <stdbool.h>

typedef enum {
  AI_FB_IMPL_MATRIX = 0,
  AI_FB_IMPL_MLP_STUB = 1,
  AI_FB_IMPL_MODEL_STUB = 2,
  AI_FB_IMPL_CSINET = 3,
  AI_FB_IMPL_ANGULAR_DELAY_MLP = 4,
  AI_FB_IMPL_ANGULAR_DELAY_REFINENET = 5,
  AI_FB_IMPL_ANGULAR_DELAY_REFINENET_LEGACY_RI_CQI = 6,
} ai_fb_impl_mode_t;

static inline bool ai_fb_impl_is_angular_delay(ai_fb_impl_mode_t mode)
{
  return mode == AI_FB_IMPL_ANGULAR_DELAY_MLP
         || mode == AI_FB_IMPL_ANGULAR_DELAY_REFINENET
         || mode == AI_FB_IMPL_ANGULAR_DELAY_REFINENET_LEGACY_RI_CQI;
}

static inline bool ai_fb_impl_is_refinenet(ai_fb_impl_mode_t mode)
{
  return mode == AI_FB_IMPL_ANGULAR_DELAY_REFINENET
         || mode == AI_FB_IMPL_ANGULAR_DELAY_REFINENET_LEGACY_RI_CQI;
}

static inline bool ai_fb_impl_uses_legacy_ri_cqi(ai_fb_impl_mode_t mode)
{
  return mode == AI_FB_IMPL_ANGULAR_DELAY_REFINENET_LEGACY_RI_CQI;
}

/* int8 latent quantization step for impl 4/5/6; UE encoder and gNB decoder must match. */
#define AI_FB_ANGULAR_LATENT_QSTEP 0.01f

#endif /* AI_FB_COMMON_H */
