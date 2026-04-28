# CSI-RS configuration walkthrough: `gnb.sa.band78.106prb.rfsim_csi_rs.conf`

This document walks through the OAI gNB libconfig profile used for band n78, 106 PRB, rfsimulator, with CSI-RS enabled. It explains what is set **in the file**, what OAI fills in **elsewhere** (`nr_radio_config.c` → `config_csirs()`), and how that relates to **OFDM symbols**, **RBs**, **subcarriers**, and **occasions per frame**.

**Reference config:** `targets/PROJECTS/GENERIC-NR-5GC/myconfig/gnb.sa.band78.106prb.rfsim_csi_rs.conf`  
**Implementation reference:** `openair2/LAYER2/NR_MAC_gNB/nr_radio_config.c` — `config_csirs()`

---

## 1. Role of the file

The file is a **libconfig** profile for the gNB: band **n78**, **106 PRB**, **rfsimulator**, plus **MIMO and CSI-RS scheduling knobs**.

The **exact RE pattern** (which subcarriers on which symbol) is **not** spelled out line-by-line in this file. It is built in **`config_csirs()`** from **antenna port count** and **BWP size**, per 3GPP NR resource mapping (TS 38.211 / 38.331).

---

## 2. Identification and RF

- **`gNB_ID`**, **`plmn_list`**, **`nr_cellid`**: cell identity.
- Used for scrambling and related signaling; CSI-RS **`scramblingID`** is tied to **`physCellId`** in OAI (`config_csirs()`).

---

## 3. MIMO and CSI-RS enable

Relevant excerpt:

```text
    # 4x4 MIMO
    pdsch_AntennaPorts_N1 = 2;
    pdsch_AntennaPorts_N2 = 2;
    pdsch_AntennaPorts_XP = 1;
    pusch_AntennaPorts    = 4;
    maxMIMO_layers        = 4;

    do_CSIRS              = 1;
    do_SRS                = 1;

    CSI_RS_periodicity_slots = 160;
    CSI_RS_slot_offset = 0;
```

| Parameter | Meaning |
|-----------|---------|
| **`pdsch_AntennaPorts_N1/N2/XP`** with `N1=2, N2=2, XP=1` | **4 logical DL ports** (2×2 UPA, single polarization). Drives the CSI-RS **row / port count** in `config_csirs` (4-port branch). |
| **`do_CSIRS = 1`** | Enables **NZP CSI-RS** and related **CSI-MeasConfig**; when ports > 1, **CSI-IM** can be associated (same source file). |
| **`CSI_RS_periodicity_slots`** | CSI-RS **repetition period in slots** (OAI maps allowed values to RRC `periodicityAndOffset`). Example: **160** at **30 kHz** → 160 × 0.5 ms = **80 ms** = **8 radio frames** between repeats of the same periodic pattern (for that resource), **not** “160 times per frame.” |
| **`CSI_RS_slot_offset = 0`** | Phase within that period (which slot index within the cycle gets the occasion). With **TDD**, only **DL** slots are valid for CSI-RS transmission. |

---

## 4. Carrier and BWP (frequency scope)

| Key | Typical meaning in this profile |
|-----|--------------------------------|
| **`dl_subcarrierSpacing = 1`** | **30 kHz** SCS. |
| **`dl_carrierBandwidth = 106`** | **106 PRB** on the carrier. |
| **`initialDLBWPlocationAndBandwidth`** | Encodes **initial DL BWP** (start RB + width) via 3GPP-style RIV encoding (see comment in config). OAI decodes this to **`curr_bwp`** used when building CSI-RS. |

In **`config_csirs()`**, frequency scope for the NZP resource uses:

- **`startingRB = 0`** (relative to the BWP used in that path),
- **`nrofRBs`**: BWP-aligned width derived from **`curr_bwp`**.

So CSI-RS is laid over the **configured BWP width** from RB 0 of that BWP. **Which REs inside those RBs** are CSI-RS is determined by **row + density + CDM** (see §6), not by this file alone.

---

## 5. TDD pattern (which slots can be DL)

Example from the same config:

```text
     dl_UL_TransmissionPeriodicity = 5;
     nrofDownlinkSlots             = 3;
     nrofDownlinkSymbols           = 10;
     nrofUplinkSlots               = 1;
     nrofUplinkSymbols             = 2;
```

Only **DL slots** can carry CSI-RS. So periodicity + offset must align with a **DL** slot; UL slots never carry this NZP CSI-RS.

---

## 6. CSI report type (UE reporting mode)

Example:

```text
    CSI_report_type = "cri_rsrp";
```

This steers **what the UE reports** (e.g. CRI+RSRP vs SSB-based measurements). It does **not** change the **physical RE mapping** of CSI-RS on the grid, but it matters for whether you see full **RI/PMI/CQI** vs RSRP-oriented reporting in the configured measurement setup.

---

## 7. RU / rfsimulator

Example:

```text
  nb_tx          = 4
  nb_rx          = 4
```

**4×4 RF chains** align with **4 DL port** MIMO in simulation; CSI-RS is generated for **4 ports** in the 4-port branch.

---

## 8. What the `.conf` does *not* show (filled in by OAI)

For **4 DL ports**, `config_csirs()` programs **one** NZP CSI-RS resource approximately as follows:

