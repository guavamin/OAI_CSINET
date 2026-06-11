#include "openair2/LAYER2/NR_MAC_COMMON/ai_fb_model_loader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "common/utils/LOG/log.h"
#include "executables/softmodem-common.h"
#include "openair2/LAYER2/NR_MAC_COMMON/ai_fb_onnx_adapter.h"

static ai_fb_model_t g_model;
static bool g_loaded = false;
static bool g_attempted = false;

#define AI_FB_MODEL_BIN_MAGIC 0x4D424641u /* 'AFBM' */
#define AI_FB_MODEL_BIN_VERSION_V1 1u
#define AI_FB_MODEL_BIN_VERSION_V2 2u
#define AI_FB_MODEL_BIN_VERSION_V3 3u

static void load_defaults(ai_fb_model_t *m)
{
  memcpy(m->enc4_w1, ai_fb_mlp_enc4_w1, sizeof(m->enc4_w1));
  memcpy(m->enc4_b1, ai_fb_mlp_enc4_b1, sizeof(m->enc4_b1));
  memcpy(m->enc4_w2, ai_fb_mlp_enc4_w2, sizeof(m->enc4_w2));
  memcpy(m->enc4_b2, ai_fb_mlp_enc4_b2, sizeof(m->enc4_b2));
  memcpy(m->dec4_w1, ai_fb_mlp_dec4_w1, sizeof(m->dec4_w1));
  memcpy(m->dec4_b1, ai_fb_mlp_dec4_b1, sizeof(m->dec4_b1));
  memcpy(m->dec4_w2, ai_fb_mlp_dec4_w2, sizeof(m->dec4_w2));
  memcpy(m->dec4_b2, ai_fb_mlp_dec4_b2, sizeof(m->dec4_b2));

  memcpy(m->enc2_w1, ai_fb_mlp_enc2_w1, sizeof(m->enc2_w1));
  memcpy(m->enc2_b1, ai_fb_mlp_enc2_b1, sizeof(m->enc2_b1));
  memcpy(m->enc2_w2, ai_fb_mlp_enc2_w2, sizeof(m->enc2_w2));
  memcpy(m->enc2_b2, ai_fb_mlp_enc2_b2, sizeof(m->enc2_b2));
  memcpy(m->dec2_w1, ai_fb_mlp_dec2_w1, sizeof(m->dec2_w1));
  memcpy(m->dec2_b1, ai_fb_mlp_dec2_b1, sizeof(m->dec2_b1));
  memcpy(m->dec2_w2, ai_fb_mlp_dec2_w2, sizeof(m->dec2_w2));
  memcpy(m->dec2_b2, ai_fb_mlp_dec2_b2, sizeof(m->dec2_b2));
  memset(m->encad_w1, 0, sizeof(m->encad_w1));
  memset(m->encad_b1, 0, sizeof(m->encad_b1));
  memset(m->encad_w2, 0, sizeof(m->encad_w2));
  memset(m->encad_b2, 0, sizeof(m->encad_b2));
  memset(m->decad_w1, 0, sizeof(m->decad_w1));
  memset(m->decad_b1, 0, sizeof(m->decad_b1));
  memset(m->decad_w2, 0, sizeof(m->decad_w2));
  memset(m->decad_b2, 0, sizeof(m->decad_b2));
  memset(m->rn_enc_conv_w, 0, sizeof(m->rn_enc_conv_w));
  memset(m->rn_enc_conv_b, 0, sizeof(m->rn_enc_conv_b));
  memset(m->rn_enc_bn_g, 0, sizeof(m->rn_enc_bn_g));
  memset(m->rn_enc_bn_b, 0, sizeof(m->rn_enc_bn_b));
  memset(m->rn_enc_bn_m, 0, sizeof(m->rn_enc_bn_m));
  memset(m->rn_enc_bn_v, 0, sizeof(m->rn_enc_bn_v));
  memset(m->rn_enc_fc_w, 0, sizeof(m->rn_enc_fc_w));
  memset(m->rn_enc_fc_b, 0, sizeof(m->rn_enc_fc_b));
  memset(m->rn_dec_fc_w, 0, sizeof(m->rn_dec_fc_w));
  memset(m->rn_dec_fc_b, 0, sizeof(m->rn_dec_fc_b));
  memset(m->rn_ref1_conv1_w, 0, sizeof(m->rn_ref1_conv1_w));
  memset(m->rn_ref1_conv1_b, 0, sizeof(m->rn_ref1_conv1_b));
  memset(m->rn_ref1_bn1_g, 0, sizeof(m->rn_ref1_bn1_g));
  memset(m->rn_ref1_bn1_b, 0, sizeof(m->rn_ref1_bn1_b));
  memset(m->rn_ref1_bn1_m, 0, sizeof(m->rn_ref1_bn1_m));
  memset(m->rn_ref1_bn1_v, 0, sizeof(m->rn_ref1_bn1_v));
  memset(m->rn_ref1_conv2_w, 0, sizeof(m->rn_ref1_conv2_w));
  memset(m->rn_ref1_conv2_b, 0, sizeof(m->rn_ref1_conv2_b));
  memset(m->rn_ref1_bn2_g, 0, sizeof(m->rn_ref1_bn2_g));
  memset(m->rn_ref1_bn2_b, 0, sizeof(m->rn_ref1_bn2_b));
  memset(m->rn_ref1_bn2_m, 0, sizeof(m->rn_ref1_bn2_m));
  memset(m->rn_ref1_bn2_v, 0, sizeof(m->rn_ref1_bn2_v));
  memset(m->rn_ref2_conv1_w, 0, sizeof(m->rn_ref2_conv1_w));
  memset(m->rn_ref2_conv1_b, 0, sizeof(m->rn_ref2_conv1_b));
  memset(m->rn_ref2_bn1_g, 0, sizeof(m->rn_ref2_bn1_g));
  memset(m->rn_ref2_bn1_b, 0, sizeof(m->rn_ref2_bn1_b));
  memset(m->rn_ref2_bn1_m, 0, sizeof(m->rn_ref2_bn1_m));
  memset(m->rn_ref2_bn1_v, 0, sizeof(m->rn_ref2_bn1_v));
  memset(m->rn_ref2_conv2_w, 0, sizeof(m->rn_ref2_conv2_w));
  memset(m->rn_ref2_conv2_b, 0, sizeof(m->rn_ref2_conv2_b));
  memset(m->rn_ref2_bn2_g, 0, sizeof(m->rn_ref2_bn2_g));
  memset(m->rn_ref2_bn2_b, 0, sizeof(m->rn_ref2_bn2_b));
  memset(m->rn_ref2_bn2_m, 0, sizeof(m->rn_ref2_bn2_m));
  memset(m->rn_ref2_bn2_v, 0, sizeof(m->rn_ref2_bn2_v));
  memset(m->rn_dec_out_w, 0, sizeof(m->rn_dec_out_w));
  memset(m->rn_dec_out_b, 0, sizeof(m->rn_dec_out_b));
}

