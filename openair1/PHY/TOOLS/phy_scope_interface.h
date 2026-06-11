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

/*! \file phy_scope_interface.h
 * \brief softscope interface API include file
 * \author Nokia BellLabs France, francois Taburet
 * \date 2019
 * \version 0.1
 * \company Nokia BellLabs France
 * \email: francois.taburet@nokia-bell-labs.com
 * \note
 * \warning
 */
#ifndef __PHY_SCOPE_INTERFACE_H__
#define __PHY_SCOPE_INTERFACE_H__

#ifdef __cplusplus
#include <atomic>
#ifndef _Atomic
#define _Atomic(X) std::atomic< X >
#endif
#endif

#include <openair1/PHY/defs_gNB.h>
#include <openair1/PHY/defs_nr_UE.h>

typedef struct {
  _Atomic(uint32_t) nb_total;
  _Atomic(uint32_t) nb_nack;
  _Atomic(uint32_t) blockSize;   // block size, to be used for throughput calculation
  _Atomic(uint16_t) nofRBs;
  _Atomic(uint8_t ) dl_mcs;
} extended_kpi_ue;

typedef struct {
  int *argc;
  char **argv;
  RU_t *ru;
  PHY_VARS_gNB *gNB;
} scopeParms_t;

enum scopeDataType {
  pbchDlChEstimateTime,
  pbchLlr,
  pbchRxdataF_comp,
  pdcchLlr,
  pdcchRxdataF_comp,
  pdschLlr,
  pdschRxdataF_comp,
  commonRxdataF,
  gNBRxdataF,
  psbchDlChEstimateTime,
  psbchLlr,
  psbchRxdataF_comp,
  gNBulDelay,
  MAX_SCOPE_TYPES,
  gNBPuschRxIq = MAX_SCOPE_TYPES,
  gNBPuschLlr,
  ueTimeDomainSamples,
  ueTimeDomainSamplesBeforeSync,
  gNbTimeDomainSamples,
  pdschChanEstimates,
  pdschRxdataF,
  ueCsirsChEstimate,   /* CSI-RS channel estimates at UE (IQ per ant/port/subcarrier) */
  gNBCsiReportParams,  /* CSI report at gNB (RI, PMI, CQI, RSRP, SINR, etc.) */
  gNBSrsChEstimate,    /* SRS channel estimates at gNB (IQ per ant/port/subcarrier) */
  EXTRA_SCOPE_TYPES
};

enum PlotTypeGnbIf {
  puschLLRe,
  puschIQe,
};

#define COPIES_MEM 4

typedef struct {
  int dataSize;
  int elementSz;
  int colSz;
  int lineSz;
} scopeGraphData_t;

typedef struct {
  int slot;
  int frame;
} metadata;

/* Payload for gNBCsiReportParams scope type: filled by MAC when a CSI report is decoded */
typedef struct {
  int frame;
  int slot;
  uint16_t rnti;
  uint8_t ri;
  uint8_t cqi;
  uint8_t pmi_x1;
  uint8_t pmi_x2;
  int8_t rsrp_dBm;
  int8_t sinr_dB;
  uint8_t csi_report_id;
  /** Configured PDSCH max MIMO layers (serving cell); imscope shows this next to CSI RI for 4-layer context. */
  uint8_t max_dl_mimo_layers;
  /** gNB logical DL ports N1·N2·XP (same as CSI-RS port count for typical config). */
  uint8_t pdsch_logical_ports;
  uint32_t ai_runtime_origin_frame;
  uint32_t ai_runtime_origin_slot;
  uint32_t ai_runtime_override_used;
  uint32_t ai_runtime_fallback_missing;
  uint32_t ai_runtime_fallback_stale;
  uint32_t ai_runtime_fallback_incomplete;
  /** Slots between \ref ai_runtime_origin_frame/slot and this report's frame/slot; 0xFFFF if tuple invalid. */
  uint16_t ai_runtime_age_slots;
  /** \ref ai_fb_runtime_sched_mode: 0=off, 1=bundled, 2=mode-2 payload (CLI snapshot). */
  uint8_t ai_sched_mode;
  uint8_t ai_runtime_tuple_valid;
  uint8_t ai_runtime_tuple_fresh;
  uint8_t ai_runtime_ri;
  uint8_t ai_runtime_cqi;
  uint8_t ai_runtime_pmi_x1;
  uint8_t ai_runtime_pmi_x2;
  /** 0=no, 1=yes, 2=N/A (no valid AI runtime tuple). */
  uint8_t ai_ri_match_decode_vs_runtime;
  /** 0=no, 1=yes, 2=N/A. */
  uint8_t ai_pmi_match_decode_vs_runtime;
  /** AI CQI minus decoded wideband CQI (1TB); saturated int8. */
  int8_t ai_cqi_delta_ai_minus_decode;
  /** 1 when sched mode on, tuple valid+fresh, and RI/CQI/PMI differ from decoded CSI (override would disagree with air decode). */
  uint8_t ai_runtime_override_disagrees_decode;
  /** DL block error rate (running EWMA from sched_ctrl->dl_bler_stats.bler), 0.0–1.0. */
  float dl_bler;
  /** MCS most recently scheduled for DL on this UE. */
  uint8_t dl_mcs;
  /** Cumulative DL MAC TX bytes for this UE (mac_stats.dl.total_bytes); imscope derives throughput from deltas. */
  uint64_t dl_total_bytes;
} csi_report_scope_payload_t;

