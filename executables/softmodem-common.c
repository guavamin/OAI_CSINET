/*
 * Licensed to the OpenAirInterface (OAI) Software Alliance under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The OpenAirInterface Software Alliance licenses this file to You under
 * the OAI Public License, Version 1.1  (the "License"); you may not use this file
 * except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.openairinterface.org/?page_id=698
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *-------------------------------------------------------------------------------
 * For more information about the OpenAirInterface (OAI) Software Alliance:
 *      contact@openairinterface.org
 */

/*! \file softmodem-common.c
 * \brief common code for 5G and LTE softmodem main xNB and UEs source (nr-softmodem.c, lte-softmodem.c...)
 * \author Nokia BellLabs France, francois Taburet
 * \date 2020
 * \version 0.1
 * \company Nokia BellLabs France
 * \email: francois.taburet@nokia-bell-labs.com
 * \note
 * \warning
 */
#include <time.h>
#include <dlfcn.h>
#include <sys/resource.h>
#include "UTIL/OPT/opt.h"
#include "common/config/config_userapi.h"
#include "common/utils/load_module_shlib.h"
#include "common/utils/telnetsrv/telnetsrv.h"
#include "executables/thread-common.h"
#include "common/utils/LOG/log.h"
#include "softmodem-common.h"
#include "nfapi/oai_integration/vendor_ext.h"
#include "openair2/LAYER2/NR_MAC_COMMON/ai_fb_common.h"

char *parallel_config=NULL;
char *worker_config=NULL;
int usrp_tx_thread = 0;
uint8_t nfapi_mode=0;

static struct timespec start;

static softmodem_params_t softmodem_params;
softmodem_params_t *get_softmodem_params(void) {
  return &softmodem_params;
}

const char *get_imscope_windows_filter(void)
{
  return get_softmodem_params()->imscope_windows;
}

char *get_softmodem_function(void)
{
  optmask_t fmask = *get_softmodem_optmask();
  if (fmask.bit.SOFTMODEM_ENB_BIT)
    return "enb";
  if (fmask.bit.SOFTMODEM_GNB_BIT)
    return "gnb";
  if (fmask.bit.SOFTMODEM_4GUE_BIT)
    return "4Gue";
  if (fmask.bit.SOFTMODEM_5GUE_BIT)
    return "5Gue";
  return "???";
}

