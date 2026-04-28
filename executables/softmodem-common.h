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

/*! \file softmodem-common.h
 * \brief Top-level threads for eNodeB
 * \author
 * \date 2012
 * \version 0.1
 * \company Eurecom
 * \email:
 * \note
 * \warning
 */
#ifndef SOFTMODEM_COMMON_H
#define SOFTMODEM_COMMON_H
#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>
#include "common/config/config_load_configmodule.h"

/* help strings definition for command line options, used in CMDLINE_XXX_DESC macros and printed when -h option is used */
#define CONFIG_HLP_RFCFGF        "Configuration file for front-end (e.g. LMS7002M)\n"
#define CONFIG_HLP_TPOOL \
  "Thread pool configuration: \n\
  list of cores, comma separated (negative value is no core affinity)\n\
  example: -1,3 launches two working threads one floating, the second set on core 3\n\
  default 8 floating threads\n\
  use N for no pool (runs in calling thread) recommended with rfsim.\n"
#define CONFIG_HLP_CALUER        "set UE RX calibration\n"
#define CONFIG_HLP_CALUERM       ""
#define CONFIG_HLP_CALUERB       ""
#define CONFIG_HLP_DBGUEPR       "UE run normal prach power ramping, but don't continue random-access\n"
#define CONFIG_HLP_CALPRACH      "UE run normal prach with maximum power, but don't continue random-access\n"
#define CONFIG_HLP_NOL2CN        "bypass L2 and upper layers\n"


#define CONFIG_HLP_DUMPFRAME     "dump UE received frame to rxsig_frame0.dat and exit\n"
#define CONFIG_HLP_PHYTST        "test UE phy layer, mac disabled\n"
#define CONFIG_HLP_DORA          "test gNB  and UE with RA procedures\n"
#define CONFIG_HLP_SL_MODE       "sets the NR sidelink mode (0: not in sidelink mode, 1: in-coverage/gNB, 2: out-of-coverage/no gNB)\n"
#define CONFIG_HLP_CLK           "tells hardware to use a clock reference (0:internal, 1:external, 2:gpsdo)\n"
#define CONFIG_HLP_TME           "tells hardware to use a time reference (0:internal, 1:external, 2:gpsdo)\n"
#define CONFIG_HLP_TUNE_OFFSET   "LO tuning offset to use in Hz\n"
#define CONFIG_HLP_DLF           "Set the downlink frequency for all component carriers\n"
#define CONFIG_HLP_ULF           "Set the uplink frequency offset for all component carriers\n"
#define CONFIG_HLP_SLF           "Set the sidelink frequency for all component carriers\n"
#define CONFIG_HLP_CHOFF         "Channel id offset\n"
#define CONFIG_HLP_SOFTS         "Enable soft scope and L1 and L2 stats (Xforms)\n"
#define CONFIG_HLP_DLMCS         "Set the maximum downlink MCS\n"
#define CONFIG_HLP_STMON         "Enable processing timing measurement of lte softmodem on per subframe basis \n"
#define CONFIG_HLP_CHESTFREQ     "Set channel estimation type in frequency domain. 0-Linear interpolation (default). 1-PRB based averaging of channel estimates in frequency. \n"
#define CONFIG_HLP_CHESTTIME     "Set channel estimation type in time domain. 0-Symbols take estimates of the last preceding DMRS symbol (default). 1-Symbol based averaging of channel estimates in time. \n"
#define CONFIG_HLP_IMSCOPE       "Enable phy scope based on imgui and implot"
#define CONFIG_HLP_IMSCOPE_WINDOWS "Comma-separated list of imscope window titles to show; default: all windows. Only listed windows are shown (one app window, dockable). Use exact titles. UE: UE KPI, UE PDCCH IQ, UE PDCCH LLR, UE PDSCH IQ, UE PDSCH Chan est, UE PDSCH IQ before compensation, UE CSI-RS channel estimates, UE CSI-RS channel estimates (per RX-TX link), Time domain samples, Time domain samples - before sync, Broadcast channel. gNB: PUSCH SLOT IQ, PUSCH LLRs, Time domain samples, SRS channel estimates, SRS channel estimates (per RX-port link), CSI report parameters."
#define CONFIG_HLP_IMSCOPE_RECORD "Enable recording scope data to filesystem"
#define CONFIG_HLP_CSI_RECORD_GNB "gNB: directory to record CSI-RS scheduling and decoded CSI feedback (gnb_csirs_scheduling.csv, gnb_csi_feedback.csv). Disabled if unset.\n"

#define CONFIG_HLP_NONSTOP       "Go back to frame sync mode after 100 consecutive PBCH failures\n"

#define CONFIG_HLP_TQFS                                                                                                          \
  "Apply three-quarter of sampling frequency, (example 23.04 Msps for LTE 20MHz) to reduce the data rate on USB/PCIe transfers " \
  "(only valid for some bandwidths)\n"
#define CONFIG_HLP_TPORT         "tracer port\n"
#define CONFIG_HLP_NOTWAIT       "don't wait for tracer, start immediately\n"

#define CONFIG_HLP_NUMEROLOGY    "adding numerology for 5G\n"
#define CONFIG_HLP_BAND          "band index\n"
#define CONFIG_HLP_PARALLEL_CMD  "three config for level of parallelism 'PARALLEL_SINGLE_THREAD', 'PARALLEL_RU_L1_SPLIT', or 'PARALLEL_RU_L1_TRX_SPLIT'\n"
#define CONFIG_HLP_WORKER_CMD    "two option for worker 'WORKER_DISABLE' or 'WORKER_ENABLE'\n"
#define CONFIG_HLP_USRP_THREAD   "having extra thead for usrp tx\n"

