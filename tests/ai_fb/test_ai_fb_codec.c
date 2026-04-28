#include "openair1/PHY/NR_UE_TRANSPORT/ai_fb_encoder.h"
#include "openair2/LAYER2/NR_MAC_gNB/ai_fb_decoder.h"
#include "openair2/LAYER2/NR_MAC_gNB/csinet_decoder.h"
#include "openair2/LAYER2/NR_MAC_COMMON/ai_fb_model_loader.h"
#include "openair2/LAYER2/NR_MAC_COMMON/ai_fb_onnx_adapter.h"

#include <assert.h>
#include <complex.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Unit-test local stubs so encoder/decoder can be built standalone.
static ai_fb_model_t g_model;
static bool g_model_init = false;

static void init_model(void)
{
  if (g_model_init)
    return;
  memcpy(g_model.enc4_w1, ai_fb_mlp_enc4_w1, sizeof(g_model.enc4_w1));
  memcpy(g_model.enc4_b1, ai_fb_mlp_enc4_b1, sizeof(g_model.enc4_b1));
  memcpy(g_model.enc4_w2, ai_fb_mlp_enc4_w2, sizeof(g_model.enc4_w2));
  memcpy(g_model.enc4_b2, ai_fb_mlp_enc4_b2, sizeof(g_model.enc4_b2));
  memcpy(g_model.dec4_w1, ai_fb_mlp_dec4_w1, sizeof(g_model.dec4_w1));
  memcpy(g_model.dec4_b1, ai_fb_mlp_dec4_b1, sizeof(g_model.dec4_b1));
  memcpy(g_model.dec4_w2, ai_fb_mlp_dec4_w2, sizeof(g_model.dec4_w2));
  memcpy(g_model.dec4_b2, ai_fb_mlp_dec4_b2, sizeof(g_model.dec4_b2));
  memcpy(g_model.enc2_w1, ai_fb_mlp_enc2_w1, sizeof(g_model.enc2_w1));
  memcpy(g_model.enc2_b1, ai_fb_mlp_enc2_b1, sizeof(g_model.enc2_b1));
  memcpy(g_model.enc2_w2, ai_fb_mlp_enc2_w2, sizeof(g_model.enc2_w2));
  memcpy(g_model.enc2_b2, ai_fb_mlp_enc2_b2, sizeof(g_model.enc2_b2));
  memcpy(g_model.dec2_w1, ai_fb_mlp_dec2_w1, sizeof(g_model.dec2_w1));
  memcpy(g_model.dec2_b1, ai_fb_mlp_dec2_b1, sizeof(g_model.dec2_b1));
  memcpy(g_model.dec2_w2, ai_fb_mlp_dec2_w2, sizeof(g_model.dec2_w2));
  memcpy(g_model.dec2_b2, ai_fb_mlp_dec2_b2, sizeof(g_model.dec2_b2));
  g_model_init = true;
}

const ai_fb_model_t *ai_fb_get_model(ai_fb_impl_mode_t mode)
{
  (void)mode;
  init_model();
  return &g_model;
}
bool ai_fb_model_available(ai_fb_impl_mode_t mode) { (void)mode; return false; }
int ai_fb_model_backend(void) { return 0; }
bool ai_fb_onnx_sessions_ready(void) { return false; }
bool ai_fb_onnx_init_from_paths(const char *enc_path, const char *dec_path) { (void)enc_path; (void)dec_path; return false; }
bool ai_fb_onnx_infer_encoder_4p(const float in_x[8], float out_z[6]) { (void)in_x; (void)out_z; return false; }
bool ai_fb_onnx_infer_encoder_2p(const float in_x[4], float out_z[6]) { (void)in_x; (void)out_z; return false; }
bool ai_fb_onnx_infer_decoder_4p(const float in_z[6], float out_x[8]) { (void)in_z; (void)out_x; return false; }
bool ai_fb_onnx_infer_decoder_2p(const float in_z[6], float out_x[4]) { (void)in_z; (void)out_x; return false; }

static void test_4p_determinism_and_phase_invariance(void)
{
  const double complex v[4] = {0.4266 + 0.0000 * I, 0.5509 - 0.0049 * I, 0.5606 - 0.0040 * I, 0.4474 - 0.0005 * I};
  uint8_t l1[6] = {0}, l2[6] = {0};
  assert(ai_fb_encode_rank1_4p(v, AI_FB_IMPL_MATRIX, l1));
  assert(ai_fb_encode_rank1_4p(v, AI_FB_IMPL_MATRIX, l2));
  assert(memcmp(l1, l2, sizeof(l1)) == 0);

  const double complex phase = cexp(I * 0.73);
  const double complex vp[4] = {v[0] * phase, v[1] * phase, v[2] * phase, v[3] * phase};
  uint8_t l3[6] = {0};
  assert(ai_fb_encode_rank1_4p(vp, AI_FB_IMPL_MATRIX, l3));
  assert(memcmp(l1, l3, sizeof(l1)) == 0);

  ai_fb_decode4p_out_t out;
  assert(ai_fb_decode_rank1_4p(l1, AI_FB_IMPL_MATRIX, &out));
  assert(out.best_metric > 0.0 && out.best_metric <= 1.0001);
  assert((out.pmi_x1 >> 3) >= 4); // canonical class representative is ll in [4..7]
}

