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

/***********************************************************************
*
* FILENAME    :  csi_rx.c
*
* MODULE      :
*
* DESCRIPTION :  function to receive the channel state information
*
************************************************************************/

#include <complex.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/time.h>

#include "executables/nr-softmodem-common.h"
#include "executables/softmodem-common.h"
#include "openair1/PHY/NR_UE_TRANSPORT/ai_fb_encoder.h"
#include "openair1/PHY/NR_UE_TRANSPORT/csinet_encoder.h"
#include "nr_transport_proto_ue.h"
#include "PHY/NR_REFSIG/nr_refsig.h"
#include "common/utils/nr/nr_common.h"
#include "PHY/NR_UE_ESTIMATION/filt16a_32.h"
#include "PHY/TOOLS/phy_scope_interface.h"

// Additional memory allocation, because of applying the filter and the memory offset to ensure memory alignment
#define FILTER_MARGIN 32

//#define NR_CSIRS_DEBUG
//#define NR_CSIIM_DEBUG

extern openair0_config_t openair0_cfg[MAX_CARDS];

/* Recording for ML CSI compression: write CSI-RS channel estimates and CSI report to ue->csi_record_path */
static pthread_mutex_t csi_record_mutex = PTHREAD_MUTEX_INITIALIZER;
static int csi_record_csv_header_done = 0;

/* H_flat may be NULL when no channel estimation was run; then only csi_reports.csv is written */
static void csi_record_write(PHY_VARS_NR_UE *ue,
                             const UE_nr_rxtx_proc_t *proc,
                             int nr_rx,
                             int n_ports,
                             int n_subc,
                             const c16_t *H_flat,
                             int rsrp_dBm,
                             uint8_t rank_indicator,
                             const uint8_t i1[3],
                             uint8_t i2,
                             uint8_t cqi,
                             int32_t precoded_sinr_dB)
{
  if (!ue->csi_record_path || ue->csi_record_path[0] == '\0')
    return;

  const int frame = proc->frame_rx;
  const int slot = proc->nr_slot_rx;

  struct timeval tv;
  gettimeofday(&tv, NULL);
  int64_t timestamp_utc_us = (int64_t)tv.tv_sec * 1000000 + (int64_t)tv.tv_usec;

  pthread_mutex_lock(&csi_record_mutex);

  /* Create base directory and IQ output subfolder on first use (ignore EEXIST) */
  if (mkdir(ue->csi_record_path, 0755) < 0 && errno != EEXIST) {}
  char csi_rs_channels_dir[PATH_MAX];
  snprintf(csi_rs_channels_dir, sizeof(csi_rs_channels_dir), "%s/csi_rs_channels", ue->csi_record_path);
  if (mkdir(csi_rs_channels_dir, 0755) < 0 && errno != EEXIST) {}

  /* path can be csi_rs_channels_dir + "/H_<timestamp>_f<frame>_s<slot>.bin"; compiler infers up to PATH_MAX-1 + ~54 */
  char path[PATH_MAX + 64];
  char h_bin_path_buf[PATH_MAX + 64];
  h_bin_path_buf[0] = '\0';

  /* Write channel estimates to csi_rs_channels/ subfolder when we have them (measurement_bitmap >= 1); filename includes timestamp for distinct files per recording */
  if (H_flat && nr_rx > 0 && n_ports > 0 && n_subc > 0) {
    snprintf(path, sizeof(path), "%s/H_%ld_f%d_s%d.bin", csi_rs_channels_dir, (long)timestamp_utc_us, frame, slot);
    snprintf(h_bin_path_buf, sizeof(h_bin_path_buf), "%s", path);
    FILE *fh = fopen(path, "wb");
    if (fh) {
      /* Header: frame, slot, nr_rx, n_ports, n_subc, timestamp_utc_us (6 int64 for alignment-friendly 48 bytes; use int32 for first 5 then pad or use 2 int64) */
      int32_t hdr[5] = {frame, slot, nr_rx, n_ports, n_subc};
      if (fwrite(hdr, sizeof(int32_t), 5, fh) == 5 && fwrite(&timestamp_utc_us, sizeof(timestamp_utc_us), 1, fh) == 1) {
        size_t total = (size_t)nr_rx * n_ports * n_subc;
        if (fwrite(H_flat, sizeof(c16_t), total, fh) == total) {}
      }
      fclose(fh);
    } else {
      h_bin_path_buf[0] = '\0';
    }
  }

  /* Always append CSI report to csi_reports.csv (labels for ML); H_bin_path points to csi_rs_channels/ H_*.bin when written */
  snprintf(path, sizeof(path), "%s/csi_reports.csv", ue->csi_record_path);
  FILE *fc = fopen(path, "a");
  if (fc) {
    if (!csi_record_csv_header_done) {
      fprintf(fc, "timestamp_utc_us,frame,slot,H_bin_path,rsrp_dBm,ri,i1_0,i1_1,i1_2,i2,cqi,sinr_dB\n");
      csi_record_csv_header_done = 1;
    }
    /* H_bin_path quoted so paths with commas are safe in CSV */
    fprintf(fc, "%ld,%d,%d,\"%s\",%d,%u,%u,%u,%u,%u,%u,%d\n",
            (long)timestamp_utc_us, frame, slot, h_bin_path_buf, rsrp_dBm, rank_indicator + 1u, i1[0], i1[1], i1[2], i2, cqi, precoded_sinr_dB);
    fclose(fc);
  }

  pthread_mutex_unlock(&csi_record_mutex);
}

void nr_det_A_MF_2x2(int32_t *a_mf_00,
                     int32_t *a_mf_01,
                     int32_t *a_mf_10,
                     int32_t *a_mf_11,
                     int32_t *det_fin,
                     const unsigned short nb_rb) {

  simde__m128i ad_re_128, bc_re_128, det_re_128;

  simde__m128i *a_mf_00_128 = (simde__m128i *)a_mf_00;
  simde__m128i *a_mf_01_128 = (simde__m128i *)a_mf_01;
  simde__m128i *a_mf_10_128 = (simde__m128i *)a_mf_10;
  simde__m128i *a_mf_11_128 = (simde__m128i *)a_mf_11;
  simde__m128i *det_fin_128 = (simde__m128i *)det_fin;

  for (int rb = 0; rb<3*nb_rb; rb++) {

    //complex multiplication (I_a+jQ_a)(I_d+jQ_d) = (I_aI_d - Q_aQ_d) + j(Q_aI_d + I_aQ_d)
    //The imag part is often zero, we compute only the real part
    ad_re_128 = simde_mm_madd_epi16(oai_mm_conj(a_mf_00_128[0]), a_mf_11_128[0]); //Re: I_a0*I_d0 - Q_a1*Q_d1

    //complex multiplication (I_b+jQ_b)(I_c+jQ_c) = (I_bI_c - Q_bQ_c) + j(Q_bI_c + I_bQ_c)
    //The imag part is often zero, we compute only the real part
    bc_re_128 = simde_mm_madd_epi16(oai_mm_conj(a_mf_01_128[0]), a_mf_10_128[0]); //Re: I_b0*I_c0 - Q_b1*Q_c1

    det_re_128 = simde_mm_sub_epi32(ad_re_128, bc_re_128);

    //det in Q30 format
    det_fin_128[0] = simde_mm_abs_epi32(det_re_128);

    det_fin_128+=1;
    a_mf_00_128+=1;
    a_mf_01_128+=1;
    a_mf_10_128+=1;
    a_mf_11_128+=1;
  }
}

void nr_squared_matrix_element(int32_t *a,
                               int32_t *a_sq,
                               const unsigned short nb_rb) {
  simde__m128i *a_128 = (simde__m128i *)a;
  simde__m128i *a_sq_128 = (simde__m128i *)a_sq;
  for (int rb=0; rb<3*nb_rb; rb++) {
    a_sq_128[0] = simde_mm_madd_epi16(a_128[0], a_128[0]);
    a_sq_128+=1;
    a_128+=1;
  }
}

void nr_numer_2x2(int32_t *a_00_sq,
                  int32_t *a_01_sq,
                  int32_t *a_10_sq,
                  int32_t *a_11_sq,
                  int32_t *num_fin,
                  const unsigned short nb_rb) {
  simde__m128i *a_00_sq_128 = (simde__m128i *)a_00_sq;
  simde__m128i *a_01_sq_128 = (simde__m128i *)a_01_sq;
  simde__m128i *a_10_sq_128 = (simde__m128i *)a_10_sq;
  simde__m128i *a_11_sq_128 = (simde__m128i *)a_11_sq;
  simde__m128i *num_fin_128 = (simde__m128i *)num_fin;
  for (int rb=0; rb<3*nb_rb; rb++) {
    simde__m128i sq_a_plus_sq_d_128 = simde_mm_add_epi32(a_00_sq_128[0], a_11_sq_128[0]);
    simde__m128i sq_b_plus_sq_c_128 = simde_mm_add_epi32(a_01_sq_128[0], a_10_sq_128[0]);
    num_fin_128[0] = simde_mm_add_epi32(sq_a_plus_sq_d_128, sq_b_plus_sq_c_128);
    num_fin_128+=1;
    a_00_sq_128+=1;
    a_01_sq_128+=1;
    a_10_sq_128+=1;
    a_11_sq_128+=1;
  }
}

bool is_csi_rs_in_symbol(const fapi_nr_dl_config_csirs_pdu_rel15_t csirs_config_pdu, const int symbol) {

  bool ret = false;

  // 38.211-Table 7.4.1.5.3-1: CSI-RS locations within a slot
  switch(csirs_config_pdu.row){
    case 1:
    case 2:
    case 3:
    case 4:
    case 6:
    case 9:
      if(symbol == csirs_config_pdu.symb_l0) {
        ret = true;
      }
      break;
    case 5:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
      if(symbol == csirs_config_pdu.symb_l0 || symbol == (csirs_config_pdu.symb_l0+1) ) {
        ret = true;
      }
      break;
    case 13:
    case 14:
    case 16:
    case 17:
      if(symbol == csirs_config_pdu.symb_l0 || symbol == (csirs_config_pdu.symb_l0+1) ||
          symbol == csirs_config_pdu.symb_l1 || symbol == (csirs_config_pdu.symb_l1+1)) {
        ret = true;
      }
      break;
    case 15:
    case 18:
      if(symbol == csirs_config_pdu.symb_l0 || symbol == (csirs_config_pdu.symb_l0+1) || symbol == (csirs_config_pdu.symb_l0+2) ) {
        ret = true;
      }
      break;
    default:
      AssertFatal(0==1, "Row %d is not valid for CSI Table 7.4.1.5.3-1\n", csirs_config_pdu.row);
  }

  return ret;
}

static int nr_get_csi_rs_signal(const PHY_VARS_NR_UE *ue,
                                const UE_nr_rxtx_proc_t *proc,
                                const fapi_nr_dl_config_csirs_pdu_rel15_t *csirs_config_pdu,
                                const nr_csi_info_t *nr_csi_info,
                                const csi_mapping_parms_t *csi_mapping,
                                const int CDM_group_size,
                                c16_t csi_rs_received_signal[][ue->frame_parms.samples_per_slot_wCP],
                                uint32_t *rsrp,
                                int *rsrp_dBm,
                                const c16_t rxdataF[][ue->frame_parms.samples_per_slot_wCP])
{
  const NR_DL_FRAME_PARMS *fp = &ue->frame_parms;
  uint16_t meas_count = 0;
  uint32_t rsrp_sum = 0;

  for (int ant_rx = 0; ant_rx < fp->nb_antennas_rx; ant_rx++) {

    for (int rb = csirs_config_pdu->start_rb; rb < (csirs_config_pdu->start_rb+csirs_config_pdu->nr_of_rbs); rb++) {

      // for freq density 0.5 checks if even or odd RB
      if(csirs_config_pdu->freq_density <= 1 && csirs_config_pdu->freq_density != (rb % 2)) {
        continue;
      }

      for (int cdm_id = 0; cdm_id < csi_mapping->size; cdm_id++) {
        for (int s = 0; s < CDM_group_size; s++)  {

          // loop over frequency resource elements within a group
          for (int kp = 0; kp <= csi_mapping->kprime; kp++) {

            uint16_t k = (fp->first_carrier_offset + (rb * NR_NB_SC_PER_RB) + csi_mapping->koverline[cdm_id] + kp) % fp->ofdm_symbol_size;

            // loop over time resource elements within a group
            for (int lp = 0; lp <= csi_mapping->lprime; lp++) {
              uint16_t symb = lp + csi_mapping->loverline[cdm_id];
              uint64_t symbol_offset = symb * fp->ofdm_symbol_size;
              const c16_t *rx_signal = &rxdataF[ant_rx][symbol_offset];
              c16_t *rx_csi_rs_signal = &csi_rs_received_signal[ant_rx][symbol_offset];
              rx_csi_rs_signal[k].r = rx_signal[k].r;
              rx_csi_rs_signal[k].i = rx_signal[k].i;

              rsrp_sum += (((int32_t)(rx_csi_rs_signal[k].r)*rx_csi_rs_signal[k].r) +
                           ((int32_t)(rx_csi_rs_signal[k].i)*rx_csi_rs_signal[k].i));

              meas_count++;

#ifdef NR_CSIRS_DEBUG
              int dataF_offset = proc->nr_slot_rx * fp->samples_per_slot_wCP;
              uint16_t port_tx = s + csi_mapping->j[cdm_id] * CDM_group_size;
              c16_t *tx_csi_rs_signal = &nr_csi_info->csi_rs_generated_signal[port_tx][symbol_offset + dataF_offset];
              LOG_I(NR_PHY,
                    "l,k (%2d,%4d) |\tport_tx %d (%4d,%4d)\tant_rx %d (%4d,%4d)\n",
                    symb,
                    k,
                    port_tx+3000,
                    tx_csi_rs_signal[k].r,
                    tx_csi_rs_signal[k].i,
                    ant_rx,
                    rx_csi_rs_signal[k].r,
                    rx_csi_rs_signal[k].i);
#endif
            }
          }
        }
      }
    }
  }


  *rsrp = rsrp_sum/meas_count;
  *rsrp_dBm = dB_fixed(*rsrp) + 30 - SQ15_SQUARED_NORM_FACTOR_DB
              - ((int)openair0_cfg[ue->rf_map.card].rx_gain[0] - (int)openair0_cfg[ue->rf_map.card].rx_gain_offset[0])
              - dB_fixed(ue->frame_parms.ofdm_symbol_size);

#ifdef NR_CSIRS_DEBUG
  LOG_I(NR_PHY, "RSRP = %i (%i dBm)\n", *rsrp, *rsrp_dBm);
#endif

  return 0;
}