#define CONFIG_HLP_NOS1          "Disable s1 interface\n"
#define CONFIG_HLP_RFSIM         "Run in rf simulator mode\n"
#define CONFIG_HLP_DISABLNBIOT   "disable nb-iot, even if defined in config\n"
#define CONFIG_HLP_USRP_THREAD   "having extra thead for usrp tx\n"
#define CONFIG_HLP_NFAPI         "Change the nFAPI mode for NR 'MONOLITHIC', 'PNF', 'VNF', 'AERIAL','UE_STUB_PNF','UE_STUB_OFFNET','STANDALONE_PNF'\n"
#define CONFIG_HLP_CONTINUOUS_TX "perform continuous transmission, even in TDD mode (to work around USRP issues)\n"
#define CONFIG_HLP_STATS_DISABLE "disable globally the stats generation and persistence"
#define CONFIG_HLP_NOITTI        "Do not start itti threads, call queue processing in place, inside the caller thread"
#define CONFIG_HLP_SYNC_REF      "UE acts a Sync Reference in Sidelink. 0-none 1-GNB 2-GNSS 4-localtiming\n"
#define CONFIG_HLP_TADV                                                                                                      \
  "Set RF board timing_advance to compensate fix delay inside the RF board between Rx and Tx timestamps (RF board internal " \
  "issues)\n"

/*-----------------------------------------------------------------------------------------------------------------------------------------------------*/
/*                                            command line parameters common to eNodeB and UE                                                          */
/*   optname                 helpstr                  paramflags      XXXptr                              defXXXval              type         numelt   */
/*-----------------------------------------------------------------------------------------------------------------------------------------------------*/
#define RF_CONFIG_FILE      softmodem_params.rf_config_file
#define TP_CONFIG           softmodem_params.threadPoolConfig
#define CONTINUOUS_TX       softmodem_params.continuous_tx
#define PHY_TEST            softmodem_params.phy_test
#define DO_RA               softmodem_params.do_ra
#define SL_MODE             softmodem_params.sl_mode
#define CHAIN_OFFSET        softmodem_params.chain_offset
#define NUMEROLOGY          softmodem_params.numerology
#define BAND                softmodem_params.band
#define CLOCK_SOURCE        softmodem_params.clock_source
#define TIMING_SOURCE       softmodem_params.timing_source
#define TUNE_OFFSET         softmodem_params.tune_offset
#define CHEST_FREQ          softmodem_params.chest_freq
#define CHEST_TIME          softmodem_params.chest_time
#define NFAPI               softmodem_params.nfapi
#define NSA                 softmodem_params.nsa
#define NODE_NUMBER         softmodem_params.node_number
#define NON_STOP            softmodem_params.non_stop
#define CONTINUOUS_TX       softmodem_params.continuous_tx
#define SYNC_REF            softmodem_params.sync_ref
#define DEFAULT_PDU_ID      softmodem_params.default_pdu_session_id
#define CSI_RECORD_PATH     softmodem_params.csi_record_path
#define IMSCOPE_WINDOWS     softmodem_params.imscope_windows
#define DL_RI_USE_DECODED   softmodem_params.dl_ri_use_decoded
#define PRINT_CSI_DEBUG     softmodem_params.print_csi_debug
#define AI_FB_ULSCH_ENABLE  softmodem_params.ai_fb_ulsch_enable
#define AI_FB_LOG_PATH      softmodem_params.ai_fb_log_path
#define AI_FB_IMPL_MODE     softmodem_params.ai_fb_impl_mode
#define AI_FB_FORCE_RANK1   softmodem_params.ai_fb_force_rank1
#define AI_FB_MODEL_PATH    softmodem_params.ai_fb_model_path
#define AI_FB_MODEL_BACKEND softmodem_params.ai_fb_model_backend
#define AI_FB_ONNX_ENC_PATH softmodem_params.ai_fb_onnx_enc_path
#define AI_FB_ONNX_DEC_PATH softmodem_params.ai_fb_onnx_dec_path
#define AI_FB_CSINET_MODEL_PATH softmodem_params.ai_fb_csinet_model_path
#define AI_FB_CSINET_LATENT_BYTES softmodem_params.ai_fb_csinet_latent_bytes
#define AI_FB_COMPARE_GATING softmodem_params.ai_fb_compare_gating
#define AI_FB_COMPARE_MAX_AGE_SLOTS softmodem_params.ai_fb_compare_max_age_slots
#define AI_FB_BUNDLED_ULSCH_ENABLE softmodem_params.ai_fb_bundled_ulsch_enable
#define AI_FB_EFF_PMI_HYST softmodem_params.ai_fb_eff_pmi_hyst
#define AI_FB_RUNTIME_SCHED_MODE softmodem_params.ai_fb_runtime_sched_mode
#define AI_FB_RUNTIME_SCHED_MAX_AGE_SLOTS softmodem_params.ai_fb_runtime_sched_max_age_slots
#define AI_FB_RUNTIME_SCHED_REQUIRE_FULL softmodem_params.ai_fb_runtime_sched_require_full
#define AI_FB_RUNTIME_LOG_ENABLE softmodem_params.ai_fb_runtime_log_enable
#define AI_FB_RUNTIME_LOG_PERIOD_FRAMES softmodem_params.ai_fb_runtime_log_period_frames
#define SRS_IMSCOPE_LOG_ENABLE softmodem_params.srs_imscope_log_enable
#define CSI_I2_HYST_THRESHOLD softmodem_params.csi_i2_hyst_threshold
#define CSI_I2_HYST_WINDOW softmodem_params.csi_i2_hyst_window

