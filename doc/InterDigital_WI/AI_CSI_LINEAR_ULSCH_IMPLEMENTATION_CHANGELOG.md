# AI CSI Linear UL-SCH Implementation Changelog

This document summarizes the implementation of the phase-1 custom AI CSI feedback path over UL-SCH using a fixed linear encoder/decoder, while keeping legacy CSI feedback over PUCCH unchanged.

## Scope

Implemented all planned steps for:

- Lab custom UL-SCH LCID with fixed 48-bit payload (6 bytes)
- UE dominant-`v1` extraction -> linear encode -> int8 latent generation
- UE UL-SCH MAC injection of custom latent sub-PDU with TBS-safe checks
- gNB UL-SCH MAC parse of custom LCID -> linear decode -> custom PMI estimation
- Legacy vs custom PMI capture, compare, counters, and optional CSV logging
- Runtime feature flags with safe default OFF

## Modified Files

### 1) Shared MAC constants

- `openair2/LAYER2/NR_MAC_COMMON/nr_mac.h`
  - Added `UL_SCH_LCID_AI_FEEDBACK` (lab-only non-3GPP LCID `0x2F`)
  - Added `NR_AI_CSI_FB_LATENT_BYTES` (`6`) and `NR_AI_CSI_FB_LATENT_BITS` (`48`)

### 2) UE PHY->MAC latent transport fields

- `nfapi/open-nFAPI/nfapi/public_inc/fapi_nr_ue_interface.h`
  - Added `NFAPI_NR_AI_CSI_FB_LATENT_BYTES` (`6`)
  - Extended `fapi_nr_l1_measurements_t` with:
    - `bool ai_fb_valid`
    - `uint8_t ai_fb_payload[6]`

- `openair2/LAYER2/NR_MAC_UE/mac_defs.h`
  - Extended UE MAC state with:
    - `bool ai_fb_valid`
    - `uint8_t ai_fb_payload[6]`
    - `frame_t ai_fb_frame`
    - `slot_t ai_fb_slot`

- `openair2/LAYER2/NR_MAC_UE/nr_ue_procedures.c`
  - In `nr_ue_process_l1_measurements(...)`, copied AI latent/valid flag from L1 measurements into UE MAC state.

### 3) UE linear encoder implementation

- `openair1/PHY/NR_UE_TRANSPORT/csi_rx.c`
  - Added helper `nr_ai_fb_encode_dominant_v(...)`:
    - Reuses CSI-based dominant eigenvector flow (`H^H H` accumulation + dominant vector)
    - Normalizes global phase for deterministic latent generation
    - Builds real vector `R^8` from complex dominant `v1`
    - Applies fixed linear encoder matrix `E` (`6x8`)
    - Quantizes to signed int8 latent (`6 bytes`)
  - In `nr_ue_csi_rs_procedures(...)`:
    - Generates latent when 4-port CSI-RS and feature flag is enabled
    - Fills `fapi_nr_l1_measurements_t.ai_fb_valid/payload`
    - Adds debug logging under `--print-csi-debug`

### 4) UE UL-SCH custom LCID injection

- `openair2/LAYER2/NR_MAC_UE/nr_ue_scheduler.c`
  - In `nr_ue_get_sdu(...)`, after standard CE handling:
    - Appends short subheader (`NR_MAC_SUBHEADER_SHORT`) + 6-byte payload with LCID `UL_SCH_LCID_AI_FEEDBACK`
    - Performs grant-space check before insertion
    - Logs skip when insufficient bytes remain
    - Clears `ai_fb_valid` after successful insertion

### 5) gNB custom LCID parse + decode + custom PMI

- `openair2/LAYER2/NR_MAC_gNB/nr_mac_gNB.h`
  - Extended `NR_UE_sched_ctrl_t` with AI feedback state/counters:
    - `ai_fb_seen`
    - `legacy_pmi_x1`, `legacy_pmi_x2`
    - `custom_pmi_x1`, `custom_pmi_x2`
    - `ai_fb_frame`, `ai_fb_slot`
    - `ai_fb_custom_decodes`, `ai_fb_legacy_decodes`, `ai_fb_pmi_matches`