static void test_2p_determinism_and_phase_invariance(void)
{
  const double complex v2[2] = {0.7071 + 0.0 * I, 0.0 + 0.7071 * I};
  uint8_t l1[6] = {0}, l2[6] = {0};
  assert(ai_fb_encode_rank1_2p(v2, AI_FB_IMPL_MATRIX, l1));
  assert(ai_fb_encode_rank1_2p(v2, AI_FB_IMPL_MATRIX, l2));
  assert(memcmp(l1, l2, sizeof(l1)) == 0);

  const double complex phase = cexp(I * 1.11);
  const double complex v2p[2] = {v2[0] * phase, v2[1] * phase};
  uint8_t l3[6] = {0};
  assert(ai_fb_encode_rank1_2p(v2p, AI_FB_IMPL_MATRIX, l3));
  assert(memcmp(l1, l3, sizeof(l1)) == 0);

  ai_fb_decode2p_out_t out;
  assert(ai_fb_decode_rank1_2p(l1, AI_FB_IMPL_MATRIX, &out));
  assert(out.best_metric > 0.0 && out.best_metric <= 1.0001);
  assert(out.pmi_x1 == 0);
  assert(out.pmi_x2 < 4);
}

static void test_backend_mode_parity_without_external_model(void)
{
  const double complex v4[4] = {0.39 + 0.01 * I, 0.54 - 0.02 * I, 0.57 + 0.03 * I, 0.46 - 0.01 * I};
  const double complex v2[2] = {0.71 + 0.02 * I, 0.70 - 0.01 * I};
  uint8_t m1_4[6] = {0}, m2_4[6] = {0};
  uint8_t m1_2[6] = {0}, m2_2[6] = {0};
  assert(ai_fb_encode_rank1_4p(v4, AI_FB_IMPL_MLP_STUB, m1_4));
  assert(ai_fb_encode_rank1_4p(v4, AI_FB_IMPL_MODEL_STUB, m2_4));
  assert(memcmp(m1_4, m2_4, sizeof(m1_4)) == 0);
  assert(ai_fb_encode_rank1_2p(v2, AI_FB_IMPL_MLP_STUB, m1_2));
  assert(ai_fb_encode_rank1_2p(v2, AI_FB_IMPL_MODEL_STUB, m2_2));
  assert(memcmp(m1_2, m2_2, sizeof(m1_2)) == 0);

  ai_fb_decode4p_out_t d1_4, d2_4;
  ai_fb_decode2p_out_t d1_2, d2_2;
  const bool ok1_4 = ai_fb_decode_rank1_4p(m1_4, AI_FB_IMPL_MLP_STUB, &d1_4);
  const bool ok2_4 = ai_fb_decode_rank1_4p(m2_4, AI_FB_IMPL_MODEL_STUB, &d2_4);
  assert(ok1_4 == ok2_4);
  if (ok1_4)
    assert(d1_4.pmi_x1 == d2_4.pmi_x1 && d1_4.pmi_x2 == d2_4.pmi_x2);
  const bool ok1_2 = ai_fb_decode_rank1_2p(m1_2, AI_FB_IMPL_MLP_STUB, &d1_2);
  const bool ok2_2 = ai_fb_decode_rank1_2p(m2_2, AI_FB_IMPL_MODEL_STUB, &d2_2);
  assert(ok1_2 == ok2_2);
  if (ok1_2)
    assert(d1_2.pmi_x1 == d2_2.pmi_x1 && d1_2.pmi_x2 == d2_2.pmi_x2);
}

static void test_csinet_decode_determinism(void)
{
  const uint8_t latent[6] = {5, 9, 11, 13, 3, 7};
  csinet_decode4p_out_t a4, b4;
  csinet_decode2p_out_t a2, b2;
  assert(csinet_decode_rank1_4p(latent, &a4));
  assert(csinet_decode_rank1_4p(latent, &b4));
  assert(a4.report.pmi_x1 == b4.report.pmi_x1 && a4.report.pmi_x2 == b4.report.pmi_x2);
  assert(csinet_decode_rank1_2p(latent, &a2));
  assert(csinet_decode_rank1_2p(latent, &b2));
  assert(a2.report.pmi_x1 == b2.report.pmi_x1 && a2.report.pmi_x2 == b2.report.pmi_x2);
}

int main(void)
{
  test_4p_determinism_and_phase_invariance();
  test_2p_determinism_and_phase_invariance();
  test_backend_mode_parity_without_external_model();
  test_csinet_decode_determinism();
  printf("test_ai_fb_codec: PASS\n");
  return 0;
}
