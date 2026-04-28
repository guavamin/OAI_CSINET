# PTRS (Phase Tracking Reference Signal) configuration

The log **`[GNB_APP] No PTRS configuration found`** means the gNB config has no **phaseTrackingRS** block. PTRS is optional; you can enable it purely by configuration—**no source code changes** are required.

## Enable PTRS via gNB config

1. In your gNB config file (e.g. `targets/PROJECTS/GENERIC-NR-5GC/CONF/gnb.sa.band78.fr1.106PRB.usrpb210.conf`), inside the first gNB block (under `gNBs = ( { ... } )`), add a **phaseTrackingRS** list with one group of parameters.

2. **Exact location**: same level as `servingCellConfigCommon`, `SCTP`, etc. (e.g. after the closing `);` of `servingCellConfigCommon` and before `SCTP`).

3. **Example block** (uncomment and adjust values as needed):

```c
phaseTrackingRS = (
{
  dl_ptrsFreqDensity0_0   = 0;
  dl_ptrsFreqDensity1_0   = 0;
  dl_ptrsTimeDensity0_0    = -1;
  dl_ptrsTimeDensity1_0    = -1;
  dl_ptrsTimeDensity2_0    = -1;
  dl_ptrsEpreRatio_0       = -1;
  dl_ptrsReOffset_0        = -1;
  ul_ptrsFreqDensity0_0    = 0;
  ul_ptrsFreqDensity1_0    = 0;
  ul_ptrsTimeDensity0_0    = -1;
  ul_ptrsTimeDensity1_0    = -1;
  ul_ptrsTimeDensity2_0    = -1;
  ul_ptrsReOffset_0        = -1;
  ul_ptrsMaxPorts_0        = 0;
  ul_ptrsPower_0           = 0;
}
);
```

4. **Parameter names** (from `openair2/GNB_APP/gnb_paramdef.h`):

| Config name | Description (3GPP 38.331 style) |
|-------------|----------------------------------|
| `dl_ptrsFreqDensity0_0`, `dl_ptrsFreqDensity1_0` | DL PTRS frequency density |
| `dl_ptrsTimeDensity0_0` … `dl_ptrsTimeDensity2_0` | DL PTRS time density |
| `dl_ptrsEpreRatio_0`, `dl_ptrsReOffset_0` | DL PTRS EPRE ratio, RE offset |
| `ul_ptrsFreqDensity0_0`, `ul_ptrsFreqDensity1_0` | UL PTRS frequency density |
| `ul_ptrsTimeDensity0_0` … `ul_ptrsTimeDensity2_0` | UL PTRS time density |
| `ul_ptrsReOffset_0`, `ul_ptrsMaxPorts_0`, `ul_ptrsPower_0` | UL PTRS RE offset, max ports, power |

Use `-1` where the standard allows “not present” or default; the code uses default/disabled behavior for those. After adding a valid **phaseTrackingRS** block and restarting the gNB, the “No PTRS configuration found” message is replaced by “PTRS configuration: …” logs for each parameter.

## PTRS config for 30 kHz FR1 (e.g. Band 78, 106 PRB)

For **30 kHz subcarrier spacing** FR1 (e.g. `gnb.sa.band78.fr1.106PRB.usrpb210.conf`: `dl_subcarrierSpacing = 1`, `initialDLBWPsubcarrierSpacing = 1`), the example block above is valid. **Value ranges** enforced by OAI (`nr_radio_config.c`): frequency density 0…276 (0 = not present); time density -1…29 (-1 = not present); `ul_ptrsReOffset_0` 0/1/2; `ul_ptrsMaxPorts_0` 0 or 1; `ul_ptrsPower_0` 0…3. PTRS density for 30 kHz is per 3GPP TS 38.214. Note: enabling **phaseTrackingRS** can trigger a segfault (see section below).

## Source code reference

- **Config read**: `openair2/GNB_APP/gnb_config.c` → `get_ptrs_config()` (looks for list `phaseTrackingRS` under `gNBs.[0]`).
- **Param definitions**: `openair2/GNB_APP/gnb_paramdef.h` → `GNB_CONFIG_STRING_PTRS` (`"phaseTrackingRS"`) and `GNB_PTRS_PARAMS_DESC`.
- **RRC use**: `openair2/LAYER2/NR_MAC_gNB/nr_radio_config.c` → `config_ulptrs()`, `config_dlptrs()`; PTRS is applied to PUSCH/PDSCH config when `configuration->ptrs` is non-NULL.

No modifications to these files are needed to enable PTRS; only the config file must contain the **phaseTrackingRS** block.

---

## How PTRS affects the PDSCH constellation and how to verify it

PTRS is used for **phase noise / common phase error (CPE) compensation**. It does affect the **PDSCH constellation** (the equalized symbols used for demodulation).

### Effect on the PDSCH constellation

1. **gNB (TX):** When **phaseTrackingRS** is configured and valid for a given PDSCH (RB count, MCS, number of symbols), the scheduler sets PTRS parameters (e.g. **PTRSFreqDensity**, **PTRSTimeDensity**) and the gNB PHY **inserts PTRS** into the PDSCH allocation (PTRS REs are overwritten in the resource grid). The data constellation is unchanged at TX; PTRS REs carry the phase-tracking sequence.