static float *lookup_tensor(ai_fb_model_t *m, const char *name, int *count)
{
  if (!strcmp(name, "enc4_w1")) { *count = AI_FB_MLP_ENC4_H * AI_FB_MLP_ENC4_IN; return &m->enc4_w1[0][0]; }
  if (!strcmp(name, "enc4_b1")) { *count = AI_FB_MLP_ENC4_H; return &m->enc4_b1[0]; }
  if (!strcmp(name, "enc4_w2")) { *count = AI_FB_MLP_ENC4_OUT * AI_FB_MLP_ENC4_H; return &m->enc4_w2[0][0]; }
  if (!strcmp(name, "enc4_b2")) { *count = AI_FB_MLP_ENC4_OUT; return &m->enc4_b2[0]; }
  if (!strcmp(name, "dec4_w1")) { *count = AI_FB_MLP_DEC4_H * AI_FB_MLP_DEC4_IN; return &m->dec4_w1[0][0]; }
  if (!strcmp(name, "dec4_b1")) { *count = AI_FB_MLP_DEC4_H; return &m->dec4_b1[0]; }
  if (!strcmp(name, "dec4_w2")) { *count = AI_FB_MLP_DEC4_OUT * AI_FB_MLP_DEC4_H; return &m->dec4_w2[0][0]; }
  if (!strcmp(name, "dec4_b2")) { *count = AI_FB_MLP_DEC4_OUT; return &m->dec4_b2[0]; }
  if (!strcmp(name, "enc2_w1")) { *count = AI_FB_MLP_ENC2_H * AI_FB_MLP_ENC2_IN; return &m->enc2_w1[0][0]; }
  if (!strcmp(name, "enc2_b1")) { *count = AI_FB_MLP_ENC2_H; return &m->enc2_b1[0]; }
  if (!strcmp(name, "enc2_w2")) { *count = AI_FB_MLP_ENC2_OUT * AI_FB_MLP_ENC2_H; return &m->enc2_w2[0][0]; }
  if (!strcmp(name, "enc2_b2")) { *count = AI_FB_MLP_ENC2_OUT; return &m->enc2_b2[0]; }
  if (!strcmp(name, "dec2_w1")) { *count = AI_FB_MLP_DEC2_H * AI_FB_MLP_DEC2_IN; return &m->dec2_w1[0][0]; }
  if (!strcmp(name, "dec2_b1")) { *count = AI_FB_MLP_DEC2_H; return &m->dec2_b1[0]; }
  if (!strcmp(name, "dec2_w2")) { *count = AI_FB_MLP_DEC2_OUT * AI_FB_MLP_DEC2_H; return &m->dec2_w2[0][0]; }
  if (!strcmp(name, "dec2_b2")) { *count = AI_FB_MLP_DEC2_OUT; return &m->dec2_b2[0]; }
  if (!strcmp(name, "encad_w1")) { *count = AI_FB_AD_H * AI_FB_AD_IN; return &m->encad_w1[0][0]; }
  if (!strcmp(name, "encad_b1")) { *count = AI_FB_AD_H; return &m->encad_b1[0]; }
  if (!strcmp(name, "encad_w2")) { *count = AI_FB_AD_OUT * AI_FB_AD_H; return &m->encad_w2[0][0]; }
  if (!strcmp(name, "encad_b2")) { *count = AI_FB_AD_OUT; return &m->encad_b2[0]; }
  if (!strcmp(name, "decad_w1")) { *count = AI_FB_AD_H * AI_FB_AD_OUT; return &m->decad_w1[0][0]; }
  if (!strcmp(name, "decad_b1")) { *count = AI_FB_AD_H; return &m->decad_b1[0]; }
  if (!strcmp(name, "decad_w2")) { *count = AI_FB_AD_IN * AI_FB_AD_H; return &m->decad_w2[0][0]; }
  if (!strcmp(name, "decad_b2")) { *count = AI_FB_AD_IN; return &m->decad_b2[0]; }
  if (!strcmp(name, "rn_enc_conv_w")) { *count = AI_FB_RN_C * AI_FB_RN_CH * AI_FB_RN_K * AI_FB_RN_K; return &m->rn_enc_conv_w[0][0][0][0]; }
  if (!strcmp(name, "rn_enc_conv_b")) { *count = AI_FB_RN_C; return &m->rn_enc_conv_b[0]; }
  if (!strcmp(name, "rn_enc_bn_g")) { *count = AI_FB_RN_C; return &m->rn_enc_bn_g[0]; }
  if (!strcmp(name, "rn_enc_bn_b")) { *count = AI_FB_RN_C; return &m->rn_enc_bn_b[0]; }
  if (!strcmp(name, "rn_enc_bn_m")) { *count = AI_FB_RN_C; return &m->rn_enc_bn_m[0]; }
  if (!strcmp(name, "rn_enc_bn_v")) { *count = AI_FB_RN_C; return &m->rn_enc_bn_v[0]; }
  if (!strcmp(name, "rn_enc_fc_w")) { *count = AI_FB_RN_LATENT * AI_FB_RN_FC; return &m->rn_enc_fc_w[0][0]; }
  if (!strcmp(name, "rn_enc_fc_b")) { *count = AI_FB_RN_LATENT; return &m->rn_enc_fc_b[0]; }
  if (!strcmp(name, "rn_dec_fc_w")) { *count = AI_FB_RN_FC * AI_FB_RN_LATENT; return &m->rn_dec_fc_w[0][0]; }
  if (!strcmp(name, "rn_dec_fc_b")) { *count = AI_FB_RN_FC; return &m->rn_dec_fc_b[0]; }
  if (!strcmp(name, "rn_ref1_conv1_w")) { *count = AI_FB_RN_C * AI_FB_RN_C * AI_FB_RN_K * AI_FB_RN_K; return &m->rn_ref1_conv1_w[0][0][0][0]; }
  if (!strcmp(name, "rn_ref1_conv1_b")) { *count = AI_FB_RN_C; return &m->rn_ref1_conv1_b[0]; }
  if (!strcmp(name, "rn_ref1_bn1_g")) { *count = AI_FB_RN_C; return &m->rn_ref1_bn1_g[0]; }
  if (!strcmp(name, "rn_ref1_bn1_b")) { *count = AI_FB_RN_C; return &m->rn_ref1_bn1_b[0]; }
  if (!strcmp(name, "rn_ref1_bn1_m")) { *count = AI_FB_RN_C; return &m->rn_ref1_bn1_m[0]; }
  if (!strcmp(name, "rn_ref1_bn1_v")) { *count = AI_FB_RN_C; return &m->rn_ref1_bn1_v[0]; }
  if (!strcmp(name, "rn_ref1_conv2_w")) { *count = AI_FB_RN_C * AI_FB_RN_C * AI_FB_RN_K * AI_FB_RN_K; return &m->rn_ref1_conv2_w[0][0][0][0]; }
  if (!strcmp(name, "rn_ref1_conv2_b")) { *count = AI_FB_RN_C; return &m->rn_ref1_conv2_b[0]; }
  if (!strcmp(name, "rn_ref1_bn2_g")) { *count = AI_FB_RN_C; return &m->rn_ref1_bn2_g[0]; }
  if (!strcmp(name, "rn_ref1_bn2_b")) { *count = AI_FB_RN_C; return &m->rn_ref1_bn2_b[0]; }
  if (!strcmp(name, "rn_ref1_bn2_m")) { *count = AI_FB_RN_C; return &m->rn_ref1_bn2_m[0]; }
  if (!strcmp(name, "rn_ref1_bn2_v")) { *count = AI_FB_RN_C; return &m->rn_ref1_bn2_v[0]; }
  if (!strcmp(name, "rn_ref2_conv1_w")) { *count = AI_FB_RN_C * AI_FB_RN_C * AI_FB_RN_K * AI_FB_RN_K; return &m->rn_ref2_conv1_w[0][0][0][0]; }
  if (!strcmp(name, "rn_ref2_conv1_b")) { *count = AI_FB_RN_C; return &m->rn_ref2_conv1_b[0]; }
  if (!strcmp(name, "rn_ref2_bn1_g")) { *count = AI_FB_RN_C; return &m->rn_ref2_bn1_g[0]; }
  if (!strcmp(name, "rn_ref2_bn1_b")) { *count = AI_FB_RN_C; return &m->rn_ref2_bn1_b[0]; }
  if (!strcmp(name, "rn_ref2_bn1_m")) { *count = AI_FB_RN_C; return &m->rn_ref2_bn1_m[0]; }
  if (!strcmp(name, "rn_ref2_bn1_v")) { *count = AI_FB_RN_C; return &m->rn_ref2_bn1_v[0]; }
  if (!strcmp(name, "rn_ref2_conv2_w")) { *count = AI_FB_RN_C * AI_FB_RN_C * AI_FB_RN_K * AI_FB_RN_K; return &m->rn_ref2_conv2_w[0][0][0][0]; }
  if (!strcmp(name, "rn_ref2_conv2_b")) { *count = AI_FB_RN_C; return &m->rn_ref2_conv2_b[0]; }
  if (!strcmp(name, "rn_ref2_bn2_g")) { *count = AI_FB_RN_C; return &m->rn_ref2_bn2_g[0]; }
  if (!strcmp(name, "rn_ref2_bn2_b")) { *count = AI_FB_RN_C; return &m->rn_ref2_bn2_b[0]; }
  if (!strcmp(name, "rn_ref2_bn2_m")) { *count = AI_FB_RN_C; return &m->rn_ref2_bn2_m[0]; }
  if (!strcmp(name, "rn_ref2_bn2_v")) { *count = AI_FB_RN_C; return &m->rn_ref2_bn2_v[0]; }
  if (!strcmp(name, "rn_dec_out_w")) { *count = AI_FB_RN_CH * AI_FB_RN_C * AI_FB_RN_K * AI_FB_RN_K; return &m->rn_dec_out_w[0][0][0][0]; }
  if (!strcmp(name, "rn_dec_out_b")) { *count = AI_FB_RN_CH; return &m->rn_dec_out_b[0]; }
  return NULL;
}