- `openair2/LAYER2/NR_MAC_gNB/gNB_scheduler_ulsch.c`
  - Added custom decoder utilities:
    - `ai_fb_decode_latent_to_vhat(...)` with fixed decoder matrix `D` (`8x6`)
    - `ai_fb_estimate_pmi_from_vhat(...)` using codebook-correlation search
  - Extended UL MAC PDU parser:
    - Recognizes `UL_SCH_LCID_AI_FEEDBACK`
    - Checks payload length (`6`)
    - Decodes latent to `vhat`, derives custom PMI, updates per-UE state/counters
    - Feature-gated by `--ai-fb-ulsch-enable`

### 6) Legacy/custom comparison logging

- `openair2/LAYER2/NR_MAC_gNB/gNB_scheduler_uci.c`
  - At legacy CSI decode point:
    - Captures latest legacy PMI
    - Increments legacy decode counter
    - Compares with most recent custom PMI when available
    - Tracks match counter
    - Emits terminal comparison log under `--print-csi-debug`
  - Added optional CSV output (when `--ai-fb-log-path` is set):
    - `gnb_ai_pmi_compare.csv`
    - Includes legacy/custom frame+slot, PMIs, match flag, running counters

### 7) Runtime flags and defaults

- `executables/softmodem-common.h`
  - Added CLI options:
    - `--ai-fb-ulsch-enable` (default `0`)
    - `--ai-fb-log-path` (default `NULL`)
    - `--ai-fb-impl-mode` (default `0`)
    - `--ai-fb-force-rank1` (default `0`, active only when AI UL-SCH is enabled)
  - Added corresponding `softmodem_params_t` fields and help text
  - Updated command-line descriptor/check arrays accordingly

- `executables/softmodem-common.c`
  - Added sanity check and startup log for `ai_fb_ulsch_enable`

## Behavior Summary

- Legacy CSI report path (PUCCH) remains active and unchanged.
- Custom AI CSI path is lab-only and disabled by default.
- When enabled, UE emits one 6-byte latent payload on UL-SCH when latent exists and grant space permits.
- gNB decodes custom latent, computes custom PMI, and compares against legacy PMI from UCI path.

## Validation Run

The implementation was built successfully with:

- `cmake --build build -j4 --target nr-uesoftmodem nr-softmodem`

No linter diagnostics were reported on the modified files during IDE lint check.

## How To Enable During Runs

- Enable custom path:
  - `--ai-fb-ulsch-enable 1`
- Optional comparison CSV output:
  - `--ai-fb-log-path <directory>`
- Optional detailed terminal tracing:
  - `--print-csi-debug 1`

## Notes

- `UL_SCH_LCID_AI_FEEDBACK` is intentionally lab-specific and non-standard.
- Temporal mismatch between latest custom and legacy reports can still occur; frame/slot stamps are logged to aid alignment during analysis.

## Rank-1 Forcing and Normalization Fixes (4-port / 4x4 Study Mode)

This section documents the temporary study-mode fixes used to run a controlled rank-1 comparison while the UE/gNB are configured for 4-port CSI-RS.

### A) Forced rank-1 RI restriction in gNB CSI codebook config

- File: `openair2/LAYER2/NR_MAC_gNB/nr_radio_config.c`
- In `config_csi_codebook(...)`, rank-1 restriction is now CLI-gated:
  - `--ai-fb-ulsch-enable=1`
  - `--ai-fb-force-rank1=1`
- Only when both flags are set, `ri_layers` is forced to `1` for AI study mode.
- Effect:
  - `typeI_SinglePanel_ri_Restriction = 0x01`
  - RI payload bit-length becomes `0` (single allowed rank index)
  - Legacy CSI payload is constrained to rank-1 semantics.
- If `--ai-fb-force-rank1=0` (default), normal multi-rank RI restriction is preserved.

