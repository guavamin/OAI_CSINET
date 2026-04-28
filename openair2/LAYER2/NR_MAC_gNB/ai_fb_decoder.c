#include "ai_fb_decoder.h"

#include <math.h>
#include <string.h>
#include "openair2/LAYER2/NR_MAC_COMMON/ai_fb_model_loader.h"
#include "openair2/LAYER2/NR_MAC_COMMON/ai_fb_onnx_adapter.h"

/* Rank hint for angular-delay custom decode: PMI metric in [0,1], not trained RI head. */
static const double ai_fb_angular_ri_metric_thresh = 0.85;

/* TS 38.211 Table 6.3.1.5-4 (two layers, two antenna ports); 'n' = -1, 'o' = -j (same as PHY nr_W_2l_2p). */
static const char ai_fb_nr_W_2l_2p[3][2][2] = {
    {{'1', '0'}, {'0', '1'}},
    {{'1', '1'}, {'1', 'n'}},
    {{'1', '1'}, {'j', 'o'}},
};

static double complex ai_fb_nr_precoder_char_to_z(char c)
{
  switch (c) {
    case '0':
      return 0.0;
    case '1':
      return 1.0;
    case 'j':
      return I;
    case 'n':
      return -1.0;
    case 'o':
      return -I;
    default:
      return 0.0;
  }
}

/* Fill W[antenna][layer] and L2-normalize each precoder column (layer). */
static void ai_fb_nr_fill_norm_W_2l_2p(int tpmi_idx, double complex W[2][2])
{
  for (int ap = 0; ap < 2; ap++)
    for (int L = 0; L < 2; L++)
      W[ap][L] = ai_fb_nr_precoder_char_to_z(ai_fb_nr_W_2l_2p[tpmi_idx][ap][L]);
  for (int L = 0; L < 2; L++) {
    double n2 = 0.0;
    for (int ap = 0; ap < 2; ap++)
      n2 += creal(W[ap][L] * conj(W[ap][L]));
    const double ns = sqrt(n2);
    if (ns < 1e-20)
      continue;
    for (int ap = 0; ap < 2; ap++)
      W[ap][L] /= ns;
  }
}

/* Energy captured by rank-2 subspace spanned by normalized W columns (|w0^H u|^2 + |w1^H u|^2). */
static double ai_fb_nr_rank2_subspace_metric(const double complex W[2][2], double complex u0, double complex u1)
{
  double s = 0.0;
  for (int L = 0; L < 2; L++) {
    const double complex d = conj(W[0][L]) * u0 + conj(W[1][L]) * u1;
    s += creal(d * conj(d));
  }
  return s;
}