| Parameter | OAI value (4 ports) |
|-----------|---------------------|
| **`frequencyDomainAllocation`** | **Row 4** (`row4`, bitmap e.g. `buf[0]=32`) |
| **`nrofPorts`** | **p4** |
| **`cdm_Type`** | **fd-CDM2** |
| **`firstOFDMSymbolInTimeDomain`** | **13** (last OFDM symbol of the slot in this configuration) |
| **`firstOFDMSymbolInTimeDomain2`** | **NULL** → **one OFDM symbol** for this resource |
| **`density`** | **one** (RRC density choice “one”) |
| **Freq band** | `startingRB = 0`, `nrofRBs` = BWP-aligned width |

Implications:

- **OFDM symbols per CSI-RS occasion:** **1 symbol** (symbol index **13**) for this auto-generated resource.
- **Which subcarriers:** Given by **TS 38.211** for **row 4**, **4 ports**, **fd-CDM2**, **density one**, over **`nrofRBs`** RBs — implemented in PHY CSI-RS mapping code.
- **`ofdm_symbol_size` (e.g. 2048):** FFT size; CSI-RS REs use **specific subcarrier indices** on that grid inside the active BWP, **not** all FFT bins.

---

## 9. Occasions per slot and per radio frame (this profile)

- **Per slot:** At most **one** NZP CSI-RS resource from this `config_csirs` path (single resource in the set). If the slot is **not DL** or **not** a periodic CSI-RS slot → **zero** transmissions of that resource.

- **Per radio frame (10 ms) at 30 kHz:** **20 slots per frame.** With **`periodicity = 160` slots**, the pattern repeats every **160 slots** (≈ **8 frames**). Within a **single** frame you typically get **at most one** occasion **if** period+offset aligns with a **DL** slot in that frame (often **0 or 1** per frame for such a slow period).

- To get **more CSI-RS occasions per frame**, reduce **`CSI_RS_periodicity_slots`** (e.g. toward **4, 5, 8, …** as allowed) subject to OAI’s `set_csirs_periodicity()` and TDD constraints.

---

## 10. How to verify

| What to check | Where |
|---------------|--------|
| RRC **NZP-CSI-RS-Resource** | Decoded UE capability / RRC trace: `resourceMapping` (row, symbols, density, `freqBand`). |
| OAI **exact numbers** | `nr_radio_config.c` → **`config_csirs()`** for `num_dl_antenna_ports` branch. |
| RE → **k** on FFT grid | PHY CSI-RS TX/RX (38.211 mapping implementation). |

---

## 11. Related questions (short answers)

**Is a CSI-RS resource “a set of RB”?**  
Partly: the **frequency span** is often expressed as **starting RB + number of RBs** within the BWP. The resource is still a **pattern of REs** (subset of subcarriers in those RBs on specific symbols), not every subcarrier in every RB.

**How do we know which subcarriers in a symbol?**  
From **RRC** `CSI-RS-Resource` / `resourceMapping` and **TS 38.211**; OAI implements this in PHY from the ASN.1 built in `config_csirs()`.

---

## 12. What **`density = one`** means (RRC / 38.211)

**Not** “CSI-RS on every subcarrier.”

The **`density`** field in `CSI-RS-ResourceMapping` selects the **frequency-domain** CSI-RS pattern per **TS 38.211** for the chosen **row**, **ports**, and **CDM**. The value **`one`** is the usual **single-density** option: **sparse** REs in frequency (on the order of **~1 CSI-RS RE per PRB** in the pattern sense for many rows), **not** all 12 subcarriers per RB.

For **imscope** (smooth \(|H|\) vs index) and **interpolation** from sparse pilots, see **`doc/CSI_RS_IMSCOPE_FFT_INDEX_AND_BWP.md`** §8–9.

---

## 13. What **`cdm_Type = fd-CDM2`** implies with **4 ports** (OAI 4-port branch)

For **`num_dl_antenna_ports == 4`**, OAI sets (`config_csirs()`):

- **`nrofPorts`:** **p4**
- **`cdm_Type`:** **fd-CDM2**

**fd-CDM2** = frequency-domain CDM with **2** orthogonal covers ⇒ **2 CSI-RS ports per CDM group** on shared REs. **Four ports** therefore typically means **two groups** × **2 ports** (pairing per **38.211** row table).

Together with **4 TX / 4 RX** MIMO, the UE still estimates a **4×4** channel matrix; CDM defines **multiplexing of the four CSI-RS ports on the grid**, not a single scalar channel.

Extended discussion: **`doc/CSI_RS_IMSCOPE_FFT_INDEX_AND_BWP.md`** §10.

---

## 14. CSI_RS documentation set

| Document | Role |
|----------|------|
| This file | Config file + `config_csirs()` walkthrough |
| `doc/CSI_RS_IMSCOPE_FFT_INDEX_AND_BWP.md` | FFT index, DC gap, NZP vs interpolated plot, density/CDM/interpolation FAQ |
| `doc/InterDigital_WI/CSI_RS_RECORDING_AND_ISAC_NOTES.md` | `--csi-record-path`, datasets |

---

## Revision

- Document generated from the project walkthrough for `gnb.sa.band78.106prb.rfsim_csi_rs.conf` and `config_csirs()` in `nr_radio_config.c`.
- **2026:** Added §12–14 (density semantics, fd-CDM2 + 4 ports, doc map).