### B) gNB RI decode fix for `ri_bitlen == 0`

- File: `openair2/LAYER2/NR_MAC_gNB/gNB_scheduler_uci.c`
- Fix 1: `evaluate_ri_report(...)` now explicitly handles `ri_bitlen == 0` by selecting the first allowed RI from `ri_restriction`.
- Fix 2: report extraction paths now call `evaluate_ri_report(...)` unconditionally (not only when `ri_bitlen > 0`).
- Why needed:
  - Without this, `r_index` could remain `-1` and be interpreted as `255` in PMI decode, causing invalid logs like `RI_reported=255`.

### C) UE PMI packing observability (legacy path)

- Files:
  - `openair1/PHY/NR_UE_TRANSPORT/csi_rx.c`
  - `openair2/LAYER2/NR_MAC_UE/nr_ue_procedures.c`
- Added debug traces to show:
  - PHY-side `i11/i12/i13` and packed `i1`
  - MAC-side field shifts/bit layout, pre-reverse payload, and final payload
- Purpose:
  - Confirm how raw PHY PMI and RI-restricted MAC payload map to gNB legacy decode outputs.

### D) Class-invariant custom PMI normalization at gNB (rank-1)

- File: `openair2/LAYER2/NR_MAC_gNB/gNB_scheduler_ulsch.c`
- Implemented phase-invariant class selection for custom decoded `vhat`:
  - class `c` groups equivalent `ll` representatives `{c, c+4}`
  - score uses `max |w^H vhat|^2` over each class
  - canonical representative is used for compare (`ll_canonical = c + 4`)
- Output:
  - custom PMI is converted into a canonical class index before comparing to legacy PMI.
- Why needed:
  - raw PMI labels can differ by equivalent beam class (global phase/sign ambiguity) even when beams are physically equivalent.

### E) Scope and limitations of this mode

- This is a controlled rank-1 study mode for validating:
  - end-to-end custom latent transport over UL-SCH
  - custom decode-to-PMI comparison infrastructure
- It is not full rank-4 custom PMI support.
- For broader/channel-agnostic studies, replace any scenario-specific assumptions with fully deterministic equivalence mapping across codebook symmetries and re-enable multi-rank RI restrictions.

### How to revert to normal multi-rank behavior

Use the steps below to switch back from rank-1 study mode to normal RI/PMI operation:

1. Disable rank-1 forcing from CLI (no code edit needed)
   - Run with:
     - `--ai-fb-force-rank1 0`
   - (Default is already `0`)
   - You can keep `--ai-fb-ulsch-enable 1` while using normal multi-rank legacy reporting.

2. Keep the gNB `ri_bitlen==0` decode fix
   - File: `openair2/LAYER2/NR_MAC_gNB/gNB_scheduler_uci.c`
   - Do **not** remove the robust `ri_bitlen==0` handling and unconditional RI decode call.
   - This improves correctness for any single-rank restricted report configuration.

3. Disable rank-1-specific custom PMI canonicalization (if undesired)
   - File: `openair2/LAYER2/NR_MAC_gNB/gNB_scheduler_ulsch.c`
   - Remove or guard the class-canonical rank-1 normalization path if you want raw custom PMI labels.
   - Optionally keep class-invariant scoring but avoid forcing a rank-1 canonical representative during compare.

4. Optionally silence extra debug traces
   - Files:
     - `openair1/PHY/NR_UE_TRANSPORT/csi_rx.c`
     - `openair2/LAYER2/NR_MAC_UE/nr_ue_procedures.c`
     - `openair2/LAYER2/NR_MAC_gNB/gNB_scheduler_ulsch.c`
   - Keep logic intact but disable logs by running with `--print-csi-debug 0`.

5. Rebuild and validate
   - Rebuild `nr-softmodem` and `nr-uesoftmodem`.
   - Confirm runtime logs show multi-rank RI restrictions (for example `0x03` or `0x0f` depending on geometry/config) and expected RI-dependent PMI bit lengths.

### CLI usage summary for replication