uint32_t calc_power_csirs(const uint16_t *x, const fapi_nr_dl_config_csirs_pdu_rel15_t *csirs_config_pdu)
{
  uint64_t sum_x = 0;
  uint64_t sum_x2 = 0;
  uint16_t size = 0;
  for (int rb = 0; rb < csirs_config_pdu->nr_of_rbs; rb++) {
    if (csirs_config_pdu->freq_density <= 1 && csirs_config_pdu->freq_density != ((rb + csirs_config_pdu->start_rb) % 2)) {
      continue;
    }
    sum_x = sum_x + x[rb];
    sum_x2 = sum_x2 + x[rb] * x[rb];
    size++;
  }
  return sum_x2 / size - (sum_x / size) * (sum_x / size);
}

static int nr_csi_rs_channel_estimation(
    const NR_DL_FRAME_PARMS *fp,
    const UE_nr_rxtx_proc_t *proc,
    const fapi_nr_dl_config_csirs_pdu_rel15_t *csirs_config_pdu,
    const nr_csi_info_t *nr_csi_info,
    const c16_t **csi_rs_generated_signal,
    const c16_t csi_rs_received_signal[][fp->samples_per_slot_wCP],
    const csi_mapping_parms_t *csi_mapping,
    const int CDM_group_size,
    uint8_t mem_offset,
    c16_t csi_rs_ls_estimated_channel[][csi_mapping->ports][fp->ofdm_symbol_size],
    c16_t csi_rs_estimated_channel_freq[][csi_mapping->ports][fp->ofdm_symbol_size + FILTER_MARGIN],
    int16_t *log2_re,
    int16_t *log2_maxh,
    uint32_t *noise_power)
{
  const int dataF_offset = proc->nr_slot_rx * fp->samples_per_slot_wCP;
  *noise_power = 0;
  int maxh = 0;
  int count = 0;

  for (int ant_rx = 0; ant_rx < fp->nb_antennas_rx; ant_rx++) {

    /// LS channel estimation

    for(uint16_t port_tx = 0; port_tx < csi_mapping->ports; port_tx++) {
      memset(csi_rs_ls_estimated_channel[ant_rx][port_tx], 0, fp->ofdm_symbol_size * sizeof(c16_t));
    }

    for (int rb = csirs_config_pdu->start_rb; rb < (csirs_config_pdu->start_rb+csirs_config_pdu->nr_of_rbs); rb++) {

      // for freq density 0.5 checks if even or odd RB
      if(csirs_config_pdu->freq_density <= 1 && csirs_config_pdu->freq_density != (rb % 2)) {
        continue;
      }

      for (int cdm_id = 0; cdm_id < csi_mapping->size; cdm_id++) {
        for (int s = 0; s < CDM_group_size; s++)  {

          uint16_t port_tx = s + csi_mapping->j[cdm_id] * CDM_group_size;

          // loop over frequency resource elements within a group
          for (int kp = 0; kp <= csi_mapping->kprime; kp++) {

            uint16_t kinit = (fp->first_carrier_offset + rb*NR_NB_SC_PER_RB) % fp->ofdm_symbol_size;
            uint16_t k = kinit + csi_mapping->koverline[cdm_id] + kp;

            // loop over time resource elements within a group
            for (int lp = 0; lp <= csi_mapping->lprime; lp++) {
              uint16_t symb = lp + csi_mapping->loverline[cdm_id];
              uint64_t symbol_offset = symb * fp->ofdm_symbol_size;
              const c16_t *tx_csi_rs_signal = &csi_rs_generated_signal[port_tx][symbol_offset+dataF_offset];
              const c16_t *rx_csi_rs_signal = &csi_rs_received_signal[ant_rx][symbol_offset];
              c16_t tmp = c16MulConjShift(tx_csi_rs_signal[k], rx_csi_rs_signal[k], nr_csi_info->csi_rs_generated_signal_bits);
              // This is not just the LS estimation for each (k,l), but also the sum of the different contributions
              // for the sake of optimizing the memory used.
              csi_rs_ls_estimated_channel[ant_rx][port_tx][kinit].r += tmp.r;
              csi_rs_ls_estimated_channel[ant_rx][port_tx][kinit].i += tmp.i;
            }
          }
        }
      }
    }

#ifdef NR_CSIRS_DEBUG
    for(int symb = 0; symb < NR_SYMBOLS_PER_SLOT; symb++) {
      if(!is_csi_rs_in_symbol(*csirs_config_pdu,symb)) {
        continue;
      }
      for(int k = 0; k < fp->ofdm_symbol_size; k++) {
        LOG_I(NR_PHY, "l,k (%2d,%4d) | ", symb, k);
        for(uint16_t port_tx = 0; port_tx < csi_mapping->ports; port_tx++) {
          uint64_t symbol_offset = symb * fp->ofdm_symbol_size;
          c16_t *tx_csi_rs_signal = (c16_t*)&csi_rs_generated_signal[port_tx][symbol_offset+dataF_offset];
          c16_t *rx_csi_rs_signal = (c16_t*)&csi_rs_received_signal[ant_rx][symbol_offset];
          c16_t *csi_rs_ls_estimated_channel16 = csi_rs_ls_estimated_channel[ant_rx][port_tx];
          printf("port_tx %d --> ant_rx %d, tx (%4d,%4d), rx (%4d,%4d), ls (%4d,%4d) | ",
                 port_tx+3000, ant_rx,
                 tx_csi_rs_signal[k].r, tx_csi_rs_signal[k].i,
                 rx_csi_rs_signal[k].r, rx_csi_rs_signal[k].i,
                 csi_rs_ls_estimated_channel16[k].r, csi_rs_ls_estimated_channel16[k].i);
        }
        printf("\n");
      }
    }
#endif

    /// Channel interpolation

    for(uint16_t port_tx = 0; port_tx < csi_mapping->ports; port_tx++) {
      memset(csi_rs_estimated_channel_freq[ant_rx][port_tx], 0, (fp->ofdm_symbol_size + FILTER_MARGIN) * sizeof(c16_t));
    }

    for (int rb = csirs_config_pdu->start_rb; rb < (csirs_config_pdu->start_rb+csirs_config_pdu->nr_of_rbs); rb++) {

      // for freq density 0.5 checks if even or odd RB
      if(csirs_config_pdu->freq_density <= 1 && csirs_config_pdu->freq_density != (rb % 2)) {
        continue;
      }

      count++;

      uint16_t k = (fp->first_carrier_offset + rb * NR_NB_SC_PER_RB) % fp->ofdm_symbol_size;
      uint16_t k_offset = k + mem_offset;
      for(uint16_t port_tx = 0; port_tx < csi_mapping->ports; port_tx++) {
        c16_t csi_rs_ls_estimated_channel16 = csi_rs_ls_estimated_channel[ant_rx][port_tx][k];
        c16_t *csi_rs_estimated_channel16 = &csi_rs_estimated_channel_freq[ant_rx][port_tx][k_offset];
        if( (k == 0) || (k == fp->first_carrier_offset) ) { // Start of OFDM symbol case or first occupied subcarrier case
          multadd_real_vector_complex_scalar(filt24_start, csi_rs_ls_estimated_channel16, csi_rs_estimated_channel16, 24);
        } else if(((k + NR_NB_SC_PER_RB) >= fp->ofdm_symbol_size) ||
                   (rb == (csirs_config_pdu->start_rb+csirs_config_pdu->nr_of_rbs-1))) { // End of OFDM symbol case or Last occupied subcarrier case
          multadd_real_vector_complex_scalar(filt24_end, csi_rs_ls_estimated_channel16, csi_rs_estimated_channel16 - 12, 24);
        } else { // Middle case
          multadd_real_vector_complex_scalar(filt24_middle, csi_rs_ls_estimated_channel16, csi_rs_estimated_channel16 - 12, 24);
        }
      }
    }

    /// Power noise estimation
    AssertFatal(csirs_config_pdu->nr_of_rbs > 0, " nr_of_rbs needs to be greater than 0\n");
    uint16_t noise_real[fp->nb_antennas_rx][csi_mapping->ports][csirs_config_pdu->nr_of_rbs];
    uint16_t noise_imag[fp->nb_antennas_rx][csi_mapping->ports][csirs_config_pdu->nr_of_rbs];
    for (int rb = csirs_config_pdu->start_rb; rb < (csirs_config_pdu->start_rb+csirs_config_pdu->nr_of_rbs); rb++) {
      if (csirs_config_pdu->freq_density <= 1 && csirs_config_pdu->freq_density != (rb % 2)) {
        continue;
      }
      uint16_t k = (fp->first_carrier_offset + rb*NR_NB_SC_PER_RB) % fp->ofdm_symbol_size;
      uint16_t k_offset = k + mem_offset;
      for(uint16_t port_tx = 0; port_tx < csi_mapping->ports; port_tx++) {
        c16_t *csi_rs_ls_estimated_channel16 = &csi_rs_ls_estimated_channel[ant_rx][port_tx][k];
        c16_t *csi_rs_estimated_channel16 = &csi_rs_estimated_channel_freq[ant_rx][port_tx][k_offset];
        noise_real[ant_rx][port_tx][rb-csirs_config_pdu->start_rb] = abs(csi_rs_ls_estimated_channel16->r-csi_rs_estimated_channel16->r);
        noise_imag[ant_rx][port_tx][rb-csirs_config_pdu->start_rb] = abs(csi_rs_ls_estimated_channel16->i-csi_rs_estimated_channel16->i);
        maxh = cmax3(maxh, abs(csi_rs_estimated_channel16->r), abs(csi_rs_estimated_channel16->i));
      }
    }
    for(uint16_t port_tx = 0; port_tx < csi_mapping->ports; port_tx++) {
      *noise_power += (calc_power_csirs(noise_real[ant_rx][port_tx], csirs_config_pdu) + calc_power_csirs(noise_imag[ant_rx][port_tx],csirs_config_pdu));
    }

#ifdef NR_CSIRS_DEBUG
    for(int k = 0; k < fp->ofdm_symbol_size; k++) {
      int rb = k >= fp->first_carrier_offset ?
               (k - fp->first_carrier_offset)/NR_NB_SC_PER_RB :
               (k + fp->ofdm_symbol_size - fp->first_carrier_offset)/NR_NB_SC_PER_RB;
      LOG_I(NR_PHY, "(k = %4d) |\t", k);
      for(uint16_t port_tx = 0; port_tx < csi_mapping->ports; port_tx++) {
        c16_t *csi_rs_ls_estimated_channel16 = &csi_rs_ls_estimated_channel[ant_rx][port_tx][0];
        c16_t *csi_rs_estimated_channel16 = &csi_rs_estimated_channel_freq[ant_rx][port_tx][mem_offset];
        printf("Channel port_tx %d --> ant_rx %d : ls (%4d,%4d), int (%4d,%4d), noise (%4d,%4d) | ",
               port_tx+3000, ant_rx,
               csi_rs_ls_estimated_channel16[k].r, csi_rs_ls_estimated_channel16[k].i,
               csi_rs_estimated_channel16[k].r, csi_rs_estimated_channel16[k].i,
               rb >= csirs_config_pdu->start_rb+csirs_config_pdu->nr_of_rbs ? 0 : noise_real[ant_rx][port_tx][rb-csirs_config_pdu->start_rb],
               rb >= csirs_config_pdu->start_rb+csirs_config_pdu->nr_of_rbs ? 0 : noise_imag[ant_rx][port_tx][rb-csirs_config_pdu->start_rb]);
      }
      printf("\n");
    }
#endif

  }

  *noise_power /= (fp->nb_antennas_rx * csi_mapping->ports);
  *log2_maxh = log2_approx(maxh - 1);
  *log2_re = log2_approx(count - 1);

#ifdef NR_CSIRS_DEBUG
  LOG_I(NR_PHY, "Noise power estimation based on CSI-RS: %i\n", *noise_power);
#endif
  return 0;
}

/* --- 4×4 CSI-RS: averaged H^H H, approximate eigenvalues (power deflation), RI / rank-1 PMI --- */

static void nr_csirs_accum_hhh_nt(const NR_DL_FRAME_PARMS *fp,
                                   const fapi_nr_dl_config_csirs_pdu_rel15_t *csirs,
                                   uint8_t mem_offset,
                                   const c16_t (*H)[4][fp->ofdm_symbol_size + FILTER_MARGIN],
                                   int n_rx,
                                   double complex Racc[4][4],
                                   int *n_re)
{
  memset(Racc, 0, sizeof(double complex) * 16);
  *n_re = 0;
  for (int rb = csirs->start_rb; rb < csirs->start_rb + csirs->nr_of_rbs; rb++) {
    if (csirs->freq_density <= 1 && csirs->freq_density != (rb % 2))
      continue;
    uint16_t k0 = (fp->first_carrier_offset + rb * NR_NB_SC_PER_RB) % fp->ofdm_symbol_size;
    int k_base = k0 + mem_offset;
    for (int sc = 0; sc < NR_NB_SC_PER_RB; sc++) {
      int k_offset = k_base + sc;
      if (k_offset < 0 || k_offset >= fp->ofdm_symbol_size + FILTER_MARGIN)
        continue;
      for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
          double complex sij = 0;
          for (int rx = 0; rx < n_rx; rx++) {
            const c16_t hi = H[rx][i][k_offset];
            const c16_t hj = H[rx][j][k_offset];
            sij += conj((double)hi.r + I * (double)hi.i) * ((double)hj.r + I * (double)hj.i);
          }
          Racc[i][j] += sij;
        }
      }
      (*n_re)++;
    }
  }
}