void get_common_options(configmodule_interface_t *cfg)
{
  int32_t stats_disabled = 0;
  uint32_t online_log_messages=0;
  uint32_t glog_level=0 ;
  uint32_t start_telnetsrv = 0, start_telnetclt = 0;
  uint32_t start_websrv = 0;
  uint32_t noS1 = 0, nonbiot = 0;
  uint32_t rfsim = 0, do_forms = 0;
  uint32_t enable_imscope = 0;
  uint32_t enable_imscope_record = 0;
  int nfapi_index = 0;
  char *logmem_filename = NULL;

  paramdef_t cmdline_params[] = CMDLINE_PARAMS_DESC;
  checkedparam_t cmdline_CheckParams[] = CMDLINE_PARAMS_CHECK_DESC;
  static_assert(sizeofArray(cmdline_params) == sizeofArray(cmdline_CheckParams),
                "cmdline_params and cmdline_CheckParams should have the same size");
  int numparams = sizeofArray(cmdline_params);
  config_set_checkfunctions(cmdline_params, cmdline_CheckParams, numparams);
  config_get(cfg, cmdline_params, numparams, NULL);
  nfapi_index = config_paramidx_fromname(cmdline_params, numparams, "nfapi");
  AssertFatal(nfapi_index >= 0,"Index for nfapi config option not found!");
  nfapi_mode = config_get_processedint(cfg, &cmdline_params[nfapi_index]);

  paramdef_t cmdline_logparams[] =CMDLINE_LOGPARAMS_DESC ;
  checkedparam_t cmdline_log_CheckParams[] = CMDLINE_LOGPARAMS_CHECK_DESC;
  static_assert(sizeofArray(cmdline_logparams) == sizeofArray(cmdline_log_CheckParams),
                "cmdline_logparams and cmdline_log_CheckParams should have the same size");

  int numlogparams = sizeofArray(cmdline_logparams);
  config_set_checkfunctions(cmdline_logparams, cmdline_log_CheckParams, numlogparams);
  config_get(cfg, cmdline_logparams, numlogparams, NULL);

  if(config_isparamset(cmdline_logparams,config_paramidx_fromname(cmdline_logparams,numlogparams, CONFIG_FLOG_OPT))) {
    set_glog_onlinelog(online_log_messages);
  }

  if(config_isparamset(cmdline_logparams,config_paramidx_fromname(cmdline_logparams,numlogparams, CONFIG_LOGL_OPT))) {
    set_glog(glog_level);
  }

  if (start_telnetsrv) {
    load_module_shlib("telnetsrv",NULL,0,NULL);
  }
  
  if (start_telnetclt) {
    IS_SOFTMODEM_TELNETCLT = true;
  }

  if (logmem_filename != NULL && strlen(logmem_filename) > 0) {
    printf("Enabling OPT for log save at memory %s\n",logmem_filename);
    logInit_log_mem(logmem_filename);
  }

  if (noS1) {
    IS_SOFTMODEM_NOS1 = true;
  }

  if (nonbiot) {
    IS_SOFTMODEM_NONBIOT = true;
  }

  if (rfsim) {
    IS_SOFTMODEM_RFSIM = true;
  }

  if (do_forms) {
    IS_SOFTMODEM_DOSCOPE = true;
  }

  if (enable_imscope) {
    IS_SOFTMODEM_IMSCOPE_ENABLED = true;
  }

  if (enable_imscope_record) {
    IS_SOFTMODEM_IMSCOPE_RECORD_ENABLED = true;
  }

  if (start_websrv) {
    load_module_shlib("websrv", NULL, 0, NULL);
  }

  if(parallel_config != NULL) set_parallel_conf(parallel_config);

  if(worker_config != NULL)   set_worker_conf(worker_config);
  nfapi_setmode(nfapi_mode);
  if (stats_disabled)
    IS_SOFTMODEM_NOSTATS = true;
  AssertFatal(get_softmodem_params()->dl_ri_use_decoded == 0 || get_softmodem_params()->dl_ri_use_decoded == 1,
              "--dl-ri-use-decoded expects 0 or 1 (is %d)\n",
              get_softmodem_params()->dl_ri_use_decoded);
  LOG_I(UTIL,
        "DL RI policy: %s (--dl-ri-use-decoded=%d)\n",
        get_softmodem_params()->dl_ri_use_decoded ? "decoded RI" : "capped/scheduled RI",
        get_softmodem_params()->dl_ri_use_decoded);

  AssertFatal(get_softmodem_params()->print_csi_debug == 0 || get_softmodem_params()->print_csi_debug == 1,
              "--print-csi-debug expects 0 or 1 (is %d)\n",
              get_softmodem_params()->print_csi_debug);
  if (get_softmodem_params()->print_csi_debug)
    LOG_I(UTIL, "CSI debug prints enabled (--print-csi-debug=1)\n");
  AssertFatal(get_softmodem_params()->ai_fb_ulsch_enable == 0 || get_softmodem_params()->ai_fb_ulsch_enable == 1,
              "--ai-fb-ulsch-enable expects 0 or 1 (is %d)\n",
              get_softmodem_params()->ai_fb_ulsch_enable);
  if (get_softmodem_params()->ai_fb_ulsch_enable)
    LOG_I(UTIL, "Lab AI CSI UL-SCH path enabled (--ai-fb-ulsch-enable=1)\n");
  AssertFatal(get_softmodem_params()->ai_fb_impl_mode >= 0 && get_softmodem_params()->ai_fb_impl_mode <= 6,
              "--ai-fb-impl-mode expects 0,1,2,3,4,5,6 (is %d)\n",
              get_softmodem_params()->ai_fb_impl_mode);
  AssertFatal(get_softmodem_params()->ai_fb_force_rank1 == 0 || get_softmodem_params()->ai_fb_force_rank1 == 1,
              "--ai-fb-force-rank1 expects 0 or 1 (is %d)\n",
              get_softmodem_params()->ai_fb_force_rank1);
  if (ai_fb_impl_is_angular_delay((ai_fb_impl_mode_t)get_softmodem_params()->ai_fb_impl_mode)
      && get_softmodem_params()->ai_fb_force_rank1 != 0) {
    LOG_W(UTIL,
      "Lab AI FB angular-delay mode requires rank-2 capable legacy CSI decode; overriding --ai-fb-force-rank1 to 0\n");
    get_softmodem_params()->ai_fb_force_rank1 = 0;
  }
  AssertFatal(get_softmodem_params()->ai_fb_model_backend >= 0 && get_softmodem_params()->ai_fb_model_backend <= 2,
              "--ai-fb-model-backend expects 0,1,2 (is %d)\n",
              get_softmodem_params()->ai_fb_model_backend);
  AssertFatal(get_softmodem_params()->ai_fb_csinet_latent_bytes > 0,
              "--ai-fb-csinet-latent-bytes expects >0 (is %d)\n",
              get_softmodem_params()->ai_fb_csinet_latent_bytes);
  AssertFatal(get_softmodem_params()->ai_fb_compare_gating == 0 || get_softmodem_params()->ai_fb_compare_gating == 1,
              "--ai-fb-compare-gating expects 0 or 1 (is %d)\n",
              get_softmodem_params()->ai_fb_compare_gating);
  AssertFatal(get_softmodem_params()->ai_fb_compare_max_age_slots >= 0,
              "--ai-fb-compare-max-age-slots expects >=0 (is %d)\n",
              get_softmodem_params()->ai_fb_compare_max_age_slots);
  AssertFatal(get_softmodem_params()->ai_fb_bundled_ulsch_enable == 0 || get_softmodem_params()->ai_fb_bundled_ulsch_enable == 1,
              "--ai-fb-bundled-ulsch-enable expects 0 or 1 (is %d)\n",
              get_softmodem_params()->ai_fb_bundled_ulsch_enable);
  AssertFatal(get_softmodem_params()->ai_fb_eff_pmi_hyst >= 1,
              "--ai-fb-eff-pmi-hyst expects >=1 (is %d)\n",
              get_softmodem_params()->ai_fb_eff_pmi_hyst);
  AssertFatal(get_softmodem_params()->ai_fb_runtime_sched_mode >= 0 && get_softmodem_params()->ai_fb_runtime_sched_mode <= 2,
              "--ai-fb-runtime-sched-mode expects 0,1,2 (is %d)\n",
              get_softmodem_params()->ai_fb_runtime_sched_mode);
  AssertFatal(get_softmodem_params()->ai_fb_runtime_sched_max_age_slots >= 0,
              "--ai-fb-runtime-sched-max-age-slots expects >=0 (is %d)\n",
              get_softmodem_params()->ai_fb_runtime_sched_max_age_slots);
  AssertFatal(get_softmodem_params()->ai_fb_runtime_sched_require_full == 0 || get_softmodem_params()->ai_fb_runtime_sched_require_full == 1,
              "--ai-fb-runtime-sched-require-full expects 0 or 1 (is %d)\n",
              get_softmodem_params()->ai_fb_runtime_sched_require_full);
  AssertFatal(get_softmodem_params()->ai_fb_runtime_log_enable == 0 || get_softmodem_params()->ai_fb_runtime_log_enable == 1,
              "--ai-fb-runtime-log-enable expects 0 or 1 (is %d)\n",
              get_softmodem_params()->ai_fb_runtime_log_enable);
  AssertFatal(get_softmodem_params()->ai_fb_runtime_log_period_frames >= 0,
              "--ai-fb-runtime-log-period-frames expects >=0 (is %d)\n",
              get_softmodem_params()->ai_fb_runtime_log_period_frames);
  AssertFatal(get_softmodem_params()->ai_fb_pucch_replace == 0 || get_softmodem_params()->ai_fb_pucch_replace == 1,
              "--ai-fb-pucch-replace expects 0 or 1 (is %d)\n",
              get_softmodem_params()->ai_fb_pucch_replace);
  if (get_softmodem_params()->ai_fb_pucch_replace)
    LOG_I(UTIL, "Lab AI CSI PUCCH replacement enabled (--ai-fb-pucch-replace=1): legacy CSI on PUCCH replaced by AI payload, carried over PUCCH Format 2 with default PRB count\n");
  AssertFatal(get_softmodem_params()->srs_imscope_log_enable == 0 || get_softmodem_params()->srs_imscope_log_enable == 1,
              "--srs-imscope-log-enable expects 0 or 1 (is %d)\n",
              get_softmodem_params()->srs_imscope_log_enable);
  AssertFatal(get_softmodem_params()->csi_i2_hyst_threshold >= 0,
              "--csi-i2-hyst-threshold expects >=0 (is %d)\n",
              get_softmodem_params()->csi_i2_hyst_threshold);
  AssertFatal(get_softmodem_params()->csi_i2_hyst_window >= 0,
              "--csi-i2-hyst-window expects >=0 (is %d)\n",
              get_softmodem_params()->csi_i2_hyst_window);
  if (get_softmodem_params()->ai_fb_ulsch_enable) {
    const char *impl = "matrix";
    if (get_softmodem_params()->ai_fb_impl_mode == 1)
      impl = "mlp-stub";
    else if (get_softmodem_params()->ai_fb_impl_mode == 2)
      impl = "model-stub";
    else if (get_softmodem_params()->ai_fb_impl_mode == 3)
      impl = "csinet";
    else if (get_softmodem_params()->ai_fb_impl_mode == 4)
      impl = "angular-delay-mlp";
    else if (get_softmodem_params()->ai_fb_impl_mode == 5)
      impl = "angular-delay-refinenet";
    else if (get_softmodem_params()->ai_fb_impl_mode == 6)
      impl = "angular-delay-refinenet-legacy-ri-cqi";
    LOG_I(UTIL, "Lab AI FB impl mode: %s (--ai-fb-impl-mode=%d)\n", impl, get_softmodem_params()->ai_fb_impl_mode);
    LOG_I(UTIL,
          "Lab AI FB rank policy: %s (--ai-fb-force-rank1=%d)\n",
          get_softmodem_params()->ai_fb_force_rank1 ? "forced rank-1" : "normal multi-rank",
          get_softmodem_params()->ai_fb_force_rank1);
    LOG_I(UTIL,
          "Lab AI FB compare gating: %s (--ai-fb-compare-gating=%d, --ai-fb-compare-max-age-slots=%d)\n",
          get_softmodem_params()->ai_fb_compare_gating ? "enabled" : "disabled",
          get_softmodem_params()->ai_fb_compare_gating,
          get_softmodem_params()->ai_fb_compare_max_age_slots);
    LOG_I(UTIL,
          "Lab AI FB bundled UL-SCH CE: %s (--ai-fb-bundled-ulsch-enable=%d)\n",
          get_softmodem_params()->ai_fb_bundled_ulsch_enable ? "enabled" : "disabled",
          get_softmodem_params()->ai_fb_bundled_ulsch_enable);
    LOG_I(UTIL,
          "Lab AI FB effective PMI hysteresis: %d (--ai-fb-eff-pmi-hyst=%d)\n",
          get_softmodem_params()->ai_fb_eff_pmi_hyst,
          get_softmodem_params()->ai_fb_eff_pmi_hyst);
    LOG_I(UTIL,
          "Lab AI FB runtime scheduling mode: %d (--ai-fb-runtime-sched-mode=%d, --ai-fb-runtime-sched-max-age-slots=%d, --ai-fb-runtime-sched-require-full=%d, --ai-fb-runtime-log-enable=%d, --ai-fb-runtime-log-period-frames=%d)\n",
          get_softmodem_params()->ai_fb_runtime_sched_mode,
          get_softmodem_params()->ai_fb_runtime_sched_mode,
          get_softmodem_params()->ai_fb_runtime_sched_max_age_slots,
          get_softmodem_params()->ai_fb_runtime_sched_require_full,
          get_softmodem_params()->ai_fb_runtime_log_enable,
          get_softmodem_params()->ai_fb_runtime_log_period_frames);
    LOG_I(UTIL,
          "UE CSI i2 smoothing knobs: threshold=%d (--csi-i2-hyst-threshold), window=%d (--csi-i2-hyst-window)\n",
          get_softmodem_params()->csi_i2_hyst_threshold,
          get_softmodem_params()->csi_i2_hyst_window);
    LOG_I(UTIL,
          "SRS/imscope logs: %s (--srs-imscope-log-enable=%d)\n",
          get_softmodem_params()->srs_imscope_log_enable ? "enabled" : "disabled",
          get_softmodem_params()->srs_imscope_log_enable);
    if (get_softmodem_params()->ai_fb_impl_mode == 2) {
      const char *mp = get_softmodem_params()->ai_fb_model_path;
      const char *encp = get_softmodem_params()->ai_fb_onnx_enc_path;
      const char *decp = get_softmodem_params()->ai_fb_onnx_dec_path;
      const char *backend = "native";
      if (get_softmodem_params()->ai_fb_model_backend == 1)
        backend = "onnx-stub";
      else if (get_softmodem_params()->ai_fb_model_backend == 2)
        backend = "tflite-stub";
      LOG_I(UTIL,
            "Lab AI FB model path: %s, backend: %s (--ai-fb-model-backend=%d)\n",
            (mp && mp[0] != '\0') ? mp : "(unset, will fallback to compiled MLP weights)",
            backend,
            get_softmodem_params()->ai_fb_model_backend);
      if (get_softmodem_params()->ai_fb_model_backend == 1) {
        LOG_I(UTIL,
              "Lab AI FB ONNX paths: enc=%s dec=%s\n",
              (encp && encp[0] != '\0') ? encp : "(unset)",
              (decp && decp[0] != '\0') ? decp : "(unset)");
      }
    }
    if (get_softmodem_params()->ai_fb_impl_mode == 3) {
      const char *cp = get_softmodem_params()->ai_fb_csinet_model_path;
      LOG_I(UTIL,
            "Lab AI FB CSINet model path: %s, requested latent bytes: %d (phase-1 transport fixed at 6 bytes)\n",
            (cp && cp[0] != '\0') ? cp : "(unset)",
            get_softmodem_params()->ai_fb_csinet_latent_bytes);
    }
    if (get_softmodem_params()->ai_fb_impl_mode == 4) {
      const char *mp = get_softmodem_params()->ai_fb_model_path;
      LOG_I(UTIL,
            "Lab AI FB angular-delay model path: %s (2D-DFT + 24-delay-row preprocessing enabled in UE runtime)\n",
            (mp && mp[0] != '\0') ? mp : "(unset, fallback to compiled angular-delay defaults)");
    }
    if (get_softmodem_params()->ai_fb_impl_mode == 5 || get_softmodem_params()->ai_fb_impl_mode == 6) {
      const char *mp = get_softmodem_params()->ai_fb_model_path;
      LOG_I(UTIL,
            "Lab AI FB angular-delay RefineNet model path: %s (conv+refinenet autoencoder over [24x4] angular-delay features%s)\n",
            (mp && mp[0] != '\0') ? mp : "(unset, fallback to compiled angular-delay defaults)",
            get_softmodem_params()->ai_fb_impl_mode == 6 ? ", legacy RI/CQI carried beside latent" : "");
    }
  }

  // To be removed in a future release (after 1 month)
  AssertFatal(get_softmodem_params()->default_pdu_session_id == -1,
              "Use uicc0.pdu_sessions.[0].id to change the requested PDU session ID (is %d)\n", get_softmodem_params()->default_pdu_session_id);
}

