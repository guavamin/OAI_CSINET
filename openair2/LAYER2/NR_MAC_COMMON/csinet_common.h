#ifndef CSINET_COMMON_H
#define CSINET_COMMON_H

#include <stdint.h>

/*
 * Phase-1 CSINet contract:
 * - UE input: wideband CSI-RS channel estimates H (complex), aggregated across REs.
 * - Encoder output: fixed latent bitstream carried over UL-SCH AI LCID.
 * - Decoder output: rank-1 proxy vector used to derive PMI parity with legacy path.
 *
 * Phase-2 extension (design-ready):
 * - Decoder feature tensor can be expanded to derive RI/PMI/CQI for rank-2/4.
 */
#define CSINET_MAX_PORTS 4
#define CSINET_MAX_RX_ANT 4

/* Keep phase-1 payload aligned with existing MAC LCID budget. */
#define CSINET_LATENT_BYTES 6
#define CSINET_LATENT_QSTEP 0.01f

typedef struct {
  uint8_t pmi_x1;
  uint8_t pmi_x2;
  uint8_t ri_rank;  /* phase-1 fixed to 1, phase-2 will carry decoded RI */
  uint8_t cqi;      /* phase-1 left at 0, phase-2 can map decoded quality */
  double metric;
} csinet_report_t;

#endif /* CSINET_COMMON_H */
