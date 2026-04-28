#ifndef AI_FB_ONNX_ADAPTER_H
#define AI_FB_ONNX_ADAPTER_H

#include <stdbool.h>

bool ai_fb_onnx_sessions_ready(void);
bool ai_fb_onnx_init_from_paths(const char *enc_path, const char *dec_path);
bool ai_fb_onnx_infer_encoder_4p(const float in_x[8], float out_z[6]);
bool ai_fb_onnx_infer_encoder_2p(const float in_x[4], float out_z[6]);
bool ai_fb_onnx_infer_decoder_4p(const float in_z[6], float out_x[8]);
bool ai_fb_onnx_infer_decoder_2p(const float in_z[6], float out_x[4]);

#endif /* AI_FB_ONNX_ADAPTER_H */
