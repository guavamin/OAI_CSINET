# RSRP Recording and Near RIC

This document describes how **RSRP (Reference Signal Received Power)** values are produced, which **reference signal** (SSB or CSI-RS) is used, how that depends on **CSI report type**, and how (and whether) RSRP is **recorded or sent to the near RIC** in this repository.

---

## 1. Reference signal used for RSRP: SSB vs CSI-RS

**Yes — it depends on the CSI report type.**

The gNB configures **one** RSRP report per UE (in addition to other CSI reports such as RI-PMI-CQI). The **reference signal** used for that RSRP report is determined by the **`CSI_report_type`** (config parameter) and RRC **reportQuantity**:

| CSI_report_type (config) | RRC reportQuantity           | Reference signal used for RSRP |
|--------------------------|-----------------------------|--------------------------------|
| **`ssb_rsrp`** (default) | `ssb_Index_RSRP`            | **SSB**                        |
| **`cri_rsrp`**           | `cri_RSRP`                  | **CSI-RS** (NZP-CSI-RS)        |
| **`ssb_sinr`**           | (SINR report, not RSRP)     | N/A — this is **SINR** on SSB, not RSRP |

- **SSB:** UE measures SSB and reports SSB index + RSRP (and optionally differential RSRP for other beams).
- **CSI-RS:** UE measures NZP-CSI-RS and reports CRI (CSI-RS Resource Indicator) + RSRP.

**Configuration (gNB):**

- Parameter: **`CSI_report_type`** in the gNB config file (e.g. `gnb.sa.band78.fr1.106PRB.usrpb210.conf`).
- Config key: **`CSI_report_type`** (string). Allowed values: `"ssb_rsrp"`, `"ssb_sinr"`, `"cri_rsrp"`.
- Default: **`ssb_rsrp`** (SSB-based RSRP).
- For **CSI-RS–based RSRP** you must set **`CSI_report_type = "cri_rsrp"`** and have **`do_CSIRS = 1`** and (for the current implementation) fewer than 4 PDSCH antenna ports.

**Where it is set in code:**

- **`openair2/LAYER2/NR_MAC_gNB/nr_radio_config.c`**: **`config_rsrp_meas_report()`**
  - If `configuration->report_type == CRI_RSRP && configuration->do_CSIRS && num_antenna_ports < 4` → **`reportQuantity = cri_RSRP`** (CSI-RS).
  - Else (for RSRP) → **`reportQuantity = ssb_Index_RSRP`** (SSB).
- **`openair2/GNB_APP/gnb_paramdef.h`**: **`CSI_report_type`** (default `"ssb_rsrp"`), mapped to `SSB_RSRP`, `SSB_SINR`, or `CRI_RSRP`.

---

## 2. Where RSRP is recorded (gNB side)

After the UE sends the CSI report on **PUCCH**, the gNB decodes it and stores RSRP in the UE’s scheduler control and (optionally) in the PHY scope.

### 2.1 Decoding and storage in MAC

- **File:** **`openair2/LAYER2/NR_MAC_gNB/gNB_scheduler_uci.c`**
- **Flow:** **`handle_nr_uci_pucch_2_3_4()`** → **`extract_pucch_csi_report()`** → for the RSRP report, **`evaluate_rsrp_report()`**.
- **`evaluate_rsrp_report()`**:
  - Dispatches by **`reportQuantity_type`**:
    - **`NR_CSI_ReportConfig__reportQuantity_PR_ssb_Index_RSRP`** → **`rsrp_report = &sched_ctrl->CSI_report.ssb_rsrp_report`**
    - **`NR_CSI_ReportConfig__reportQuantity_PR_cri_RSRP`** → **`rsrp_report = &sched_ctrl->CSI_report.csirs_rsrp_report`**
  - Fills **`rsrp_report->r[].resource_id`** (SSB or CRI index), **`rsrp_report->r[].RSRP`** (dBm, from 7-bit payload per 38.212 Table 6.3.1.1.2-2), and for multiple reported resources, differential RSRP.
  - Also updates **`UE->mac_stats.cumul_rsrp`** and **`UE->mac_stats.num_rsrp_meas`** (used for internal stats; not sent to RIC in current code).