static void nr_herm4_power_deflation_eigs(double complex Rin[4][4], double lam[4])
{
  double complex A[4][4];
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++)
      A[i][j] = Rin[i][j];

  for (int ev = 0; ev < 4; ev++) {
    double complex v[4] = {1, 1, 1, 1};
    for (int it = 0; it < 48; it++) {
      double complex w[4] = {0};
      for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
          w[i] += A[i][j] * v[j];
      double nrm2 = 0;
      for (int i = 0; i < 4; i++)
        nrm2 += creal(w[i] * conj(w[i]));
      const double nrm = sqrt(nrm2);
      if (nrm < 1e-20)
        break;
      for (int i = 0; i < 4; i++)
        v[i] = w[i] / nrm;
    }
    double complex Av[4] = {0};
    for (int i = 0; i < 4; i++)
      for (int j = 0; j < 4; j++)
        Av[i] += A[i][j] * v[j];
    double complex rq = 0;
    for (int i = 0; i < 4; i++)
      rq += conj(v[i]) * Av[i];
    lam[ev] = fmax(0.0, creal(rq));
    for (int i = 0; i < 4; i++)
      for (int j = 0; j < 4; j++)
        A[i][j] -= lam[ev] * v[i] * conj(v[j]);
  }
  for (int a = 0; a < 4; a++)
    for (int b = a + 1; b < 4; b++)
      if (lam[b] > lam[a]) {
        const double t = lam[a];
        lam[a] = lam[b];
        lam[b] = t;
      }
}

/* Dominant eigenvector of a 4x4 Hermitian matrix using power iteration.
 * For H \in C^(Nr x 4), this vector is the dominant right singular vector ("best V" column). */
static void nr_herm4_dominant_eigvec(const double complex Rin[4][4], double complex v_out[4], double *lam_out)
{
  double complex v[4] = {1.0 + 0.0 * I, 0.0 + 0.0 * I, 0.0 + 0.0 * I, 0.0 + 0.0 * I};
  for (int it = 0; it < 64; it++) {
    double complex w[4] = {0};
    for (int i = 0; i < 4; i++)
      for (int j = 0; j < 4; j++)
        w[i] += Rin[i][j] * v[j];
    double nrm2 = 0.0;
    for (int i = 0; i < 4; i++)
      nrm2 += creal(w[i] * conj(w[i]));
    const double nrm = sqrt(nrm2);
    if (nrm < 1e-20)
      break;
    for (int i = 0; i < 4; i++)
      v[i] = w[i] / nrm;
  }

  double complex Av[4] = {0};
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++)
      Av[i] += Rin[i][j] * v[j];

  double complex rq = 0;
  for (int i = 0; i < 4; i++)
    rq += conj(v[i]) * Av[i];

  for (int i = 0; i < 4; i++)
    v_out[i] = v[i];
  *lam_out = fmax(0.0, creal(rq));
}

static void nr_herm2_dominant_eigvec(const double complex Rin[2][2], double complex v_out[2], double *lam_out)
{
  double complex v[2] = {1.0 + 0.0 * I, 0.0 + 0.0 * I};
  for (int it = 0; it < 32; it++) {
    double complex w[2] = {0};
    for (int i = 0; i < 2; i++)
      for (int j = 0; j < 2; j++)
        w[i] += Rin[i][j] * v[j];
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
      Av[i] += Rin[i][j] * v[j];
  double complex rq = 0;
  for (int i = 0; i < 2; i++)
    rq += conj(v[i]) * Av[i];
  v_out[0] = v[0];
  v_out[1] = v[1];
  *lam_out = fmax(0.0, creal(rq));
}

/* Match tools/ai_fb/train_export_mlp_stub.py::_angular_delay_features():
 *   h_eff = mean_rx(H)                 [P x n_sc]
 *   H_sp  = fft(h_eff, axis=0, ortho)  [P x n_sc]  (spatial FFT per subcarrier)
 *   ad    = ifft(H_sp, axis=1, ortho)[:, :keep_rows]
 * then pad to 4xkeep_rows and flatten [Re; Im].
 * This is NOT the same as a per-port-only delay IDFT along frequency. */
static void nr_ai_fb_build_angular_delay_features(const NR_DL_FRAME_PARMS *fp,
                                                  const c16_t H[][4][fp->ofdm_symbol_size + FILTER_MARGIN],
                                                  int ports,
                                                  float out_feat[AI_FB_AD_IN])
{
  memset(out_feat, 0, sizeof(float) * AI_FB_AD_IN);
  const int n_sc = fp->ofdm_symbol_size;
  const int keep_rows = AI_FB_AD_ROWS;
  const int rows = keep_rows < n_sc ? keep_rows : n_sc;
  const int P = ports > AI_FB_AD_PORTS ? AI_FB_AD_PORTS : ports;
  if (P <= 0 || n_sc <= 0)
    return;

  double complex *h_mean = (double complex *)calloc((size_t)P * (size_t)n_sc, sizeof(double complex));
  double complex *h_sp = (double complex *)calloc((size_t)AI_FB_AD_PORTS * (size_t)n_sc, sizeof(double complex));
  if (!h_mean || !h_sp) {
    free(h_mean);
    free(h_sp);
    return;
  }

  for (int p = 0; p < P; p++) {
    for (int k = 0; k < n_sc; k++) {
      double complex h_avg = 0.0;
      for (int rx = 0; rx < fp->nb_antennas_rx; rx++) {
        const c16_t hs = H[rx][p][k];
        h_avg += (double)hs.r + I * (double)hs.i;
      }
      h_avg /= (double)fp->nb_antennas_rx;
      h_mean[p * n_sc + k] = h_avg;
    }
  }

  const double inv_sqrt_P = 1.0 / sqrt((double)P);
  for (int k = 0; k < n_sc; k++) {
    for (int i = 0; i < P; i++) {
      double complex acc = 0.0;
      for (int p = 0; p < P; p++)
        acc += h_mean[p * n_sc + k] * cexp(-I * (2.0 * M_PI * (double)p * (double)i) / (double)P);
      h_sp[i * n_sc + k] = acc * inv_sqrt_P;
    }
    for (int i = P; i < AI_FB_AD_PORTS; i++)
      h_sp[i * n_sc + k] = 0.0;
  }

  const double inv_sqrt_nsc = 1.0 / sqrt((double)n_sc);
  double complex ad[AI_FB_AD_PORTS][AI_FB_AD_ROWS];
  memset(ad, 0, sizeof(ad));
  for (int i = 0; i < AI_FB_AD_PORTS; i++) {
    for (int d = 0; d < rows; d++) {
      double complex s = 0.0;
      for (int k = 0; k < n_sc; k++)
        s += h_sp[i * n_sc + k] * cexp(I * (2.0 * M_PI * (double)k * (double)d) / (double)n_sc);
      ad[i][d] = s * inv_sqrt_nsc;
    }
  }

  for (int p = 0; p < AI_FB_AD_PORTS; p++) {
    for (int d = 0; d < keep_rows; d++) {
      const int idx = p * keep_rows + d;
      out_feat[idx] = (float)creal(ad[p][d]);
      out_feat[AI_FB_AD_PORTS * keep_rows + idx] = (float)cimag(ad[p][d]);
    }
  }

  free(h_mean);
  free(h_sp);
}

static void nr_ai_fb_normalize_angular_features(float feat[AI_FB_AD_IN])
{
  float max_abs = 0.0f;
  for (int i = 0; i < AI_FB_AD_IN; i++) {
    const float a = fabsf(feat[i]);
    if (a > max_abs)
      max_abs = a;
  }
  if (max_abs < 1e-9f)
    return;
  const float inv = 1.0f / max_abs;
  for (int i = 0; i < AI_FB_AD_IN; i++)
    feat[i] *= inv;
}

static bool nr_ai_fb_encode_dominant_v(const NR_DL_FRAME_PARMS *fp,
                                       const fapi_nr_dl_config_csirs_pdu_rel15_t *csirs_config_pdu,
                                       uint8_t mem_offset,
                                       const c16_t csi_rs_estimated_channel_freq[][4][fp->ofdm_symbol_size + FILTER_MARGIN],
                                       int ports,
                                       uint8_t out_payload[NFAPI_NR_AI_CSI_FB_LATENT_BYTES])
{
  const ai_fb_impl_mode_t impl_mode = (ai_fb_impl_mode_t)get_softmodem_params()->ai_fb_impl_mode;
  if (impl_mode == AI_FB_IMPL_ANGULAR_DELAY_MLP || impl_mode == AI_FB_IMPL_ANGULAR_DELAY_REFINENET) {
    static float last_valid_feat[AI_FB_AD_IN] = {0};
    static bool last_valid_feat_ok = false;
    float feat[AI_FB_AD_IN];
    nr_ai_fb_build_angular_delay_features(fp, csi_rs_estimated_channel_freq, ports, feat);
    nr_ai_fb_normalize_angular_features(feat);
    float fmin = feat[0], fmax = feat[0], fn2 = 0.0f;
    for (int i = 0; i < AI_FB_AD_IN; i++) {
      if (feat[i] < fmin)
        fmin = feat[i];
      if (feat[i] > fmax)
        fmax = feat[i];
      fn2 += feat[i] * feat[i];
    }
    const float fl2 = sqrtf(fn2);
    if (fl2 < 1e-9f && last_valid_feat_ok) {
      memcpy(feat, last_valid_feat, sizeof(last_valid_feat));
      if (get_softmodem_params()->print_csi_debug) {
        LOG_W(NR_PHY,
              "AI FB angular features low-energy (l2=%.6e), reusing last valid feature vector\n",
              fl2);
      }
    } else if (fl2 >= 1e-9f) {
      memcpy(last_valid_feat, feat, sizeof(last_valid_feat));
      last_valid_feat_ok = true;
    }
    if (get_softmodem_params()->print_csi_debug) {
      LOG_I(NR_PHY,
            "AI FB angular feature stats: min=%.6f max=%.6f l2=%.6f first=[%.6f %.6f %.6f %.6f %.6f %.6f]\n",
            fmin,
            fmax,
            fl2,
            feat[0],
            feat[1],
            feat[2],
            feat[3],
            feat[4],
            feat[5]);
    }
    if (impl_mode == AI_FB_IMPL_ANGULAR_DELAY_REFINENET)
      return ai_fb_encode_angular_refinenet_features(feat, out_payload);
    return ai_fb_encode_angular_delay_features(feat, out_payload);
  }
  if (impl_mode == AI_FB_IMPL_CSINET) {
    const c16_t *h0 = &csi_rs_estimated_channel_freq[0][0][0];
    if (ports == 2)
      return csinet_encode_wideband_2p(fp, h0, fp->nb_antennas_rx, 4, fp->ofdm_symbol_size, out_payload);
    if (ports == 4)
      return csinet_encode_wideband_4p(fp, h0, fp->nb_antennas_rx, 4, fp->ofdm_symbol_size, out_payload);
    return false;
  }
  if (ports == 2) {
    double complex R2[2][2] = {0};
    int n_re = 0;
    for (int aarx = 0; aarx < fp->nb_antennas_rx; aarx++) {
      for (int sc = 0; sc < fp->ofdm_symbol_size; sc++) {
        const c16_t h0s = csi_rs_estimated_channel_freq[aarx][0][sc];
        const c16_t h1s = csi_rs_estimated_channel_freq[aarx][1][sc];
        const double complex h0 = (double)h0s.r + I * (double)h0s.i;
        const double complex h1 = (double)h1s.r + I * (double)h1s.i;
        R2[0][0] += conj(h0) * h0;
        R2[0][1] += conj(h0) * h1;
        R2[1][0] += conj(h1) * h0;
        R2[1][1] += conj(h1) * h1;
        n_re++;
      }
    }
    if (n_re <= 0)
      return false;
    const double inv = 1.0 / (double)n_re;
    for (int i = 0; i < 2; i++)
      for (int j = 0; j < 2; j++)
        R2[i][j] *= inv;
    double complex v2[2];
    double lam = 0.0;
    nr_herm2_dominant_eigvec(R2, v2, &lam);
    if (lam < 1e-12)
      return false;
    return ai_fb_encode_rank1_2p(v2, impl_mode, out_payload);
  }

  if (ports != 4)
    return false;

  double complex Racc[4][4];
  int n_re = 0;
  nr_csirs_accum_hhh_nt(fp,
                        csirs_config_pdu,
                        mem_offset,
                        csi_rs_estimated_channel_freq,
                        fp->nb_antennas_rx,
                        Racc,
                        &n_re);
  if (n_re <= 0)
    return false;

  const double inv = 1.0 / (double)n_re;
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++)
      Racc[i][j] *= inv;

  double complex v[4];
  double lam = 0.0;
  nr_herm4_dominant_eigvec(Racc, v, &lam);
  if (lam < 1e-12)
    return false;

  return ai_fb_encode_rank1_4p(v, impl_mode, out_payload);
}

static uint8_t nr_ri_from_sorted_eigs(const double lam[4])
{
  if (lam[0] < 1e-6)
    return 0;
  int r = 1;
  /* Thresholds vs dominant eigenvalue lam[0]: tuned for 4×4 CSI-RS / rfsim so rank 4 is reachable when the
   * channel Gram is near full rank (was often stopping at RI=2 with stricter ratios). */
  if (lam[1] > lam[0] * 5e-5)
    r++;
  if (lam[2] > lam[0] * 2e-4)
    r++;
  if (lam[3] > lam[0] * 1e-3)
    r++;
  return (uint8_t)(r - 1);
}

