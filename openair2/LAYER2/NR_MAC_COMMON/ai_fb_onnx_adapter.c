#include "openair2/LAYER2/NR_MAC_COMMON/ai_fb_onnx_adapter.h"

#include <string.h>

#include "common/utils/LOG/log.h"

#ifdef OAI_AI_FB_ONNX_ENABLED
#include <onnxruntime_c_api.h>

static const OrtApi *g_ort = NULL;
static OrtEnv *g_env = NULL;
static OrtSessionOptions *g_opts = NULL;
static OrtSession *g_enc = NULL;
static OrtSession *g_dec = NULL;
static bool g_ready = false;

static bool check_status(OrtStatus *st, const char *what)
{
  if (!st)
    return true;
  LOG_W(NR_MAC, "AI FB ONNX %s failed: %s\n", what, g_ort->GetErrorMessage(st));
  g_ort->ReleaseStatus(st);
  return false;
}

static bool infer1(OrtSession *sess, const float *input, int in_dim, float *output, int out_dim)
{
  if (!sess || !g_ready)
    return false;
  OrtMemoryInfo *mem = NULL;
  OrtValue *x = NULL, *y = NULL;
  bool ok = false;
  int64_t shape[2] = {1, in_dim};
  if (!check_status(g_ort->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &mem), "CreateCpuMemoryInfo"))
    goto done;
  if (!check_status(g_ort->CreateTensorWithDataAsOrtValue(mem,
                                                           (void *)input,
                                                           (size_t)in_dim * sizeof(float),
                                                           shape,
                                                           2,
                                                           ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
                                                           &x),
                    "CreateTensorWithDataAsOrtValue"))
    goto done;

  OrtAllocator *allocator = NULL;
  if (!check_status(g_ort->GetAllocatorWithDefaultOptions(&allocator), "GetAllocatorWithDefaultOptions"))
    goto done;
  char *in_name = NULL;
  char *out_name = NULL;
  if (!check_status(g_ort->SessionGetInputName(sess, 0, allocator, &in_name), "SessionGetInputName"))
    goto done;
  if (!check_status(g_ort->SessionGetOutputName(sess, 0, allocator, &out_name), "SessionGetOutputName"))
    goto done;
  const char *ins[1] = {in_name};
  const char *outs[1] = {out_name};
  if (!check_status(g_ort->Run(sess, NULL, ins, (const OrtValue *const *)&x, 1, outs, 1, &y), "Run"))
    goto done;
  float *out_ptr = NULL;
  if (!check_status(g_ort->GetTensorMutableData(y, (void **)&out_ptr), "GetTensorMutableData"))
    goto done;
  memcpy(output, out_ptr, (size_t)out_dim * sizeof(float));
  ok = true;
done:
  if (y)
    g_ort->ReleaseValue(y);
  if (x)
    g_ort->ReleaseValue(x);
  if (mem)
    g_ort->ReleaseMemoryInfo(mem);
  return ok;
}

bool ai_fb_onnx_init_from_paths(const char *enc_path, const char *dec_path)
{
  if (g_ready)
    return true;
  if (!enc_path || !enc_path[0] || !dec_path || !dec_path[0]) {
    LOG_W(NR_MAC, "AI FB ONNX paths are not set\n");
    return false;
  }
  g_ort = OrtGetApiBase()->GetApi(ORT_API_VERSION);
  if (!g_ort)
    return false;
  if (!check_status(g_ort->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "ai_fb_onnx", &g_env), "CreateEnv"))
    return false;
  if (!check_status(g_ort->CreateSessionOptions(&g_opts), "CreateSessionOptions"))
    return false;
  if (!check_status(g_ort->CreateSession(g_env, enc_path, g_opts, &g_enc), "CreateSession(encoder)"))
    return false;
  if (!check_status(g_ort->CreateSession(g_env, dec_path, g_opts, &g_dec), "CreateSession(decoder)"))
    return false;
  g_ready = true;
  LOG_I(NR_MAC, "AI FB ONNX sessions initialized\n");
  return true;
}

bool ai_fb_onnx_sessions_ready(void) { return g_ready; }
bool ai_fb_onnx_infer_encoder_4p(const float in_x[8], float out_z[6]) { return infer1(g_enc, in_x, 8, out_z, 6); }
bool ai_fb_onnx_infer_encoder_2p(const float in_x[4], float out_z[6]) { return infer1(g_enc, in_x, 4, out_z, 6); }
bool ai_fb_onnx_infer_decoder_4p(const float in_z[6], float out_x[8]) { return infer1(g_dec, in_z, 6, out_x, 8); }
bool ai_fb_onnx_infer_decoder_2p(const float in_z[6], float out_x[4]) { return infer1(g_dec, in_z, 6, out_x, 4); }

#else
bool ai_fb_onnx_init_from_paths(const char *enc_path, const char *dec_path)
{
  (void)enc_path;
  (void)dec_path;
  LOG_W(NR_MAC, "AI FB ONNX backend requested but not compiled (OAI_AI_FB_ONNX=OFF)\n");
  return false;
}
bool ai_fb_onnx_sessions_ready(void) { return false; }
bool ai_fb_onnx_infer_encoder_4p(const float in_x[8], float out_z[6]) { (void)in_x; (void)out_z; return false; }
bool ai_fb_onnx_infer_encoder_2p(const float in_x[4], float out_z[6]) { (void)in_x; (void)out_z; return false; }
bool ai_fb_onnx_infer_decoder_4p(const float in_z[6], float out_x[8]) { (void)in_z; (void)out_x; return false; }
bool ai_fb_onnx_infer_decoder_2p(const float in_z[6], float out_x[4]) { (void)in_z; (void)out_x; return false; }
#endif
