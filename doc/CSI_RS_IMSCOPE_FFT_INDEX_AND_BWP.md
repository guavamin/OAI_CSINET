# CSI-RS channel estimate vs FFT index (imscope): BWP, DC, and NZP RE positions

This note explains how to read **“Channel coefficient index”** (0 … `ofdm_symbol_size` [+ margin]) in imscope’s **“CSI-RS per link (RMS)”** plot, how that relates to **carrier / BWP** placement in OpenAirInterface NR, and why you see a **large zero gap** in the middle of the 2048-point axis.

Later sections (**§8–§12**) record FAQ answers on **RRC `density`**, **sparse pilots vs smooth interpolated plots** (106 PRB / 4×4 context), **`fd-CDM2` with 4 ports**, and a **concrete OAI row-4 RE/port map** (**§12**).

It complements `doc/CSI_RS_CONFIG_WALKTHROUGH_gnb_band78_rfsim.md`.

---

## 1. What the x-axis actually is

- imscope plots **one sample per FFT bin** in the UE PHY buffer for the **frequency-domain CSI-RS channel estimate** (after processing in `csi_rx.c`), **not** “subcarrier number 0 … 12·N_RB−1” as a single contiguous block.

Code reference (`openair1/PHY/TOOLS/imscope/imscope.cpp`):

- `n_sc = ue->frame_parms.ofdm_symbol_size + CSIRS_FILTER_MARGIN` with `CSIRS_FILTER_MARGIN == 32`.
- The plot uses `&power[link * n_sc]` over **`n_sc`** points per TX–RX link.

So the horizontal axis is **OAI’s internal frequency index** `k ∈ [0, n_sc − 1]` (typically **2048 + 32** if `ofdm_symbol_size == 2048`). If your screenshot shows **0 … 2048**, that is essentially the **OFDM FFT size** window (any extra margin may be off-screen or clipped depending on version/zoom).

---

## 2. How OAI places the **carrier** on the FFT grid (`first_carrier_offset`)

For downlink, OAI sets (`openair1/PHY/INIT/nr_parms.c`, `set_scs_parameters`):

```c
fp->first_carrier_offset = fp->ofdm_symbol_size - (N_RB_DL * 12 / 2);
```

Where:

- `ofdm_symbol_size` = FFT size `N_fft` (e.g. **2048** for large enough BW),
- `N_RB_DL * 12` = number of occupied subcarriers on the **cell carrier** used to size the FFT (e.g. **106 × 12 = 1272**).

Define:

- `N_sc = N_RB_DL * 12` (e.g. 1272),
- `Half = N_sc / 2` (e.g. **636**),
- `k0 = first_carrier_offset = N_fft − Half` (e.g. **2048 − 636 = 1412**).

Then the **allocated carrier subcarriers** occupy **two index ranges** on `k` (wrap-around layout):

| Segment | Index range `k` (inclusive) | Width |
|--------|-----------------------------|--------|
| **Low side** | `0` … `Half − 1` | 636 (example) |
| **High side** | `k0` … `N_fft − 1` | 636 (example) |
| **Unused / guard around DC** | `Half` … `k0 − 1` | `N_fft − N_sc` = **776** (example) |

For **N_fft = 2048**, **N_sc = 1272**:

- Low: **k = 0 … 635**
- Gap (zeros in an idle carrier): **k = 636 … 1411** (contains the **DC / unused** region between the two halves — your plot’s **~640 … ~1410** matches this)
- High: **k = 1412 … 2047**

So:

- The **BWP/carrier in frequency** is **not** “indices 0 … 1271” on this axis; it is **two sidebands** around the **central hole**.
- **DC** (conceptually) lies in the **middle of that hole** (near `N_fft/2 = 1024` is inside **636 … 1411**).

This explains the **two plateaus** and the **central null** without any NZP-specific logic: the null is mostly **unallocated FFT bins** between the two halves of the **same** occupied carrier.

---

## 3. Relation to **BWP** (initial/active BWP vs carrier)

- `N_RB_DL` in `frame_parms` is the **carrier grid size** OAI used to pick `N_fft` and `first_carrier_offset` (often matches **carrier bandwidth** in PRB, e.g. 106).
- Your **initial DL BWP** (e.g. 48 PRB in the middle of 106 PRB) is a **subset** of that carrier in **3GPP RB/subcarrier** space; in the FFT buffer it still maps into **some subset** of the union of the two `k` ranges above, **not** the full 0…2047 uniformly.

To map **BWP start / width** to `k`, you need the same **subcarrier → FFT index** rules the UE uses when placing the BWP on the grid (Point A, `k0` for the carrier, etc.). At minimum:

- **Carrier** ⇒ two intervals of `k` as in §2.
- **BWP** ⇒ a contiguous band of **logical** subcarriers that appear inside those intervals (possibly only on one side in special cases — your config usually uses a block inside the 106 PRB carrier).

For a **quantitative** BWP↔`k` map, use the UE logs / RRC for **absolute frequency** and the OAI helpers that convert **RB** to **FFT index** (search for `first_carrier_offset`, `N_RB`, `subcarrier` in `nr_parms` / channel estimation).

---

## 4. Where **NZP CSI-RS** sits (why the plateau may be narrower than 636 | 636)

**NZP CSI-RS** is **not** energy on **every** subcarrier in the carrier/BWP:

- RRC gives **freqBand** (`startingRB`, `nrofRBs`) and **resource mapping** (row, density, CDM, OFDM symbol index).
- TS **38.211** defines which **REs** (specific `k` and symbol `l`) carry CSI-RS for that row/density.

The UE builds `csi_rs_estimated_channel_freq[rx][port][k]` on the **full** `ofdm_symbol_size` (+ margin) grid, but **values are only driven by CSI-RS REs** (plus **interpolation / filtering** in `csi_rx.c`, which can spread energy slightly). Therefore:

- You may see **zeros or low RMS** near the **inner edges** of each `k` segment (e.g. ~0…150 in your plot) if there is **no CSI-RS RE** there (BWP edge, pattern, or row), or due to **filter transients**.
- The **flat top** regions (~150…640 and ~1412…2047 in your screenshot) align with **BWP + CSI-RS pattern** coverage after interpolation, **not** automatically “all 636 bins”.

**Exact NZP subcarrier indices** require:

1. **RRC** `NZP-CSI-RS-Resource` / FAPI CSI-RS PDU (`start_rb`, `nr_of_rbs`, row, density, symbol),
2. **38.211** table for the row,
3. Conversion to OAI **FFT index** `k` using the same **carrier/BWP** origin as the UE.

---

## 5. Quick worksheet (106 PRB, N_fft = 2048)

| Quantity | Value |
|----------|--------|
| `N_sc` | 106 × 12 = **1272** |
| `Half` | **636** |
| `k0 = first_carrier_offset` | 2048 − 636 = **1412** |
| Low occupied `k` | **0 … 635** |
| Central unused `k` (DC region) | **636 … 1411** |
| High occupied `k` | **1412 … 2047** |

**NZP CSI-RS:** subset of REs inside the **BWP** portion of those ranges, on the configured **OFDM symbol(s)** — not the entire interval unless the pattern fills it (it usually does not).

---

## 6. Summary

| Question | Answer |
|----------|--------|
| What is “Channel coefficient index”? | OAI **FFT bin index** `k` on the frequency-domain channel vector (`ofdm_symbol_size` typically 2048). |
| Where is the **carrier / usable band**? | **Two** index ranges (see §2); **not** one contiguous 0 … N_sc−1. |
| Why a **big zero gap** in the middle? | **Unoccupied** FFT bins between the two halves of the carrier; **DC** lies in that band. |
| How do I get **BWP** from the plot alone? | You infer **approximate** extent from non-zero RMS; **exact** BWP needs **N_RB / BWP RRC** + mapping; carrier layout from **`first_carrier_offset`** formula. |
| How do I get **NZP CSI-RS** subcarriers? | **38.211** RE pattern + RRC resource + mapping to `k`; plot shows **estimate** (possibly interpolated), not a binary mask of NZP REs. |

---

## 7. References in repo

- FFT placement: `openair1/PHY/INIT/nr_parms.c` — `first_carrier_offset`
- CSI-RS processing: `openair1/PHY/NR_UE_TRANSPORT/csi_rx.c` — `csi_rs_estimated_channel_freq`, filtering
- imscope plot: `openair1/PHY/TOOLS/imscope/imscope.cpp` — “CSI-RS per link (RMS)”
- Config context: `doc/CSI_RS_CONFIG_WALKTHROUGH_gnb_band78_rfsim.md`

---

## 8. FAQ: Does **`density = one`** mean every subcarrier has CSI-RS?

**No.** In NR, the RRC field **`density`** (in `CSI-RS-ResourceMapping`) is **not** “use all subcarriers.”

- It selects one of the **frequency-density** patterns defined in **TS 38.211** together with the **row**, **number of ports**, and **CDM** type.
- **`one`** typically corresponds to the **sparsest** common pattern in that family: on the order of **about one CSI-RS RE per PRB** (in the 38.211 sense for that row), **not** 12 occupied subcarriers per RB.
- Other choices (e.g. **`three`**, **`dot5`**) change how many REs per PRB / which RBs are used.

So even with **`density = one`**, **most** subcarriers in each RB are **not** NZP CSI-RS on that OFDM symbol; they can carry other signals (e.g. PDSCH) on other symbols, etc.