extern int usrp_tx_thread;
// clang-format off
#define CMDLINE_PARAMS_DESC {  \
  {"rf-config-file",        CONFIG_HLP_RFCFGF,        0,              .strptr=&RF_CONFIG_FILE,                .defstrval=NULL,          TYPE_STRING, 0},  \
  {"thread-pool",           CONFIG_HLP_TPOOL,         0,              .strptr=&TP_CONFIG,                     .defstrval="-1,-1,-1,-1,-1,-1,-1,-1",  TYPE_STRING, 0},     \
  {"phy-test",              CONFIG_HLP_PHYTST,        PARAMFLAG_BOOL, .iptr=&PHY_TEST,                        .defintval=0,             TYPE_INT,    0},  \
  {"do-ra",                 CONFIG_HLP_DORA,          PARAMFLAG_BOOL, .iptr=&DO_RA,                           .defintval=0,             TYPE_INT,    0},  \
  {"sl-mode",               CONFIG_HLP_SL_MODE,       0,              .u8ptr=&SL_MODE,                        .defintval=0,             TYPE_UINT8,  0},  \
  {"clock-source",          CONFIG_HLP_CLK,           0,              .uptr=&CLOCK_SOURCE,                    .defintval=0,             TYPE_UINT,   0},  \
  {"time-source",           CONFIG_HLP_TME,           0,              .uptr=&TIMING_SOURCE,                   .defintval=0,             TYPE_UINT,   0},  \
  {"tune-offset",           CONFIG_HLP_TUNE_OFFSET,   0,              .dblptr=&TUNE_OFFSET,                   .defintval=0,             TYPE_DOUBLE, 0},  \
  {"C" ,                    CONFIG_HLP_DLF,           0,              .u64ptr=&(downlink_frequency[0][0]),    .defint64val=0,           TYPE_UINT64, 0},  \
  {"CO" ,                   CONFIG_HLP_ULF,           0,              .iptr=&(uplink_frequency_offset[0][0]), .defintval=0,             TYPE_INT,    0},  \
  {"a" ,                    CONFIG_HLP_CHOFF,         0,              .iptr=&CHAIN_OFFSET,                    .defintval=0,             TYPE_INT,    0},  \
  {"d" ,                    CONFIG_HLP_SOFTS,         PARAMFLAG_BOOL, .uptr=&do_forms,                        .defintval=0,             TYPE_UINT,   0},  \
  {"q" ,                    CONFIG_HLP_STMON,         PARAMFLAG_BOOL, .iptr=&cpu_meas_enabled,                     .defintval=0,             TYPE_INT,    0},  \
  {"numerology" ,           CONFIG_HLP_NUMEROLOGY,    0,              .iptr=&NUMEROLOGY,                      .defintval=1,             TYPE_INT,    0},  \
  {"band" ,                 CONFIG_HLP_BAND,          0,              .iptr=&BAND,                            .defintval=78,            TYPE_INT,    0},  \
  {"parallel-config",       CONFIG_HLP_PARALLEL_CMD,  0,              .strptr=&parallel_config,               .defstrval=NULL,          TYPE_STRING, 0},  \
  {"worker-config",         CONFIG_HLP_WORKER_CMD,    0,              .strptr=&worker_config,                 .defstrval=NULL,          TYPE_STRING, 0},  \
  {"noS1",                  CONFIG_HLP_NOS1,          PARAMFLAG_BOOL, .uptr=&noS1,                            .defintval=0,             TYPE_UINT,   0},  \
  {"rfsim",                 CONFIG_HLP_RFSIM,         PARAMFLAG_BOOL, .uptr=&rfsim,                           .defintval=0,             TYPE_UINT,   0},  \
  {"nbiot-disable",         CONFIG_HLP_DISABLNBIOT,   PARAMFLAG_BOOL, .uptr=&nonbiot,                         .defuintval=0,            TYPE_UINT,   0},  \
  {"chest-freq",            CONFIG_HLP_CHESTFREQ,     0,              .iptr=&CHEST_FREQ,                      .defintval=0,             TYPE_INT,    0},  \
  {"chest-time",            CONFIG_HLP_CHESTTIME,     0,              .iptr=&CHEST_TIME,                      .defintval=0,             TYPE_INT,    0},  \
  {"nsa",                   CONFIG_HLP_NSA,           PARAMFLAG_BOOL, .iptr=&NSA,                             .defintval=0,             TYPE_INT,    0},  \
  {"node-number",           NULL,                     0,              .u16ptr=&NODE_NUMBER,                   .defuintval=0,            TYPE_UINT16, 0},  \
  {"usrp-tx-thread-config", CONFIG_HLP_USRP_THREAD,   0,              .iptr=&usrp_tx_thread,                  .defstrval=0,             TYPE_INT,    0},  \
  {"nfapi",                 CONFIG_HLP_NFAPI,         0,              .strptr=NULL,                           .defstrval="MONOLITHIC",  TYPE_STRING, 0},  \
  {"non-stop",              CONFIG_HLP_NONSTOP,       PARAMFLAG_BOOL, .iptr=&NON_STOP,                        .defintval=0,             TYPE_INT,    0},  \
  {"continuous-tx",         CONFIG_HLP_CONTINUOUS_TX, PARAMFLAG_BOOL, .iptr=&CONTINUOUS_TX,                   .defintval=0,             TYPE_INT,    0},  \
  {"disable-stats",         CONFIG_HLP_STATS_DISABLE, PARAMFLAG_BOOL, .iptr=&stats_disabled,                  .defintval=0,             TYPE_INT,    0},  \
  {"no-itti-threads",       CONFIG_HLP_NOITTI,        PARAMFLAG_BOOL, .iptr=&softmodem_params.no_itti,        .defintval=0,             TYPE_INT,    0},  \
  {"sync-ref",              CONFIG_HLP_SYNC_REF,      0,              .uptr=&SYNC_REF,                        .defintval=0,             TYPE_UINT,   0},  \
  {"A" ,                    CONFIG_HLP_TADV,          0,             .iptr=&softmodem_params.command_line_sample_advance,.defintval=0,            TYPE_INT,   0},  \
  {"E" ,                    CONFIG_HLP_TQFS,          PARAMFLAG_BOOL, .iptr=&softmodem_params.threequarter_fs, .defintval=0,            TYPE_INT,    0}, \
  {"imscope" ,              CONFIG_HLP_IMSCOPE,       PARAMFLAG_BOOL, .uptr=&enable_imscope,                   .defintval=0,            TYPE_UINT,   0}, \
  {"imscope-windows" ,      CONFIG_HLP_IMSCOPE_WINDOWS, 0,           .strptr=&IMSCOPE_WINDOWS,                .defstrval=NULL,         TYPE_STRING, 0}, \
  {"imscope-record" ,       CONFIG_HLP_IMSCOPE_RECORD,PARAMFLAG_BOOL, .uptr=&enable_imscope_record,            .defintval=0,            TYPE_UINT,   0}, \
  {"default-pdu-id",        NULL,                     0,              .iptr=&DEFAULT_PDU_ID,                   .defintval=-1,           TYPE_INT,    0}, \
  {"csi-record-path",      CONFIG_HLP_CSI_RECORD_GNB, 0,              .strptr=&CSI_RECORD_PATH,                .defstrval=NULL,         TYPE_STRING, 0}, \
  {"dl-ri-use-decoded",    CONFIG_HLP_DL_RI_USE_DECODED, 0,           .iptr=&DL_RI_USE_DECODED,                .defintval=0,            TYPE_INT,    0}, \
  {"print-csi-debug",     CONFIG_HLP_PRINT_CSI_DEBUG, PARAMFLAG_BOOL, .iptr=&PRINT_CSI_DEBUG,                .defintval=0,            TYPE_INT,    0}, \
  {"ai-fb-ulsch-enable",  CONFIG_HLP_AI_FB_ULSCH_ENABLE, PARAMFLAG_BOOL, .iptr=&AI_FB_ULSCH_ENABLE,          .defintval=0,            TYPE_INT,    0}, \
  {"ai-fb-log-path",      CONFIG_HLP_AI_FB_LOG_PATH, 0,               .strptr=&AI_FB_LOG_PATH,                .defstrval=NULL,         TYPE_STRING, 0}, \
  {"ai-fb-impl-mode",     CONFIG_HLP_AI_FB_IMPL_MODE, 0,              .iptr=&AI_FB_IMPL_MODE,                 .defintval=0,            TYPE_INT,    0}, \
  {"ai-fb-force-rank1",   CONFIG_HLP_AI_FB_FORCE_RANK1, PARAMFLAG_BOOL, .iptr=&AI_FB_FORCE_RANK1,            .defintval=0,            TYPE_INT,    0}, \
  {"ai-fb-model-path",    CONFIG_HLP_AI_FB_MODEL_PATH, 0,             .strptr=&AI_FB_MODEL_PATH,              .defstrval=NULL,         TYPE_STRING, 0}, \
  {"ai-fb-model-backend", CONFIG_HLP_AI_FB_MODEL_BACKEND, 0,          .iptr=&AI_FB_MODEL_BACKEND,             .defintval=0,            TYPE_INT,    0}, \
  {"ai-fb-onnx-enc-path", CONFIG_HLP_AI_FB_ONNX_ENC_PATH, 0,          .strptr=&AI_FB_ONNX_ENC_PATH,           .defstrval=NULL,         TYPE_STRING, 0}, \
  {"ai-fb-onnx-dec-path", CONFIG_HLP_AI_FB_ONNX_DEC_PATH, 0,          .strptr=&AI_FB_ONNX_DEC_PATH,           .defstrval=NULL,         TYPE_STRING, 0}, \
  {"ai-fb-csinet-model-path", CONFIG_HLP_AI_FB_CSINET_MODEL_PATH, 0,  .strptr=&AI_FB_CSINET_MODEL_PATH,       .defstrval=NULL,         TYPE_STRING, 0}, \
  {"ai-fb-csinet-latent-bytes", CONFIG_HLP_AI_FB_CSINET_LATENT_BYTES, 0, .iptr=&AI_FB_CSINET_LATENT_BYTES,   .defintval=6,            TYPE_INT,    0}, \
  {"ai-fb-compare-gating", CONFIG_HLP_AI_FB_COMPARE_GATING, 0,        .iptr=&AI_FB_COMPARE_GATING,            .defintval=1,            TYPE_INT,    0}, \
  {"ai-fb-compare-max-age-slots", CONFIG_HLP_AI_FB_COMPARE_MAX_AGE_SLOTS, 0, .iptr=&AI_FB_COMPARE_MAX_AGE_SLOTS, .defintval=2,         TYPE_INT,    0}, \
  {"ai-fb-bundled-ulsch-enable", CONFIG_HLP_AI_FB_BUNDLED_ULSCH_ENABLE, PARAMFLAG_BOOL, .iptr=&AI_FB_BUNDLED_ULSCH_ENABLE, .defintval=0, TYPE_INT, 0}, \
  {"ai-fb-eff-pmi-hyst", CONFIG_HLP_AI_FB_EFF_PMI_HYST, 0, .iptr=&AI_FB_EFF_PMI_HYST, .defintval=3, TYPE_INT, 0}, \
  {"ai-fb-runtime-sched-mode", CONFIG_HLP_AI_FB_RUNTIME_SCHED_MODE, 0, .iptr=&AI_FB_RUNTIME_SCHED_MODE, .defintval=0, TYPE_INT, 0}, \
  {"ai-fb-runtime-sched-max-age-slots", CONFIG_HLP_AI_FB_RUNTIME_SCHED_MAX_AGE_SLOTS, 0, .iptr=&AI_FB_RUNTIME_SCHED_MAX_AGE_SLOTS, .defintval=2, TYPE_INT, 0}, \
  {"ai-fb-runtime-sched-require-full", CONFIG_HLP_AI_FB_RUNTIME_SCHED_REQUIRE_FULL, PARAMFLAG_BOOL, .iptr=&AI_FB_RUNTIME_SCHED_REQUIRE_FULL, .defintval=1, TYPE_INT, 0}, \
  {"ai-fb-runtime-log-enable", CONFIG_HLP_AI_FB_RUNTIME_LOG_ENABLE, PARAMFLAG_BOOL, .iptr=&AI_FB_RUNTIME_LOG_ENABLE, .defintval=0, TYPE_INT, 0}, \
  {"ai-fb-runtime-log-period-frames", CONFIG_HLP_AI_FB_RUNTIME_LOG_PERIOD_FRAMES, 0, .iptr=&AI_FB_RUNTIME_LOG_PERIOD_FRAMES, .defintval=100, TYPE_INT, 0}, \
  {"srs-imscope-log-enable", CONFIG_HLP_SRS_IMSCOPE_LOG_ENABLE, PARAMFLAG_BOOL, .iptr=&SRS_IMSCOPE_LOG_ENABLE, .defintval=0, TYPE_INT, 0}, \
  {"csi-i2-hyst-threshold", CONFIG_HLP_CSI_I2_HYST_THRESHOLD, 0, .iptr=&CSI_I2_HYST_THRESHOLD, .defintval=0, TYPE_INT, 0}, \
  {"csi-i2-hyst-window", CONFIG_HLP_CSI_I2_HYST_WINDOW, 0, .iptr=&CSI_I2_HYST_WINDOW, .defintval=0, TYPE_INT, 0}, \
}
// clang-format on