static void nr_type1_fill_v_lm(int N1,
                               int N2,
                               int O1,
                               int O2,
                               double complex v_lm[][N2 * O2 + O2][N2 * N1])
{
  const int max_l = N1 * O1 + 4 * O1;
  const int max_m = N2 * O2 + O2;
  double complex v[max_l][N1];
  for (int ll = 0; ll < max_l; ll++) {
    for (int nn = 0; nn < N1; nn++)
      v[ll][nn] = cexp(I * (2 * M_PI * nn * ll) / (N1 * O1));
  }
  double complex u[max_m][N2];
  for (int mm = 0; mm < max_m; mm++) {
    for (int nn = 0; nn < N2; nn++)
      u[mm][nn] = cexp(I * (2 * M_PI * nn * mm) / (N2 * O2));
  }
  for (int ll = 0; ll < max_l; ll++) {
    for (int mm = 0; mm < max_m; mm++) {
      for (int nn1 = 0; nn1 < N1; nn1++)
        for (int nn2 = 0; nn2 < N2; nn2++)
          v_lm[ll][mm][nn1 * N2 + nn2] = v[ll][nn1] * u[mm][nn2];
    }
  }
}

static double nr_wh_r_w(const double complex R[4][4], const double complex w[4])
{
  double complex Rw[4] = {0};
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++)
      Rw[i] += R[i][j] * w[j];
  double complex acc = 0;
  for (int i = 0; i < 4; i++)
    acc += conj(w[i]) * Rw[i];
  return creal(acc);
}

static int nr_csi_rs_ri_estimation_4x4(const PHY_VARS_NR_UE *ue,
                                       const fapi_nr_dl_config_csirs_pdu_rel15_t *csirs_config_pdu,
                                       uint8_t mem_offset,
                                       c16_t csi_rs_estimated_channel_freq[][4][ue->frame_parms.ofdm_symbol_size + FILTER_MARGIN],
                                       uint8_t *rank_indicator)
{
  const NR_DL_FRAME_PARMS *fp = &ue->frame_parms;
  double complex Racc[4][4];
  int n_re = 0;
  nr_csirs_accum_hhh_nt(fp, csirs_config_pdu, mem_offset, csi_rs_estimated_channel_freq, fp->nb_antennas_rx, Racc, &n_re);
  if (n_re <= 0)
    return -1;
  const double inv = 1.0 / (double)n_re;
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++)
      Racc[i][j] *= inv;
  double lam[4];
  nr_herm4_power_deflation_eigs(Racc, lam);
  const uint8_t rank_raw = nr_ri_from_sorted_eigs(lam);
  /* Report full RI; rank-1 and rank-4 Type I PMI for 4 ports are implemented. Ranks 2–3 PMI not implemented yet. */
  const uint8_t max_rank_indicator_for_pmi = 3;
  *rank_indicator = rank_raw > max_rank_indicator_for_pmi ? max_rank_indicator_for_pmi : rank_raw;
  if (get_softmodem_params()->print_csi_debug) {
    LOG_I(NR_PHY,
          "4×4 RI: eig ratios %.2e %.2e %.2e → rank_indicator=%u (RI=%u)\n",
          lam[0] > 0 ? lam[1] / lam[0] : 0.0,
          lam[0] > 0 ? lam[2] / lam[0] : 0.0,
          lam[0] > 0 ? lam[3] / lam[0] : 0.0,
          *rank_indicator,
          *rank_indicator + 1u);
  }
  return 0;
}

static int nr_csi_rs_ri_estimation_2x4(const PHY_VARS_NR_UE *ue,
                                       const fapi_nr_dl_config_csirs_pdu_rel15_t *csirs_config_pdu,
                                       uint8_t mem_offset,
                                       c16_t csi_rs_estimated_channel_freq[][4][ue->frame_parms.ofdm_symbol_size + FILTER_MARGIN],
                                       uint8_t *rank_indicator)
{
  const NR_DL_FRAME_PARMS *fp = &ue->frame_parms;
  double complex Racc[4][4];
  int n_re = 0;
  nr_csirs_accum_hhh_nt(fp, csirs_config_pdu, mem_offset, csi_rs_estimated_channel_freq, fp->nb_antennas_rx, Racc, &n_re);
  if (n_re <= 0)
    return -1;
  const double inv = 1.0 / (double)n_re;
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++)
      Racc[i][j] *= inv;
  double lam[4];
  nr_herm4_power_deflation_eigs(Racc, lam);
  /* UE has only 2 RX chains in 2x4; cap RI to max 2 layers. */
  uint8_t rank_raw = 0;
  if (lam[0] > 1e-6 && lam[1] > lam[0] * 5e-5)
    rank_raw = 1;
  *rank_indicator = rank_raw;
  if (get_softmodem_params()->print_csi_debug) {
    LOG_I(NR_PHY,
          "2×4 RI: eig ratios %.2e %.2e %.2e -> rank_indicator=%u (RI=%u)\n",
          lam[0] > 0 ? lam[1] / lam[0] : 0.0,
          lam[0] > 0 ? lam[2] / lam[0] : 0.0,
          lam[0] > 0 ? lam[3] / lam[0] : 0.0,
          *rank_indicator,
          *rank_indicator + 1u);
  }
  return 0;
}

static int nr_csi_rs_pmi_estimation_4port_rank1(const PHY_VARS_NR_UE *ue,
                                                const fapi_nr_dl_config_csirs_pdu_rel15_t *csirs_config_pdu,
                                                uint8_t mem_offset,
                                                const c16_t csi_rs_estimated_channel_freq[][4][ue->frame_parms.ofdm_symbol_size + FILTER_MARGIN],
                                                const uint32_t interference_plus_noise_power,
                                                const int16_t log2_re,
                                                uint8_t *i1,
                                                uint8_t *i2,
                                                int32_t *precoded_sinr_dB)
{
  const NR_DL_FRAME_PARMS *fp = &ue->frame_parms;
  (void)log2_re;
  /* Match gNB init_DL_MIMO_codebook for N1=N2=2, four logical ports, XP=1 (single-pol 2×2 UPA). */
  const int N1 = 2;
  const int N2 = 2;
  const int O1 = 4;
  const int O2 = 4;
  const int K1 = 1;
  const int K2 = 1;
  const int I2 = 4;

  const int max_l = N1 * O1 + 4 * O1;
  const int max_m = N2 * O2 + O2;
  double complex v_lm[max_l][max_m][N2 * N1];
  nr_type1_fill_v_lm(N1, N2, O1, O2, v_lm);

  double complex R[4][4];
  int n_re = 0;
  nr_csirs_accum_hhh_nt(fp, csirs_config_pdu, mem_offset, csi_rs_estimated_channel_freq, fp->nb_antennas_rx, R, &n_re);
  if (n_re <= 0)
    return -1;
  const double inv = 1.0 / (double)n_re;
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++)
      R[i][j] *= inv;

  double best_metric = -1;
  int best_ll = 0, best_mm = 0, best_nn = 0;
  for (int k2 = 0; k2 < K2; k2++) {
    for (int k1 = 0; k1 < K1; k1++) {
      for (int mm = 0; mm < N2 * O2; mm++) {
        for (int ll = 0; ll < N1 * O1; ll++) {
          for (int nn = 0; nn < I2; nn++) {
            const int llc = ll + (k1 * O1 * 0);
            const int mmc = mm + (k2 * O2 * 0);
            double complex wcol[4];
            for (int p = 0; p < N1 * N2; p++)
              wcol[p] = (1.0 / sqrt(1.0)) * v_lm[llc][mmc][p];
            double wn2 = 0;
            for (int p = 0; p < 4; p++)
              wn2 += creal(wcol[p] * conj(wcol[p]));
            wn2 = sqrt(wn2);
            if (wn2 < 1e-12)
              continue;
            for (int p = 0; p < 4; p++)
              wcol[p] /= wn2;
            const double sig = nr_wh_r_w(R, wcol);
            if (sig > best_metric) {
              best_metric = sig;
              best_ll = ll;
              best_mm = mm;
              best_nn = nn;
            }
          }
        }
      }
    }
  }

  i1[0] = (uint8_t)best_ll;
  i1[1] = (uint8_t)best_mm;
  i1[2] = 0;
  i2[0] = (uint8_t)best_nn;
  if (interference_plus_noise_power > 0 && best_metric >= 0) {
    const double sinr_lin_d = best_metric / (double)interference_plus_noise_power;
    const uint32_t sinr_lin = sinr_lin_d > (double)UINT32_MAX ? UINT32_MAX : (uint32_t)llround(sinr_lin_d);
    *precoded_sinr_dB = dB_fixed(sinr_lin > 0 ? sinr_lin : 1);
  } else
    *precoded_sinr_dB = 0;

  if (get_softmodem_params()->print_csi_debug) {
    double complex best_v[4];
    for (int p = 0; p < 4; p++)
      best_v[p] = v_lm[best_ll][best_mm][p];
    double vn2 = 0.0;
    for (int p = 0; p < 4; p++)
      vn2 += creal(best_v[p] * conj(best_v[p]));
    const double vn = sqrt(vn2);
    if (vn > 1e-12) {
      for (int p = 0; p < 4; p++)
        best_v[p] /= vn;
    }

    /* Current CSI payload strategy conveys best-V implicitly via codebook indices (PMI i1/i2),
     * not raw complex vector entries. */
    const int bits_i11 = (int)ceil(log2((double)(N1 * O1))); /* ll index */
    const int bits_i12 = (int)ceil(log2((double)(N2 * O2))); /* mm index */
    const int bits_i2 = (int)ceil(log2((double)I2));         /* nn index */
    const int pmi_index_bits = bits_i11 + bits_i12 + bits_i2;

    /* If raw best-V were packed directly as Q1.15 complex entries:
     * 4 complex numbers * (real16 + imag16) = 128 bits -> split 64/64 in part1/part2. */
    const int raw_v_bits = 4 * 2 * 16;
    const int raw_v_part1_bits = 64;
    const int raw_v_part2_bits = 64;

    LOG_I(NR_PHY,
          "Best V (rank-1) shape [4 x 1]: "
          "[(%.4f%+.4fj), (%.4f%+.4fj), (%.4f%+.4fj), (%.4f%+.4fj)]\n",
          creal(best_v[0]), cimag(best_v[0]),
          creal(best_v[1]), cimag(best_v[1]),
          creal(best_v[2]), cimag(best_v[2]),
          creal(best_v[3]), cimag(best_v[3]));
    LOG_I(NR_PHY,
          "Best V payload sizing: current PMI-index packing=%d bits (i11=%d, i12=%d, i2=%d); "
          "raw-V Q1.15 packing=%d bits (part1=%d, part2=%d)\n",
          pmi_index_bits,
          bits_i11,
          bits_i12,
          bits_i2,
          raw_v_bits,
          raw_v_part1_bits,
          raw_v_part2_bits);
  }

  return 0;
}

/* TS 38.214 / gNB get_k1_k2_indices mapping */
static void ue_get_k1_k2_indices(int layers, int N1, int N2, int i13, int *k1, int *k2)
{
  *k1 = 0;
  *k2 = 0;
  if (layers == 2) {
    if (N2 == 1)
      *k1 = i13;
    else if (N1 == N2) {
      *k1 = i13 & 1;
      *k2 = i13 >> 1;
    } else {
      *k1 = (i13 & 1) + (i13 == 3);
      *k2 = (i13 == 2);
    }
  }
  if (layers == 3 || layers == 4) {
    if (N2 == 1)
      *k1 = i13 + 1;
    else if (N1 == 2 && N2 == 2) {
      *k1 = !(i13 & 1);
      *k2 = (i13 > 0);
    } else {
      if (i13 == 0)
        *k1 = 1;
      if (i13 == 1)
        *k2 = 1;
      if (i13 == 2) {
        *k1 = 1;
        *k2 = 1;
      }
      if (i13 == 3)
        *k1 = 2;
    }
  }
}

/* Match openair2/LAYER2/NR_MAC_gNB/config.c precoding_weigths_generation for L=4, XP=1 (four logical ports). */
static void nr_type1_build_w_4layer_4port(int ll,
                                          int mm,
                                          int k1,
                                          int k2,
                                          int nn,
                                          int N1,
                                          int N2,
                                          int O1,
                                          int O2,
                                          int L,
                                          const double complex v_lm[][N2 * O2 + O2][N2 * N1],
                                          double complex W[N2 * N1][4])
{
  (void)nn;
  for (int j_col = 0; j_col < L; j_col++) {
    const int llc = ll + (k1 * O1 * (j_col & 1));
    const int mmc = mm + (k2 * O2 * (j_col & 1));
    for (int i_rows = 0; i_rows < N1 * N2; i_rows++)
      W[i_rows][j_col] = sqrt(1 / (double)L) * v_lm[llc][mmc][i_rows];
  }
}

/* M = W^H R W, R and W are 4×4 (Nt=4, L=4). */
static void nr_form_m_wherm_rw(const double complex R[4][4], const double complex W[4][4], double complex M[4][4])
{
  double complex RW[4][4];
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) {
      double complex s = 0;
      for (int k = 0; k < 4; k++)
        s += R[i][k] * W[k][j];
      RW[i][j] = s;
    }
  }
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) {
      double complex s = 0;
      for (int k = 0; k < 4; k++)
        s += conj(W[k][i]) * RW[k][j];
      M[i][j] = s;
    }
  }
}

/* log2 det(B) for Hermitian SPD B = LL^H, L lower triangular with real positive diagonal. */
static int nr_cholesky_herm_lower_logdet(const double complex B[4][4], int n, double *log2_det)
{
  double complex L[4][4];
  memset(L, 0, sizeof(L));
  for (int j = 0; j < n; j++) {
    double complex s = B[j][j];
    for (int k = 0; k < j; k++)
      s -= L[j][k] * conj(L[j][k]);
    const double ljj2 = creal(s);
    if (ljj2 <= 1e-14)
      return -1;
    L[j][j] = sqrt(ljj2);
    for (int i = j + 1; i < n; i++) {
      s = B[i][j];
      for (int k = 0; k < j; k++)
        s -= L[i][k] * conj(L[j][k]);
      L[i][j] = s / L[j][j];
    }
  }
  double acc = 0;
  for (int i = 0; i < n; i++)
    acc += 2.0 * log2(creal(L[i][i]));
  *log2_det = acc;
  return 0;
}

