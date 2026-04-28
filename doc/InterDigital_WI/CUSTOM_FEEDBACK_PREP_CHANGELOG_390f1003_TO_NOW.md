# Custom Feedback Prep Changelog (`390f1003-576b-4acf-9897-a2e609b521d0` -> now)

This note records the concrete code updates made in this chat window to support CSI/SVD observability before implementing the custom AI feedback pipeline.

## Scope

- Primary file changed for this step: `openair1/PHY/NR_UE_TRANSPORT/csi_rx.c`
- Trigger condition for logs: `--print-csi-debug` (UE side)
- Goal: expose channel tensor dimensions and SVD-related `V` diagnostics to help design a compact custom feedback payload.

## What Was Added

## 1) Raw channel tensor size logging

Inside `nr_ue_csi_rs_procedures(...)`, after CSI-RS channel estimation:

- Logs `H_ls` tensor shape and memory size:
  - shape `[nb_antennas_rx x ports x ofdm_symbol_size]`
- Logs `H_freq` tensor shape and memory size:
  - shape `[nb_antennas_rx x ports x (ofdm_symbol_size + FILTER_MARGIN)]`

Purpose:
- Quickly verify the effective channel object dimensions used by the UE before any compression.

## 2) SVD-style summary for 4-port CSI-RS

Still in `nr_ue_csi_rs_procedures(...)`:

- Reuses existing Gram/eigen helpers in `csi_rx.c`:
  - averaged `R = H^H H` over CSI-RS REs
  - approximate singular values via eigenvalues of `R`
- Logs:
  - effective `H` shape `[n_rx x 4 x n_re]`
  - `Sigma` shape `[4]`
  - singular values
  - best singular value estimate

Purpose:
- Validate whether the channel is strongly rank-1 or multi-rank in live runs.

## 3) Dominant `V` (`4x1`) extraction helper

Added helper:

- `nr_herm4_dominant_eigvec(...)`

This computes the dominant eigenvector of 4x4 Hermitian `R` (power iteration), used as the dominant right-singular direction proxy.

Purpose:
- Provide a direct `V` candidate for custom latent design experiments.

## 4) Best `V` printing + payload size estimation

Two related debug outputs were added:

### A) In rank-1 PMI function

Inside `nr_csi_rs_pmi_estimation_4port_rank1(...)`:

- Prints normalized best `V` values:
  - `[(re0+jim0), ..., (re3+jim3)]`
- Prints payload sizing estimates:
  - current OAI PMI-index style (bits for `i11`, `i12`, `i2`)
  - hypothetical raw-`V` Q1.15 packing size (`128` bits total, `64/64` split)

### B) In common 4-port PMI entry path

Inside `nr_csi_rs_pmi_estimation(...)`:

- Added the same dominant-`V` + payload-size logs before rank-specific branching.
- This ensures logs appear even when execution goes through rank-2/3/4 PMI paths (not only rank-1 path).

Purpose:
- Remove ambiguity when rank branch changes prevent rank-1-only logs from appearing.

## Important Observations Captured

From current logs:

- Current OAI legacy PMI payload style is very compact (index-based).
- Hypothetical raw-`V` direct packing at Q1.15 is `128` bits (`64+64`), which exceeds current legacy CSI packing guardrails in the active path.

## What Was Not Changed

- No scheduler behavior change.
- No PUCCH/PUSCH transport behavior change.
- No new custom feedback pipeline hooks added yet.
- Only observability/debug instrumentation was added.

## How to Use

Run UE with:

- `--print-csi-debug`

Then look for log lines containing:

- `CSI raw channel tensor sizes:`
- `CSI SVD approx:`
- `Dominant V (from H^H H) shape [4 x 1]`
- `Dominant V payload sizing:`

## Notes / Caveats

- SVD-style logs currently target the 4-port path (`ports == 4`) used in your row-4 setup.
- `H` aggregation is over CSI-RS REs from the configured mapping occasion (typically one CSI-RS symbol with many frequency REs in current setup).
- Dominant `V` shown is a direction proxy from `H^H H`, useful for custom feedback prototyping.