/* CMDLINE_PARAMS_CHECK_DESC: must have exactly as many entries as CMDLINE_PARAMS_DESC, in the same order.
 * Entry i is the check for param i. Use { .s5 = { NULL } } for no check; the only non-NULL check here is
 * .s3a for "nfapi" (param index 25). Wrong count → static_assert fail at build; wrong order → segfault in
 * config_checkstr_assign_integer. See doc/SOFTMODEM_CMDLINE_PARAMS.md when adding params. */
// clang-format off
#define CMDLINE_PARAMS_CHECK_DESC {         \
    { .s5 = { NULL } },                     \
    { .s5 = { NULL } },                     \
    { .s5 = { NULL } },                     \
    { .s5 = { NULL } },                     \
    { .s5 = { NULL } },                     \
    { .s5 = { NULL } },                     \
    { .s5 = { NULL } },                     \
    { .s5 = { NULL } },                     \
    { .s5 = { NULL } },                     \
    { .s5 = { NULL } },                     \
    { .s5 = { NULL } },                     \
    { .s5 = { NULL } },                     \
    { .s5 = { NULL } },                     \
    { .s5 = { NULL } },                     \
    { .s5 = { NULL } },                     \
    { .s5 = { NULL } },                     \
    { .s5 = { NULL } },                     \
    { .s5 = { NULL } },                     \
    { .s5 = { NULL } },                     \
    { .s5 = { NULL } },                     \
    { .s5 = { NULL } },                     \
    { .s5 = { NULL } },                     \
    { .s5 = { NULL } },                     \
    { .s5 = { NULL } },                     \
    { .s5 = { NULL } },                     \
    { .s3a = { config_checkstr_assign_integer, \
               {"MONOLITHIC", "PNF", "VNF", "AERIAL","UE_STUB_PNF","UE_STUB_OFFNET","STANDALONE_PNF"}, \
               {NFAPI_MONOLITHIC, NFAPI_MODE_PNF, NFAPI_MODE_VNF, NFAPI_MODE_AERIAL,NFAPI_UE_STUB_PNF,NFAPI_UE_STUB_OFFNET,NFAPI_MODE_STANDALONE_PNF}, \
               7 } }, \
    { .s5 = { NULL } },                     \
    { .s5 = { NULL } },                     \
    { .s5 = { NULL } },                     \
    { .s5 = { NULL } },                     \
    { .s5 = { NULL } },                     \
    { .s5 = { NULL } },                     \
    { .s5 = { NULL } },                     \
    { .s5 = { NULL } },                     \
    { .s5 = { NULL } },                     \
    { .s5 = { NULL } },                     \
    { .s5 = { NULL } },                     \
    { .s5 = { NULL } },                     \
    { .s5 = { NULL } },                     \
   { .s5 = { NULL } },                     \
   { .s5 = { NULL } },                     \
   { .s5 = { NULL } },                     \
   { .s5 = { NULL } },                     \
   { .s5 = { NULL } },                     \
   { .s5 = { NULL } },                     \
   { .s5 = { NULL } },                     \
   { .s5 = { NULL } },                     \
   { .s5 = { NULL } },                     \
   { .s5 = { NULL } },                     \
   { .s5 = { NULL } },                     \
   { .s5 = { NULL } },                     \
   { .s5 = { NULL } },                     \
   { .s5 = { NULL } },                     \
   { .s5 = { NULL } },                     \
   { .s5 = { NULL } },                     \
   { .s5 = { NULL } },                     \
   { .s5 = { NULL } },                     \
   { .s5 = { NULL } },                     \
   { .s5 = { NULL } },                     \
   { .s5 = { NULL } },                     \
   { .s5 = { NULL } },                     \
   { .s5 = { NULL } },                     \
}
// clang-format on

