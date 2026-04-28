#include "ai_fb_encoder.h"

#include <math.h>
#include "common/utils/LOG/log.h"
#include "executables/softmodem-common.h"
#include "openair2/LAYER2/NR_MAC_COMMON/ai_fb_model_loader.h"
#include "openair2/LAYER2/NR_MAC_COMMON/ai_fb_onnx_adapter.h"

static void ai_fb_phase_fix_4p(double complex v[4])
{
  const double phi = carg(v[0]);
  const double complex rot = cexp(-I * phi);
  for (int i = 0; i < 4; i++)
    v[i] *= rot;
  if (creal(v[0]) < 0) {
    for (int i = 0; i < 4; i++)
      v[i] = -v[i];
  }
}

static void ai_fb_phase_fix_2p(double complex v[2])
{
  const double phi = carg(v[0]);
  const double complex rot = cexp(-I * phi);
  for (int i = 0; i < 2; i++)
    v[i] *= rot;
  if (creal(v[0]) < 0) {
    v[0] = -v[0];
    v[1] = -v[1];
  }
}

static inline uint8_t ai_fb_quantize_to_int8(float z, float qstep)
{
  int q = (int)lrintf(z / qstep);
  if (q > 127)
    q = 127;
  if (q < -127)
    q = -127;
  return (uint8_t)((int8_t)q);
}

static void ai_fb_mlp_linear(const float *in, int in_dim, const float *w, const float *b, int out_dim, float *out)
{
  for (int o = 0; o < out_dim; o++) {
    float acc = b[o];
    for (int i = 0; i < in_dim; i++)
      acc += w[o * in_dim + i] * in[i];
    out[o] = acc;
  }
}

static inline float ai_fb_leaky_relu(float x)
{
  return x > 0.0f ? x : (0.1f * x);
}

static void ai_fb_conv2d_same(const float *in,
                              int in_c,
                              int h,
                              int w,
                              const float *k,
                              const float *b,
                              int out_c,
                              int ksz,
                              float *out)
{
  const int pad = ksz / 2;
  for (int oc = 0; oc < out_c; oc++) {
    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++) {
        float acc = b ? b[oc] : 0.0f;
        for (int ic = 0; ic < in_c; ic++) {
          for (int ky = 0; ky < ksz; ky++) {
            const int iy = y + ky - pad;
            if (iy < 0 || iy >= h)
              continue;
            for (int kx = 0; kx < ksz; kx++) {
              const int ix = x + kx - pad;
              if (ix < 0 || ix >= w)
                continue;
              const int in_idx = (ic * h + iy) * w + ix;
              const int w_idx = (((oc * in_c + ic) * ksz + ky) * ksz + kx);
              acc += k[w_idx] * in[in_idx];
            }
          }
        }
        out[(oc * h + y) * w + x] = acc;
      }
    }
  }
}

static void ai_fb_bn_apply(float *x, int c, int h, int w, const float *g, const float *b, const float *m, const float *v)
{
  const float eps = 1e-5f;
  for (int ch = 0; ch < c; ch++) {
    const float inv_std = 1.0f / sqrtf(v[ch] + eps);
    for (int y = 0; y < h; y++) {
      for (int xw = 0; xw < w; xw++) {
        const int idx = (ch * h + y) * w + xw;
        const float n = (x[idx] - m[ch]) * inv_std;
        x[idx] = g[ch] * n + b[ch];
      }
    }
  }
}

static void ai_fb_encode_rank1_4p_mlp(const ai_fb_model_t *m, const float x[8], uint8_t out_payload[6])
{
  float h[AI_FB_MLP_ENC4_H];
  float z[AI_FB_MLP_ENC4_OUT];
  ai_fb_mlp_linear(x,
                   AI_FB_MLP_ENC4_IN,
                   &m->enc4_w1[0][0],
                   m->enc4_b1,
                   AI_FB_MLP_ENC4_H,
                   h);
  ai_fb_mlp_linear(h,
                   AI_FB_MLP_ENC4_H,
                   &m->enc4_w2[0][0],
                   m->enc4_b2,
                   AI_FB_MLP_ENC4_OUT,
                   z);
  const float qstep = 0.01f;
  for (int r = 0; r < 6; r++)
    out_payload[r] = ai_fb_quantize_to_int8(z[r], qstep);
}