static bool load_model_file(const char *path, ai_fb_model_t *m)
{
  FILE *f = fopen(path, "r");
  if (!f)
    return false;
  char line[16384];
  if (!fgets(line, sizeof(line), f)) {
    fclose(f);
    return false;
  }
  if (strncmp(line, "AI_FB_MODEL_TXT_V1", 18) != 0 && strncmp(line, "AI_FB_MODEL_TXT_V2_ANGULAR_DELAY", 32) != 0
      && strncmp(line, "AI_FB_MODEL_TXT_V3_ANGULAR_REFINENET", 36) != 0) {
    fclose(f);
    return false;
  }
  while (fgets(line, sizeof(line), f)) {
    if (line[0] == '#' || line[0] == '\n')
      continue;
    char *save = NULL;
    char *name = strtok_r(line, " \t\r\n", &save);
    char *cnts = strtok_r(NULL, " \t\r\n", &save);
    if (!name || !cnts)
      continue;
    int expected = 0;
    float *dst = lookup_tensor(m, name, &expected);
    if (!dst)
      continue;
    int provided = atoi(cnts);
    if (provided != expected)
      continue;
    for (int i = 0; i < expected; i++) {
      char *v = strtok_r(NULL, " \t\r\n", &save);
      if (!v) {
        fclose(f);
        return false;
      }
      dst[i] = (float)atof(v);
    }
  }
  fclose(f);
  return true;
}