#define CONFIG_HLP_NSA           "Enable NSA mode \n"
#define CONFIG_HLP_DL_RI_USE_DECODED "DL RI policy selector: 0 = use capped/scheduled RI layers (default), 1 = use decoded RI layers directly\n"
#define CONFIG_HLP_PRINT_CSI_DEBUG "Print CSI debug traces (RI/PMI/RX-estimator + gNB decode/policy) on terminal\n"
#define CONFIG_HLP_AI_FB_ULSCH_ENABLE "Enable lab custom AI CSI feedback over UL-SCH LCID (legacy PUCCH CSI remains active)\n"
#define CONFIG_HLP_AI_FB_LOG_PATH "Path to append AI-vs-legacy PMI comparison CSV at gNB (optional)\n"
#define CONFIG_HLP_AI_FB_IMPL_MODE "AI feedback implementation mode: 0=matrix(default), 1=MLP stub, 2=model stub, 3=csinet, 4=angular-delay-mlp, 5=angular-delay-refinenet\n"
#define CONFIG_HLP_AI_FB_FORCE_RANK1 "Force CSI RI restriction to rank-1 only for AI study mode (applied only when --ai-fb-ulsch-enable=1)\n"
#define CONFIG_HLP_AI_FB_MODEL_PATH "Path to model-stub weights file (.bin preferred, .txt supported) used when --ai-fb-impl-mode=2\n"
#define CONFIG_HLP_AI_FB_MODEL_BACKEND "Model-stub backend selector: 0=native loader, 1=ONNX stub, 2=TFLite stub\n"
#define CONFIG_HLP_AI_FB_ONNX_ENC_PATH "Path to ONNX encoder model file (used when --ai-fb-impl-mode=2 --ai-fb-model-backend=1)\n"
#define CONFIG_HLP_AI_FB_ONNX_DEC_PATH "Path to ONNX decoder model file (used when --ai-fb-impl-mode=2 --ai-fb-model-backend=1)\n"
#define CONFIG_HLP_AI_FB_CSINET_MODEL_PATH "Path to CSINet model artifact for --ai-fb-impl-mode=3 (phase-1 optional placeholder)\n"
#define CONFIG_HLP_AI_FB_CSINET_LATENT_BYTES "Requested CSINet latent payload bytes (phase-1 fixed transport budget is 6)\n"
#define CONFIG_HLP_AI_FB_COMPARE_GATING "Enable freshness gating for legacy-vs-custom PMI comparison: 0=off, 1=on\n"
#define CONFIG_HLP_AI_FB_COMPARE_MAX_AGE_SLOTS "Max slot age allowed for compare gating (used when --ai-fb-compare-gating=1)\n"
#define CONFIG_HLP_AI_FB_BUNDLED_ULSCH_ENABLE "Enable bundled UL-SCH MAC CE carrying legacy CSI part1 raw bits + AI latent from same UE observation\n"
#define CONFIG_HLP_AI_FB_EFF_PMI_HYST "Bundled compare analysis-only debounced PMI hysteresis threshold (consecutive opposite samples before switch)\n"
#define CONFIG_HLP_AI_FB_RUNTIME_SCHED_MODE "AI runtime DL scheduling mode: 0=off, 1=use fresh bundled AI tuple for RI/PMI/CQI with fallback, 2=legacy-CSI-payload-replaced-by-AI mode\n"
#define CONFIG_HLP_AI_FB_RUNTIME_SCHED_MAX_AGE_SLOTS "Max slot age for AI tuple freshness in runtime scheduling modes 1/2\n"
#define CONFIG_HLP_AI_FB_RUNTIME_SCHED_REQUIRE_FULL "Require AI RI+PMI+CQI tuple to all be present before overriding scheduling (1=yes,0=no)\n"
#define CONFIG_HLP_AI_FB_RUNTIME_LOG_ENABLE "Enable periodic AI runtime scheduling logs independent from --print-csi-debug: 0=off, 1=on\n"
#define CONFIG_HLP_AI_FB_RUNTIME_LOG_PERIOD_FRAMES "Periodicity (in frames) for AI runtime scheduling debug logs; 0 disables periodic logs\n"
#define CONFIG_HLP_SRS_IMSCOPE_LOG_ENABLE "Enable SRS/imscope-related logs in gNB PHY scheduling path: 0=disable, 1=enable\n"
#define CONFIG_HLP_CSI_I2_HYST_THRESHOLD "UE CSI 2-port rank-2 i2 hysteresis threshold (consecutive opposite filtered decisions before effective i2 switch); 0 disables\n"
#define CONFIG_HLP_CSI_I2_HYST_WINDOW "UE CSI 2-port rank-2 i2 majority window size over raw decisions before hysteresis; 0/1 disables\n"
#define CONFIG_HLP_FLOG          "Enable online log \n"
#define CONFIG_HLP_LOGL          "Set the global log level, valid options: (4:trace, 3:debug, 2:info, 1:warn, (0:error))\n"
#define CONFIG_HLP_TELN          "Start embedded telnet server \n"
#define CONFIG_HLP_WEB "Start embedded web server \n"
#define CONFIG_FLOG_OPT          "R"
#define CONFIG_LOGL_OPT          "g"
/*-------------------------------------------------------------------------------------------------------------------------------------------------*/
/*                                            command line parameters for LOG utility                                                              */
/*   optname                        helpstr       paramflags        XXXptr                              defXXXval            type           numelt */
/*-------------------------------------------------------------------------------------------------------------------------------------------------*/
// clang-format off
#define CMDLINE_LOGPARAMS_DESC  { \
  {CONFIG_FLOG_OPT, CONFIG_HLP_FLOG, 0,                                      .uptr = &online_log_messages, .defintval = 1,    TYPE_INT,    0}, \
  {CONFIG_LOGL_OPT, CONFIG_HLP_LOGL, 0,                                      .uptr = &glog_level,          .defintval = 0,    TYPE_UINT,   0}, \
  {"telnetsrv",     CONFIG_HLP_TELN, PARAMFLAG_BOOL | PARAMFLAG_CMDLINEONLY, .uptr = &start_telnetsrv,     .defintval = 0,    TYPE_UINT,   0}, \
  {"websrv",        CONFIG_HLP_WEB,  PARAMFLAG_BOOL | PARAMFLAG_CMDLINEONLY, .uptr = &start_websrv,        .defintval = 0,    TYPE_UINT,   0}, \
  {"log-mem",       NULL,            0,                                      .strptr = &logmem_filename,   .defstrval = NULL, TYPE_STRING, 0}, \
  {"telnetclt",     NULL,            0,                                      .uptr = &start_telnetclt,     .defstrval = NULL, TYPE_UINT,   0}, \
}
// clang-format on