**Bottom line:** **`density = one`** ⇒ **standard “single-density” 38.211 pattern** (sparse in frequency), **not** “CSI-RS on every subcarrier.”

---

## 9. FAQ: **106 PRB** — “106 complex channels” vs **one** channel vs **interpolation**

A precise way to say it:

- **106 PRB** ⇒ **1272 subcarriers** on the carrier (106 × 12). For each **TX–RX pair**, the object is **one** frequency-selective **MIMO channel** **H(k)** — conceptually **one complex gain per subcarrier** \(k\) over the band.

**What CSI-RS measures**

- NZP CSI-RS places pilots on a **sparse set of REs** (density + row + CDM). You do **not** get **106 independent “channels per PRB”** as raw measurements; you get **least-squares (or similar) samples** on those **pilot REs**.

**What OAI / imscope shows**

- The UE chain **interpolates / filters** sparse estimates into **`csi_rs_estimated_channel_freq[rx][port][k]`** across the FFT-sized grid (`csi_rx.c`). That is why **“CSI-RS per link (RMS)”** looks **smooth** vs **k** over the occupied regions — it is **not** a comb of spikes only at pilot **k** indices.

**Short wording**

- **Yes (intuition):** sparse CSI-RS observations → **interpolation / filtering** → dense per-subcarrier estimate → **smooth** \(|H|\) plot.
- **Adjust (precision):** it is **one complex channel per TX–RX pair**, **sampled sparsely** by CSI-RS, then **reconstructed** across subcarriers — **not** “106 separate channel objects” in the measurement sense.

---

## 10. FAQ: **`cdm_Type = fd-CDM2`** with **4 ports** and **4×4 MIMO**

This matches the OAI auto-configuration for **4 DL ports** in `config_csirs()` (`openair2/LAYER2/NR_MAC_gNB/nr_radio_config.c`): **`nrofPorts = p4`**, **`cdm_Type = fd-CDM2`**.

### What **fd-CDM2** means

- **CDM** = multiple **CSI-RS antenna ports** share **time–frequency REs** and are separated by **orthogonal cover codes (OCC)**.
- **`fd-CDM2`** = **frequency-domain** CDM with **2** orthogonal patterns ⇒ **2 ports per CDM group** on the **same REs** (UE applies OCC to separate them).

### What **4 ports + fd-CDM2** implies

- Typically **two CDM groups** × **2 ports per group** = **4 ports** total (exact pairing follows **38.211** for the configured **row**).
- You are **not** placing 4 ports on four fully disjoint RE grids only; you use **two fd-CDM2 pairs** (shared REs + OCC within each pair).

### Link to **4 TX / 4 RX**

- **gNB:** 4 CSI-RS ports map to **4 DL antenna ports** (e.g. **N1=2, N2=2, XP=1**).
- **UE:** For **4 RX** antennas, you estimate a **4×4** matrix **H** at CSI-RS REs (per subcarrier / after processing); **fd-CDM2** defines **how** those 4 ports are multiplexed on the grid, **not** that you only have one scalar channel.

### Contrast with **`noCDM`**

| | **`noCDM`** | **`fd-CDM2`** |
|---|-------------|----------------|
| **Idea** | Ports often use **distinct REs** (simpler patterns). | **Pairs** of ports **share REs**; separation by **2× frequency OCC**. |

Exact RE positions still come from **row + density + freqBand** in **38.211** Table 7.4.1.5.3-1 (and related).

---

## 11. Document map (CSI_RS_*.md)

| File | Contents |
|------|----------|
| `doc/CSI_RS_CONFIG_WALKTHROUGH_gnb_band78_rfsim.md` | gNB `.conf` + `config_csirs()` parameters |
| `doc/CSI_RS_IMSCOPE_FFT_INDEX_AND_BWP.md` | imscope axis, `first_carrier_offset`, NZP vs plot, **§8–12 FAQ**, **§12 row-4 RE/port map** |
| `doc/InterDigital_WI/CSI_RS_RECORDING_AND_ISAC_NOTES.md` | Recording formats, ML sync — see **Related docs** there |

---

## 12. **OAI row 4 + `fd-CDM2` + density `one`**: RE and port map (`config_csirs`, 4 DL ports)

This is the mapping implied by OAI’s **`get_csi_mapping_parms()`** / **`csi_rs_resource_mapping()`** (`openair1/PHY/nr_phy_common/src/nr_phy_common_csirs.c`) for the **auto NZP CSI-RS** built in **`config_csirs()`** (`openair2/LAYER2/NR_MAC_gNB/nr_radio_config.c`) when **`num_dl_antenna_ports == 4`**.

### 12.1 RRC → FAPI/PHY **`freq_domain` bitmap** (row 4)