void softmodem_verify_mode(const softmodem_params_t *p)
{
  if (IS_SA_MODE(p)) {
    LOG_I(UTIL, "running in SA mode (no --phy-test, --do-ra, --nsa option present)\n");
    return;
  }

  if (p->phy_test)
    LOG_I(UTIL, "running in phy-test mode (--phy-test)\n");
  if (p->do_ra)
    LOG_I(UTIL, "running in do-ra mode (--do-ra)\n");
  if (p->nsa)
    LOG_I(UTIL, "running in NSA mode (--nsa)\n");
  int num_modes = p->phy_test + p->do_ra + p->nsa;
  AssertFatal(num_modes == 1, "--phy-test, --do-ra, and --nsa are mutually exclusive\n");
}

void softmodem_printresources(int sig, telnet_printfunc_t pf) {
  struct rusage usage;
  struct timespec stop;

  clock_gettime(CLOCK_BOOTTIME, &stop);

  uint64_t elapse = (stop.tv_sec - start.tv_sec) ;   // in seconds


  int st = getrusage(RUSAGE_SELF,&usage);
  if (!st) {
    pf("\nRun time: %lluh %llus\n",(unsigned long long)elapse/3600,(unsigned long long)(elapse - (elapse/3600)));
    pf("\tTime executing user inst.: %lds %ldus\n",(long)usage.ru_utime.tv_sec,(long)usage.ru_utime.tv_usec);
    pf("\tTime executing system inst.: %lds %ldus\n",(long)usage.ru_stime.tv_sec,(long)usage.ru_stime.tv_usec);
    pf("\tMax. Phy. memory usage: %ldkB\n",(long)usage.ru_maxrss);
    pf("\tPage fault number (no io): %ld\n",(long)usage.ru_minflt);
    pf("\tPage fault number (requiring io): %ld\n",(long)usage.ru_majflt);
    pf("\tNumber of file system read: %ld\n",(long)usage.ru_inblock);
    pf("\tNumber of filesystem write: %ld\n",(long)usage.ru_oublock);
    pf("\tNumber of context switch (process origin, io...): %ld\n",(long)usage.ru_nvcsw);
    pf("\tNumber of context switch (os origin, priority...): %ld\n",(long)usage.ru_nivcsw);
  }
}