- Baseline (legacy only, normal multi-rank):
  - `--ai-fb-ulsch-enable 0`
- AI pipeline enabled, normal multi-rank legacy CSI:
  - `--ai-fb-ulsch-enable 1 --ai-fb-force-rank1 0`
- AI pipeline enabled, rank-1 study mode:
  - `--ai-fb-ulsch-enable 1 --ai-fb-force-rank1 1`

## 2x2 (2-port) Rank-1 Study Support

The custom feedback path was extended so that 2-port CSI-RS scenarios also produce and decode AI latent feedback in rank-1 study mode (previously limited to 4-port path).

### What was added

- UE latent generation now supports both `ports==2` and `ports==4`:
  - File: `openair1/PHY/NR_UE_TRANSPORT/csi_rx.c`
  - 2-port path:
    - compute 2x2 Gram matrix and dominant eigenvector
    - phase-fix dominant vector
    - encode with a compact fixed linear map to 6-byte latent
  - 4-port path remains active as before.

- gNB custom latent decode now selects estimator by configured DL antenna ports:
  - File: `openair2/LAYER2/NR_MAC_gNB/gNB_scheduler_ulsch.c`
  - If configured ports are 2:
    - decode to 2-element complex vector `vhat2`
    - estimate rank-1 2-port PMI as `x2` over 4 phase candidates
    - set custom PMI as `x1=0`, `x2=<estimated>`
  - If configured ports are 4:
    - use existing class-invariant 4-port normalization and canonical mapping.

### Runtime confirmation

- 2-port mode:
  - UE logs: `AI feedback latent ready for UL-SCH (ports=2, 48 bits): [...]`
  - gNB logs: `AI CSI UL-SCH decode (2-port) ... -> custom PMI x1=0x0 x2=...`
- 4-port mode:
  - gNB logs continue to show `AI CSI UL-SCH decode (4-port) ...`

### How to revert 2x2 custom support

If needed, restore custom feedback to 4-port-only behavior:

1. UE side
   - File: `openair1/PHY/NR_UE_TRANSPORT/csi_rx.c`
   - Restrict latent generation condition back to `mapping_parms.ports == 4`.
   - Remove/disable 2-port dominant-vector encoder helper logic.

2. gNB side
   - File: `openair2/LAYER2/NR_MAC_gNB/gNB_scheduler_ulsch.c`
   - Remove/disable 2-port decode branch and keep only 4-port custom PMI estimation.

3. Rebuild
   - Rebuild `nr-softmodem` and `nr-uesoftmodem`.

## AI FB Module Refactor (Encoder/Decoder APIs)

To support sequential upgrades from matrix baseline to NN inference without modifying PHY/MAC parser logic each time, the AI feedback math was modularized.

### New module files

- UE encoder module:
  - `openair1/PHY/NR_UE_TRANSPORT/ai_fb_encoder.h`
  - `openair1/PHY/NR_UE_TRANSPORT/ai_fb_encoder.c`
- gNB decoder module:
  - `openair2/LAYER2/NR_MAC_gNB/ai_fb_decoder.h`
  - `openair2/LAYER2/NR_MAC_gNB/ai_fb_decoder.c`

### API contracts (v1)

- UE:
  - `ai_fb_encode_rank1_4p(...)`
  - `ai_fb_encode_rank1_2p(...)`
- gNB:
  - `ai_fb_decode_rank1_4p(...)`
  - `ai_fb_decode_rank1_2p(...)`
- Current implementation mode enum is shared in both headers:
  - `AI_FB_IMPL_MATRIX`
  - `AI_FB_IMPL_MLP_STUB`
  - `AI_FB_IMPL_MODEL_STUB`

In this phase, all modes use matrix behavior as deterministic baseline.

### Caller wiring

- UE caller:
  - `openair1/PHY/NR_UE_TRANSPORT/csi_rx.c`
  - now computes dominant vector and calls module APIs for latent encoding.