So the **reference signal** used for RSRP is exactly the one implied by the report type: **SSB** for `ssb_Index_RSRP`, **CSI-RS** for `cri_RSRP`.

### 2.2 PHY scope (imscope)

When a CSI report is decoded and the code builds the **gNBCsiReportParams** scope payload (same file, after decoding CSI part1):

- **`payload.rsrp_dBm`** is set from:
  - **`sched_ctrl->CSI_report.csirs_rsrp_report.r[0].RSRP`** if **`csirs_rsrp_report.nb > 0`** (CSI-RS RSRP),
  - otherwise it remains 0 (SSB RSRP is not copied into `rsrp_dBm` in this path; SINR can still be taken from **`ssb_rsrp_report`** for display).
- This payload is then sent to the **PHY scope** via **`gNBscopeCopyWithMetadata(gNB, gNBCsiReportParams, &payload, ...)`**.

So **RSRP is recorded** into:

1. **MAC:** **`sched_ctrl->CSI_report.ssb_rsrp_report`** or **`sched_ctrl->CSI_report.csirs_rsrp_report`** (depending on report type).
2. **Scope:** **`gNBCsiReportParams`** (for imscope); the `rsrp_dBm` field is filled from **CSI-RS RSRP** when that report is present.

---

## 3. Recording to the near RIC

In this repository, **RSRP is not sent to the near RIC** over the standard E2 interface in the current implementation.

- **E2SM-KPM** (E2AP README and **`openair2/E2AP/RAN_FUNCTION/O-RAN/ran_func_kpm.c`**): The KPM measurement list for gNB includes only metrics such as `DRB.PdcpSduVolumeDL`, `DRB.UEThpDl`, `RRU.PrbTotDl`, etc. **No RSRP** is defined or reported.
- **MAC custom Service Model** (**`openair2/E2AP/RAN_FUNCTION/CUSTOMIZED/ran_func_mac.c`**): The MAC indication to the RIC carries **`mac_ue_stats_impl_t`** (e.g. CQI, MCS, BLER, throughput, PUSCH/PUCCH SNR). **RSRP is not part of this struct** in the current code; therefore RSRP is not “recorded to the near RIC” via the MAC SM either.

So today:

- **RSRP is recorded** in the gNB MAC (and optionally in the PHY scope for imscope).
- **RSRP is not reported to the near RIC** via E2 (neither KPM nor the current MAC SM).

To send RSRP to the near RIC you would need to extend the implementation, for example:

- Add an RSRP field to **`mac_ue_stats_impl_t`** and fill it in **`read_mac_sm()`** from **`sched_ctrl->CSI_report.ssb_rsrp_report.r[0].RSRP`** or **`csirs_rsrp_report.r[0].RSRP`** (depending on which report is configured), or
- Use a custom E2 SM or xApp that reads from the same MAC structures or from the scope path (if exposed), and then sends data to the near RIC.

---

## 4. Flow diagram