static inline void ue_get_k1_k2_counts(const int N1, const int N2, const int layers, int *K1, int *K2);

static int nr_csi_rs_pmi_estimation_4port_rank4(const PHY_VARS_NR_UE *ue,
                                                const fapi_nr_dl_config_csirs_pdu_rel15_t *csirs_config_pdu,
                                                uint8_t mem_offset,
                                                const c16_t csi_rs_estimated_channel_freq[][4][ue->frame_parms.ofdm_symbol_size + FILTER_MARGIN],
                                                const uint32_t interference_plus_noise_power,
                                                const int16_t log2_re,
                                                uint8_t *i1,
                                                uint8_t *i2,
                                                int32_t *precoded_sinr_dB)
{
  (void)log2_re;
  const NR_DL_FRAME_PARMS *fp = &ue->frame_parms;
  const int N1 = 2;
  const int N2 = 2;
  const int O1 = 4;
  const int O2 = 4;
  const int L = 4;
  const int max_l = N1 * O1 + 4 * O1;
  const int max_m = N2 * O2 + O2;
  double complex v_lm[max_l][max_m][N2 * N1];
  nr_type1_fill_v_lm(N1, N2, O1, O2, v_lm);

  double complex R[4][4];
  int n_re = 0;
  nr_csirs_accum_hhh_nt(fp, csirs_config_pdu, mem_offset, csi_rs_estimated_channel_freq, fp->nb_antennas_rx, R, &n_re);
  if (n_re <= 0)
    return -1;
  const double inv = 1.0 / (double)n_re;
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++)
      R[i][j] *= inv;

  const double inv_noise = 1.0 / (double)interference_plus_noise_power;
  double best_cap = -1e300;
  double best_tr = 0;
  int best_i11 = 0, best_i12 = 0, best_i13 = 0, best_i2 = 0;

  int K1 = 0, K2 = 0;
  ue_get_k1_k2_counts(N1, N2, L, &K1, &K2);
  const int I13 = K1 * K2;
  for (int i13 = 0; i13 < I13; i13++) {
    int k1 = 0, k2 = 0;
    ue_get_k1_k2_indices(L, N1, N2, i13, &k1, &k2);
    for (int i11 = 0; i11 < N1 * O1; i11++) {
      for (int i12 = 0; i12 < N2 * O2; i12++) {
        for (int i2b = 0; i2b < 2; i2b++) {
          double complex W[4][4];
          nr_type1_build_w_4layer_4port(i11, i12, k1, k2, i2b, N1, N2, O1, O2, L, v_lm, W);
          double complex M[4][4];
          nr_form_m_wherm_rw(R, W, M);
          double complex B[4][4];
          for (int a = 0; a < 4; a++) {
            for (int b = 0; b < 4; b++) {
              B[a][b] = (a == b ? 1.0 : 0.0) + inv_noise * M[a][b];
            }
          }
          double log2_det = 0;
          double tr_m = 0;
          for (int d = 0; d < 4; d++)
            tr_m += creal(M[d][d]);
          if (nr_cholesky_herm_lower_logdet(B, 4, &log2_det) != 0)
            continue;
          if (log2_det > best_cap) {
            best_cap = log2_det;
            best_tr = tr_m;
            best_i11 = i11;
            best_i12 = i12;
            best_i13 = i13;
            best_i2 = i2b;
          }
        }
      }
    }
  }

  if (best_cap < -1e200)
    return -1;

  i1[0] = (uint8_t)best_i11;
  i1[1] = (uint8_t)best_i12;
  i1[2] = (uint8_t)best_i13;
  i2[0] = (uint8_t)best_i2;

  if (interference_plus_noise_power > 0 && best_tr >= 0) {
    const double sinr_lin_d = best_tr / (4.0 * (double)interference_plus_noise_power);
    const uint32_t sinr_lin = sinr_lin_d > (double)UINT32_MAX ? UINT32_MAX : (uint32_t)llround(sinr_lin_d);
    *precoded_sinr_dB = dB_fixed(sinr_lin > 0 ? sinr_lin : 1);
  } else
    *precoded_sinr_dB = 0;
  return 0;
}

static inline void ue_get_k1_k2_counts(const int N1, const int N2, const int layers, int *K1, int *K2)
{
  if (layers == 1) {
    *K1 = 1;
    *K2 = 1;
  } else if (layers == 2) {
    *K2 = N2 == 1 ? 1 : 2;
    if (N2 == N1 || N1 == 2)
      *K1 = 2;
    else if (N2 == 1)
      *K1 = 4;
    else
      *K1 = 3;
  } else {
    *K2 = N2 == 1 ? 1 : 2;
    *K1 = N1 == 6 ? 5 : N1;
  }
}

static int nr_csi_rs_pmi_estimation_4port_rank23(const PHY_VARS_NR_UE *ue,
                                                  const fapi_nr_dl_config_csirs_pdu_rel15_t *csirs_config_pdu,
                                                  uint8_t mem_offset,
                                                  const c16_t csi_rs_estimated_channel_freq[][4][ue->frame_parms.ofdm_symbol_size + FILTER_MARGIN],
                                                  const uint32_t interference_plus_noise_power,
                                                  const int16_t log2_re,
                                                  uint8_t rank_indicator,
                                                  uint8_t *i1,
                                                  uint8_t *i2,
                                                  int32_t *precoded_sinr_dB)
{
  (void)log2_re;
  const NR_DL_FRAME_PARMS *fp = &ue->frame_parms;
  const int N1 = 2, N2 = 2, O1 = 4, O2 = 4;
  const int L = rank_indicator + 1;
  AssertFatal(L == 2 || L == 3, "rank23 estimator called with L=%d\n", L);
  const int max_l = N1 * O1 + 4 * O1;
  const int max_m = N2 * O2 + O2;
  double complex v_lm[max_l][max_m][N2 * N1];
  nr_type1_fill_v_lm(N1, N2, O1, O2, v_lm);
  double complex R[4][4];
  int n_re = 0;
  nr_csirs_accum_hhh_nt(fp, csirs_config_pdu, mem_offset, csi_rs_estimated_channel_freq, fp->nb_antennas_rx, R, &n_re);
  if (n_re <= 0)
    return -1;
  const double inv = 1.0 / (double)n_re;
  for (int a = 0; a < 4; a++)
    for (int b = 0; b < 4; b++)
      R[a][b] *= inv;
  const double inv_noise = 1.0 / (double)interference_plus_noise_power;
  int K1 = 0, K2 = 0;
  ue_get_k1_k2_counts(N1, N2, L, &K1, &K2);
  const int I13 = K1 * K2;
  double best_cap = -1e300, best_tr = 0;
  int best_i11 = 0, best_i12 = 0, best_i13 = 0, best_i2 = 0;
  for (int i13 = 0; i13 < I13; i13++) {
    int k1 = 0, k2 = 0;
    ue_get_k1_k2_indices(L, N1, N2, i13, &k1, &k2);
    for (int i11 = 0; i11 < N1 * O1; i11++) {
      for (int i12 = 0; i12 < N2 * O2; i12++) {
        for (int i2b = 0; i2b < 2; i2b++) {
          double complex W[4][4] = {0};
          for (int j_col = 0; j_col < L; j_col++) {
            const int llc = i11 + (k1 * O1 * (j_col & 1));
            const int mmc = i12 + (k2 * O2 * (j_col & 1));
            for (int r = 0; r < N1 * N2; r++)
              W[r][j_col] = sqrt(1 / (double)L) * v_lm[llc][mmc][r];
          }
          double complex M[4][4];
          nr_form_m_wherm_rw(R, W, M);
          double complex B[4][4] = {0};
          double tr_m = 0;
          for (int a = 0; a < L; a++) {
            tr_m += creal(M[a][a]);
            for (int b = 0; b < L; b++)
              B[a][b] = (a == b ? 1.0 : 0.0) + inv_noise * M[a][b];
          }
          double log2_det = 0;
          if (nr_cholesky_herm_lower_logdet(B, L, &log2_det) != 0)
            continue;
          if (log2_det > best_cap) {
            best_cap = log2_det;
            best_tr = tr_m;
            best_i11 = i11;
            best_i12 = i12;
            best_i13 = i13;
            best_i2 = i2b;
          }
        }
      }
    }
  }
  if (best_cap < -1e200)
    return -1;
  i1[0] = (uint8_t)best_i11;
  i1[1] = (uint8_t)best_i12;
  i1[2] = (uint8_t)best_i13;
  i2[0] = (uint8_t)best_i2;
  if (interference_plus_noise_power > 0 && best_tr >= 0) {
    const double sinr_lin_d = best_tr / ((double)L * (double)interference_plus_noise_power);
    const uint32_t sinr_lin = sinr_lin_d > (double)UINT32_MAX ? UINT32_MAX : (uint32_t)llround(sinr_lin_d);
    *precoded_sinr_dB = dB_fixed(sinr_lin > 0 ? sinr_lin : 1);
  } else
    *precoded_sinr_dB = 0;
  return 0;
}

int nr_csi_rs_ri_estimation(const PHY_VARS_NR_UE *ue,
                            const fapi_nr_dl_config_csirs_pdu_rel15_t *csirs_config_pdu,
                            const nr_csi_info_t *nr_csi_info,
                            const uint8_t N_ports,
                            uint8_t mem_offset,
                            c16_t csi_rs_estimated_channel_freq[][N_ports][ue->frame_parms.ofdm_symbol_size + FILTER_MARGIN],
                            const int16_t log2_maxh,
                            uint8_t *rank_indicator)
{
  (void)nr_csi_info;
  const NR_DL_FRAME_PARMS *frame_parms = &ue->frame_parms;
  const int16_t cond_dB_threshold = 5;
  int count = 0;
  *rank_indicator = 0;

  if (ue->frame_parms.nb_antennas_rx == 1 || N_ports == 1) {
    return 0;
  }
  if (ue->frame_parms.nb_antennas_rx == 2 && N_ports == 4) {
    return nr_csi_rs_ri_estimation_2x4(ue, csirs_config_pdu, mem_offset, csi_rs_estimated_channel_freq, rank_indicator);
  }
  if (ue->frame_parms.nb_antennas_rx == 4 && N_ports == 4) {
    return nr_csi_rs_ri_estimation_4x4(ue, csirs_config_pdu, mem_offset, csi_rs_estimated_channel_freq, rank_indicator);
  }
  if (!(ue->frame_parms.nb_antennas_rx == 2 && N_ports == 2)) {
    LOG_W(NR_PHY, "Rank indicator computation is not implemented for %i x %i system\n",
          ue->frame_parms.nb_antennas_rx, N_ports);
    return -1;
  }

  /* Example 2x2: Hh x H =
  *            | conjch00 conjch10 | x | ch00 ch01 | = | conjch00*ch00+conjch10*ch10 conjch00*ch01+conjch10*ch11 |
  *            | conjch01 conjch11 |   | ch10 ch11 |   | conjch01*ch00+conjch11*ch10 conjch01*ch01+conjch11*ch11 |
  */

  c16_t csi_rs_estimated_conjch_ch[frame_parms->nb_antennas_rx][N_ports][frame_parms->nb_antennas_rx][N_ports]
                                  [frame_parms->ofdm_symbol_size + FILTER_MARGIN] __attribute__((aligned(32)));
  int32_t csi_rs_estimated_A_MF[N_ports][N_ports][frame_parms->ofdm_symbol_size + FILTER_MARGIN] __attribute__((aligned(32)));
  int32_t csi_rs_estimated_A_MF_sq[N_ports][N_ports][frame_parms->ofdm_symbol_size + FILTER_MARGIN] __attribute__((aligned(32)));
  int32_t csi_rs_estimated_determ_fin[frame_parms->ofdm_symbol_size + FILTER_MARGIN] __attribute__((aligned(32)));
  int32_t csi_rs_estimated_numer_fin[frame_parms->ofdm_symbol_size + FILTER_MARGIN] __attribute__((aligned(32)));
  const uint8_t sum_shift = 1; // log2(2x2) = 2, which is a shift of 1 bit
  
  for (int rb = csirs_config_pdu->start_rb; rb < (csirs_config_pdu->start_rb+csirs_config_pdu->nr_of_rbs); rb++) {

    if (csirs_config_pdu->freq_density <= 1 && csirs_config_pdu->freq_density != (rb % 2)) {
      continue;
    }
    uint16_t k = (frame_parms->first_carrier_offset + rb*NR_NB_SC_PER_RB) % frame_parms->ofdm_symbol_size;
    uint16_t k_offset = k + mem_offset;

    for (int ant_rx_conjch = 0; ant_rx_conjch < frame_parms->nb_antennas_rx; ant_rx_conjch++) {
      for(uint16_t port_tx_conjch = 0; port_tx_conjch < N_ports; port_tx_conjch++) {
        for (int ant_rx_ch = 0; ant_rx_ch < frame_parms->nb_antennas_rx; ant_rx_ch++) {
          for(uint16_t port_tx_ch = 0; port_tx_ch < N_ports; port_tx_ch++) {

            // conjch x ch computation
            nr_conjch0_mult_ch1(&csi_rs_estimated_channel_freq[ant_rx_conjch][port_tx_conjch][k_offset],
                                &csi_rs_estimated_channel_freq[ant_rx_ch][port_tx_ch][k_offset],
                                &csi_rs_estimated_conjch_ch[ant_rx_conjch][port_tx_conjch][ant_rx_ch][port_tx_ch][k_offset],
                                1,
                                log2_maxh);

            // construct Hh x H elements
            if(ant_rx_conjch == ant_rx_ch) {
              nr_a_sum_b(
                  (c16_t *)&csi_rs_estimated_A_MF[port_tx_conjch][port_tx_ch][k_offset], (c16_t *)&csi_rs_estimated_conjch_ch[ant_rx_conjch][port_tx_conjch][ant_rx_ch][port_tx_ch][k_offset], 1);
            }
          }
        }
      }
    }

    // compute the determinant of A_MF (denominator)
    nr_det_A_MF_2x2(&csi_rs_estimated_A_MF[0][0][k_offset],
                    &csi_rs_estimated_A_MF[0][1][k_offset],
                    &csi_rs_estimated_A_MF[1][0][k_offset],
                    &csi_rs_estimated_A_MF[1][1][k_offset],
                    &csi_rs_estimated_determ_fin[k_offset],
                    1);

    // compute the square of A_MF (numerator)
    nr_squared_matrix_element(&csi_rs_estimated_A_MF[0][0][k_offset], &csi_rs_estimated_A_MF_sq[0][0][k_offset], 1);
    nr_squared_matrix_element(&csi_rs_estimated_A_MF[0][1][k_offset], &csi_rs_estimated_A_MF_sq[0][1][k_offset], 1);
    nr_squared_matrix_element(&csi_rs_estimated_A_MF[1][0][k_offset], &csi_rs_estimated_A_MF_sq[1][0][k_offset], 1);
    nr_squared_matrix_element(&csi_rs_estimated_A_MF[1][1][k_offset], &csi_rs_estimated_A_MF_sq[1][1][k_offset], 1);
    nr_numer_2x2(&csi_rs_estimated_A_MF_sq[0][0][k_offset],
                 &csi_rs_estimated_A_MF_sq[0][1][k_offset],
                 &csi_rs_estimated_A_MF_sq[1][0][k_offset],
                 &csi_rs_estimated_A_MF_sq[1][1][k_offset],
                 &csi_rs_estimated_numer_fin[k_offset],
                 1);

#ifdef NR_CSIRS_DEBUG
    for(uint16_t port_tx_conjch = 0; port_tx_conjch < N_ports; port_tx_conjch++) {
      for(uint16_t port_tx_ch = 0; port_tx_ch < N_ports; port_tx_ch++) {
        c16_t *csi_rs_estimated_A_MF_k = (c16_t *) &csi_rs_estimated_A_MF[port_tx_conjch][port_tx_ch][k_offset];
        LOG_I(NR_PHY, "(%i) csi_rs_estimated_A_MF[%i][%i] = (%i, %i)\n",
              k, port_tx_conjch, port_tx_ch, csi_rs_estimated_A_MF_k->r, csi_rs_estimated_A_MF_k->i);
        c16_t *csi_rs_estimated_A_MF_sq_k = (c16_t *) &csi_rs_estimated_A_MF_sq[port_tx_conjch][port_tx_ch][k_offset];
        LOG_I(NR_PHY, "(%i) csi_rs_estimated_A_MF_sq[%i][%i] = (%i, %i)\n",
              k, port_tx_conjch, port_tx_ch, csi_rs_estimated_A_MF_sq_k->r, csi_rs_estimated_A_MF_sq_k->i);
      }
    }
    LOG_I(NR_PHY, "(%i) csi_rs_estimated_determ_fin = %i\n", k, csi_rs_estimated_determ_fin[k_offset]);
    LOG_I(NR_PHY, "(%i) csi_rs_estimated_numer_fin = %i\n", k, csi_rs_estimated_numer_fin[k_offset]>>sum_shift);
#endif

    // compute the conditional number
    for (int sc_idx=0; sc_idx < NR_NB_SC_PER_RB; sc_idx++) {
      int8_t csi_rs_estimated_denum_db = dB_fixed(csi_rs_estimated_determ_fin[k_offset + sc_idx]);
      int8_t csi_rs_estimated_numer_db = dB_fixed(csi_rs_estimated_numer_fin[k_offset + sc_idx]>>sum_shift);
      int8_t cond_db = csi_rs_estimated_numer_db - csi_rs_estimated_denum_db;

#ifdef NR_CSIRS_DEBUG
      LOG_I(NR_PHY, "csi_rs_estimated_denum_db = %i\n", csi_rs_estimated_denum_db);
      LOG_I(NR_PHY, "csi_rs_estimated_numer_db = %i\n", csi_rs_estimated_numer_db);
      LOG_I(NR_PHY, "cond_db = %i\n", cond_db);
#endif

      if (cond_db < cond_dB_threshold) {
        count++;
      } else {
        count--;
      }
    }
  }

  // conditional number is lower than cond_dB_threshold in half on more REs
  if (count > 0) {
    *rank_indicator = 1;
  }

#ifdef NR_CSIRS_DEBUG
  LOG_I(NR_PHY, "count = %i\n", count);
  LOG_I(NR_PHY, "rank = %i\n", (*rank_indicator)+1);
#endif

  return 0;
}