- gNB caller:
  - `openair2/LAYER2/NR_MAC_gNB/gNB_scheduler_ulsch.c`
  - now calls module decode APIs and consumes decoded output structs.

### Build integration

- Added to `PHY_NR_UE` sources in `CMakeLists.txt`:
  - `openair1/PHY/NR_UE_TRANSPORT/ai_fb_encoder.c`
- Added to `L2_NR` sources in `CMakeLists.txt`:
  - `openair2/LAYER2/NR_MAC_gNB/ai_fb_decoder.c`

### Runtime mode hook

- New CLI option:
  - `--ai-fb-impl-mode` (`0=matrix`, `1=mlp-stub`, `2=model-stub`)
- Files:
  - `executables/softmodem-common.h`
  - `executables/softmodem-common.c`
- Current behavior:
  - mode is validated/logged and passed into module APIs.
  - all modes currently execute matrix baseline logic.

### Regression tests

- Added deterministic module tests:
  - `tests/ai_fb/test_ai_fb_codec.c`
  - `tests/ai_fb/CMakeLists.txt`
  - wired via `tests/CMakeLists.txt` when `ENABLE_TESTS=ON`
- Coverage:
  - deterministic encode output
  - phase-invariance sanity
  - 2-port and 4-port decode metric and PMI range checks

### Revert instructions for module refactor

To revert to previous inline implementation:

1. In `CMakeLists.txt`, remove:
   - `openair1/PHY/NR_UE_TRANSPORT/ai_fb_encoder.c`
   - `openair2/LAYER2/NR_MAC_gNB/ai_fb_decoder.c`
2. In caller files, restore inline encode/decode logic:
   - `openair1/PHY/NR_UE_TRANSPORT/csi_rx.c`
   - `openair2/LAYER2/NR_MAC_gNB/gNB_scheduler_ulsch.c`
3. Optionally remove CLI mode option:
   - `--ai-fb-impl-mode` in `executables/softmodem-common.h/.c`

## MLP-stub NN path (mode=1)

To support an NN-like inference path while keeping stable APIs, the `mlp-stub` mode now runs a 2-layer MLP encoder/decoder using static weights.

### Runtime integration

- Encoder side:
  - `openair1/PHY/NR_UE_TRANSPORT/ai_fb_encoder.c`
  - `AI_FB_IMPL_MLP_STUB` now executes:
    - `x -> Linear(enc_w1,b1) -> Linear(enc_w2,b2) -> quantize`
- Decoder side:
  - `openair2/LAYER2/NR_MAC_gNB/ai_fb_decoder.c`
  - `AI_FB_IMPL_MLP_STUB` now executes:
    - `z -> Linear(dec_w1,b1) -> Linear(dec_w2,b2) -> normalize -> PMI estimation`
- Shared default weights:
  - `openair2/LAYER2/NR_MAC_COMMON/ai_fb_mlp_weights.h`
  - initialized to reproduce current matrix baseline behavior.

### Data collection for training

Use existing CSI recording (already in-tree):

- UE:
  - `--csi-record-path <dir>`
  - writes:
    - `<dir>/csi_reports.csv`
    - `<dir>/csi_rs_channels/H_*.bin`
- gNB (optional monitoring labels):
  - `--csi-record-path <dir>`
  - writes:
    - `<dir>/gnb_csi_feedback.csv`
    - `<dir>/gnb_csirs_scheduling.csv`

### Training + export pipeline

- Script:
  - `tools/ai_fb/train_export_mlp_stub.py`
- It:
  1. loads UE `H_*.bin` from `csi_reports.csv`
  2. derives dominant rank-1 `v`
  3. trains per-port (2-port / 4-port) autoencoder MLP
  4. exports C header weights compatible with runtime symbols in `ai_fb_mlp_weights.h`
  5. exports text model file for `model-stub` runtime loader

Example:

- `python3 tools/ai_fb/train_export_mlp_stub.py --dataset-dir ./csi_ml_data --output-header openair2/LAYER2/NR_MAC_COMMON/ai_fb_mlp_weights.h --output-model-txt ./csi_ml_data/ai_fb_model.txt --output-model-bin ./csi_ml_data/ai_fb_model.bin --hidden4 16 --hidden2 16`