/* check function for global log level */
// clang-format off
#define CMDLINE_LOGPARAMS_CHECK_DESC { \
  { .s5= {NULL} } ,                       \
  { .s2= {config_check_intrange, {0,4}}}, \
  { .s5= {NULL} } ,                       \
  { .s5= {NULL} } ,                       \
  { .s5= {NULL} } ,                       \
  { .s5= {NULL} } ,                       \
}
// clang-format on

/***************************************************************************************************************************************/

#define IS_SOFTMODEM_NOS1 (get_softmodem_optmask()->bit.SOFTMODEM_NOS1_BIT)
#define IS_SOFTMODEM_NONBIOT (get_softmodem_optmask()->bit.SOFTMODEM_NONBIOT_BIT)
#define IS_SOFTMODEM_RFSIM (get_softmodem_optmask()->bit.SOFTMODEM_RFSIM_BIT)
#define IS_SOFTMODEM_SIML1 (get_softmodem_optmask()->bit.SOFTMODEM_SIML1_BIT)
#define IS_SOFTMODEM_DLSIM (get_softmodem_optmask()->bit.SOFTMODEM_DLSIM_BIT)
#define IS_SOFTMODEM_DOSCOPE (get_softmodem_optmask()->bit.SOFTMODEM_DOSCOPE_BIT)
#define IS_SOFTMODEM_IQPLAYER (get_softmodem_optmask()->bit.SOFTMODEM_RECPLAY_BIT)
#define IS_SOFTMODEM_IQRECORDER (get_softmodem_optmask()->bit.SOFTMODEM_RECRECORD_BIT)
#define IS_SOFTMODEM_TELNETCLT (get_softmodem_optmask()->bit.SOFTMODEM_TELNETCLT_BIT)
#define IS_SOFTMODEM_ENB (get_softmodem_optmask()->bit.SOFTMODEM_ENB_BIT)
#define IS_SOFTMODEM_GNB (get_softmodem_optmask()->bit.SOFTMODEM_GNB_BIT)
#define IS_SOFTMODEM_4GUE (get_softmodem_optmask()->bit.SOFTMODEM_4GUE_BIT)
#define IS_SOFTMODEM_5GUE (get_softmodem_optmask()->bit.SOFTMODEM_5GUE_BIT)
#define IS_SOFTMODEM_NOSTATS (get_softmodem_optmask()->bit.SOFTMODEM_NOSTATS_BIT)
#define IS_SOFTMODEM_IMSCOPE_ENABLED (get_softmodem_optmask()->bit.SOFTMODEM_IMSCOPE_BIT)
#define IS_SOFTMODEM_IMSCOPE_RECORD_ENABLED (get_softmodem_optmask()->bit.SOFTMODEM_IMSCOPE_RECORD_BIT)
typedef struct optmask_s {
  union {
    struct {
      uint64_t SOFTMODEM_NOS1_BIT: 1;
      uint64_t SOFTMODEM_NOKRNMOD_BIT: 1;
      uint64_t SOFTMODEM_NONBIOT_BIT: 1;
      uint64_t SOFTMODEM_RFSIM_BIT: 1;
      uint64_t SOFTMODEM_SIML1_BIT: 1;
      uint64_t SOFTMODEM_DLSIM_BIT: 1;
      uint64_t SOFTMODEM_DOSCOPE_BIT: 1;
      uint64_t SOFTMODEM_RECPLAY_BIT: 1;
      uint64_t SOFTMODEM_TELNETCLT_BIT: 1;
      uint64_t SOFTMODEM_RECRECORD_BIT: 1;
      uint64_t SOFTMODEM_ENB_BIT: 1;
      uint64_t SOFTMODEM_GNB_BIT: 1;
      uint64_t SOFTMODEM_4GUE_BIT: 1;
      uint64_t SOFTMODEM_5GUE_BIT: 1;
      uint64_t SOFTMODEM_NOSTATS_BIT: 1;
      uint64_t SOFTMODEM_IMSCOPE_BIT: 1;
      uint64_t SOFTMODEM_IMSCOPE_RECORD_BIT : 1;
    } bit;
    uint64_t v; // allow to export entire bit set, force to 64 bit processor atomic size
  };
} optmask_t;
typedef struct {
  optmask_t optmask;
  //THREAD_STRUCT  thread_struct;
  char           *rf_config_file;
  char *threadPoolConfig;
  int            phy_test;
  int            do_ra;
  uint8_t        sl_mode;
  int            chain_offset;
  int            numerology;
  int            band;
  uint32_t       clock_source;
  uint32_t       timing_source;
  double         tune_offset;
  int command_line_sample_advance;
  uint32_t       send_dmrs_sync;
  int            use_256qam_table;
  int            chest_time;
  int            chest_freq;
  uint8_t        nfapi;
  int            nsa;
  uint16_t       node_number;
  int            non_stop;
  int            continuous_tx;
  uint32_t       sync_ref;
  int no_itti;
  int threequarter_fs;
  int default_pdu_session_id;
  int extra_pdu_session_id;
  char *csi_record_path;  /* gNB: record CSI-RS scheduling + decoded CSI feedback when set */
  char *imscope_windows;   /* NULL = show all; else comma-separated list of window titles to show when --imscope is used */
  int dl_ri_use_decoded;   /* gNB DL scheduler: 0=capped RI, 1=decoded RI */
  int print_csi_debug;   /* Print extra CSI-related debug traces on terminal */
  int ai_fb_ulsch_enable; /* Enable custom AI CSI latent over UL-SCH lab path */
  char *ai_fb_log_path;   /* Optional CSV output for AI-vs-legacy PMI comparison */
  int ai_fb_impl_mode;    /* AI FB module implementation selector */
  int ai_fb_force_rank1;  /* Gate rank-1 RI restriction for AI study mode */
  char *ai_fb_model_path; /* Optional model-stub weights file path */
  int ai_fb_model_backend; /* 0=native, 1=ONNX stub, 2=TFLite stub */
  char *ai_fb_onnx_enc_path; /* Optional ONNX encoder model path */
  char *ai_fb_onnx_dec_path; /* Optional ONNX decoder model path */
  char *ai_fb_csinet_model_path; /* Optional CSINet model path */
  int ai_fb_csinet_latent_bytes; /* Requested CSINet latent payload bytes */
  int ai_fb_compare_gating; /* Gate compare stats by custom PMI freshness */
  int ai_fb_compare_max_age_slots; /* Max age (slots) for fresh compare */
  int ai_fb_bundled_ulsch_enable; /* Enable bundled legacy+AI UL-SCH MAC CE path */
  int ai_fb_eff_pmi_hyst; /* Analysis-only effective PMI hysteresis threshold */
  int ai_fb_runtime_sched_mode; /* 0=off,1=bundled override,2=legacy payload replaced by AI */
  int ai_fb_runtime_sched_max_age_slots; /* Freshness for runtime AI scheduling */
  int ai_fb_runtime_sched_require_full; /* Require full AI tuple before override */
  int ai_fb_runtime_log_enable; /* Enable periodic runtime selector logs */
  int ai_fb_runtime_log_period_frames; /* Periodicity of runtime selector logs */
  int srs_imscope_log_enable; /* Print SRS/imscope visualization logs */
  int csi_i2_hyst_threshold; /* UE-side hysteresis for 2-port rank-2 i2 */
  int csi_i2_hyst_window; /* UE-side majority window for 2-port rank-2 i2 */
} softmodem_params_t;

#define IS_SA_MODE(sM_params) (!(sM_params)->phy_test && !(sM_params)->do_ra && !(sM_params)->nsa)
void softmodem_verify_mode(const softmodem_params_t *p);

#define get_softmodem_optmask() (&(get_softmodem_params()->optmask))
softmodem_params_t *get_softmodem_params(void);
void get_common_options(configmodule_interface_t *cfg);
char *get_softmodem_function(void);
#define SOFTMODEM_RTSIGNAL  (SIGRTMIN+1)
void set_softmodem_sighandler(void);
extern uint64_t downlink_frequency[MAX_NUM_CCs][4];
extern int32_t uplink_frequency_offset[MAX_NUM_CCs][4];
extern int usrp_tx_thread;
extern int sf_ahead;
extern int oai_exit;

void ru_tx_func(void *param);
void configure_ru(void *, void *arg);
void configure_rru(void *, void *arg);
struct timespec timespec_add(struct timespec lhs, struct timespec rhs);
struct timespec timespec_sub(struct timespec lhs, struct timespec rhs);
extern uint8_t nfapi_mode;
extern char *parallel_config;
extern char *worker_config;
extern double cpuf;

#ifdef __cplusplus
}
#endif
#endif