static bool load_model_file_bin(const char *path, ai_fb_model_t *m)
{
  FILE *f = fopen(path, "rb");
  if (!f)
    return false;
  uint32_t magic = 0, version = 0, tensor_count = 0;
  if (fread(&magic, sizeof(magic), 1, f) != 1 ||
      fread(&version, sizeof(version), 1, f) != 1 ||
      fread(&tensor_count, sizeof(tensor_count), 1, f) != 1) {
    fclose(f);
    return false;
  }
  if (magic != AI_FB_MODEL_BIN_MAGIC
      || (version != AI_FB_MODEL_BIN_VERSION_V1 && version != AI_FB_MODEL_BIN_VERSION_V2
          && version != AI_FB_MODEL_BIN_VERSION_V3)) {
    fclose(f);
    return false;
  }

  for (uint32_t t = 0; t < tensor_count; t++) {
    uint32_t name_len = 0, count = 0;
    if (fread(&name_len, sizeof(name_len), 1, f) != 1 ||
        fread(&count, sizeof(count), 1, f) != 1) {
      fclose(f);
      return false;
    }
    if (name_len == 0 || name_len > 128) {
      fclose(f);
      return false;
    }
    char name[129];
    if (fread(name, 1, name_len, f) != name_len) {
      fclose(f);
      return false;
    }
    name[name_len] = '\0';
    int expected = 0;
    float *dst = lookup_tensor(m, name, &expected);
    if (!dst || (uint32_t)expected != count) {
      fclose(f);
      return false;
    }
    if (fread(dst, sizeof(float), count, f) != count) {
      fclose(f);
      return false;
    }
  }

  fclose(f);
  return true;
}