For **row 4**, the scheduler sets the PDU field from the **packed** RRC octet (see `openair2/LAYER2/NR_MAC_gNB/gNB_scheduler_primitives.c`):

- `freq_domain = (row4.buf[0] >> 5) & 0x07` (3-bit row-4 pattern selector).

OAI’s default for 4 ports uses **`row4.buf[0] = 32`** (`0b00100000`). Then:

- `freq_domain = (32 >> 5) & 7 = 1`.

The PHY table walk in **`get_csi_mapping_parms(..., row = 4, b = freq_domain)`** scans **`b` from bit 0 upward** and takes the **first** set bit as **`fi`**, with **`k_n[0] = fi << 2`**. For **`b = 1`** ⇒ **`fi = 0`** ⇒ **`k_n[0] = 0`**.

So the **meaningful** selector is the **shifted 3-bit value** (`freq_domain`), **not** the raw byte `32` passed to `get_csi_mapping_parms` as if it were the full octet.

### 12.2 Mapping parameters (case **row 4** in code)

From ```274:291:openair1/PHY/nr_phy_common/src/nr_phy_common_csirs.c```:

- **Ports:** 4  
- **`k' = 1`** ⇒ two frequency chips per CDM group (**`kp ∈ {0,1}`**).  
- **`l' = 0`** ⇒ one OFDM symbol per group (**`lp = 0` only**).  
- **`size = 2`** ⇒ **two CDM groups** in frequency (`ji ∈ {0,1}`).  
- **`j[i] = i`** ⇒ group 0 uses **ports 0–1**, group 1 uses **ports 2–3** (via `p = s + j[ji]*gs`, `gs = 2` for **`fd-CDM2`**).  
- **`k̄₀ = k_n[0] + 0`**, **`k̄₁ = k_n[0] + 2`**. With **`k_n[0] = 0`**: **`k̄₀ = 0`**, **`k̄₁ = 2`**.

**Density:** RRC **`density = one`** ⇒ ASN **`present = PR_one`** ⇒ FAPI **`freq_density = 2`** ⇒ **`rho = 1`** (`get_csi_rho`), so **every RB** in **`[start_rb, start_rb + nr_of_rbs)`** gets the pattern (see `csi_rs_resource_mapping`: condition **`freq_density > 1`**).

### 12.3 Per-PRB picture (one symbol **`l = l0`**)

For each **physical RB index** `n` in the CSI-RS frequency band, define the **12 subcarriers** of that RB relative to the band start as indices **`d = 0 … 11`**. With **`start_sc = first_carrier_offset`** (gNB TX), OAI uses:

`k = (start_sc + 12·n + k̄_ji + kp) mod N_fft`

With **`k_n[0]=0`** this places CSI-RS on **four** subcarriers in that RB:

| Subcarrier offset **`d` in RB** (`k̄_ji + kp`) | `ji` | Ports on the **same** `(l, k)` (separated by **fd-CDM2 OCC**) |
|-----------------------------------------------|------|----------------------------------------------------------------|
| **0, 1** | 0 | **0** and **1** (same two **`k`**, different OCC **`wf`**) |
| **2, 3** | 1 | **2** and **3** |

So you get **two adjacent-subcarrier pairs** per RB on **one** OFDM symbol: each pair is **one fd-CDM2 group** (2 ports × 2 frequency OCC chips). It is **not** “four disjoint single-subcarrier pilots” in the sense of four independent REs per port; **ports in a group share the same two subcarriers** and are separated by the cover code (**`wf`** on **`kp`**, see ```47:49:openair1/PHY/nr_phy_common/src/nr_phy_common_csirs.c```).

**Auto symbol index:** `config_csirs()` sets **`firstOFDMSymbolInTimeDomain = 13`** (last symbol in the slot for the normal 14-symbol case).

### 12.4 FFT index for imscope

For a given RB `n`, the four CSI-RS subcarriers correspond to **`k`** values as above; they map to the **low four subcarriers of that RB** on the **carrier grid** (then wrapped into the two-sided FFT layout described in §2–§3). Use **`first_carrier_offset`**, **`start_rb`**, and **`n`** from the CSI-RS PDU if you need numeric **`k`** for a trace.

---

## Revision

- Initial note: imscope FFT index, `first_carrier_offset`, NZP vs interpolated channel estimate.
- **2026:** Added §8–11 (density FAQ, 106 PRB / interpolation, fd-CDM2 + 4 ports), document map — from technical Q&A aligned with OAI behavior; later extended with **§12**.
- **2026:** Added **§12** — OAI **row 4** + **`fd-CDM2`** + density **`one`**: **`freq_domain`** from **`row4.buf[0]`**, per-PRB subcarrier offsets **0–3**, port pairing **{0,1}** / **{2,3}**.