The script now reports train/validation loss (MSE + cosine consistency + latent regularization) and supports configurable hidden widths for wider MLPs.

Then rebuild and run with:

- `--ai-fb-ulsch-enable 1 --ai-fb-impl-mode 1`

### Model-stub loader path (mode=2)

- Added runtime model loader:
  - `openair2/LAYER2/NR_MAC_COMMON/ai_fb_model_loader.h`
  - `openair2/LAYER2/NR_MAC_COMMON/ai_fb_model_loader.c`
- New CLI option:
  - `--ai-fb-model-path <path_to_ai_fb_model.txt>`
  - `--ai-fb-model-backend <0|1|2>` (`0=native loader`, `1=ONNX stub`, `2=TFLite stub`)
- Behavior:
  - `--ai-fb-impl-mode 2` loads model tensors from `--ai-fb-model-path`
  - supported file formats:
    - `.bin` (fast packed float32 tensors, preferred)
    - `.txt` (debug-friendly fallback)
    - auto-detect by extension; fallback try order is bin then txt
  - if loading fails/unset path, runtime falls back to compiled weights
  - both UE encoder and gNB decoder use the loaded tensors
  - ONNX/TFLite backend hooks are wired via CLI.

## ONNX model-stub integration (optional build)

### Build flag

- CMake option:
  - `-DOAI_AI_FB_ONNX=ON` (default OFF)
- When ON:
  - ONNX Runtime include/lib are discovered
  - `OAI_AI_FB_ONNX_ENABLED` is defined
  - ONNX adapter is linked into `MAC_NR_COMMON`
- When OFF:
  - ONNX adapter compiles in stub mode with safe fallback.

### Runtime options

- `--ai-fb-impl-mode 2`
- `--ai-fb-model-backend 1` (ONNX)
- `--ai-fb-onnx-enc-path <encoder.onnx>`
- `--ai-fb-onnx-dec-path <decoder.onnx>`
- fallback:
  - if ONNX init/inference fails, native model loader path remains available (`--ai-fb-model-path` with `.bin/.txt`)

### Native fast model format

- Preferred native artifact for backend `0`:
  - `.bin` packed float32 tensors
- Also supported:
  - `.txt` tensor dump

### Training script artifacts

`tools/ai_fb/train_export_mlp_stub.py` now exports:

- C header weights (`.h`)
- native text model (`.txt`)
- native packed model (`.bin`)
- ONNX encoder/decoder models (`*_encoder.onnx`, `*_decoder.onnx`)

### CSINet-ready migration note

Current ONNX path assumes flat tensor contracts compatible with the MLP baseline:
- encoder input: `[1, in_dim]` -> latent `[1, 6]`
- decoder input: `[1, 6]` -> reconstructed feature `[1, in_dim]`

For CSINet migration:
- keep these runtime API boundaries stable
- swap ONNX graphs and preprocess/postprocess internals without touching UE/gNB caller logic.

## CSINet mode-3 integration (phase-1 + phase-2 design hooks)

### New implementation mode

- `--ai-fb-impl-mode 3` enables CSINet mode-3 path.
- New options:
  - `--ai-fb-csinet-model-path <path>` (phase-1 optional placeholder path)
  - `--ai-fb-csinet-latent-bytes <N>` (requested budget; phase-1 transport is fixed at 6 bytes)

### New modules

- UE:
  - `openair1/PHY/NR_UE_TRANSPORT/csinet_encoder.h`
  - `openair1/PHY/NR_UE_TRANSPORT/csinet_encoder.c`
  - `openair1/PHY/NR_UE_TRANSPORT/csinet_encoder_weights.h`
- gNB:
  - `openair2/LAYER2/NR_MAC_gNB/csinet_decoder.h`
  - `openair2/LAYER2/NR_MAC_gNB/csinet_decoder.c`
  - `openair2/LAYER2/NR_MAC_COMMON/csinet_decoder_weights.h`