static void ai_fb_encode_rank1_2p_mlp(const ai_fb_model_t *m, const float x[4], uint8_t out_payload[6])
{
  float h[AI_FB_MLP_ENC2_H];
  float z[AI_FB_MLP_ENC2_OUT];
  ai_fb_mlp_linear(x,
                   AI_FB_MLP_ENC2_IN,
                   &m->enc2_w1[0][0],
                   m->enc2_b1,
                   AI_FB_MLP_ENC2_H,
                   h);
  ai_fb_mlp_linear(h,
                   AI_FB_MLP_ENC2_H,
                   &m->enc2_w2[0][0],
                   m->enc2_b2,
                   AI_FB_MLP_ENC2_OUT,
                   z);
  const float qstep = 0.01f;
  for (int r = 0; r < 6; r++)
    out_payload[r] = ai_fb_quantize_to_int8(z[r], qstep);
}

bool ai_fb_encode_rank1_4p(const double complex v_in[4], ai_fb_impl_mode_t mode, uint8_t out_payload[6])
{
  double complex v[4] = {v_in[0], v_in[1], v_in[2], v_in[3]};
  ai_fb_phase_fix_4p(v);

  const float x[8] = {(float)creal(v[0]),
                      (float)creal(v[1]),
                      (float)creal(v[2]),
                      (float)creal(v[3]),
                      (float)cimag(v[0]),
                      (float)cimag(v[1]),
                      (float)cimag(v[2]),
                      (float)cimag(v[3])};

  // Matrix baseline (also used by stub modes for deterministic behavior).
  static const float E[6][8] = {
      {0.50f, 0.00f, 0.00f, 0.00f, 0.50f, 0.00f, 0.00f, 0.00f},
      {0.00f, 0.50f, 0.00f, 0.00f, 0.00f, 0.50f, 0.00f, 0.00f},
      {0.00f, 0.00f, 0.50f, 0.00f, 0.00f, 0.00f, 0.50f, 0.00f},
      {0.00f, 0.00f, 0.00f, 0.50f, 0.00f, 0.00f, 0.00f, 0.50f},
      {0.35f, 0.35f, -0.35f, -0.35f, 0.00f, 0.00f, 0.00f, 0.00f},
      {0.00f, 0.00f, 0.00f, 0.00f, 0.35f, 0.35f, -0.35f, -0.35f},
  };
  if (mode == AI_FB_IMPL_MLP_STUB || mode == AI_FB_IMPL_MODEL_STUB) {
    if (mode == AI_FB_IMPL_MODEL_STUB && ai_fb_model_backend() == 1 && ai_fb_model_available(mode)) {
      float z[6] = {0};
      if (ai_fb_onnx_infer_encoder_4p(x, z)) {
        const float qstep = 0.01f;
        for (int r = 0; r < 6; r++)
          out_payload[r] = ai_fb_quantize_to_int8(z[r], qstep);
        return true;
      }
    }
    const ai_fb_model_t *m = ai_fb_get_model(mode);
    ai_fb_encode_rank1_4p_mlp(m, x, out_payload);
    return true;
  }

  const float qstep = 0.01f;
  for (int r = 0; r < 6; r++) {
    float z = 0.0f;
    for (int c = 0; c < 8; c++)
      z += E[r][c] * x[c];
    out_payload[r] = ai_fb_quantize_to_int8(z, qstep);
  }
  return true;
}

bool ai_fb_encode_rank1_2p(const double complex v_in[2], ai_fb_impl_mode_t mode, uint8_t out_payload[6])
{
  double complex v[2] = {v_in[0], v_in[1]};
  ai_fb_phase_fix_2p(v);

  const float x[4] = {(float)creal(v[0]), (float)creal(v[1]), (float)cimag(v[0]), (float)cimag(v[1])};
  static const float E2[6][4] = {
      {0.50f, 0.00f, 0.00f, 0.00f},
      {0.00f, 0.50f, 0.00f, 0.00f},
      {0.00f, 0.00f, 0.50f, 0.00f},
      {0.00f, 0.00f, 0.00f, 0.50f},
      {0.35f, 0.35f, 0.00f, 0.00f},
      {0.00f, 0.00f, 0.35f, 0.35f},
  };
  if (mode == AI_FB_IMPL_MLP_STUB || mode == AI_FB_IMPL_MODEL_STUB) {
    if (mode == AI_FB_IMPL_MODEL_STUB && ai_fb_model_backend() == 1 && ai_fb_model_available(mode)) {
      float z[6] = {0};
      if (ai_fb_onnx_infer_encoder_2p(x, z)) {
        const float qstep = 0.01f;
        for (int r = 0; r < 6; r++)
          out_payload[r] = ai_fb_quantize_to_int8(z[r], qstep);
        return true;
      }
    }
    const ai_fb_model_t *m = ai_fb_get_model(mode);
    ai_fb_encode_rank1_2p_mlp(m, x, out_payload);
    return true;
  }

  const float qstep = 0.01f;
  for (int r = 0; r < 6; r++) {
    float z = 0.0f;
    for (int c = 0; c < 4; c++)
      z += E2[r][c] * x[c];
    out_payload[r] = ai_fb_quantize_to_int8(z, qstep);
  }
  return true;
}

