#include "openair1/PHY/NR_UE_TRANSPORT/csinet_encoder.h"

#include <math.h>
#include <complex.h>

#include "openair1/PHY/NR_UE_TRANSPORT/csinet_encoder_weights.h"

static inline uint8_t quantize_latent(float z)
{
  int q = (int)lrintf(z / CSINET_LATENT_QSTEP);
  if (q > 127)
    q = 127;
  if (q < -127)
    q = -127;
  return (uint8_t)((int8_t)q);
}

static void phase_fix_4p(double complex v[4])
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

static void phase_fix_2p(double complex v[2])
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

static void dominant_eigvec_4p(double complex R[4][4], double complex v_out[4], double *lam_out)
{
  double complex v[4] = {1.0 + 0.0 * I, 0.0 + 0.0 * I, 0.0 + 0.0 * I, 0.0 + 0.0 * I};
  for (int it = 0; it < 32; it++) {
    double complex w[4] = {0};
    for (int i = 0; i < 4; i++)
      for (int j = 0; j < 4; j++)
        w[i] += R[i][j] * v[j];
    double n2 = 0.0;
    for (int i = 0; i < 4; i++)
      n2 += creal(w[i] * conj(w[i]));
    const double n = sqrt(n2);
    if (n < 1e-20)
      break;
    for (int i = 0; i < 4; i++)
      v[i] = w[i] / n;
  }
  double complex Av[4] = {0};
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++)
      Av[i] += R[i][j] * v[j];
  double complex rq = 0;
  for (int i = 0; i < 4; i++)
    rq += conj(v[i]) * Av[i];
  for (int i = 0; i < 4; i++)
    v_out[i] = v[i];
  *lam_out = fmax(0.0, creal(rq));
}

static void dominant_eigvec_2p(double complex R[2][2], double complex v_out[2], double *lam_out)
{
  double complex v[2] = {1.0 + 0.0 * I, 0.0 + 0.0 * I};
  for (int it = 0; it < 32; it++) {
    double complex w[2] = {0};
    for (int i = 0; i < 2; i++)
      for (int j = 0; j < 2; j++)
        w[i] += R[i][j] * v[j];
    double n2 = 0.0;
    for (int i = 0; i < 2; i++)
      n2 += creal(w[i] * conj(w[i]));
    const double n = sqrt(n2);
    if (n < 1e-20)
      break;
    for (int i = 0; i < 2; i++)
      v[i] = w[i] / n;
  }
  double complex Av[2] = {0};
  for (int i = 0; i < 2; i++)
    for (int j = 0; j < 2; j++)
      Av[i] += R[i][j] * v[j];
  double complex rq = 0;
  for (int i = 0; i < 2; i++)
    rq += conj(v[i]) * Av[i];
  v_out[0] = v[0];
  v_out[1] = v[1];
  *lam_out = fmax(0.0, creal(rq));
}

bool csinet_encode_wideband_4p(const NR_DL_FRAME_PARMS *fp,
                               const c16_t *h_freq,
                               int n_rx,
                               int n_ports_stride,
                               int n_sc,
                               uint8_t out_payload[CSINET_LATENT_BYTES])
{
  (void)fp;
  if (!h_freq || n_rx <= 0 || n_sc <= 0)
    return false;
  double complex R[4][4] = {0};
  int n_re = 0;
  for (int aarx = 0; aarx < n_rx; aarx++) {
    for (int sc = 0; sc < n_sc; sc++) {
      double complex h[4];
      for (int p = 0; p < 4; p++) {
        const c16_t hs = h_freq[(aarx * n_ports_stride + p) * n_sc + sc];
        h[p] = (double)hs.r + I * (double)hs.i;
      }
      for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
          R[i][j] += conj(h[i]) * h[j];
      n_re++;
    }
  }
  if (n_re <= 0)
    return false;
  const double inv = 1.0 / (double)n_re;
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++)
      R[i][j] *= inv;
  double complex v[4];
  double lam = 0.0;
  dominant_eigvec_4p(R, v, &lam);
  if (lam < 1e-12)
    return false;
  phase_fix_4p(v);
  const float x[8] = {(float)creal(v[0]), (float)creal(v[1]), (float)creal(v[2]), (float)creal(v[3]),
                      (float)cimag(v[0]), (float)cimag(v[1]), (float)cimag(v[2]), (float)cimag(v[3])};
  for (int r = 0; r < CSINET_LATENT_BYTES; r++) {
    float z = 0.0f;
    for (int c = 0; c < 8; c++)
      z += csinet_enc4_w[r][c] * x[c];
    out_payload[r] = quantize_latent(z);
  }
  return true;
}

bool csinet_encode_wideband_2p(const NR_DL_FRAME_PARMS *fp,
                               const c16_t *h_freq,
                               int n_rx,
                               int n_ports_stride,
                               int n_sc,
                               uint8_t out_payload[CSINET_LATENT_BYTES])
{
  (void)fp;
  if (!h_freq || n_rx <= 0 || n_sc <= 0)
    return false;
  double complex R[2][2] = {0};
  int n_re = 0;
  for (int aarx = 0; aarx < n_rx; aarx++) {
    for (int sc = 0; sc < n_sc; sc++) {
      double complex h[2];
      for (int p = 0; p < 2; p++) {
        const c16_t hs = h_freq[(aarx * n_ports_stride + p) * n_sc + sc];
        h[p] = (double)hs.r + I * (double)hs.i;
      }
      for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++)
          R[i][j] += conj(h[i]) * h[j];
      n_re++;
    }
  }
  if (n_re <= 0)
    return false;
  const double inv = 1.0 / (double)n_re;
  for (int i = 0; i < 2; i++)
    for (int j = 0; j < 2; j++)
      R[i][j] *= inv;
  double complex v[2];
  double lam = 0.0;
  dominant_eigvec_2p(R, v, &lam);
  if (lam < 1e-12)
    return false;
  phase_fix_2p(v);
  const float x[4] = {(float)creal(v[0]), (float)creal(v[1]), (float)cimag(v[0]), (float)cimag(v[1])};
  for (int r = 0; r < CSINET_LATENT_BYTES; r++) {
    float z = 0.0f;
    for (int c = 0; c < 4; c++)
      z += csinet_enc2_w[r][c] * x[c];
    out_payload[r] = quantize_latent(z);
  }
  return true;
}