- shared contract:
  - `openair2/LAYER2/NR_MAC_COMMON/csinet_common.h`

### Phase-1 tensor contract (wideband H -> rank-1 parity)

- UE input:
  - wideband CSI-RS channel estimate tensor `H` aggregated over REs
- encoder output:
  - quantized latent payload of 6 bytes over existing AI UL-SCH LCID
- decoder input:
  - dequantized latent vector
- decoder output:
  - rank-1 proxy vector to derive custom PMI (`x1/x2`) for parity checks

### Phase-2 rank-2/4 design notes

- decoder output can be extended from rank-1 proxy to richer reconstructed feature tensor
- post-processing extension points:
  - RI selection from reconstructed rank metrics
  - rank-dependent PMI mapping from reconstructed beam/channel features
  - CQI mapping from reconstructed quality proxy/SINR estimate
- rollout remains staged: rank-1 parity first, then disable/relax forced-rank1 for full-rank validation.

### Notes

- This first NN integration stage focuses on autoencoder-style reconstruction over dominant rank-1 beam features.
- For strict supervised target training (`RI/PMI/CQI` from UE or gNB), add task-specific heads in the training script and export corresponding decoder heads while preserving module API shape.

## PMI Compare Freshness Knobs (Gating Control)

To make stale-sample filtering tunable at runtime, the legacy-vs-custom PMI compare path now exposes two CLI knobs:

- `--ai-fb-compare-gating <0|1>`
  - default: `1`
  - `1`: compare only when custom PMI is fresh enough
  - `0`: disable freshness filtering (compare against latest seen custom PMI regardless of age)

- `--ai-fb-compare-max-age-slots <N>`
  - default: `2`
  - valid range: `N >= 0`
  - used only when `--ai-fb-compare-gating=1`

### Runtime behavior

- Freshness age is computed in slots between:
  - legacy PMI decode slot (`frame/slot` from UCI path)
  - most recent custom PMI decode slot (`ai_fb_frame/ai_fb_slot` from AI UL-SCH path)
- If gating is enabled and `age_slots > max_age_slots`, compare is skipped and excluded from match-rate counters.
- Skip event is printed when `--print-csi-debug=1` with age and threshold details.

### Example usage

- Default gated compare:
  - `--ai-fb-compare-gating 1 --ai-fb-compare-max-age-slots 2`
- Disable gating for raw/stress analysis:
  - `--ai-fb-compare-gating 0`
- Keep gating, but tolerate larger alignment delay:
  - `--ai-fb-compare-gating 1 --ai-fb-compare-max-age-slots 8`

## Bundled UL-SCH Payload (Idea 2)

This phase adds a separate lab-only UL-SCH MAC CE path that bundles legacy CSI part1 raw bits and AI latent from the same UE observation epoch.

### New UL-SCH LCID and payload contract

- New lab-only LCID:
  - `UL_SCH_LCID_AI_BUNDLED_FEEDBACK` (`0x2A`)
- Compact variable CE payload (v1):
  - fixed header bytes:
    - `version`, `obs_frame`, `obs_slot`, `obs_seq`, `legacy_p1_bits`, `legacy_p1_nbytes`
  - variable bytes:
    - `legacy_part1_bytes[legacy_p1_nbytes]` where `legacy_p1_nbytes=ceil(legacy_p1_bits/8)`
    - `ai_latent[6]`
  - motivation:
    - reduce UL grant pressure vs fixed 8-byte legacy payload carriage

### Runtime control

- New CLI flag:
  - `--ai-fb-bundled-ulsch-enable <0|1>` (default `0`)
- Behavior:
  - when `0`: existing AI LCID path (`UL_SCH_LCID_AI_FEEDBACK`) remains as before
  - when `1`: UE attempts bundled CE insertion (same-observation gate + grant-space check), and legacy async PMI compare on UCI side is bypassed to avoid mixed statistics

### UE behavior