```mermaid
flowchart TB
  subgraph CONFIG["gNB config"]
    A[CSI_report_type\nssb_rsrp | cri_rsrp | ssb_sinr]
    B[config_rsrp_meas_report]
    C{report_type?}
    D[reportQuantity =\nssb_Index_RSRP]
    E[reportQuantity =\ncri_RSRP]
    A --> B --> C
    C -->|ssb_rsrp| D
    C -->|cri_rsrp + do_CSIRS| E
  end

  subgraph UE["UE"]
    F[Measure SSB or NZP-CSI-RS]
    G[Encode RSRP on PUCCH]
    F --> G
  end

  subgraph GNB_MAC["gNB MAC (gNB_scheduler_uci.c)"]
    H[extract_pucch_csi_report]
    I[evaluate_rsrp_report]
    J{reportQuantity?}
    K[(ssb_rsrp_report)]
    L[(csirs_rsrp_report)]
    G --> H --> I --> J
    J -->|ssb_Index_RSRP| K
    J -->|cri_RSRP| L
  end

  subgraph RECORD["Recording / consumers"]
    M[mac_stats.cumul_rsrp\nnum_rsrp_meas]
    N[gNBCsiReportParams\nscope → imscope]
    O[Near RIC via E2]
    K --> M
    L --> M
    L -.->|rsrp_dBm when csirs used| N
    K -.-> N
    M -.->|not in current KPM/MAC SM| O
  end

  D --> F
  E --> F
```

**Summary:**

- **Reference signal for RSRP:** **SSB** if `CSI_report_type = "ssb_rsrp"`, **CSI-RS** if `CSI_report_type = "cri_rsrp"` (with `do_CSIRS` and antenna constraints). **Yes, it depends on CSI report type.**
- **Where RSRP is recorded:** In the gNB in **`sched_ctrl->CSI_report.ssb_rsrp_report`** or **`csirs_rsrp_report`**, and optionally in the PHY scope (**gNBCsiReportParams**) for imscope.
- **Near RIC:** RSRP is **not** currently sent to the near RIC; E2SM-KPM and the current MAC SM do not include RSRP. Sending RSRP to the near RIC would require extending the MAC SM (or adding another path) and filling it from the same report structures above.

---

## 5. Custom SM for RSRP and SRS channel estimates (what is needed)

A **custom Service Model (SM)** can report **RSRP** and, when enabled, **SRS channel estimates** to the near-RIC over the E2 interface. Below is what is needed in OAI and in the RIC/FlexRIC side.

### 5.1 Overview

- **RSRP** is available in the gNB **MAC** (per-UE): `sched_ctrl->CSI_report.ssb_rsrp_report` or `csirs_rsrp_report` (see Section 2).
- **SRS channel estimates** are available in the gNB **PHY** (per slot when SRS is processed): `srs_estimated_channel_freq[][]` in **`openair1/SCHED_NR/phy_procedures_nr_gNB.c`**; they are already fed to the scope as **gNBSrsChEstimate** when imscope is used.
- The E2 agent runs in the same process as the gNB and has access to **`RC.nrmac[mod_id]`** (MAC) and, for PHY data, would need access to the gNB PHY context (e.g. **`RC.gNB[]`** or a shared buffer filled by PHY).

Two implementation options:

| Option | Description |
|--------|--------------|
| **A. Extend existing MAC SM** | Add RSRP (and optionally a small SRS summary) to **`mac_ue_stats_impl_t`** and fill it in **`read_mac_sm()`**. RIC/FlexRIC must use the same updated struct. SRS channel **matrix** (IQ) is large; for Option A only lightweight SRS metadata (e.g. SRS SNR, “SRS valid” flag) is practical. |
| **B. New custom SM “RSRP_SRS”** | New ran function (new SM type and read callback) that reports RSRP per UE and, when enabled, SRS data. SRS can be reported as compressed/downsampled IQ or as a reference to a shared buffer. Requires new SM type in FlexRIC and new entry in **`init_ran_func.c`**. |

### 5.2 What is needed in OAI (agent / RAN function)

1. **Data source for RSRP**
   - **MAC:** In **`read_mac_sm()`** (or in the new SM’s read callback), for each UE read:
     - **`sched_ctrl->CSI_report.ssb_rsrp_report.r[0].RSRP`** and **`.resource_id`** when SSB RSRP is configured,
     - or **`sched_ctrl->CSI_report.csirs_rsrp_report.r[0].RSRP`** and **`.resource_id`** when CSI-RS RSRP is configured.
   - Decide which report to use (e.g. from config or from which report has `nb > 0`). Same pattern as in **`gNB_scheduler_uci.c`** when building the scope payload (Section 2.2).