typedef struct scopeData_s {
  int *argc;
  char **argv;
  RU_t *ru;
  PHY_VARS_gNB *gNB;
  scopeGraphData_t *liveData[MAX_SCOPE_TYPES];
  void (*copyData)(void *, enum scopeDataType, void *data, int elementSz, int colSz, int lineSz, int offset, metadata *meta);
  pthread_mutex_t copyDataMutex;
  scopeGraphData_t *copyDataBufs[MAX_SCOPE_TYPES][COPIES_MEM];
  int copyDataBufsIdx[MAX_SCOPE_TYPES];
  void (*scopeUpdater)(enum PlotTypeGnbIf plotType, int numElements);
  bool (*tryLockScopeData)(enum scopeDataType type, int elementSz, int colSz, int lineSz, metadata *meta);
  void (*copyDataUnsafeWithOffset)(enum scopeDataType type, void *dataIn, size_t size, size_t offset, int copy_index);
  void (*unlockScopeData)(enum scopeDataType type);
  void (*dumpScopeData)(int slot, int frame, const char *cause_string);
  const char *imscope_windows; /* NULL or "" = show all; else comma-separated window titles (from --imscope-windows) */
} scopeData_t;

int load_softscope(char *exectype, void *initarg);
int end_forms(void) ;
int copyDataMutexInit(scopeData_t *);
void copyData(void *, enum scopeDataType type, void *dataIn, int elementSz, int colSz, int lineSz, int offset, metadata *meta);

/* Return value of --imscope-windows (NULL or "" = show all). Implemented in executables/softmodem-common.c */
const char *get_imscope_windows_filter(void);

#define UEscopeCopyWithMetadata(ue, type, ...) \
  if (ue->scopeData) {               \
    ((scopeData_t *)ue->scopeData)->copyData((scopeData_t *)ue->scopeData, type, ##__VA_ARGS__); \
  }
#define UEscopeCopy(ue, type, ...) \
  if (ue->scopeData) {               \
    metadata mt = {.slot = -1, .frame = -1}; \
    ((scopeData_t *)ue->scopeData)->copyData((scopeData_t *)ue->scopeData, type, ##__VA_ARGS__, &mt); \
  }
#define gNBscopeCopyWithMetadata(gnb, type, ...) \
  if (gnb->scopeData) {              \
    ((scopeData_t *)gnb->scopeData)->copyData((scopeData_t *)gnb->scopeData, type, ##__VA_ARGS__); \
  }
#define gNBscopeCopy(gnb, type, ...) \
  if (gnb->scopeData) {              \
    metadata mt = {.slot = -1, .frame = -1}; \
    ((scopeData_t *)gnb->scopeData)->copyData((scopeData_t *)gnb->scopeData, type, ##__VA_ARGS__, &mt); \
  }
#define GnbScopeUpdate(gnb, type, numElt) \
  if (gnb->scopeData)                     \
    ((scopeData_t *)gnb->scopeData)->scopeUpdater(type, numElt);

#define gNBTryLockScopeData(gnb, type, ...)                                          \
  gnb->scopeData && ((scopeData_t *)gnb->scopeData)->tryLockScopeData \
      && ((scopeData_t *)gnb->scopeData)->tryLockScopeData(type, ##__VA_ARGS__)

#define gNBscopeCopyUnsafe(gnb, type, ...) \
  scopeData_t *scope_data = (scopeData_t *)gnb->scopeData; \
  if (scope_data && scope_data->copyDataUnsafeWithOffset) { \
    scope_data->copyDataUnsafeWithOffset(type, ##__VA_ARGS__); \
  }

#define gNBunlockScopeData(gnb, type) \
  scopeData_t *scope_data = (scopeData_t *)gnb->scopeData; \
  if (scope_data && scope_data->unlockScopeData) { \
    scope_data->unlockScopeData(type); \
  }

#define gNBdumpScopeData(gnb, slot, frame, cause_string)          \
  do {                                                            \
    scopeData_t *scope_data = (scopeData_t *)gnb->scopeData;      \
    if (scope_data && scope_data->dumpScopeData) {                \
      scope_data->dumpScopeData((slot), (frame), (cause_string)); \
    }                                                             \
  } while (0)

#define UEScopeHasTryLock(ue) \
  (ue->scopeData && ((scopeData_t *)ue->scopeData)->tryLockScopeData)

#define UETryLockScopeData(ue, type, ...)                           \
  ue->scopeData && ((scopeData_t *)ue->scopeData)->tryLockScopeData \
      && ((scopeData_t *)ue->scopeData)->tryLockScopeData(type, ##__VA_ARGS__)

#define UEscopeCopyUnsafe(ue, type, ...) \
  scopeData_t *scope_data = (scopeData_t *)ue->scopeData; \
  if (scope_data && scope_data->copyDataUnsafeWithOffset) { \
    scope_data->copyDataUnsafeWithOffset(type, ##__VA_ARGS__); \
  }

#define UEunlockScopeData(ue, type) \
  scopeData_t *scope_data = (scopeData_t *)ue->scopeData; \
  if (scope_data && scope_data->unlockScopeData) { \
    scope_data->unlockScopeData(type); \
  }

#define UEdumpScopeData(ue, slot, frame, cause_string)            \
  do {                                                            \
    scopeData_t *scope_data = (scopeData_t *)ue->scopeData;       \
    if (scope_data && scope_data->dumpScopeData) {                \
      scope_data->dumpScopeData((slot), (frame), (cause_string)); \
    }                                                             \
  } while (0)

#ifdef __cplusplus
extern "C" {
#endif
extended_kpi_ue* getKPIUE();
#ifdef __cplusplus
}
#endif

#endif