void signal_handler(int sig) {
  //void *array[10];
  //size_t size;

  if (sig==SIGSEGV) {
    // get void*'s for all entries on the stack
    /* backtrace uses malloc, that is not good in signal handlers
     * I let the code, because it would be nice to make it better
    size = backtrace(array, 10);
    // print out all the frames to stderr
    fprintf(stderr, "Error: signal %d:\n", sig);
    backtrace_symbols_fd(array, size, 2);
    */
    exit(-1);
  } else {
    if(sig==SIGINT ||sig==SOFTMODEM_RTSIGNAL)
      softmodem_printresources(sig,(telnet_printfunc_t)printf);
    if (sig != SOFTMODEM_RTSIGNAL) {
      printf("Linux signal %s...\n",strsignal(sig));
      exit_function(__FILE__, __FUNCTION__, __LINE__, "softmodem starting exit procedure\n", OAI_EXIT_NORMAL);
    }
  }
}



void set_softmodem_sighandler(void) {
  struct sigaction  act,oldact;
  clock_gettime(CLOCK_BOOTTIME, &start);
  memset(&act,0,sizeof(act));
  act.sa_handler=signal_handler;
  sigaction(SOFTMODEM_RTSIGNAL,&act,&oldact);
  // Disabled in order generate a core dump for analysis with gdb
  // Enable for clean exit on CTRL-C (i.e. record player, USRP...) 
  signal(SIGINT,  signal_handler);
  # if 0
  printf("Send signal %d to display resource usage...\n",SIGRTMIN+1);
  signal(SIGSEGV, signal_handler);
  signal(SIGTERM, signal_handler);
  signal(SIGABRT, signal_handler);
  #endif
}