static bool load_model_auto(const char *path, ai_fb_model_t *m)
{
  const char *ext = strrchr(path, '.');
  if (ext && !strcmp(ext, ".bin"))
    return load_model_file_bin(path, m);
  if (ext && !strcmp(ext, ".txt"))
    return load_model_file(path, m);
  if (load_model_file_bin(path, m))
    return true;
  return load_model_file(path, m);
}

const ai_fb_model_t *ai_fb_get_model(ai_fb_impl_mode_t mode)
{
  if (mode != AI_FB_IMPL_MODEL_STUB && mode != AI_FB_IMPL_ANGULAR_DELAY_MLP && !ai_fb_impl_is_refinenet(mode)) {
    load_defaults(&g_model);
    g_loaded = true;
    return &g_model;
  }

  if (!g_attempted) {
    g_attempted = true;
    load_defaults(&g_model);
    const int backend = get_softmodem_params()->ai_fb_model_backend;
    if (mode == AI_FB_IMPL_MODEL_STUB && backend == 1) {
      const char *enc = get_softmodem_params()->ai_fb_onnx_enc_path;
      const char *dec = get_softmodem_params()->ai_fb_onnx_dec_path;
      if (!ai_fb_onnx_init_from_paths(enc, dec))
        LOG_W(NR_MAC, "AI FB ONNX init failed; fallback to native loader path\n");
      else
        LOG_I(NR_MAC, "AI FB ONNX backend initialized\n");
    } else if (mode == AI_FB_IMPL_MODEL_STUB && backend == 2) {
      LOG_W(NR_MAC, "AI FB model backend TFLite selected but not linked yet; falling back to native loader\n");
    }
    const char *path = get_softmodem_params()->ai_fb_model_path;
    if (path && path[0] != '\0' && load_model_auto(path, &g_model)) {
      g_loaded = true;
      LOG_I(NR_MAC, "AI FB model loaded from file: %s (mode=%d)\n", path, mode);
    } else {
      g_loaded = false;
      LOG_W(NR_MAC, "AI FB model load failed (mode=%d), using compiled defaults\n", mode);
    }
  }
  return &g_model;
}

bool ai_fb_model_available(ai_fb_impl_mode_t mode)
{
  (void)ai_fb_get_model(mode);
  if (mode != AI_FB_IMPL_MODEL_STUB && mode != AI_FB_IMPL_ANGULAR_DELAY_MLP && !ai_fb_impl_is_refinenet(mode))
    return true;
  if (get_softmodem_params()->ai_fb_model_backend == 1)
    return ai_fb_onnx_sessions_ready();
  return g_loaded;
}

int ai_fb_model_backend(void)
{
  return get_softmodem_params()->ai_fb_model_backend;
}
