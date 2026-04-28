#ifndef CSINET_DECODER_H
#define CSINET_DECODER_H

#include <stdbool.h>
#include <stdint.h>
#include <complex.h>

#include "openair2/LAYER2/NR_MAC_COMMON/csinet_common.h"

typedef struct {
  double complex vhat[4];
  int best_ll;
  int best_mm;
  int best_class;
  csinet_report_t report;
} csinet_decode4p_out_t;

typedef struct {
  double complex vhat2[2];
  csinet_report_t report;
} csinet_decode2p_out_t;

bool csinet_decode_rank1_4p(const uint8_t latent[CSINET_LATENT_BYTES], csinet_decode4p_out_t *out);
bool csinet_decode_rank1_2p(const uint8_t latent[CSINET_LATENT_BYTES], csinet_decode2p_out_t *out);

#endif /* CSINET_DECODER_H */