2. **Data source for SRS channel estimates (if enabled)**
   - **PHY:** SRS estimates are produced in **`openair1/SCHED_NR/phy_procedures_nr_gNB.c`** in **`nr_srs_rx_procedures()`** → **`srs_estimated_channel_freq[nb_antennas_rx][N_ap][ofdm_symbol_size * N_symb_SRS]`** (complex int16 per antenna, SRS port, subcarrier).
   - They are **not** stored in MAC. To expose them to the E2 agent you need one of:
     - **Shared buffer:** PHY writes the latest SRS estimate (or a downsampled/compressed version) to a shared structure (e.g. in **`RC.gNB[]`** or a dedicated **`e2_srs_buffer_t`**) that the custom SM read callback reads from; or
     - **Copy from PHY at read time:** The read callback is invoked when the RIC requests an indication; at that time it can only use data that is already stored somewhere. So PHY must periodically (e.g. each SRS slot) copy the estimate into a buffer accessible to the agent (same process, so e.g. **`RC.gNB[0]->e2_srs_channel_buffer`**).
   - **Size:** Full SRS channel is large (antennas × ports × subcarriers × 2 × 2 bytes). For RIC reporting, consider downsampling in frequency, reporting only a subset of antennas/ports, or compressing (e.g. magnitude/phase or PCA) and document the format in the SM.