int nr_csi_rs_pmi_estimation(const PHY_VARS_NR_UE *ue,
                             const fapi_nr_dl_config_csirs_pdu_rel15_t *csirs_config_pdu,
                             const nr_csi_info_t *nr_csi_info,
                             const uint8_t N_ports,
                             uint8_t mem_offset,
                             const c16_t csi_rs_estimated_channel_freq[][N_ports][ue->frame_parms.ofdm_symbol_size + FILTER_MARGIN],
                             const uint32_t interference_plus_noise_power,
                             const uint8_t rank_indicator,
                             const int16_t log2_re,
                             uint8_t *i1,
                             uint8_t *i2,
                             int32_t *precoded_sinr_dB)
{
  const NR_DL_FRAME_PARMS *frame_parms = &ue->frame_parms;

  // i1 is a three-element vector in the form of [i11 i12 i13], when CodebookType is specified as 'Type1SinglePanel'.
  // Note that i13 is not applicable when the number of transmission layers is one of {1, 5, 6, 7, 8}.
  // i2, for 'Type1SinglePanel' codebook type, it is a scalar when PMIMode is specified as 'wideband', and when PMIMode
  // is specified as 'subband' or when PRGSize, the length of the i2 vector equals to the number of subbands or PRGs.
  // Note that when the number of CSI-RS ports is 2, the applicable codebook type is 'Type1SinglePanel'. In this case,
  // the precoding matrix is obtained by a single index (i2 field here) based on TS 38.214 Table 5.2.2.2.1-1.
  // The first column is applicable if the UE is reporting a Rank = 1, whereas the second column is applicable if the
  // UE is reporting a Rank = 2.

  if (interference_plus_noise_power == 0) {
    return 0;
  }

  if (N_ports == 4 && get_softmodem_params()->print_csi_debug) {
    double complex Racc[4][4];
    int n_re_dbg = 0;
    const c16_t (*H4)[4][ue->frame_parms.ofdm_symbol_size + FILTER_MARGIN] =
        (const c16_t (*)[4][ue->frame_parms.ofdm_symbol_size + FILTER_MARGIN])csi_rs_estimated_channel_freq;
    nr_csirs_accum_hhh_nt(frame_parms,
                          csirs_config_pdu,
                          mem_offset,
                          H4,
                          frame_parms->nb_antennas_rx,
                          Racc,
                          &n_re_dbg);
    if (n_re_dbg > 0) {
      const double inv = 1.0 / (double)n_re_dbg;
      for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
          Racc[i][j] *= inv;

      double complex v_best[4];
      double lam_best = 0.0;
      nr_herm4_dominant_eigvec(Racc, v_best, &lam_best);

      /* Current CSI feedback packs codebook indices (PMI), not raw V entries. */
      const int N1 = 2, N2 = 2, O1 = 4, O2 = 4, I2 = 4;
      const int bits_i11 = (int)ceil(log2((double)(N1 * O1)));
      const int bits_i12 = (int)ceil(log2((double)(N2 * O2)));
      const int bits_i2 = (int)ceil(log2((double)I2));
      const int pmi_index_bits = bits_i11 + bits_i12 + bits_i2;

      const int raw_v_bits = 4 * 2 * 16; // 4 complex entries, Q1.15 (real+imag)
      const int raw_v_part1_bits = 64;
      const int raw_v_part2_bits = 64;

      LOG_I(NR_PHY,
            "Dominant V (from H^H H) shape [4 x 1], RI path=%u (layers=%u): "
            "[(%.4f%+.4fj), (%.4f%+.4fj), (%.4f%+.4fj), (%.4f%+.4fj)]\n",
            (unsigned)rank_indicator,
            (unsigned)rank_indicator + 1u,
            creal(v_best[0]), cimag(v_best[0]),
            creal(v_best[1]), cimag(v_best[1]),
            creal(v_best[2]), cimag(v_best[2]),
            creal(v_best[3]), cimag(v_best[3]));
      LOG_I(NR_PHY,
            "Dominant V payload sizing: current PMI-index packing=%d bits (i11=%d, i12=%d, i2=%d); "
            "raw-V Q1.15 packing=%d bits (part1=%d, part2=%d)\n",
            pmi_index_bits,
            bits_i11,
            bits_i12,
            bits_i2,
            raw_v_bits,
            raw_v_part1_bits,
            raw_v_part2_bits);
    }
  }

  if (N_ports == 4 && rank_indicator == 0) {
    return nr_csi_rs_pmi_estimation_4port_rank1(ue,
                                                csirs_config_pdu,
                                                mem_offset,
                                                csi_rs_estimated_channel_freq,
                                                interference_plus_noise_power,
                                                log2_re,
                                                i1,
                                                i2,
                                                precoded_sinr_dB);
  }
  if (N_ports == 4 && rank_indicator == 3) {
    return nr_csi_rs_pmi_estimation_4port_rank4(ue,
                                                csirs_config_pdu,
                                                mem_offset,
                                                csi_rs_estimated_channel_freq,
                                                interference_plus_noise_power,
                                                log2_re,
                                                i1,
                                                i2,
                                                precoded_sinr_dB);
  }
  if (N_ports == 4 && (rank_indicator == 1 || rank_indicator == 2)) {
    return nr_csi_rs_pmi_estimation_4port_rank23(ue,
                                                 csirs_config_pdu,
                                                 mem_offset,
                                                 csi_rs_estimated_channel_freq,
                                                 interference_plus_noise_power,
                                                 log2_re,
                                                 rank_indicator,
                                                 i1,
                                                 i2,
                                                 precoded_sinr_dB);
  }
  if (N_ports == 4 && rank_indicator > 3) {
    LOG_W(NR_PHY, "PMI for 4 CSI-RS ports with rank %u is not implemented\n", (unsigned)rank_indicator + 1u);
    return -1;
  }

  if (N_ports == 1) {
    // SISO case: SINR = E[|h|^2] / noise_power. No PMI to estimate.
    int64_t signal_power = 0;
    int count = 0;

    for (int rb = csirs_config_pdu->start_rb; rb < (csirs_config_pdu->start_rb + csirs_config_pdu->nr_of_rbs); rb++) {
      if (csirs_config_pdu->freq_density <= 1 && csirs_config_pdu->freq_density != (rb % 2)) {
        continue;
      }
      uint16_t k = (frame_parms->first_carrier_offset + rb * NR_NB_SC_PER_RB) % frame_parms->ofdm_symbol_size;
      uint16_t k_offset = k + mem_offset;

      const c16_t h = csi_rs_estimated_channel_freq[0][0][k_offset];
      signal_power += (int64_t)h.r * h.r + (int64_t)h.i * h.i;
      count++;
    }

    if (count > 0) {
      const int64_t avg_signal_power = signal_power / count;
      const uint32_t sinr = avg_signal_power / interference_plus_noise_power;
      *precoded_sinr_dB = dB_fixed(sinr);
    }

    return 0;
  }

  if (N_ports != 2) {
    LOG_W(NR_PHY, "PMI computation is not implemented for %u CSI-RS ports\n", (unsigned)N_ports);
    return -1;
  }

  if(rank_indicator == 0 || rank_indicator == 1) {
    c64_t sum[4] = {0};
    c64_t sum2[4] = {0};
    int64_t tested_precoded_sinr[4] = {0};

    for (int rb = csirs_config_pdu->start_rb; rb < (csirs_config_pdu->start_rb+csirs_config_pdu->nr_of_rbs); rb++) {

      if (csirs_config_pdu->freq_density <= 1 && csirs_config_pdu->freq_density != (rb % 2)) {
        continue;
      }
      uint16_t k = (frame_parms->first_carrier_offset + rb * NR_NB_SC_PER_RB) % frame_parms->ofdm_symbol_size;
      uint16_t k_offset = k + mem_offset;
      for (int ant_rx = 0; ant_rx < frame_parms->nb_antennas_rx; ant_rx++) {
        const c16_t p0 = csi_rs_estimated_channel_freq[ant_rx][0][k_offset];
        const c16_t p1 = csi_rs_estimated_channel_freq[ant_rx][1][k_offset];

        // H_p0 + 1*H_p1 = (H_p0_re + H_p1_re) + 1j*(H_p0_im + H_p1_im)
        sum[0].r += (p0.r + p1.r);
        sum[0].i += (p0.i + p1.i);
        sum2[0].r += (sum[0].r * sum[0].r) >> log2_re;
        sum2[0].i += (sum[0].i * sum[0].i) >> log2_re;

        // H_p0 + 1j*H_p1 = (H_p0_re - H_p1_im) + 1j*(H_p0_im + H_p1_re)
        sum[1].r += (p0.r - p1.i);
        sum[1].i += (p0.i + p1.r);
        sum2[1].r += (sum[1].r * sum[1].r) >> log2_re;
        sum2[1].i += (sum[1].i * sum[1].i) >> log2_re;

        // H_p0 - 1*H_p1 = (H_p0_re - H_p1_re) + 1j*(H_p0_im - H_p1_im)
        sum[2].r += (p0.r - p1.r);
        sum[2].i += (p0.i - p1.i);
        sum2[2].r += (sum[2].r * sum[2].r) >> log2_re;
        sum2[2].i += (sum[2].i * sum[2].i) >> log2_re;

        // H_p0 - 1j*H_p1 = (H_p0_re + H_p1_im) + 1j*(H_p0_im - H_p1_re)
        sum[3].r += (p0.r + p1.i);
        sum[3].i += (p0.i - p1.r);
        sum2[3].r += (sum[3].r * sum[3].r) >> log2_re;
        sum2[3].i += (sum[3].i * sum[3].i) >> log2_re;
      }
    }

    // We should perform >>nr_csi_info->log2_re here for all terms, but since sum2_re and sum2_im can be high values,
    // we performed this above.
    for(int p = 0; p<4; p++) {
      int64_t power_re = sum2[p].r - (sum[p].r >> log2_re) * (sum[p].r >> log2_re);
      int64_t power_im = sum2[p].i - (sum[p].i >> log2_re) * (sum[p].i >> log2_re);
      tested_precoded_sinr[p] = (power_re + power_im) / interference_plus_noise_power;
    }

    if(rank_indicator == 0) {
      for(int tested_i2 = 0; tested_i2 < 4; tested_i2++) {
        if(tested_precoded_sinr[tested_i2] > tested_precoded_sinr[i2[0]]) {
          i2[0] = tested_i2;
        }
      }
      *precoded_sinr_dB = dB_fixed(tested_precoded_sinr[i2[0]]);
    } else {
      const int64_t score0 = tested_precoded_sinr[0] + tested_precoded_sinr[2];
      const int64_t score1 = tested_precoded_sinr[1] + tested_precoded_sinr[3];
      const uint8_t raw_i2 = (score0 > score1) ? 0 : 1;
      uint8_t majority_i2 = raw_i2;
      uint8_t effective_i2 = raw_i2;
      static uint8_t i2_hist[64] = {0};
      static uint8_t i2_hist_count = 0;
      static uint8_t i2_hist_pos = 0;
      static uint8_t i2_eff = 0;
      static uint8_t i2_pending = 0;
      static uint8_t i2_pending_count = 0;
      static bool i2_eff_init = false;

      const int cfg_window = get_softmodem_params()->csi_i2_hyst_window;
      const int cfg_hyst = get_softmodem_params()->csi_i2_hyst_threshold;
      const uint8_t window = (uint8_t)((cfg_window < 0) ? 0 : ((cfg_window > 64) ? 64 : cfg_window));
      const uint8_t hyst = (uint8_t)((cfg_hyst < 0) ? 0 : ((cfg_hyst > 255) ? 255 : cfg_hyst));

      if (window > 1) {
        i2_hist[i2_hist_pos] = raw_i2;
        i2_hist_pos = (uint8_t)((i2_hist_pos + 1) % 64);
        if (i2_hist_count < window)
          i2_hist_count++;
        uint8_t ones = 0;
        for (uint8_t i = 0; i < i2_hist_count; i++)
          ones += i2_hist[i] ? 1 : 0;
        majority_i2 = (2 * ones >= i2_hist_count) ? 1 : 0;
      }

      if (!i2_eff_init) {
        i2_eff = majority_i2;
        i2_pending = majority_i2;
        i2_pending_count = 0;
        i2_eff_init = true;
      } else if (hyst > 0) {
        if (majority_i2 == i2_eff) {
          i2_pending = majority_i2;
          i2_pending_count = 0;
        } else if (majority_i2 == i2_pending) {
          if (i2_pending_count < 255)
            i2_pending_count++;
        } else {
          i2_pending = majority_i2;
          i2_pending_count = 1;
        }
        if (i2_pending_count >= hyst) {
          i2_eff = majority_i2;
          i2_pending_count = 0;
        }
      } else {
        i2_eff = majority_i2;
      }
      effective_i2 = i2_eff;
      i2[0] = effective_i2;
      *precoded_sinr_dB = dB_fixed((tested_precoded_sinr[i2[0]] + tested_precoded_sinr[i2[0] + 2]) >> 1);

      if (get_softmodem_params()->print_csi_debug) {
        const int64_t margin = llabs(score0 - score1);
        const double denom = (double)(llabs(score0) + llabs(score1) + 1);
        const double confidence = (double)margin / denom;
        LOG_I(NR_PHY,
              "UE CSI 2p rank2 i2 decision: raw=%u majority=%u effective=%u score0=%lld score1=%lld margin=%lld conf=%.6f window=%u hyst=%u\n",
              (unsigned)raw_i2,
              (unsigned)majority_i2,
              (unsigned)effective_i2,
              (long long)score0,
              (long long)score1,
              (long long)margin,
              confidence,
              (unsigned)window,
              (unsigned)hyst);
      }
    }

  } else {
    LOG_W(NR_PHY, "PMI computation is not implemented for rank indicator %i\n", rank_indicator+1);
    return -1;
  }

  return 0;
}

