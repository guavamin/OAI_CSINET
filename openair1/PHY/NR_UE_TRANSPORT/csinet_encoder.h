#ifndef CSINET_ENCODER_H
#define CSINET_ENCODER_H

#include <stdbool.h>
#include <stdint.h>

#include "PHY/defs_nr_common.h"
#include "common/utils/nr/nr_common.h"
#include "openair2/LAYER2/NR_MAC_COMMON/csinet_common.h"

bool csinet_encode_wideband_4p(const NR_DL_FRAME_PARMS *fp,
                               const c16_t *h_freq,
                               int n_rx,
                               int n_ports_stride,
                               int n_sc,
                               uint8_t out_payload[CSINET_LATENT_BYTES]);

bool csinet_encode_wideband_2p(const NR_DL_FRAME_PARMS *fp,
                               const c16_t *h_freq,
                               int n_rx,
                               int n_ports_stride,
                               int n_sc,
                               uint8_t out_payload[CSINET_LATENT_BYTES]);

#endif /* CSINET_ENCODER_H */