3. **Read callback (indication)**
   - **Option A:** In **`openair2/E2AP/RAN_FUNCTION/CUSTOMIZED/ran_func_mac.c`**, extend **`read_mac_sm()`** to fill new fields (e.g. **`rd->rsrp_dBm`**, **`rd->rsrp_resource_id`**, and optionally **`rd->srs_snr_dB10`** or **`rd->srs_valid`**). No new SM type.
   - **Option B:** Add a new RAN function (e.g. **RSRP_SRS_STATS_V0**): new **`ran_func_rsrp_srs.c`** with **`read_rsrp_srs_sm(void* data)`** that fills a new struct (e.g. **`rsrp_srs_ind_data_t`**) with RSRP per UE and, if enabled, SRS channel pointer/size or inlined compressed block. Register in **`openair2/E2AP/RAN_FUNCTION/init_ran_func.c`**:
     - **`(*read_ind_tbl)[RSRP_SRS_STATS_V0] = read_rsrp_srs_sm;`**
     - and, if the SM has a setup phase, a **`read_rsrp_srs_setup_sm`** in **`read_setup_tbl**.
   - The callback must only run when E2 agent is enabled (same as MAC SM: **NGRAN_GNB_DU** for gNB-mono/DU).

4. **Build**
   - E2 agent and RAN functions are built with the gNB when **`--build-e2`** (or **`-DE2_AGENT=ON`**) is used. The new or modified SM is part of the same **e2_ran_func_du_cucp_cuup** (or the target that contains **ran_func_mac**) so no extra build flag is needed beyond ensuring E2 is built.

### 5.3 What is needed in FlexRIC / near-RIC

1. **Struct and encoding**
   - **Option A:** In FlexRIC, extend **`mac_ue_stats_impl_t`** with the new fields (e.g. **`rsrp_dBm`**, **`rsrp_resource_id`**, **`srs_snr_dB10`**). The near-RIC and xApps must use the same struct layout (plain binary; see E2AP README §3.2).
   - **Option B:** Define a new indication message type and struct (e.g. **`rsrp_srs_ind_data_t`**) in FlexRIC and handle it in the RIC and xApps. The RAN function name and ID for the new SM must be registered in FlexRIC so the RIC subscribes to the right ranFunctionID.

2. **RAN function registration**
   - The E2 agent reports supported RAN functions at E2 Setup. For a **new** SM (Option B), FlexRIC must know the new ranFunctionID and (if applicable) OAM/configuration to subscribe for RSRP_SRS indications. For Option A, the existing MAC SM subscription continues to work; xApps just interpret the extended **`mac_ue_stats_impl_t`**.

3. **xApp / consumer**
   - An xApp (or the RIC) that subscribes to the MAC SM (Option A) or to the new RSRP_SRS SM (Option B) will receive indication messages. It must decode the binary payload according to the agreed struct and, for SRS, handle the chosen format (e.g. downsampled IQ or compressed).

### 5.4 Enabling “SRS channel estimates” in the custom SM

- **Config / feature flag:** Add a gNB or E2 config flag (e.g. **`e2_agent.report_srs_channel = 1`** or **`report_srs_channel_estimates = true`**) so that:
  - PHY (or a small glue in MAC/PHY) copies SRS channel data into the shared buffer only when enabled.
  - The read callback includes SRS data in the indication only when enabled (and optionally only when the buffer is valid for the current period).
- **When SRS is available:** SRS channel estimates exist only in slots where SRS is transmitted and processed (see **`nr_srs_rx_procedures`** and **`gNBSrsChEstimate`** scope feed in **phy_procedures_nr_gNB.c**). The custom SM should indicate “SRS valid” (e.g. timestamp or flag) so the RIC knows whether the latest SRS block is current.

### 5.5 Summary checklist

| Item | Where | Action |
|------|--------|--------|
| RSRP source | MAC | Read **`ssb_rsrp_report`** or **csirs_rsrp_report** in read callback (Option A or B). |
| SRS source | PHY | Add shared buffer or copy from **srs_estimated_channel_freq** in **phy_procedures_nr_gNB.c**; read in SM callback (Option B or extended MAC). |
| Struct | FlexRIC / OAI | Extend **mac_ue_stats_impl_t** (A) or add **rsrp_srs_ind_data_t** (B); keep OAI and RIC in sync. |
| Read callback | OAI **init_ran_func.c** | Extend **read_mac_sm** (A) or add **read_rsrp_srs_sm** + **RSRP_SRS_STATS_V0** (B). |
| RIC / xApp | FlexRIC | Decode extended MAC (A) or new RSRP_SRS indication (B); subscribe to correct ranFunctionID. |
| Enable SRS | gNB / E2 config | Flag to turn on SRS channel reporting and PHY→buffer copy. |

---

## 6. References in code

| Topic | File(s) |
|-------|--------|
| CSI_report_type config | `targets/.../gnb.sa.band78.fr1.106PRB.usrpb210.conf`, `openair2/GNB_APP/gnb_paramdef.h` |
| RSRP report config (SSB vs CSI-RS) | `openair2/LAYER2/NR_MAC_gNB/nr_radio_config.c` — `config_rsrp_meas_report()` |
| Decode RSRP and store in ssb/csirs report | `openair2/LAYER2/NR_MAC_gNB/gNB_scheduler_uci.c` — `evaluate_rsrp_report()` |
| Scope payload (rsrp_dBm) | `openair2/LAYER2/NR_MAC_gNB/gNB_scheduler_uci.c` — CSI report handling, `gNBscopeCopyWithMetadata(gNBCsiReportParams, ...)` |
| E2/KPM (no RSRP) | `openair2/E2AP/RAN_FUNCTION/O-RAN/ran_func_kpm.c`, `openair2/E2AP/README.md` |
| MAC SM (no RSRP in ue_stats) | `openair2/E2AP/RAN_FUNCTION/CUSTOMIZED/ran_func_mac.c` — `read_mac_sm()` |
| E2 agent init / custom SM registration | `openair2/E2AP/RAN_FUNCTION/init_ran_func.c` — `read_ind_tbl`, `read_setup_tbl` |
| SRS channel estimates (PHY) | `openair1/SCHED_NR/phy_procedures_nr_gNB.c` — `nr_srs_rx_procedures()`, `srs_estimated_channel_freq`, `gNBSrsChEstimate` scope feed |