2. **UE (RX):** After channel estimation and **equalization**, the UE has **rxdataF_comp** (equalized frequency-domain symbols). **Before** computing LLRs for the decoder:
   - The UE runs **`nr_pdsch_ptrs_processing()`** (in **`openair1/PHY/NR_UE_ESTIMATION/nr_dl_channel_estimation.c`**):
     - It **estimates** the common phase error on each PTRS symbol.
     - It **interpolates** the phase estimate in time across the slot.
     - It **rotates** (compensates) **rxdataF_comp** by the conjugate of that phase, symbol by symbol.
   - **LLRs** are then computed from this **compensated** **rxdataF_comp** in **`nr_dlsch_llr()`** (in **`openair1/PHY/NR_UE_TRANSPORT/nr_dlsch_demodulation.c`**).

So the **constellation** that drives the decoder is the **equalized PDSCH constellation after PTRS phase compensation**. Without PTRS (or with phase noise and no PTRS), the constellation would be rotated by phase noise; with PTRS, that rotation is removed and the constellation is cleaner, which can improve BLER and throughput.

### How to verify that your phaseTrackingRS config is in effect

| Check | What to do |
|-------|------------|
| **1. Config loaded** | At gNB startup you should **not** see **`[GNB_APP] No PTRS configuration found`**. You should see **`[GNB_APP] PTRS configuration: dl_FreqDensity0_0 …`** etc. (in **`openair2/GNB_APP/gnb_config.c`**). |
| **2. PDSCH PDU has PTRS** | When PTRS is enabled and **set_dl_ptrs_values()** returns valid (RB ≥ 3, valid K_ptrs/L_ptrs for the current MCS and symbol count), the gNB sets **pdsch_pdu->pduBitmap \|= 0x1** (bit 0 = PTRS present) in **`openair2/LAYER2/NR_MAC_gNB/gNB_scheduler_dlsch.c`** → **prepare_pdsch_pdu()**. So PTRS is only actually used when the allocation and MCS yield valid density values. |
| **3. UE uses PTRS** | The UE MAC fills **dlsch_config** from RRC (including PTRS from **phaseTrackingRS**). When the DCI / PDSCH indicates PTRS (e.g. **pduBitmap & 0x1**), the UE PHY runs **nr_pdsch_ptrs_processing()** only if **`(pduBitmap & 0x1) && (dlsch[0].rnti_type == TYPE_C_RNTI_)`** (see **`openair1/PHY/NR_UE_TRANSPORT/nr_dlsch_demodulation.c`**). You can enable **LOG_D(MAC, …)** in **`openair2/LAYER2/NR_MAC_UE/nr_ue_procedures.c`** (e.g. the existing **“DL PTRS values: PTRS time den: %d, PTRS freq den: %d”**) to confirm the UE receives non-zero PTRS density. |
| **4. Optional: debug PTRS in PHY** | In **`openair1/PHY/NR_UE_ESTIMATION/nr_dl_channel_estimation.c`** you can **#define DEBUG_DL_PTRS 1** (and rebuild). Then the UE will log PTRS phase estimates and write **rxdataF_comp** to `.m` files (e.g. **ptrsEst.m**, **rxdataF_bf_ptrs_comp.m**) so you can compare constellation before/after PTRS compensation in MATLAB/Octave. |
| **5. Effect on performance** | The most direct way to see that PTRS is “making an effect” on the PDSCH constellation is to compare **BLER** or **throughput** with **phaseTrackingRS** enabled vs disabled (or vs all PTRS densities set to 0/-1) in a scenario with non-negligible phase noise or high MCS. With PTRS working, you typically see better BLER at high MCS and/or in phase-noisy conditions. |

### Summary

- **Yes**, your **phaseTrackingRS** block (with e.g. **dl_ptrsFreqDensity0_0 = 5**, **dl_ptrsTimeDensity0_0 = 2**, etc.) **can** affect the PDSCH constellation: the UE derotates the equalized PDSCH symbols using PTRS before computing LLRs.
- You know it is in effect if: (1) gNB logs show PTRS configuration, (2) the PDSCH PDU has **pduBitmap** bit 0 set when the allocation is valid for PTRS, (3) the UE runs **nr_pdsch_ptrs_processing()** (and optionally you see “DL PTRS values” or **DEBUG_DL_PTRS** dumps), and (4) BLER/throughput improves when PTRS is enabled in phase-noisy or high-MCS conditions.

---

## "Segmentation fault" after enabling PTRS

If you see a **segmentation fault** right after the log line **`[NGAP] No AMF is associated to the gNB`**, the crash may occur when **phaseTrackingRS** is enabled. You can avoid the crash by **commenting out the `phaseTrackingRS` block** in the gNB config file. With PTRS disabled, the gNB runs without that segfault. Enabling PTRS currently requires further debugging of the gNB code path.

