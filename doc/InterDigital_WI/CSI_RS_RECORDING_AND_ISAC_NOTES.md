# CSI-RS recording, timing sync, and ISAC notes (OAI)

**Related CSI-RS docs (general PHY / imscope / gNB config):**

- `doc/CSI_RS_CONFIG_WALKTHROUGH_gnb_band78_rfsim.md` — walkthrough of `gnb.sa.band78.106prb.rfsim_csi_rs.conf` and `config_csirs()` (periodicity, row, density, **fd-CDM2**, symbol index).
- `doc/CSI_RS_IMSCOPE_FFT_INDEX_AND_BWP.md` — imscope **channel coefficient index**, `first_carrier_offset` / DC gap, **NZP REs vs interpolated** plot, FAQ on **density = one**, **106 PRB / interpolation**, **fd-CDM2 + 4 ports**.

---

This note summarizes the changes and conclusions from an interactive working session on:

- CSI recording (`--csi-record-path`) for **UE** and **gNB**
- Adding **timestamps** and making binary channel files unique
- Building a **time-synced dataset** for supervised ML
- UE imscope windows and what “PDSCH channel estimates” represent
- CSI-RS periodicity/offset behavior in TDD (30 kHz SCS)
- Why UE PMI fine index (`i2`) can flip even in static simulated channels

---

## 1) CSI recording outputs (current behavior)

### UE (nr-uesoftmodem)

When `--csi-record-path <dir>` is set on the UE:

- Creates CSV: `<dir>/csi_reports.csv`
- Creates folder for channel binaries: `<dir>/csi_rs_channels/`
- Writes CSI-RS channel estimates as:
  - `<dir>/csi_rs_channels/H_<timestamp_utc_us>_f<frame>_s<slot>.bin`
- Each CSV row includes a column `H_bin_path` pointing to the corresponding `H_*.bin` file (or empty if no channel was written for that row).

**CSV format (`csi_reports.csv`)**

- Header:
  - `timestamp_utc_us,frame,slot,H_bin_path,rsrp_dBm,ri,i1_0,i1_1,i1_2,i2,cqi,sinr_dB`
- `timestamp_utc_us` uses `gettimeofday()` in microseconds since Unix epoch.
- `H_bin_path` is quoted to be robust to commas in paths.

**Binary format (`H_*.bin`)**

- Header:
  - 5 × int32: `frame, slot, nr_rx, n_ports, n_subc`
  - 1 × int64: `timestamp_utc_us`
- Payload:
  - `nr_rx * n_ports * n_subc` × `c16_t` (flat channel matrix)

**Implementation**

- UE code: `openair1/PHY/NR_UE_TRANSPORT/csi_rx.c`
- Writes are guarded by a mutex to avoid interleaved appends.
- The `csi_rs_channels/` folder is created under the record path (mkdir; EEXIST ignored).

**Path buffer sizing**

- `csi_rs_channels_dir` uses `PATH_MAX`.
- `path`/`h_bin_path_buf` use `PATH_MAX + 64` to avoid GCC `-Wformat-truncation` when concatenating directory + filename.

### gNB (nr-softmodem)

When `--csi-record-path <dir>` is set on the gNB:

- `<dir>/gnb_csi_feedback.csv`: decoded CSI feedback (UCI)
- `<dir>/gnb_csirs_scheduling.csv`: CSI-RS scheduling info

Both files now have **`timestamp_utc_us` as the first column** (microseconds since epoch via `gettimeofday()`).

**Implementation**

- `gnb_csi_feedback.csv`: `openair2/LAYER2/NR_MAC_gNB/gNB_scheduler_uci.c`
- `gnb_csirs_scheduling.csv`: `openair2/LAYER2/NR_MAC_gNB/gNB_scheduler_primitives.c`

---

## 2) Time synchronization for supervised ML datasets

### Goal

Create a dataset where:

- UE-side CSI-RS channel snapshots (`H_*.bin`) and UE-side label CSV rows (`csi_reports.csv`)
- gNB-side decoded CSI feedback (`gnb_csi_feedback.csv`)

can be **joined in time**.

### Mechanism

All CSV rows and UE binary file metadata use the same semantic timestamp:

- **`timestamp_utc_us`** = microseconds since Unix epoch from `gettimeofday()`

### Recommendation

For best alignment in distributed setups:

- keep gNB and UE clocks synchronized (NTP/PTP)
- join by `timestamp_utc_us` with a small tolerance window (processing delays exist)

Note: UE `(frame,slot)` refers to the **DL measurement slot** for CSI-RS. gNB `(frame,slot)` refers to the **UL decode slot** (for CSI feedback UCI). These are not inherently identical; timestamp matching is the intended sync key.

---

## 3) UE imscope windows and reference signals

### What “UE PDSCH Chan est” represents

In UE imscope, **“UE PDSCH Chan est”** corresponds to a **DM-RS-based channel estimate** for the scheduled **PDSCH** allocation. It is not CSI-RS.

### Related UE windows (examples)

From `doc/IMSCOPE_WINDOWS_FILTER.md` (UE):

- `UE PDSCH IQ`: equalized PDSCH symbols (`pdschRxdataF_comp`)
- `UE PDSCH IQ before compensation`: pre-equalization symbols (`pdschRxdataF`)
- `UE PDSCH Chan est`: PDSCH DM-RS channel estimates (`pdschChanEstimates`)
- `UE CSI-RS channel estimates`: CSI-RS channel estimates (`ueCsirsChEstimate`)