- UE caches latest legacy CSI part1 raw payload and its `frame/slot`.
- UE already tracks AI latent and its `frame/slot`.
- Bundled CE is appended only when all are true:
  - bundled flag enabled
  - legacy raw payload valid
  - AI latent valid
  - `(legacy_frame,legacy_slot) == (ai_frame,ai_slot)` (strict same-observation gate)
  - UL grant has enough room for subheader + CE payload

### gNB behavior

- gNB parses the new bundled LCID branch in UL-SCH decoder.
- Legacy branch:
  - reuses shared helper `nr_extract_csi_report_from_raw_payload(...)` (refactored from PUCCH-only flow) to decode carried part1 bits into standard legacy CSI report structures.
- AI branch:
  - reuses existing AI decoder flow (`ai_fb_decode_rank1_*` or `csinet_decode_rank1_*`).
- Comparison:
  - compares PMI from bundled legacy decode and bundled AI decode in the same CE handling path
  - tracks separate bundled counters (`ai_fb_bundle_decodes`, `ai_fb_bundle_pmi_matches`)

## Rank-2 Angular-Delay Bundled Mode (impl-mode=4)

- Added isolated mode `--ai-fb-impl-mode 4` (`angular-delay-mlp`) so existing matrix/MLP/model-stub/CSINet behavior is unchanged.
- In mode 4, runtime enforces rank-2-capable legacy decode behavior by overriding `--ai-fb-force-rank1` to `0`.
- Training/export pipeline now includes angular-delay preprocessing:
  - average CSI over RX chains,
  - frequency+delay transform,
  - keep first 24 delay rows,
  - flatten real/imag to fixed input tensor.
- Added angular-delay model artifact support (`AI_FB_MODEL_TXT_V2_ANGULAR_DELAY` / binary v2 tensors: `encad_*`, `decad_*`) in the runtime loader.
- UE mode-4 branch now builds angular-delay features from full CSI `H` and emits the same fixed 6-byte latent payload over bundled UL-SCH.
- gNB bundled compare now logs tuple context (`RI/PMI/CQI`) for legacy and custom paths while KPI matching remains PMI-only (`pmi_x1/pmi_x2`).
- Diagrams and encoder/decoder flow (impl modes 4–5): `CSI_encoder_Decoder.md` in this directory.

## NR-aligned `pmi_x2` (i2) for 2-port rank-2 custom decode (angular-delay path)

- **Motivation:** The previous lab decoder built a unit-norm 2-vector from decoded angular features, then searched four fixed phase offsets on the second port. That produced a **0..3** `pmi_x2` that did not match NR **Type I single-panel** semantics for **2 ports, 2 layers**, where **i2** is typically **1 bit** (`pmi_x2 ∈ {0,1}` per OAI `pmi_x2_bitlen`), so legacy vs custom PMI comparison was misleading.
- **Change (TS 38.211 Table 6.3.1.5-4):** In `openair2/LAYER2/NR_MAC_gNB/ai_fb_decoder.c`, for **`AI_FB_IMPL_ANGULAR_DELAY_MLP`** and **`AI_FB_IMPL_ANGULAR_DELAY_REFINENET`** inside `ai_fb_decode_rank1_2p`, the four-phase search is replaced by choosing **`i2 ∈ {0,1}`** between **TPMI codebook rows 0 and 1** (same character encoding as PHY `nr_W_2l_2p` in `openair1/PHY/MODULATION/nr_modulation.c`). Columns are L2-normalized; the winner maximizes subspace energy \(\sum_L |w_L^H \hat{v}|^2\) against the decoded unit direction \(\hat{v}\).
- **Scope:** Stub MLP / matrix / other non-angular implementations **still** use the original four-phase lab mapping for `pmi_x2`.
- **Limit:** Only two TPMI rows are evaluated, matching the usual **1-bit i2** configuration. If RRC exposes **2-bit** `pmi_x2`, a third row (TPMI index 2 in the same table) would need to be included and driven from configured bit width (not wired through the decoder today).