int nr_csi_rs_cqi_estimation(const uint32_t precoded_sinr,
                             uint8_t *cqi) {

  *cqi = 0;

  // Default SINR table for an AWGN channel for SISO scenario, considering 0.1 BLER condition and TS 38.214 Table 5.2.2.1-2
  if(precoded_sinr>0 && precoded_sinr<=2) {
    *cqi = 4;
  } else if(precoded_sinr==3) {
    *cqi = 5;
  } else if(precoded_sinr>3 && precoded_sinr<=5) {
    *cqi = 6;
  } else if(precoded_sinr>5 && precoded_sinr<=7) {
    *cqi = 7;
  } else if(precoded_sinr>7 && precoded_sinr<=9) {
    *cqi = 8;
  } else if(precoded_sinr==10) {
    *cqi = 9;
  } else if(precoded_sinr>10 && precoded_sinr<=12) {
    *cqi = 10;
  } else if(precoded_sinr>12 && precoded_sinr<=15) {
    *cqi = 11;
  } else if(precoded_sinr==16) {
    *cqi = 12;
  } else if(precoded_sinr>16 && precoded_sinr<=18) {
    *cqi = 13;
  } else if(precoded_sinr==19) {
    *cqi = 14;
  } else if(precoded_sinr>19) {
    *cqi = 15;
  }

  return 0;
}

static void nr_csi_im_power_estimation(const PHY_VARS_NR_UE *ue,
                                       const fapi_nr_dl_config_csiim_pdu_rel15_t *csiim_config_pdu,
                                       uint32_t *interference_plus_noise_power,
                                       const c16_t rxdataF[][ue->frame_parms.samples_per_slot_wCP])
{
  const NR_DL_FRAME_PARMS *frame_parms = &ue->frame_parms;

  const uint16_t end_rb = csiim_config_pdu->start_rb + csiim_config_pdu->nr_of_rbs > csiim_config_pdu->bwp_size ?
                          csiim_config_pdu->bwp_size : csiim_config_pdu->start_rb + csiim_config_pdu->nr_of_rbs;

  int32_t count = 0;
  int32_t sum_re = 0;
  int32_t sum_im = 0;
  int32_t sum2_re = 0;
  int32_t sum2_im = 0;

  int l_csiim[4] = {-1, -1, -1, -1};

  for(int symb_idx = 0; symb_idx < 4; symb_idx++) {

    uint8_t symb = csiim_config_pdu->l_csiim[symb_idx];
    bool done = false;
    for (int symb_idx2 = 0; symb_idx2 < symb_idx; symb_idx2++) {
      if (l_csiim[symb_idx2] == symb) {
        done = true;
      }
    }

    if (done) {
      continue;
    }

    l_csiim[symb_idx] = symb;
    uint64_t symbol_offset = symb*frame_parms->ofdm_symbol_size;

    for (int ant_rx = 0; ant_rx < frame_parms->nb_antennas_rx; ant_rx++) {

      const c16_t *rx_signal = &rxdataF[ant_rx][symbol_offset];

      for (int rb = csiim_config_pdu->start_rb; rb < end_rb; rb++) {

        uint16_t sc0_offset = (frame_parms->first_carrier_offset + rb*NR_NB_SC_PER_RB) % frame_parms->ofdm_symbol_size;

        for (int sc_idx = 0; sc_idx < 4; sc_idx++) {

          uint16_t sc = sc0_offset + csiim_config_pdu->k_csiim[sc_idx];
          if (sc >= frame_parms->ofdm_symbol_size) {
            sc -= frame_parms->ofdm_symbol_size;
          }

#ifdef NR_CSIIM_DEBUG
          LOG_I(NR_PHY, "(ant_rx %i, sc %i) real %i, imag %i\n", ant_rx, sc, rx_signal[sc].r, rx_signal[sc].i);
#endif

          if (sc == 0) // skip DC for noise power estimation
            continue;
          sum_re += rx_signal[sc].r;
          sum_im += rx_signal[sc].i;
          sum2_re += rx_signal[sc].r * rx_signal[sc].r;
          sum2_im += rx_signal[sc].i * rx_signal[sc].i;
          count++;
        }
      }
    }
  }

  int32_t power_re = sum2_re / count - (sum_re / count) * (sum_re / count);
  int32_t power_im = sum2_im / count - (sum_im / count) * (sum_im / count);

  *interference_plus_noise_power = power_re + power_im;

#ifdef NR_CSIIM_DEBUG
  LOG_I(NR_PHY, "interference_plus_noise_power based on CSI-IM = %i\n", *interference_plus_noise_power);
#endif
}

void nr_ue_csi_im_procedures(PHY_VARS_NR_UE *ue,
                             const UE_nr_rxtx_proc_t *proc,
                             const c16_t rxdataF[][ue->frame_parms.samples_per_slot_wCP],
                             const fapi_nr_dl_config_csiim_pdu_rel15_t *csiim_config_pdu)
{

#ifdef NR_CSIIM_DEBUG
  LOG_I(NR_PHY, "csiim_config_pdu->bwp_size = %i\n", csiim_config_pdu->bwp_size);
  LOG_I(NR_PHY, "csiim_config_pdu->bwp_start = %i\n", csiim_config_pdu->bwp_start);
  LOG_I(NR_PHY, "csiim_config_pdu->subcarrier_spacing = %i\n", csiim_config_pdu->subcarrier_spacing);
  LOG_I(NR_PHY, "csiim_config_pdu->start_rb = %i\n", csiim_config_pdu->start_rb);
  LOG_I(NR_PHY, "csiim_config_pdu->nr_of_rbs = %i\n", csiim_config_pdu->nr_of_rbs);
  LOG_I(NR_PHY, "csiim_config_pdu->k_csiim = %i.%i.%i.%i\n", csiim_config_pdu->k_csiim[0], csiim_config_pdu->k_csiim[1], csiim_config_pdu->k_csiim[2], csiim_config_pdu->k_csiim[3]);
  LOG_I(NR_PHY, "csiim_config_pdu->l_csiim = %i.%i.%i.%i\n", csiim_config_pdu->l_csiim[0], csiim_config_pdu->l_csiim[1], csiim_config_pdu->l_csiim[2], csiim_config_pdu->l_csiim[3]);
#endif

  nr_csi_im_power_estimation(ue, csiim_config_pdu, &ue->nr_csi_info->interference_plus_noise_power, rxdataF);
  ue->nr_csi_info->csi_im_meas_computed = true;
}