### Why different windows have different “sizes”

FFT size is constant per OFDM symbol, but RS resources occupy only specific REs/PRBs:

- DM-RS only exists within allocated PDSCH/PUSCH resources
- CSI-RS can be sparse (density/comb) and can cover a configured BWP region

So the internal arrays exported to imscope/recording are often “packed” representations of the RS-related coefficients rather than a full `Nfft` grid for every symbol.

---

## 4) UL channel tracking: PUSCH DM-RS vs PUCCH DM-RS (conceptual)

For UL tracking at gNB:

- **PUSCH DM-RS** is typically richer (more PRBs, more REs → more frequency information) but only exists when PUSCH is scheduled.
- **PUCCH DM-RS** is typically narrowband (few PRBs), but may be present even when PUSCH is not scheduled.

PUCCH DM-RS “size” can change depending on PUCCH format/resource (symbols/PRBs/hopping).

---

## 5) Periodic CSI-RS: fastest valid periodicity in TDD (30 kHz SCS)

At 30 kHz SCS (\u03bc=1):

- 2 slots per 1 ms
- 2.5 ms TDD period ⇒ 5 slots per pattern
- 5 ms TDD period ⇒ 10 slots per pattern

This OAI repo enforces that the chosen CSI-RS periodicity must be compatible with the TDD pattern length (via `check_periodicity(...)`).

For a 2.5 ms TDD period (5 slots), the fastest standard periodicity that aligns is typically **5 slots**.
For a 5 ms TDD period (10 slots), the fastest is typically **10 slots**.

---

## 6) Configurable CSI-RS slot offset (Option B implemented)

### Motivation

The CSI-RS “slot offset” (RRC `periodicityAndOffset`) was originally derived from `id` (effectively `ssb_index/2`), with no config knob.

### New config key

You can now set in gNB config:

- `CSI_RS_slot_offset = -1` (auto/default behavior)
- or `CSI_RS_slot_offset = <0..period-1>`

If the configured offset is ≥ periodicity, modulo is applied with a warning.

### Code locations

- New field in MAC config:
  - `openair2/LAYER2/NR_MAC_gNB/nr_mac_gNB.h`: `int csi_rs_slot_offset;`
- Parse from config:
  - `openair2/GNB_APP/gnb_config.c`: reads `CSI_RS_slot_offset` under `gNBs.[0]`
- Apply to RRC CSI-RS resource:
  - `openair2/LAYER2/NR_MAC_gNB/nr_radio_config.c`: `set_csirs_periodicity(..., configured_slot_offset, ...)`

### Example

```c
do_CSIRS = 1;
CSI_RS_periodicity_slots = 5;  # for 2.5 ms pattern at 30 kHz
CSI_RS_slot_offset = 0;        # choose DL slot inside the pattern
```

---

## 7) PMI fine index `i2` flipping in static channels: what we observed

Observed on UE logs:

- RSRP, RI, i1, CQI, SINR appear stable
- `i2` flips between 0 and 1 when CSI-RS periodicity is very fast (e.g., 5 slots in 2.5 ms TDD)
- Increasing `CSI_RS_periodicity_slots` to 10, 20, ... made `i2` stabilize (e.g., to 1)

### Key code fact: no temporal averaging/smoothing

In `openair1/PHY/NR_UE_TRANSPORT/csi_rx.c`:

- RSRP is averaged **over REs within the CSI-RS occasion** (`rsrp_sum / meas_count`)
- RI/PMI/CQI are computed per occasion; **no time averaging** or hysteresis across occasions exists.

For rank-2, `i2` is decided via a strict comparison of candidate metrics (effectively a 1-bit decision), so it can flip near decision boundaries even if the underlying channel is static.

### Practical mitigation (without code changes)

- Use a larger `CSI_RS_periodicity_slots` (slower updates) if that is acceptable.
- Prefer using the recorded `H_*.bin` (or derived stable features) rather than `i2` alone as a sensing/ML signal.

---

## 8) Aperiodic CSI-RS status (high level)

The repo supports periodic NZP-CSI-RS configuration and contains ASN.1 structures for aperiodic trigger states, but a full end-to-end “aperiodic CSI-RS” feature (RRC config + MAC triggering + scheduling + PHY generation aligned to triggers) is not currently implemented as a turnkey capability.

---

## 9) Files touched in this work (non-exhaustive)

UE:

- `openair1/PHY/NR_UE_TRANSPORT/csi_rx.c` (timestamps, CSV columns, H_*.bin folder/name/header)

gNB:

- `openair2/LAYER2/NR_MAC_gNB/gNB_scheduler_uci.c` (timestamped CSI feedback CSV)
- `openair2/LAYER2/NR_MAC_gNB/gNB_scheduler_primitives.c` (timestamped CSI-RS scheduling CSV)
- `openair2/GNB_APP/gnb_config.c` (parse `CSI_RS_slot_offset`)
- `openair2/LAYER2/NR_MAC_gNB/nr_radio_config.c` (apply slot offset to `periodicityAndOffset`)
- `openair2/LAYER2/NR_MAC_gNB/nr_mac_gNB.h` (new config field)

Docs:

- `doc/CSI_RECORD_MODIFICATIONS.md`
- `targets/PROJECTS/GENERIC-NR-5GC/CONF/gnb.sa.band78.fr1.106PRB.usrpb210.conf`

