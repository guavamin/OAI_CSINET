#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 2 ]]; then
  echo "Usage: $0 <ue_log_file> <gnb_log_file>"
  exit 1
fi

UE_LOG="$1"
GNB_LOG="$2"

if [[ ! -f "$UE_LOG" ]]; then
  echo "UE log file not found: $UE_LOG"
  exit 1
fi
if [[ ! -f "$GNB_LOG" ]]; then
  echo "gNB log file not found: $GNB_LOG"
  exit 1
fi

ue_ri_count=$(awk '/UE CSI RI trace:/{c++} END{print c+0}' "$UE_LOG")
ue_pmi_count=$(awk '/UE CSI PMI trace:/{c++} END{print c+0}' "$UE_LOG")
ue_2x4_ri_count=$(awk '/2×4 RI:|2x4 RI:/{c++} END{print c+0}' "$UE_LOG")
ue_not_impl_count=$(awk '/Rank indicator computation is not implemented for 2 x 4 system/{c++} END{print c+0}' "$UE_LOG")

gnb_ri_count=$(awk '/gNB CSI RI decode:/{c++} END{print c+0}' "$GNB_LOG")
gnb_pmi_count=$(awk '/gNB CSI PMI decode:/{c++} END{print c+0}' "$GNB_LOG")
gnb_pol_capped=$(awk '/DL layers policy\(capped\):/{c++} END{print c+0}' "$GNB_LOG")
gnb_pol_decoded=$(awk '/DL layers policy\(decoded\):/{c++} END{print c+0}' "$GNB_LOG")

echo "=== 2x4 CSI Validation Summary ==="
echo "UE RI trace lines:                 $ue_ri_count"
echo "UE PMI trace lines:                $ue_pmi_count"
echo "UE 2x4 RI estimator lines:         $ue_2x4_ri_count"
echo "UE 2x4 not-implemented warnings:   $ue_not_impl_count"
echo "gNB RI decode lines:               $gnb_ri_count"
echo "gNB PMI decode lines:              $gnb_pmi_count"
echo "gNB policy(capped) lines:          $gnb_pol_capped"
echo "gNB policy(decoded) lines:         $gnb_pol_decoded"

echo
echo "=== Basic checks ==="
if [[ "$ue_not_impl_count" -gt 0 ]]; then
  echo "[FAIL] Found legacy 2x4 RI not-implemented warning."
else
  echo "[PASS] No legacy 2x4 RI not-implemented warning."
fi

if [[ "$ue_2x4_ri_count" -gt 0 ]]; then
  echo "[PASS] 2x4 RI estimator path is active."
else
  echo "[WARN] No explicit 2x4 RI estimator log found."
fi

if [[ "$ue_ri_count" -gt 0 && "$gnb_ri_count" -gt 0 ]]; then
  echo "[PASS] UE/gNB RI trace logs present."
else
  echo "[FAIL] Missing RI trace logs on UE or gNB."
fi

if [[ "$ue_pmi_count" -gt 0 && "$gnb_pmi_count" -gt 0 ]]; then
  echo "[PASS] UE/gNB PMI trace logs present."
else
  echo "[FAIL] Missing PMI trace logs on UE or gNB."
fi

if [[ "$gnb_pol_capped" -gt 0 || "$gnb_pol_decoded" -gt 0 ]]; then
  echo "[PASS] Scheduler policy logs present."
else
  echo "[FAIL] Missing scheduler policy logs."
fi