void nr_ue_csi_rs_procedures(PHY_VARS_NR_UE *ue,
                             const UE_nr_rxtx_proc_t *proc,
                             const c16_t rxdataF[][ue->frame_parms.samples_per_slot_wCP],
                             fapi_nr_dl_config_csirs_pdu_rel15_t *csirs_config_pdu)
{

#ifdef NR_CSIRS_DEBUG
  LOG_I(NR_PHY, "csirs_config_pdu->subcarrier_spacing = %i\n", csirs_config_pdu->subcarrier_spacing);
  LOG_I(NR_PHY, "csirs_config_pdu->cyclic_prefix = %i\n", csirs_config_pdu->cyclic_prefix);
  LOG_I(NR_PHY, "csirs_config_pdu->start_rb = %i\n", csirs_config_pdu->start_rb);
  LOG_I(NR_PHY, "csirs_config_pdu->nr_of_rbs = %i\n", csirs_config_pdu->nr_of_rbs);
  LOG_I(NR_PHY, "csirs_config_pdu->csi_type = %i (0:TRS, 1:CSI-RS NZP, 2:CSI-RS ZP)\n", csirs_config_pdu->csi_type);
  LOG_I(NR_PHY, "csirs_config_pdu->row = %i\n", csirs_config_pdu->row);
  LOG_I(NR_PHY, "csirs_config_pdu->freq_domain = %i\n", csirs_config_pdu->freq_domain);
  LOG_I(NR_PHY, "csirs_config_pdu->symb_l0 = %i\n", csirs_config_pdu->symb_l0);
  LOG_I(NR_PHY, "csirs_config_pdu->symb_l1 = %i\n", csirs_config_pdu->symb_l1);
  LOG_I(NR_PHY, "csirs_config_pdu->cdm_type = %i\n", csirs_config_pdu->cdm_type);
  LOG_I(NR_PHY, "csirs_config_pdu->freq_density = %i (0: dot5 (even RB), 1: dot5 (odd RB), 2: one, 3: three)\n", csirs_config_pdu->freq_density);
  LOG_I(NR_PHY, "csirs_config_pdu->scramb_id = %i\n", csirs_config_pdu->scramb_id);
  LOG_I(NR_PHY, "csirs_config_pdu->power_control_offset = %i\n", csirs_config_pdu->power_control_offset);
  LOG_I(NR_PHY, "csirs_config_pdu->power_control_offset_ss = %i\n", csirs_config_pdu->power_control_offset_ss);
#endif

  if(csirs_config_pdu->csi_type == 0) {
    LOG_E(NR_PHY, "Handling of CSI-RS for tracking not handled yet at PHY\n");
    return;
  }

  if(csirs_config_pdu->csi_type == 2) {
    LOG_E(NR_PHY, "Handling of ZP CSI-RS not handled yet at PHY\n");
    return;
  }

  const NR_DL_FRAME_PARMS *frame_parms = &ue->frame_parms;
  csi_mapping_parms_t mapping_parms = get_csi_mapping_parms(csirs_config_pdu->row,
                                                            csirs_config_pdu->freq_domain,
                                                            csirs_config_pdu->symb_l0,
                                                            csirs_config_pdu->symb_l1);
  nr_csi_info_t *csi_info = ue->nr_csi_info;
  nr_generate_csi_rs(frame_parms,
                     &mapping_parms,
                     AMP,
                     proc->nr_slot_rx,
                     csirs_config_pdu->freq_density,
                     csirs_config_pdu->start_rb,
                     csirs_config_pdu->nr_of_rbs,
                     csirs_config_pdu->symb_l0,
                     csirs_config_pdu->symb_l1,
                     csirs_config_pdu->row,
                     csirs_config_pdu->scramb_id,
                     csirs_config_pdu->power_control_offset_ss,
                     csirs_config_pdu->cdm_type,
                     csi_info->csi_rs_generated_signal);

  csi_info->csi_rs_generated_signal_bits = log2_approx(AMP);

  c16_t csi_rs_ls_estimated_channel[frame_parms->nb_antennas_rx][mapping_parms.ports][frame_parms->ofdm_symbol_size];
  c16_t csi_rs_estimated_channel_freq[frame_parms->nb_antennas_rx][mapping_parms.ports]
                                     [frame_parms->ofdm_symbol_size + FILTER_MARGIN];

  // (long)&csi_rs_estimated_channel_freq[0][0][frame_parms->first_carrier_offset] & 0x1F
  // gives us the remainder of the integer division by 32 of the memory address
  // By subtracting the previous value of 32, we know how much is left to have a multiple of 32.
  // Doing >> 2 <=> /sizeof(int32_t), we know what is the index offset of the array.
  uint8_t mem_offset = (((32 - ((long)&csi_rs_estimated_channel_freq[0][0][frame_parms->first_carrier_offset])) & 0x1F) >> 2);
  int CDM_group_size = get_cdm_group_size(csirs_config_pdu->cdm_type);
  c16_t csi_rs_received_signal[frame_parms->nb_antennas_rx][frame_parms->samples_per_slot_wCP];
  uint32_t rsrp = 0;
  int rsrp_dBm = 0;
  nr_get_csi_rs_signal(ue,
                       proc,
                       csirs_config_pdu,
                       csi_info,
                       &mapping_parms,
                       CDM_group_size,
                       csi_rs_received_signal,
                       &rsrp,
                       &rsrp_dBm,
                       rxdataF);


  if (csirs_config_pdu->measurement_bitmap == 0) {
    LOG_D(NR_PHY, "No CSI-RS measurements configured\n");
    return;
  }

  uint32_t noise_power = 0;
  int16_t log2_re = 0;
  int16_t log2_maxh = 0;
  /* Run channel estimation when any report (RI/PMI/CQI) is needed, or when only RSRP
   * is requested (measurement_bitmap == 1) but we want to feed imscope or record H.
   * With pdsch_AntennaPorts_N1=1 the gNB often configures cri_RSRP (num_antenna_ports < 4),
   * which gives measurement_bitmap == 1; without this we would skip H and imscope/record get nothing. */
  if (csirs_config_pdu->measurement_bitmap >= 1) {
    nr_csi_rs_channel_estimation(frame_parms,
                                 proc,
                                 csirs_config_pdu,
                                 csi_info,
                                 (const c16_t **)csi_info->csi_rs_generated_signal,
                                 csi_rs_received_signal,
                                 &mapping_parms,
                                 CDM_group_size,
                                 mem_offset,
                                 csi_rs_ls_estimated_channel,
                                 csi_rs_estimated_channel_freq,
                                 &log2_re,
                                 &log2_maxh,
                                 &noise_power);

    if (get_softmodem_params()->print_csi_debug) {
      const long h_ls_entries = (long)frame_parms->nb_antennas_rx * mapping_parms.ports * frame_parms->ofdm_symbol_size;
      const long h_freq_entries =
          (long)frame_parms->nb_antennas_rx * mapping_parms.ports * (frame_parms->ofdm_symbol_size + FILTER_MARGIN);
      LOG_I(NR_PHY,
            "CSI raw channel tensor sizes: H_ls shape [%d x %d x %d], entries=%ld, bytes=%ld | "
            "H_freq shape [%d x %d x %d], entries=%ld, bytes=%ld\n",
            frame_parms->nb_antennas_rx,
            mapping_parms.ports,
            frame_parms->ofdm_symbol_size,
            h_ls_entries,
            h_ls_entries * (long)sizeof(c16_t),
            frame_parms->nb_antennas_rx,
            mapping_parms.ports,
            frame_parms->ofdm_symbol_size + FILTER_MARGIN,
            h_freq_entries,
            h_freq_entries * (long)sizeof(c16_t));

      if (mapping_parms.ports == 4) {
        double complex Racc[4][4];
        int n_re_dbg = 0;
        nr_csirs_accum_hhh_nt(frame_parms,
                              csirs_config_pdu,
                              mem_offset,
                              csi_rs_estimated_channel_freq,
                              frame_parms->nb_antennas_rx,
                              Racc,
                              &n_re_dbg);
        if (n_re_dbg > 0) {
          const double inv = 1.0 / (double)n_re_dbg;
          for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
              Racc[i][j] *= inv;

          double lam[4];
          nr_herm4_power_deflation_eigs(Racc, lam);
          const double sigma0 = sqrt(fmax(0.0, lam[0]));
          const double sigma1 = sqrt(fmax(0.0, lam[1]));
          const double sigma2 = sqrt(fmax(0.0, lam[2]));
          const double sigma3 = sqrt(fmax(0.0, lam[3]));

          double complex v_best[4];
          double lam_best = 0.0;
          nr_herm4_dominant_eigvec(Racc, v_best, &lam_best);

          LOG_I(NR_PHY,
                "CSI SVD approx: H shape [%d x %d x n_re=%d], Sigma shape [4], "
                "singular values=[%.3e %.3e %.3e %.3e], best V shape [4 x 1], sigma_best=%.3e\n",
                frame_parms->nb_antennas_rx,
                mapping_parms.ports,
                n_re_dbg,
                sigma0,
                sigma1,
                sigma2,
                sigma3,
                sqrt(fmax(0.0, lam_best)));
        } else {
          LOG_I(NR_PHY, "CSI SVD approx skipped: n_re=0 after CSI-RS accumulation\n");
        }
      } else {
        LOG_I(NR_PHY, "CSI SVD approx currently logged for 4-port CSI-RS only (ports=%d)\n", mapping_parms.ports);
      }
    }

    /* Feed CSI-RS channel estimates to imscope if enabled */
    {
      metadata meta = { .slot = proc->nr_slot_rx, .frame = proc->frame_rx };
      const int lineSz = frame_parms->nb_antennas_rx * mapping_parms.ports
                         * (frame_parms->ofdm_symbol_size + FILTER_MARGIN);
      UEscopeCopyWithMetadata(ue,
                              ueCsirsChEstimate,
                              &csi_rs_estimated_channel_freq[0][0][0],
                              sizeof(c16_t),
                              1,
                              lineSz,
                              0,
                              &meta);
    }
  }

  uint8_t rank_indicator = 0;
  // bit 1 in bitmap to indicate RI measurment
  if (csirs_config_pdu->measurement_bitmap & 2) {
    nr_csi_rs_ri_estimation(ue,
                            csirs_config_pdu,
                            csi_info,
                            mapping_parms.ports,
                            mem_offset,
                            csi_rs_estimated_channel_freq,
                            log2_maxh,
                            &rank_indicator);
  }

  uint8_t i1[3] = {0};
  uint8_t i2[1] = {0};
  uint8_t cqi = 0;
  int32_t precoded_sinr_dB = 0;
  LOG_I(NR_PHY, "-------------------------------------------CSI-RS----------------------------\n");
  // bit 3 in bitmap to indicate RI measurment
  if (csirs_config_pdu->measurement_bitmap & 8) {
    nr_csi_rs_pmi_estimation(ue,
                             csirs_config_pdu,
                             csi_info,
                             mapping_parms.ports,
                             mem_offset,
                             csi_rs_estimated_channel_freq,
                             csi_info->csi_im_meas_computed ? csi_info->interference_plus_noise_power : noise_power,
                             rank_indicator,
                             log2_re,
                             i1,
                             i2,
                             &precoded_sinr_dB);

    // bit 4 in bitmap to indicate RI measurment
    if(csirs_config_pdu->measurement_bitmap & 16)
      nr_csi_rs_cqi_estimation(precoded_sinr_dB, &cqi);
  }

  switch (csirs_config_pdu->measurement_bitmap) {
    case 1 :
      LOG_I(NR_PHY, "[UE %d] RSRP = %i dBm\n", ue->Mod_id, rsrp_dBm);
      break;
    case 26 :
      LOG_I(NR_PHY, "RI = %i i1 = %i.%i.%i, i2 = %i, SINR = %i dB, CQI = %i\n",
            rank_indicator + 1, i1[0], i1[1], i1[2], i2[0], precoded_sinr_dB, cqi);
      break;
    case 27 :
      LOG_I(NR_PHY, "RSRP = %i dBm, RI = %i i1 = %i.%i.%i, i2 = %i, SINR = %i dB, CQI = %i\n",
            rsrp_dBm, rank_indicator + 1, i1[0], i1[1], i1[2], i2[0], precoded_sinr_dB, cqi);
      break;
    default :
      AssertFatal(false, "Not supported measurement configuration\n");
  }

  /* Record CSI report (and channel estimates when available) for ML when csi_record_path is set.
   * CSV is written for any measurement (bitmap >= 1); H bin when channel estimation was done (bitmap >= 1). */
  if (ue->csi_record_path && ue->csi_record_path[0] != '\0') {
    const c16_t *H_ptr = (csirs_config_pdu->measurement_bitmap >= 1)
                         ? (const c16_t *)&csi_rs_estimated_channel_freq[0][0][0]
                         : NULL;
    csi_record_write(ue,
                     proc,
                     frame_parms->nb_antennas_rx,
                     mapping_parms.ports,
                     frame_parms->ofdm_symbol_size + FILTER_MARGIN,
                     H_ptr,
                     rsrp_dBm,
                     rank_indicator,
                     i1,
                     i2[0],
                     cqi,
                     precoded_sinr_dB);
  }

  // Send CSI measurements to MAC
  if (!ue->if_inst || !ue->if_inst->dl_indication)
    return;

  /* Wideband Type I single-panel: pack into pmi_x1 (LSB = i13 in gNB get_pm_index unpack order). */
  uint16_t i1_packed = i1[0];
  if (mapping_parms.ports == 4) {
    const unsigned b11 = 3;
    const unsigned b12 = 3;
    const unsigned b13 = 2;
    if (rank_indicator > 0) {
      /* RI=2/3/4: i11 (MSB) | i12 | i13 (LSB) — matches nr_mac_common set_bitlen + get_pm_index. */
      i1_packed = (uint16_t)((((unsigned)i1[0] & ((1u << b11) - 1u)) << (b12 + b13))
                              | (((unsigned)i1[1] & ((1u << b12) - 1u)) << b13)
                              | ((unsigned)i1[2] & ((1u << b13) - 1u)));
    } else {
      i1_packed = (uint16_t)(((unsigned)i1[0] & ((1u << b11) - 1u)) << b12) | ((unsigned)i1[1] & ((1u << b12) - 1u));
    }
    if (get_softmodem_params()->print_csi_debug) {
      const unsigned ri_payload_rank = (rank_indicator == 0) ? 1u : (unsigned)rank_indicator + 1u;
      LOG_I(NR_PHY,
            "UE PHY PMI pack trace: RI_raw=%u (layers=%u), RI_payload_rank=%u, i11=%u, i12=%u, i13=%u, packed_i1=0x%x, i2=0x%x\n",
            (unsigned)rank_indicator,
            (unsigned)rank_indicator + 1u,
            ri_payload_rank,
            (unsigned)i1[0],
            (unsigned)i1[1],
            (unsigned)i1[2],
            (unsigned)i1_packed,
            (unsigned)*i2);
    }
  }

  bool ai_fb_valid = false;
  uint8_t ai_fb_payload[NFAPI_NR_AI_CSI_FB_LATENT_BYTES] = {0};
  if ((mapping_parms.ports == 4 || mapping_parms.ports == 2)
      && (get_softmodem_params()->ai_fb_ulsch_enable || get_softmodem_params()->ai_fb_pucch_replace)) {
    ai_fb_valid = nr_ai_fb_encode_dominant_v(frame_parms,
                                             csirs_config_pdu,
                                             mem_offset,
                                             csi_rs_estimated_channel_freq,
                                             mapping_parms.ports,
                                             ai_fb_payload);
    if (get_softmodem_params()->print_csi_debug && ai_fb_valid) {
      LOG_I(NR_PHY,
            "AI feedback latent ready for UL-SCH (ports=%u, %d bits): [%d,%d,%d,%d,%d,%d]\n",
            (unsigned)mapping_parms.ports,
            NFAPI_NR_AI_CSI_FB_LATENT_BYTES * 8,
            (int8_t)ai_fb_payload[0],
            (int8_t)ai_fb_payload[1],
            (int8_t)ai_fb_payload[2],
            (int8_t)ai_fb_payload[3],
            (int8_t)ai_fb_payload[4],
            (int8_t)ai_fb_payload[5]);
    }
  }

  fapi_nr_l1_measurements_t l1_measurements = {
    .gNB_index = proc->gNB_id,
    .meas_type = NFAPI_NR_CSI_MEAS,
    .Nid_cell = frame_parms->Nid_cell,
    .is_neighboring_cell = false,
    .rsrp_dBm = rsrp_dBm,
    .rank_indicator = rank_indicator,
    .i1 = i1_packed,
    .i2 = *i2,
    .cqi = cqi,
    .ai_fb_valid = ai_fb_valid,
    .radiolink_monitoring = RLM_no_monitoring, // TODO do be activated in case of RLM based on CSI-RS
  };
  memcpy(l1_measurements.ai_fb_payload, ai_fb_payload, sizeof(l1_measurements.ai_fb_payload));
  nr_downlink_indication_t dl_indication;
  fapi_nr_rx_indication_t rx_ind = {0};
  nr_fill_dl_indication(&dl_indication, NULL, &rx_ind, proc, ue, NULL);
  nr_fill_rx_indication(&rx_ind, FAPI_NR_MEAS_IND, ue, NULL, NULL, 1, proc, (void *)&l1_measurements, NULL);
  ue->if_inst->dl_indication(&dl_indication);
}
