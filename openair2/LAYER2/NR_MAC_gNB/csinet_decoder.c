#include "openair2/LAYER2/NR_MAC_gNB/csinet_decoder.h"

#include <math.h>
#include <string.h>

#include "openair2/LAYER2/NR_MAC_COMMON/csinet_decoder_weights.h"

static void fill_v_lm(double complex v_lm[16][12][4])
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

bool csinet_decode_rank1_4p(const uint8_t latent[CSINET_LATENT_BYTES], csinet_decode4p_out_t *out)
{
  memset(out, 0, sizeof(*out));
  float z[CSINET_LATENT_BYTES];
  for (int i = 0; i < CSINET_LATENT_BYTES; i++)
    z[i] = (float)((int8_t)latent[i]) * CSINET_LATENT_QSTEP;
  float x[8] = {0};
  for (int r = 0; r < 8; r++)
    for (int c = 0; c < CSINET_LATENT_BYTES; c++)
      x[r] += csinet_dec4_w[r][c] * z[c];
  for (int i = 0; i < 4; i++)
    out->vhat[i] = (double)x[i] + I * (double)x[4 + i];
  double n2 = 0.0;
  for (int i = 0; i < 4; i++)
    n2 += creal(out->vhat[i] * conj(out->vhat[i]));
  const double n = sqrt(n2);
  if (n < 1e-12)
    return false;
  for (int i = 0; i < 4; i++)
    out->vhat[i] /= n;

  double complex v_lm[16][12][4];
  fill_v_lm(v_lm);
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
  out->report.metric = best;
  out->report.ri_rank = 1;
  out->report.cqi = 0;
  const int ll_canonical = best_class + 4;
  out->report.pmi_x1 = (uint8_t)(((ll_canonical & 0x7) << 3) | (best_mm & 0x7));
  out->report.pmi_x2 = 0;
  return true;
}

bool csinet_decode_rank1_2p(const uint8_t latent[CSINET_LATENT_BYTES], csinet_decode2p_out_t *out)
{
  memset(out, 0, sizeof(*out));
  float z[CSINET_LATENT_BYTES];
  for (int i = 0; i < CSINET_LATENT_BYTES; i++)
    z[i] = (float)((int8_t)latent[i]) * CSINET_LATENT_QSTEP;
  float x[4] = {0};
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < CSINET_LATENT_BYTES; c++)
      x[r] += csinet_dec2_w[r][c] * z[c];
  out->vhat2[0] = (double)x[0] + I * (double)x[2];
  out->vhat2[1] = (double)x[1] + I * (double)x[3];
  double n2 = creal(out->vhat2[0] * conj(out->vhat2[0])) + creal(out->vhat2[1] * conj(out->vhat2[1]));
  const double n = sqrt(n2);
  if (n < 1e-12)
    return false;
  out->vhat2[0] /= n;
  out->vhat2[1] /= n;
  double best = -1.0;
  uint8_t best_k = 0;
  for (uint8_t k = 0; k < 4; k++) {
    const double phase = (M_PI / 2.0) * (double)k;
    const double complex w0 = M_SQRT1_2 + 0.0 * I;
    const double complex w1 = M_SQRT1_2 * cexp(I * phase);
    const double complex dot = conj(w0) * out->vhat2[0] + conj(w1) * out->vhat2[1];
    const double metric = creal(dot * conj(dot));
    if (metric > best) {
      best = metric;
      best_k = k;
    }
  }
  out->report.metric = best;
  out->report.ri_rank = 1;
  out->report.cqi = 0;
  out->report.pmi_x1 = 0;
  out->report.pmi_x2 = best_k;
  return true;
}
