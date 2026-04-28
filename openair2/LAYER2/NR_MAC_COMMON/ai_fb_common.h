#ifndef AI_FB_COMMON_H
#define AI_FB_COMMON_H

typedef enum {
  AI_FB_IMPL_MATRIX = 0,
  AI_FB_IMPL_MLP_STUB = 1,
  AI_FB_IMPL_MODEL_STUB = 2,
  AI_FB_IMPL_CSINET = 3,
  AI_FB_IMPL_ANGULAR_DELAY_MLP = 4,
  AI_FB_IMPL_ANGULAR_DELAY_REFINENET = 5,
} ai_fb_impl_mode_t;

/* int8 latent quantization step for impl 4/5; UE encoder and gNB decoder must match. */
#define AI_FB_ANGULAR_LATENT_QSTEP 0.01f

#endif /* AI_FB_COMMON_H */