static void ai_fb_fill_v_lm(double complex v_lm[16][12][4])
{
  const int N1 = 2, N2 = 2, O1 = 4, O2 = 4;
  const int max_l = 16;
  const int max_m = 12;
  double complex v[16][2];
  double complex u[12][2];
  for (int ll = 0; ll < max_l; ll++)
    for (int nn = 0; nn < N1; nn++)
      v[ll][nn] = cexp(I * (2 * M_PI * nn * ll) / (N1 * O1));
  for (int mm = 0; mm < max_m; mm++)
    for (int nn = 0; nn < N2; nn++)
      u[mm][nn] = cexp(I * (2 * M_PI * nn * mm) / (N2 * O2));
  for (int ll = 0; ll < max_l; ll++)
    for (int mm = 0; mm < max_m; mm++)
      for (int nn1 = 0; nn1 < N1; nn1++)
        for (int nn2 = 0; nn2 < N2; nn2++)
          v_lm[ll][mm][nn1 * N2 + nn2] = v[ll][nn1] * u[mm][nn2];
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
  return x > 0.0f ? x : 0.1f * x;
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

static void ai_fb_ref_block(float *x,
                            const ai_fb_model_t *m,
                            bool second)
{
  float t1[AI_FB_RN_FC];
  float t2[AI_FB_RN_FC];
  if (!second) {
    ai_fb_conv2d_same(x, AI_FB_RN_C, AI_FB_RN_H, AI_FB_RN_W, &m->rn_ref1_conv1_w[0][0][0][0], m->rn_ref1_conv1_b, AI_FB_RN_C, AI_FB_RN_K, t1);
    ai_fb_bn_apply(t1, AI_FB_RN_C, AI_FB_RN_H, AI_FB_RN_W, m->rn_ref1_bn1_g, m->rn_ref1_bn1_b, m->rn_ref1_bn1_m, m->rn_ref1_bn1_v);
    for (int i = 0; i < AI_FB_RN_FC; i++)
      t1[i] = ai_fb_leaky_relu(t1[i]);
    ai_fb_conv2d_same(t1, AI_FB_RN_C, AI_FB_RN_H, AI_FB_RN_W, &m->rn_ref1_conv2_w[0][0][0][0], m->rn_ref1_conv2_b, AI_FB_RN_C, AI_FB_RN_K, t2);
    ai_fb_bn_apply(t2, AI_FB_RN_C, AI_FB_RN_H, AI_FB_RN_W, m->rn_ref1_bn2_g, m->rn_ref1_bn2_b, m->rn_ref1_bn2_m, m->rn_ref1_bn2_v);
  } else {
    ai_fb_conv2d_same(x, AI_FB_RN_C, AI_FB_RN_H, AI_FB_RN_W, &m->rn_ref2_conv1_w[0][0][0][0], m->rn_ref2_conv1_b, AI_FB_RN_C, AI_FB_RN_K, t1);
    ai_fb_bn_apply(t1, AI_FB_RN_C, AI_FB_RN_H, AI_FB_RN_W, m->rn_ref2_bn1_g, m->rn_ref2_bn1_b, m->rn_ref2_bn1_m, m->rn_ref2_bn1_v);
    for (int i = 0; i < AI_FB_RN_FC; i++)
      t1[i] = ai_fb_leaky_relu(t1[i]);
    ai_fb_conv2d_same(t1, AI_FB_RN_C, AI_FB_RN_H, AI_FB_RN_W, &m->rn_ref2_conv2_w[0][0][0][0], m->rn_ref2_conv2_b, AI_FB_RN_C, AI_FB_RN_K, t2);
    ai_fb_bn_apply(t2, AI_FB_RN_C, AI_FB_RN_H, AI_FB_RN_W, m->rn_ref2_bn2_g, m->rn_ref2_bn2_b, m->rn_ref2_bn2_m, m->rn_ref2_bn2_v);
  }
  for (int i = 0; i < AI_FB_RN_FC; i++)
    x[i] = ai_fb_leaky_relu(x[i] + t2[i]);
}

static void ai_fb_decode_refinenet(const float z[AI_FB_RN_LATENT], float out_feat[AI_FB_AD_IN])
{
  const ai_fb_model_t *m = ai_fb_get_model(AI_FB_IMPL_ANGULAR_DELAY_REFINENET);
  float x[AI_FB_RN_FC];
  ai_fb_mlp_linear(z, AI_FB_RN_LATENT, &m->rn_dec_fc_w[0][0], m->rn_dec_fc_b, AI_FB_RN_FC, x);
  ai_fb_ref_block(x, m, false);
  ai_fb_ref_block(x, m, true);
  float out2[AI_FB_RN_CH * AI_FB_RN_H * AI_FB_RN_W];
  ai_fb_conv2d_same(x, AI_FB_RN_C, AI_FB_RN_H, AI_FB_RN_W, &m->rn_dec_out_w[0][0][0][0], m->rn_dec_out_b, AI_FB_RN_CH, AI_FB_RN_K, out2);
  for (int i = 0; i < AI_FB_RN_CH * AI_FB_RN_H * AI_FB_RN_W; i++)
    out2[i] = tanhf(out2[i]);
  for (int p = 0; p < AI_FB_AD_PORTS; p++) {
    for (int d = 0; d < AI_FB_AD_ROWS; d++) {
      const int dst = p * AI_FB_AD_ROWS + d;
      out_feat[dst] = out2[(0 * AI_FB_RN_H + d) * AI_FB_RN_W + p];
      out_feat[AI_FB_AD_PORTS * AI_FB_AD_ROWS + dst] = out2[(1 * AI_FB_RN_H + d) * AI_FB_RN_W + p];
    }
  }
}

bool ai_fb_decode_rank1_4p(const uint8_t latent[6], ai_fb_impl_mode_t mode, ai_fb_decode4p_out_t *out)
{
  memset(out, 0, sizeof(*out));
  float z[6];
  const float qstep = (mode == AI_FB_IMPL_ANGULAR_DELAY_MLP || mode == AI_FB_IMPL_ANGULAR_DELAY_REFINENET) ? AI_FB_ANGULAR_LATENT_QSTEP
                                                                                                           : 0.01f;
  for (int i = 0; i < 6; i++)
    z[i] = (float)((int8_t)latent[i]) * qstep;

  static const float D[8][6] = {
      {1.0f, 0.0f, 0.0f, 0.0f, 0.7f, 0.0f},
      {0.0f, 1.0f, 0.0f, 0.0f, 0.7f, 0.0f},
      {0.0f, 0.0f, 1.0f, 0.0f, -0.7f, 0.0f},
      {0.0f, 0.0f, 0.0f, 1.0f, -0.7f, 0.0f},
      {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.7f},
      {0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.7f},
      {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, -0.7f},
      {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -0.7f},
  };
  float x[8] = {0};
  if (mode == AI_FB_IMPL_ANGULAR_DELAY_MLP || mode == AI_FB_IMPL_ANGULAR_DELAY_REFINENET) {
    if (mode == AI_FB_IMPL_ANGULAR_DELAY_REFINENET) {
      float feat[AI_FB_AD_IN] = {0};
      ai_fb_decode_refinenet(z, feat);
      for (int p = 0; p < 4; p++) {
        float re = 0.0f;
        float im = 0.0f;
        for (int d = 0; d < AI_FB_AD_ROWS; d++) {
          re += feat[p * AI_FB_AD_ROWS + d];
          im += feat[AI_FB_AD_PORTS * AI_FB_AD_ROWS + p * AI_FB_AD_ROWS + d];
        }
        x[p] = re / (float)AI_FB_AD_ROWS;
        x[4 + p] = im / (float)AI_FB_AD_ROWS;
      }
    } else {
      for (int r = 0; r < 8; r++)
        for (int c = 0; c < 6; c++)
          x[r] += D[r][c] * z[c];
    }
  } else if (mode == AI_FB_IMPL_MLP_STUB || mode == AI_FB_IMPL_MODEL_STUB) {
    if (mode == AI_FB_IMPL_MODEL_STUB && ai_fb_model_backend() == 1 && ai_fb_model_available(mode)) {
      if (!ai_fb_onnx_infer_decoder_4p(z, x)) {
        memset(x, 0, sizeof(x));
      }
    } else {
      const ai_fb_model_t *m = ai_fb_get_model(mode);
      float h[AI_FB_MLP_DEC4_H];
      ai_fb_mlp_linear(z,
                       AI_FB_MLP_DEC4_IN,
                       &m->dec4_w1[0][0],
                       m->dec4_b1,
                       AI_FB_MLP_DEC4_H,
                       h);
      ai_fb_mlp_linear(h,
                       AI_FB_MLP_DEC4_H,
                       &m->dec4_w2[0][0],
                       m->dec4_b2,
                       AI_FB_MLP_DEC4_OUT,
                       x);
    }
  } else {
    for (int r = 0; r < 8; r++)
      for (int c = 0; c < 6; c++)
        x[r] += D[r][c] * z[c];
  }

  for (int i = 0; i < 4; i++)
    out->vhat[i] = (double)x[i] + I * (double)x[4 + i];
  double n2 = 0.0;
  for (int i = 0; i < 4; i++)
    n2 += creal(out->vhat[i] * conj(out->vhat[i]));
  const double n = sqrt(n2);
  if (n < 1e-12) {
    if (mode == AI_FB_IMPL_ANGULAR_DELAY_MLP || mode == AI_FB_IMPL_ANGULAR_DELAY_REFINENET) {
      out->low_energy_fallback = true;
      out->pmi_x1 = 0;
      out->pmi_x2 = 0;
      out->best_metric = 0.0;
      return true;
    }
    return false;
  }
  for (int i = 0; i < 4; i++)
    out->vhat[i] /= n;

  double complex v_lm[16][12][4];
  ai_fb_fill_v_lm(v_lm);
  double best = -1.0;
  int best_ll = 0, best_mm = 0, best_class = 0;
  for (int c = 0; c < 4; c++) {
    const int ll0 = c;
    const int ll1 = c + 4;
    for (int mm = 0; mm < 8; mm++) {
      double class_best = -1.0;
      int ll_pick = ll0;
      for (int k = 0; k < 2; k++) {
        const int ll = (k == 0) ? ll0 : ll1;
        double complex w[4];
        for (int p = 0; p < 4; p++)
          w[p] = v_lm[ll][mm][p];
        double wn2 = 0.0;
        for (int p = 0; p < 4; p++)
          wn2 += creal(w[p] * conj(w[p]));
        const double wn = sqrt(wn2);
        if (wn < 1e-12)
          continue;
        for (int p = 0; p < 4; p++)
          w[p] /= wn;
        double complex dot = 0;
        for (int p = 0; p < 4; p++)
          dot += conj(w[p]) * out->vhat[p];
        const double metric = creal(dot * conj(dot));
        if (metric > class_best) {
          class_best = metric;
          ll_pick = ll;
        }
      }
      if (class_best > best) {
        best = class_best;
        best_ll = ll_pick;
        best_mm = mm;
        best_class = c;
      }
    }
  }

  out->best_ll = best_ll;
  out->best_mm = best_mm;
  out->best_class = best_class;
  out->best_metric = best;
  const int ll_canonical = best_class + 4;
  out->pmi_x1 = (uint8_t)(((ll_canonical & 0x7) << 3) | (best_mm & 0x7));
  out->pmi_x2 = 0;
  if (mode == AI_FB_IMPL_ANGULAR_DELAY_MLP || mode == AI_FB_IMPL_ANGULAR_DELAY_REFINENET) {
    const double m = (best < 0.0) ? 0.0 : best;
    out->cqi = (uint8_t)fmin(15.0, fmax(0.0, m * 16.0));
    out->ri = (m > ai_fb_angular_ri_metric_thresh) ? 1 : 0;
  } else {
    out->ri = 0;
    out->cqi = (uint8_t)fmin(fmax(best * 16.0, 0.0), 15.0);
  }
  return true;
}

bool ai_fb_decode_rank1_2p(const uint8_t latent[6], ai_fb_impl_mode_t mode, ai_fb_decode2p_out_t *out)
{
  memset(out, 0, sizeof(*out));
  const float qstep = (mode == AI_FB_IMPL_ANGULAR_DELAY_MLP || mode == AI_FB_IMPL_ANGULAR_DELAY_REFINENET) ? AI_FB_ANGULAR_LATENT_QSTEP
                                                                                                           : 0.01f;
  float x0, x1, x2, x3;
  if (mode == AI_FB_IMPL_ANGULAR_DELAY_MLP || mode == AI_FB_IMPL_ANGULAR_DELAY_REFINENET) {
    if (mode == AI_FB_IMPL_ANGULAR_DELAY_REFINENET) {
      float z[6];
      for (int i = 0; i < 6; i++)
        z[i] = (float)((int8_t)latent[i]) * qstep;
      float feat[AI_FB_AD_IN] = {0};
      ai_fb_decode_refinenet(z, feat);
      float re0 = 0.0f, re1 = 0.0f, im0 = 0.0f, im1 = 0.0f;
      for (int d = 0; d < AI_FB_AD_ROWS; d++) {
        re0 += feat[0 * AI_FB_AD_ROWS + d];
        re1 += feat[1 * AI_FB_AD_ROWS + d];
        im0 += feat[AI_FB_AD_PORTS * AI_FB_AD_ROWS + 0 * AI_FB_AD_ROWS + d];
        im1 += feat[AI_FB_AD_PORTS * AI_FB_AD_ROWS + 1 * AI_FB_AD_ROWS + d];
      }
      x0 = re0 / (float)AI_FB_AD_ROWS;
      x1 = re1 / (float)AI_FB_AD_ROWS;
      x2 = im0 / (float)AI_FB_AD_ROWS;
      x3 = im1 / (float)AI_FB_AD_ROWS;
    } else {
      x0 = 2.0f * (float)((int8_t)latent[0]) * qstep;
      x1 = 2.0f * (float)((int8_t)latent[1]) * qstep;
      x2 = 2.0f * (float)((int8_t)latent[2]) * qstep;
      x3 = 2.0f * (float)((int8_t)latent[3]) * qstep;
    }
  } else if (mode == AI_FB_IMPL_MLP_STUB || mode == AI_FB_IMPL_MODEL_STUB) {
    float z[6];
    for (int i = 0; i < 6; i++)
      z[i] = (float)((int8_t)latent[i]) * qstep;
    if (mode == AI_FB_IMPL_MODEL_STUB && ai_fb_model_backend() == 1 && ai_fb_model_available(mode)) {
      float x[AI_FB_MLP_DEC2_OUT] = {0};
      if (ai_fb_onnx_infer_decoder_2p(z, x)) {
        x0 = x[0];
        x1 = x[1];
        x2 = x[2];
        x3 = x[3];
      } else {
        x0 = x1 = x2 = x3 = 0.0f;
      }
    } else {
      const ai_fb_model_t *m = ai_fb_get_model(mode);
      float h[AI_FB_MLP_DEC2_H];
      float x[AI_FB_MLP_DEC2_OUT];
      ai_fb_mlp_linear(z,
                       AI_FB_MLP_DEC2_IN,
                       &m->dec2_w1[0][0],
                       m->dec2_b1,
                       AI_FB_MLP_DEC2_H,
                       h);
      ai_fb_mlp_linear(h,
                       AI_FB_MLP_DEC2_H,
                       &m->dec2_w2[0][0],
                       m->dec2_b2,
                       AI_FB_MLP_DEC2_OUT,
                       x);
      x0 = x[0];
      x1 = x[1];
      x2 = x[2];
      x3 = x[3];
    }
  } else {
    x0 = 2.0f * (float)((int8_t)latent[0]) * qstep;
    x1 = 2.0f * (float)((int8_t)latent[1]) * qstep;
    x2 = 2.0f * (float)((int8_t)latent[2]) * qstep;
    x3 = 2.0f * (float)((int8_t)latent[3]) * qstep;
  }
  out->vhat2[0] = (double)x0 + I * (double)x2;
  out->vhat2[1] = (double)x1 + I * (double)x3;
  double n2 = creal(out->vhat2[0] * conj(out->vhat2[0])) + creal(out->vhat2[1] * conj(out->vhat2[1]));
  const double n = sqrt(n2);
  if (n < 1e-12) {
    if (mode == AI_FB_IMPL_ANGULAR_DELAY_MLP || mode == AI_FB_IMPL_ANGULAR_DELAY_REFINENET) {
      out->low_energy_fallback = true;
      out->pmi_x1 = 0;
      out->pmi_x2 = 0;
      out->best_metric = 0.0;
      return true;
    }
    return false;
  }
  out->vhat2[0] /= n;
  out->vhat2[1] /= n;

  double best = -1.0;
  uint8_t best_pmi_x2 = 0;
  if (mode == AI_FB_IMPL_ANGULAR_DELAY_MLP || mode == AI_FB_IMPL_ANGULAR_DELAY_REFINENET) {
    /* NR Type I single-panel i2 for 2 ports, 2 layers: TPMI rows 0–1 of TS 38.211 Table 6.3.1.5-4 (1-bit i2 in OAI configs). */
    for (uint8_t i2 = 0; i2 < 2; i2++) {
      double complex W[2][2];
      ai_fb_nr_fill_norm_W_2l_2p((int)i2, W);
      const double metric = ai_fb_nr_rank2_subspace_metric(W, out->vhat2[0], out->vhat2[1]);
      if (metric > best) {
        best = metric;
        best_pmi_x2 = i2;
      }
    }
  } else {
    for (uint8_t k = 0; k < 4; k++) {
      const double phase = (M_PI / 2.0) * (double)k;
      const double complex w0 = M_SQRT1_2 + 0.0 * I;
      const double complex w1 = M_SQRT1_2 * cexp(I * phase);
      const double complex dot = conj(w0) * out->vhat2[0] + conj(w1) * out->vhat2[1];
      const double metric = creal(dot * conj(dot));
      if (metric > best) {
        best = metric;
        best_pmi_x2 = k;
      }
    }
  }
  out->best_metric = best;
  out->pmi_x1 = 0;
  out->pmi_x2 = best_pmi_x2;
  if (mode == AI_FB_IMPL_ANGULAR_DELAY_MLP || mode == AI_FB_IMPL_ANGULAR_DELAY_REFINENET) {
    const double m = (best < 0.0) ? 0.0 : best;
    out->cqi = (uint8_t)fmin(15.0, fmax(0.0, m * 16.0));
    out->ri = (m > ai_fb_angular_ri_metric_thresh) ? 1 : 0;
  } else {
    out->ri = 0;
    out->cqi = (uint8_t)fmin(fmax(best * 16.0, 0.0), 15.0);
  }
  return true;
}