bool ai_fb_encode_angular_delay_features(const float in_feat[AI_FB_AD_IN], uint8_t out_payload[6])
{
  const ai_fb_model_t *m = ai_fb_get_model(AI_FB_IMPL_ANGULAR_DELAY_MLP);
  float h[AI_FB_AD_H];
  float z[AI_FB_AD_OUT];
  ai_fb_mlp_linear(in_feat,
                   AI_FB_AD_IN,
                   &m->encad_w1[0][0],
                   m->encad_b1,
                   AI_FB_AD_H,
                   h);
  for (int i = 0; i < AI_FB_AD_H; i++)
    h[i] = tanhf(h[i]);
  ai_fb_mlp_linear(h,
                   AI_FB_AD_H,
                   &m->encad_w2[0][0],
                   m->encad_b2,
                   AI_FB_AD_OUT,
                   z);
  float zmin = z[0], zmax = z[0], zn2 = 0.0f;
  for (int r = 0; r < AI_FB_AD_OUT; r++) {
    if (z[r] < zmin)
      zmin = z[r];
    if (z[r] > zmax)
      zmax = z[r];
    zn2 += z[r] * z[r];
  }
  const float qstep = AI_FB_ANGULAR_LATENT_QSTEP;
  if (get_softmodem_params()->print_csi_debug) {
    LOG_I(NR_PHY,
          "AI FB angular encoder pre-quant stats: z_min=%.6f z_max=%.6f z_l2=%.6f qstep=%.6f\n",
          zmin,
          zmax,
          sqrtf(zn2),
          qstep);
  }
  for (int r = 0; r < 6; r++)
    out_payload[r] = ai_fb_quantize_to_int8(z[r], qstep);
  return true;
}

bool ai_fb_encode_angular_refinenet_features(const float in_feat[AI_FB_AD_IN], uint8_t out_payload[6])
{
  const ai_fb_model_t *m = ai_fb_get_model(AI_FB_IMPL_ANGULAR_DELAY_REFINENET);
  float xin[AI_FB_RN_CH * AI_FB_RN_H * AI_FB_RN_W];
  for (int p = 0; p < AI_FB_RN_W; p++) {
    for (int d = 0; d < AI_FB_RN_H; d++) {
      const int src = p * AI_FB_RN_H + d;
      xin[(0 * AI_FB_RN_H + d) * AI_FB_RN_W + p] = in_feat[src];
      xin[(1 * AI_FB_RN_H + d) * AI_FB_RN_W + p] = in_feat[AI_FB_AD_PORTS * AI_FB_AD_ROWS + src];
    }
  }
  float h1[AI_FB_RN_C * AI_FB_RN_H * AI_FB_RN_W];
  ai_fb_conv2d_same(xin,
                    AI_FB_RN_CH,
                    AI_FB_RN_H,
                    AI_FB_RN_W,
                    &m->rn_enc_conv_w[0][0][0][0],
                    m->rn_enc_conv_b,
                    AI_FB_RN_C,
                    AI_FB_RN_K,
                    h1);
  ai_fb_bn_apply(h1, AI_FB_RN_C, AI_FB_RN_H, AI_FB_RN_W, m->rn_enc_bn_g, m->rn_enc_bn_b, m->rn_enc_bn_m, m->rn_enc_bn_v);
  for (int i = 0; i < AI_FB_RN_FC; i++)
    h1[i] = ai_fb_leaky_relu(h1[i]);
  float z[AI_FB_RN_LATENT];
  ai_fb_mlp_linear(h1, AI_FB_RN_FC, &m->rn_enc_fc_w[0][0], m->rn_enc_fc_b, AI_FB_RN_LATENT, z);
  float zmin = z[0], zmax = z[0], zn2 = 0.0f;
  for (int r = 0; r < AI_FB_RN_LATENT; r++) {
    if (z[r] < zmin)
      zmin = z[r];
    if (z[r] > zmax)
      zmax = z[r];
    zn2 += z[r] * z[r];
  }
  const float qstep = AI_FB_ANGULAR_LATENT_QSTEP;
  if (get_softmodem_params()->print_csi_debug) {
    LOG_I(NR_PHY,
          "AI FB angular refinenet encoder pre-quant stats: z_min=%.6f z_max=%.6f z_l2=%.6f qstep=%.6f\n",
          zmin,
          zmax,
          sqrtf(zn2),
          qstep);
  }
  for (int r = 0; r < 6; r++)
    out_payload[r] = ai_fb_quantize_to_int8(z[r], qstep);
  return true;
}
