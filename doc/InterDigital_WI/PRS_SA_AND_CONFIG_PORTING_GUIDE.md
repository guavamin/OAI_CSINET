# PRS with Standalone (SA), Core Network, and Lab Config — Porting Guide

This document describes the **functional areas** touched in this fork so you can **reproduce the same behaviour on a freshly cloned** OpenAirInterface5G (OAI) tree. It ties together **NR PRS** (Rel‑16 positioning reference signal), **SA operation** (NGAP to AMF, full stack), **gNB/nrUE configuration**, and **optional instrumentation** (CSI recording, imscope, USRP GPIO).

Upstream PRS usage (parameters, `--phy-test`, multi‑gNB notes) remains documented in:

- `doc/RUN_NR_PRS.md`
- `openair2/COMMON/prs_nr_paramdef.h` (libconfig field names and help strings)

The **PHY transmit path** for PRS (`nr_generate_prs`, `prs_config` → `gNB->prs_vars`) is part of the standard OAI codebase; this fork mainly adds **SA/RAN/config robustness** and **measurement/diagnostic** hooks around it.

---

## 1. How to port to a new clone

### 1.1 Recommended approach (step-by-step)

You need **two working copies** of the tree:

| Role | Meaning |
|------|--------|
| **Reference repo** | Your fork with all modifications committed (or staged). This is where you **create** the patch. |
| **Target repo** | A **fresh** clone (usually clean `develop`) where you **apply** the patch. |

Replace paths below with yours; **`openairinterface5g`** is always the directory that contains `CMakeLists.txt` at the repo root.

---

#### Step A — Prepare the reference repo

1. **Open a terminal** and go to the reference repo root:

   ```bash
   cd /path/to/reference/openairinterface5g
   ```

2. **Update remote refs** so `origin/develop` matches the upstream you care about:

   ```bash
   git fetch origin
   ```

3. **Record the merge base** (the commit your branch diverged from `origin/develop`). Save it for reproducibility and for optional “apply on exact same base” workflows:

   ```bash
   mkdir -p doc
   git merge-base HEAD origin/develop | tee doc/oai-base-commit.txt
   ```

   - The file **`doc/oai-base-commit.txt`** should contain one **40-character** commit hash.
   - If you do **not** use `origin/develop`, substitute your upstream branch (e.g. `upstream/develop`) in both **`git fetch`** and **`git merge-base HEAD upstream/develop`**.

4. **Include untracked files in the patch** if you rely on them (50 MHz configs, this guide under `doc/InterDigital_WI/`, etc.). Plain **`git diff origin/develop`** only sees **tracked** changes.

   - Either **commit** them on your branch, or stage new files without committing:

     ```bash
     git add doc/InterDigital_WI/PRS_SA_AND_CONFIG_PORTING_GUIDE.md
     git add targets/PROJECTS/GENERIC-NR-5GC/CONF/gnb0.sa.band261.fr2.50mhz.usrpx310.conf
     # …add any other paths from §1.3…
     ```

   - Then use **`git diff --cached origin/develop`** when generating the patch (see Step B), **or** commit everything and use **`git diff origin/develop`** as below.

   Full list of easy-to-miss paths: **§1.3**.

---

#### Step B — Generate the patch (choose one)

**Option A — full delta (recommended first time; nothing omitted)**

From the **reference** repo root, with all wanted changes **committed** (simplest) or staged as above:

```bash
cd /path/to/reference/openairinterface5g
# If everything is committed:
git diff origin/develop > ~/prs_sa_fork_full.patch

# If you staged new files but did not commit:
# git diff --cached origin/develop > ~/prs_sa_fork_full.patch
```

Sanity check the file is non-empty and looks like a unified diff:

```bash
wc -l ~/prs_sa_fork_full.patch
head -n 20 ~/prs_sa_fork_full.patch
git apply --stat ~/prs_sa_fork_full.patch
```

**Option B — scoped paths** (smaller patch for review; keep this list in sync with **§3** when the fork grows)

```bash
cd /path/to/reference/openairinterface5g
git diff origin/develop -- \
  common/utils/nr/nr_common.c \
  doc/RUN_NR_PRS.md \
  doc/rach_processing_in_gNB.md \
  doc/InterDigital_WI/PRS_SA_AND_CONFIG_PORTING_GUIDE.md \
  executables/ \
  openair1/PHY/INIT/nr_parms.c \
  openair1/PHY/NR_TRANSPORT/nr_prach.c \
  openair1/PHY/NR_UE_TRANSPORT/csi_rx.c \
  openair1/PHY/TOOLS/ \
  openair1/PHY/defs_nr_UE.h \
  openair1/SCHED_NR/phy_procedures_nr_gNB.c \
  openair2/ENB_APP/enb_paramdef.h \
  openair2/GNB_APP/ \
  openair2/LAYER2/NR_MAC_gNB/ \
  openair2/LAYER2/NR_MAC_UE/config_ue.c \
  radio/COMMON/common_lib.h \
  radio/USRP/usrp_lib.cpp \
  targets/PROJECTS/GENERIC-NR-5GC/CONF/ \
  > ~/prs_sa_fork.patch
```

**Commit-based port:** if your fork has clean commits on top of `develop`, use **`git format-patch origin/develop -o /tmp/prs_patches`** and on the target **`git am /tmp/prs_patches/*.patch`** instead of **`git apply`**.

---

#### Step C — Create the target clone

1. **Choose the checkout** you will patch onto:

   - **Same base as the reference** (fewest surprises): checkout the hash stored in **`doc/oai-base-commit.txt`** from Step A.
   - **Current `develop`**: you may need to **resolve conflicts** manually if upstream moved.

2. **Clone** (example: sibling directory):

   ```bash
   cd /path/to/parent
   git clone <YOUR_OAI_UPSTREAM_URL> oai-target
   cd oai-target
   git fetch origin
   ```

   Use the same remote you diffed against in the reference repo (official OAI, a fork, or a mirror).

3. **Checkout the intended base** (example using the saved file from the reference repo):

   ```bash
   git checkout "$(cat /path/to/reference/openairinterface5g/doc/oai-base-commit.txt)"
   ```

   Or stay on **`origin/develop`**:

   ```bash
   git checkout origin/develop
   ```

---

#### Step D — Apply the patch on the target repo

1. Go to the **target** repo root:

   ```bash
   cd /path/to/parent/oai-target
   ```

2. **Dry run** (reports problems without writing files):

   ```bash
   git apply --check ~/prs_sa_fork_full.patch
   ```

   If this fails with “**does not apply**”, your target tree is not the same as the diff base: try checking out the hash in **`oai-base-commit.txt`**, or regenerate the patch from the reference repo against **`origin/develop`** that matches the target.

3. **Apply** for real:

   ```bash
   git apply ~/prs_sa_fork_full.patch
   ```

   Or for Option B:

   ```bash
   git apply ~/prs_sa_fork.patch
   ```

4. **Inspect** what changed:

   ```bash
   git status
   git diff --stat
   ```

5. **If you used `git am`**: commits appear directly; with **`git apply`**, changes are **uncommitted** until you **`git add`** and **`git commit`**.

---

#### Step E — Rebuild binaries

1. Follow **`doc/BUILD.md`** for your platform.

2. Build at least **`nr-softmodem`** and **`nr-uesoftmodem`** (typical **`cmake_targets`** / **`ran_build`** flow for your branch).

3. If the patch touches **`openair1/PHY/TOOLS/tools_defs.h`** or **`oai_dfts*.c`**, rebuild the **DFT** library (**`dfts`** / **`libdfts.so`**) so runtime **`get_idft()`** matches the rebuilt table (**§3.12**). Then rebuild the RAN executables so they link the new library.

4. Run a quick smoke command (paths depend on your build tree), e.g. **`nr-softmodem --help`** / **`nr-uesoftmodem --help`**, to confirm the binaries start.

### 1.2 Verify line counts (sanity check)

As of the last refresh of this section, against **`origin/develop`** the tree showed on the order of **37 files**, **~+1340 / −292** lines (`git diff --stat`). If your patch is far smaller, you may have omitted **CONF**, **DFT**, **PRACH**, or **`nr_common.c`**.

### 1.3 Untracked or relocated assets

These are **easy to miss** in a plain `git diff origin/develop` until they are committed:

| Item | Notes |
|------|--------|
| `doc/InterDigital_WI/PRS_SA_AND_CONFIG_PORTING_GUIDE.md` | This guide; copy or `git add` in the reference repo before exporting. |
| `doc/InterDigital_WI/*.md` | Other lab notes (CSI, imscope, etc.); optional for minimal PRS+SA port. |
| `doc/export-modifications.sh`, `doc/oai-base-commit.txt` | Optional helpers for recording base + diff scope. |
| `targets/PROJECTS/GENERIC-NR-5GC/CONF/gnb0.sa.band261.fr2.50mhz.usrpx310.conf` | 50 MHz FR2 gNB profile (§3.11). |
| `targets/PROJECTS/GENERIC-NR-5GC/CONF/ue.sa.band261.fr2.50mhz.conf`, `…/ue.nr.prs.fr2.50mhz.conf` | Matching nrUE / PRS fragments when present. |
| `targets/PROJECTS/GENERIC-NR-5GC/gnb.sa.band78.106prb.rfsim_csi_rs.conf` | rfsim CSI-RS lab profile if you maintain it locally. |

After adding them: **`git diff origin/develop --stat`** should reflect the full port surface.

---

## 2. Concept: PRS + SA (not `--phy-test` only)

- **`--phy-test`**: RF/PHY centric; does **not** match `IS_SA_MODE` in `executables/softmodem-common.h` (`IS_SA_MODE` is false when `phy_test`, `do_ra`, or `nsa` is set).
- **Full SA with AMF**: run **without** `--phy-test`, with **NG** enabled (default SA), **`amf_ip_address`** and **`NETWORK_INTERFACES`** in the gNB config, and a matching **PLMN + S‑NSSAI** configuration so **NGSetupRequest** encodes correctly.

PRS is configured via **`prs_config`** in the gNB libconfig; set **`NumPRSResources = 0`** to disable PRS transmission on the gNB (see `RUN_NR_PRS.md`).

---

## 3. File-by-file change catalog

Paths are relative to the repository root **`openairinterface5g/`**.

### 3.1 NGAP / PLMN / S‑NSSAI (SA + core, mandatory for AMF)

| Path | Change summary |
|------|------------------|
| `openair2/GNB_APP/gnb_config_ng.c` | **`set_plmn_config(p, k)`** instead of **`set_plmn_config(p, 0)`** so each gNB entry in `gNBs.[]` uses its own `plmn_list`. **`AssertFatal(num_nssai >= 1, ...)`** after `set_snssai_config`: NGAP **Supported TA → Broadcast PLMN → TAISliceSupportList** must be non-empty (`SIZE(1..1024)`). Without **`snssaiList`** under **`plmn_list`**, PER encoding of **NGSetupRequest** fails (`ngap_gNB_encoder.c`: `failed to encode NGAP msg`). |

**Config requirement (gNB libconfig):**

```libconfig
plmn_list = ({ mcc = …; mnc = …; mnc_length = 2 or 3; snssaiList = ({ sst = 1; }) });
```

Align **`sst` / `sd`** with your AMF/NSSF. Example pattern: `targets/PROJECTS/GENERIC-NR-5GC/CONF/gnb.sa.band78.fr1.106PRB.usrpb210.conf` and other `gnb.sa.*.conf` files in the same tree.

---

### 3.2 SA UE carrier frequency (avoid `get_freq_range_from_freq(0)`)

| Path | Change summary |
|------|------------------|
| `openair1/PHY/INIT/nr_parms.c` | In **`nr_init_frame_parms_ue_sa()`**, **`AssertFatal(downlink_frequency > 0, ...)`** before **`get_freq_range_from_freq()`**. SA UE must set **`-C <DL_Hz>`** or **`cells[].rf_freq`** in libconfig (see `executables/nr-ue-ru.c`, `CONFIG_STRING_NRUE_CELL_LIST`). |

**Reference fragment for FR2 + 66 PRB + band 261 (merge into UE config or use as template):**

- `targets/PROJECTS/GENERIC-NR-5GC/CONF/ue.sa.band261.fr2.66prb.gnb0.cells_fragment.conf`

---

### 3.3 gNB MAC / RRC radio config — CSI‑RS / SRS periodicity (configurable)

| Path | Change summary |
|------|------------------|
| `openair2/GNB_APP/gnb_paramdef.h` | New gNB libconfig keys: **`CSI_RS_periodicity_slots`**, **`SRS_periodicity_slots`** (+ help strings). Index macros extended; **`GNBParamCheck`** gains matching **`{ .s5 = { NULL } }`** entries. |
| `openair2/GNB_APP/gnb_config.c` | Reads **`CSI_RS_periodicity_slots`**, **`SRS_periodicity_slots`**, and optional **`CSI_RS_slot_offset`** (via small inline **`config_get`**), fills **`nr_mac_config_t`**. |
| `openair2/LAYER2/NR_MAC_gNB/nr_mac_gNB.h` | Struct **`nr_mac_config_s`**: **`csi_rs_periodicity_slots`**, **`csi_rs_slot_offset`**, **`srs_periodicity_slots`**. |
| `openair2/LAYER2/NR_MAC_gNB/nr_radio_config.c` | **`set_csirs_periodicity` / `config_csirs`**: map requested periodicity to **allowed 3GPP values**; apply **`CSI_RS_slot_offset`** (with modulo if ≥ period). **`configure_periodic_srs`**: similar mapping for **SRS** when **`SRS_periodicity_slots > 0`**. |

**gNB libconfig example (inside each `gNBs.[]` block):**

```libconfig
CSI_RS_periodicity_slots = 160;  # 0 = auto (legacy)
CSI_RS_slot_offset = 0;          # -1 = auto; else 0 .. period-1
SRS_periodicity_slots = 16;      # 0 = auto
```

---

### 3.4 gNB MAC — CSI CSV recording + imscope CSI feed

| Path | Change summary |
|------|------------------|
| `executables/softmodem-common.h` | New **`softmodem_params_t`** fields: **`csi_record_path`**, **`imscope_windows`**. CMDLINE: **`--csi-record-path`**, **`--imscope-windows`**. **`CMDLINE_PARAMS_CHECK_DESC`**: two extra **`{ .s5 = { NULL } }`** entries (must stay aligned with **`CMDLINE_PARAMS_DESC`**). |
| `executables/softmodem-common.c` | **`get_imscope_windows_filter()`**. **Bugfix**: **`config_paramidx_fromname(..., numlogparams, ...)`** (was **`numparams`**) for **`-O`** / **`-d`** log options. |
| `openair2/LAYER2/NR_MAC_gNB/gNB_scheduler_primitives.c` | When **`csi_record_path`** is set, append **`gnb_csirs_scheduling.csv`** (CSI‑RS scheduling metadata + UTC µs timestamp). |
| `openair2/LAYER2/NR_MAC_gNB/gNB_scheduler_uci.c` | When **`csi_record_path`** set, append **`gnb_csi_feedback.csv`** (decoded CSI). When **`--imscope`** and CSI report available, **`gNBscopeCopyWithMetadata(..., gNBCsiReportParams, ...)`** with **`csi_report_scope_payload_t`**. |
| `openair1/PHY/TOOLS/phy_scope_interface.h` | New scope types **`gNBCsiReportParams`**, **`gNBSrsChEstimate`**; struct **`csi_report_scope_payload_t`**; **`scopeData_t.imscope_windows`**; **`get_imscope_windows_filter()`** declaration. |
| `openair1/PHY/TOOLS/imscope/imscope.cpp` | UI for new gNB windows; optional **window title filter** via **`get_imscope_windows_filter()`**. |
| `openair1/PHY/TOOLS/imscope/imscope_common.cpp` | Names for new scope types. |
| `openair1/PHY/TOOLS/imscope/imscope_internal.h` | Internal declarations as needed. |
| `openair1/PHY/TOOLS/imscope/imscope_init.cpp` | Init hook if any. |

**CLI examples:**

```bash
./nr-softmodem ... --imscope --imscope-windows "CSI report parameters,SRS channel estimates"
./nr-softmodem ... --csi-record-path ./gnb_csi_data
```

---

### 3.5 gNB PHY — SRS → imscope + diagnostics

| Path | Change summary |
|------|------------------|
| `openair1/SCHED_NR/phy_procedures_nr_gNB.c` | After SRS channel estimation: optional **`gNBscopeCopyWithMetadata(..., gNBSrsChEstimate, ...)`**; one-time warning if **`max_nb_srs == 0`**; log wording tweak. |

---

### 3.6 UE — CSI recording path + PHY CSI‑RS scope

| Path | Change summary |
|------|------------------|
| `executables/nr-uesoftmodem.h` | **`nrUE_params_t.csi_record_path`**; **`--csi-record-path`** in UE CMDLINE table. |
| `executables/nr-uesoftmodem.c` | **`UE_CC->csi_record_path`** from UE-specific **`--csi-record-path`**, else fallback to common **`get_softmodem_params()->csi_record_path`**. |
| `openair1/PHY/defs_nr_UE.h` | UE PRS-related / measurement fields as in fork (e.g. **`NR_UE_PRS`**, **`prs_active_gNBs`**) — align with **`ue.nr.prs.*.conf`** multi‑gNB PRS. |
| `openair1/PHY/NR_UE_TRANSPORT/csi_rx.c` | Extended CSI‑RS receive / recording hooks (UE-side `.bin` / CSV behaviour when recording path set — see fork diff for exact format). |

---

### 3.7 UE MAC — CellGroupConfig robustness

| Path | Change summary |
|------|------------------|
| `openair2/LAYER2/NR_MAC_UE/config_ue.c` | **`AssertFatal(mac != NULL, ...)`** in **`nr_rrc_mac_config_req_cg`**. TAG loop: guard **`TAG_list.array`** and **`current_UL_BWP != NULL`** before **`configure_timeAlignmentTimer`**. |

---

### 3.8 USRP / RU — TDD front-end GPIO (mmWave / external TR switch)

| Path | Change summary |
|------|------------------|
| `radio/COMMON/common_lib.h` | Enum **`RU_GPIO_CONTROL_TDD_FRONTEND`**; **`openair0_config_t.gpio_tdd_advance_sec`** (device args, e.g. **`gpio_tdd_advance_us`**). |
| `radio/USRP/usrp_lib.cpp` | **`trx_usrp_start_tdd_frontend_gpio`**: single GPIO **low = RX, high = TX**; optional timed advance vs ATR. |
| `executables/nr-ru.c` | RU config parses **`gpio_controller = "tdd_frontend"`** / **`"none"`**; switch cases avoid assert for these modes. |
| `executables/nr-ue-ru.c` | UE **`RUs`** section: **`gpio_controller`** string → same enum; pass through **`openair0_cfg`**. |
| `openair2/ENB_APP/enb_paramdef.h` | Help text lists **`none`**, **`generic`**, **`interdigital`**, **`tdd_frontend`**. |

---

### 3.9 Target configuration files

| Path | Notes |
|------|--------|
| `targets/PROJECTS/GENERIC-NR-5GC/CONF/gnb0.prs.band261.fr2.64PRB.usrpx310.conf` | **FR2 n261 SA + PRS**, **120 kHz**, **66 PRB**. The path segment **`64PRB` is historical**; the file header comment states this. **Aligned fields:** `dl_carrierBandwidth` / `ul_carrierBandwidth` **66**; **`prs_config.NumRB` = 66**; **`RUs.bands = [261]`** (matches `dl_frequencyBand` / `ul_frequencyBand`, not FR1 band 7); **`if_freq`** comment points to external mmWave IF (`doc/RUN_NR_PRS.md`). **RAN:** FR2 **SSB / Point A** comments, **`initialDLBWPcontrolResourceSetZero`** 0–7 (TS 38.213 Table **13‑8**), **`ssb_PositionsInBurst_Bitmap`** so **SIB1** slots are **DL** for the TDD pattern (§4). **`plmn_list` + `snssaiList`** for NGAP. **CSI/SRS** periodicity keys; CSI‑RS comment text uses **120 kHz** slot timing. **RU:** `sdr_addrs`, optional **`gpio_controller`**. |
| `targets/PROJECTS/GENERIC-NR-5GC/CONF/gnb1.prs.band78.fr1.106PRB.usrpx310.conf` | Minor edits in fork (e.g. alignment with PRS/CSI patterns). |
| `targets/PROJECTS/GENERIC-NR-5GC/CONF/ue.nr.prs.fr2.64prb.conf` | UE **PRS** reception: **`prs_config0` / `prs_config1`** with **`NumRB = 66`** (matches gNB carrier). Filename **`64prb`** is historical (comment in file). **`Active_gNBs`**, per‑gNB **`prs_configN`**. |
| `targets/PROJECTS/GENERIC-NR-5GC/CONF/ue.sa.band261.fr2.66prb.gnb0.cells_fragment.conf` | Optional **`cells`** libconfig fragment: **`rf_freq`**, **`numerology = 3`**, **`N_RB_DL = 66`**, **`ssb_start`** aligned with gNB ARFCNs. |
| `targets/PROJECTS/GENERIC-NR-5GC/gnb.sa.band78.106prb.rfsim_csi_rs.conf` | SA gNB profile tweaks (CSI‑RS related) in fork. |

### 3.10 FR2 reference config clean-up (what was aligned)

When porting or reviewing diffs, these **inconsistencies were removed** in the reference FR2 PRS profiles above:

- **`RUs.bands`**: was **`[7]`** (FR1) while RRC used **n261** → set to **`[261]`**.
- **PRS `NumRB`**: was **64** while the cell uses **66 PRB** (64 is not a valid FR2 120 kHz carrier size in OAI’s supported set) → **gNB `prs_config`** and **UE `ue.nr.prs.fr2.64prb.conf`** both use **`NumRB = 66`**.
- **Carrier comments**: stray **`#64`** after `dl_carrierBandwidth` / `ul_carrierBandwidth` removed.
- **gNB identification block**: duplicate / malformed comment lines around **TAC / PLMN** removed; single **`plmn_list`** with **`snssaiList`** kept.
- **CSI‑RS help comment**: wording fixed for **120 kHz** (not 30 kHz).

Paths were **not renamed** (`64PRB` / `64prb` in filenames) so existing scripts and `RUN_NR_PRS.md` references keep working; the **file header comments** document the naming mismatch.

### 3.11 FR2 SA — 50 MHz channel bandwidth (32 PRB @ 120 kHz)

**3GPP mapping:** TS **38.104** Table **5.3.2-2** (FR2-1, 120 kHz SCS): **50 MHz** channel bandwidth ↔ **N_RB = 32** (transmission bandwidth **32 × 12 × 120 kHz = 46.08 MHz** inside the 50 MHz channel).

**OAI support:** **`tables_5_3_2`** already listed **32** for 120 kHz FR2; **`get_samplerate_and_bw()`** (`common/utils/nr/nr_common.c`) already defines **61.44 Msps** (full sampling) or **92.16 Msps** (`-E`, 3/4 sampling) and **50 MHz** `tx_bw` / `rx_bw` for **μ = 3**, **32 PRB**. **No new PRB-table entries were required.**

**New config files (do not replace the 66 PRB / `64PRB` profiles):**

| Path | Role |
|------|------|
| `targets/PROJECTS/GENERIC-NR-5GC/CONF/gnb0.sa.band261.fr2.50mhz.usrpx310.conf` | gNB **SA** on **n261**, **120 kHz**, **32 PRB** DL/UL, **`initialDLBWPlocationAndBandwidth` / `initialULBWPlocationAndBandwidth` = 8525** (full 32‑PRB BWP on grid 275). **`Active_gNBs` / `gNB_name` = `gNB-SA-n261-50MHz`** so it does not collide with **`gNB-Eurecom-5GNRBox`**. Same SSB/Point A/TDD/NG patterns as the reference FR2 file; **`prs_config.NumRB` = 32**. |
| `targets/PROJECTS/GENERIC-NR-5GC/CONF/ue.sa.band261.fr2.50mhz.conf` | nrUE **`cells`** block: **`N_RB_DL = 32`**, **`numerology = 3`**, **`band = 261`**, **`ssb_start = 84`** (same ARFCN pairing as gNB). **`rf_freq`** must match the gNB **`-C`** centre (may differ from the 66 PRB case). |
| `targets/PROJECTS/GENERIC-NR-5GC/CONF/ue.nr.prs.fr2.50mhz.conf` | Optional **PRS** reception fragment: **`NumRB = 32`**, **`gNB_id = 0`**. |

**Code comment:** `nr_common.c` annotates the **120 FR2** row of **`tables_5_3_2`** with the **38.104** channel‑BW ↔ **N_RB** mapping (documentation only).

### 3.12 DFT / iDFT dispatch, PRACH RU path, and `nr_common` (50 MHz FR2 support)

| Path | Change summary |
|------|----------------|
| `openair1/PHY/TOOLS/tools_defs.h` | **`FOREACH_IDFTSZ`**: include **`SZ_DEF(384)`** so **`get_idft(384)`** is valid. Needed for **32 PRB @ 120 kHz** with **3/4 sampling** (`-E`, `threequarter_fs`): FFT size **384** appears on the iDFT dispatch path. |
| `openair1/PHY/TOOLS/oai_dfts.c`, `openair1/PHY/TOOLS/oai_dfts_neon.c` | Keep iDFT size checks consistent with the iDFT table (e.g. **`IDFT_SIZE_IDXTABLESIZE`** where the code indexes iDFT factories). |
| `openair1/PHY/NR_TRANSPORT/nr_prach.c` | Fork-specific **PRACH RU RX** handling / guards for **FR2 50 MHz** + rfsim; still associated with the **Known issue** below until fully stable. |
| `common/utils/nr/nr_common.c` | **`get_samplerate_and_bw()`** / **38.104** ↔ **N_RB** documentation for **FR2 120 kHz** (incl. **32 PRB / 50 MHz** row). |

**Rebuild reminder:** after changing **`tools_defs.h`** or **`oai_dfts*.c`**, rebuild the **DFT library** (`dfts` / `libdfts.so`) and link **nr-softmodem** / **nr-uesoftmodem** against the updated build so runtime **`get_idft()`** matches sources.

**Log note (nrUE / PRS ToA):** `LOG_I` lines such as **“DL PRS ToA”** with **peak −inf dBm** and absurd **SNR** usually mean **no peak above the estimator threshold** in `nr_prs_channel_estimation()` / `peak_estimator()`; **SNR** there uses **`log10`** on terms that are ill-conditioned on noise. Treat those fields as **invalid** unless peak power and ToA are stable across slots. (Upstream `openair1/PHY/NR_UE_ESTIMATION/nr_dl_channel_estimation.c`.)

---

## 4. FR2 SA checklist (gNB config semantics)

When bringing **PRS + SA** on **band 261 / 120 kHz**, verify:

1. **Carrier bandwidth**: FR2 @ 120 kHz SCS — OAI **`tables_5_3_2`** allows **32 / 66 / 132 / 264** PRB (**32 = 50 MHz**, **66 = 100 MHz** channel per TS 38.104 Table 5.3.2-2). **`initialDLBWPlocationAndBandwidth` / `initialULBWPlocationAndBandwidth`** (RIV) must match **`dl_carrierBandwidth` / `ul_carrierBandwidth`** (e.g. **32 PRB → 8525** on max grid **275** via `PRBalloc_to_locationandbandwidth0(32,0,275)`). Arbitrary counts such as **64** PRB are **not** in the table for 120 kHz FR2.
2. **SSB raster**: **`absoluteFrequencySSB`** on the **GSCN** step for the band (see comments in `gnb0.prs.band261…conf`).
3. **CORESET0 index**: For **120 kHz SSB + 120 kHz PDCCH**, **`initialDLBWPcontrolResourceSetZero`** must be **0–7** (Table 13‑8).
4. **SIB1 slot vs TDD**: With **`initialDLBWPsearchSpaceZero`** and **mux pattern** from Table 13‑12, **type0 PDCCH slot** is derived from **SSB index**. **TDD** slot allocation must leave that slot **DL-capable** (`is_dl_slot`). If not, change **SSB bitmap**, **searchSpaceZero**, or **TDD pattern** (see earlier analysis: e.g. avoid SSB indices whose mapped slot falls on full **UL** slots in the period).
5. **NGAP**: **`snssaiList`** under every **`plmn_list`** entry used for broadcast (§3.1).

---

## Known issue: 50 MHz FR2 SA PRACH segfault (gNB / rfsim)

During testing of **FR2 SA 50 MHz (mu=3, 32 PRB)** with **3/4 sampling enabled** (`threequarter_fs=1`, typical when using `-E`), the PRS-related `get_idft() : unsupported iDFT size 384` assertion was resolved by enabling the `384` iDFT in the DFT dispatch table.

However, a runtime **segmentation fault** can still occur when running the **gNB RU PRACH RX** path with `--rfsim` while no UE is connected (RFSIM generates “void samples”).

Observed crash location:

- `openair1/PHY/NR_TRANSPORT/nr_prach.c`
- `rx_nr_prach_ru_internal()` (around the PRACH repetition combining loop)

Impact:

- This prevents completing a full “PRS + SA” run under the tested PRACH parameters for this 50 MHz profile.

Recommendation:

- Use PRS validation runs where PRACH processing is avoided (e.g. isolate `--phy-test` flows or ensure RA is not triggered), until PRACH window/buffer handling is fully corrected for this operating point.
- If you hit the fault again, always collect a `gdb` backtrace (`bt`) and the exact `nr-softmodem` command line; that will let us adjust PRACH buffer/window computation without regressing FR1/FR2 working points.

## 5. Typical run commands (illustrative)

**gNB — SA + AMF + USRP (no phy-test):**

```bash
sudo ./nr-softmodem -E -O targets/PROJECTS/GENERIC-NR-5GC/CONF/gnb0.prs.band261.fr2.64PRB.usrpx310.conf \
  --csi-record-path ./gnb_csi_data --imscope
```

**nrUE — SA (must pass carrier for RF / rfsim):**

Merge or include **`CONF/ue.nr.prs.fr2.64prb.conf`** PRS settings with your UE libconfig if you use multi‑gNB PRS (see `RUN_NR_PRS.md`).

```bash
./nr-uesoftmodem -r 66 --numerology 3 -C <DL_center_Hz> --ssb <ssb_start_sc> \
  -O targets/PROJECTS/GENERIC-NR-5GC/ue.conf \
  --csi-record-path ./ue_csi_data
```

Use the same **`-C`** as the gNB RF centre when not using **`cells[].rf_freq`**. **`--ssb`** must match the cell’s SSB subcarrier offset (for the example **Point A / SSB ARFCN** pairing in `gnb0…`, **`ssb_start` = 84** in the fragment file).

**gNB / nrUE — 50 MHz FR2 SA (32 PRB, lower IQ rate than 66 PRB):**

```bash
# gNB (adjust -C / amf_ip / sdr_addrs to your lab; -E → 92.16 Msps, 50 MHz RF filter in OAI)
sudo ./nr-softmodem -E -O targets/PROJECTS/GENERIC-NR-5GC/CONF/gnb0.sa.band261.fr2.50mhz.usrpx310.conf -C <DL_center_Hz>

# rfsimulator / no RF
sudo ./nr-softmodem -O targets/PROJECTS/GENERIC-NR-5GC/CONF/gnb0.sa.band261.fr2.50mhz.usrpx310.conf --noS1 --rfsim -C <DL_center_Hz>

# nrUE: -r 32 matches 32 PRB; -C must match gNB; merge ue.nr.prs.fr2.50mhz.conf if using PRS
./nr-uesoftmodem -r 32 --numerology 3 -C <DL_center_Hz> --ssb 84 \
  -O targets/PROJECTS/GENERIC-NR-5GC/CONF/ue.sa.band261.fr2.50mhz.conf
```

---

## 6. Related documentation in this tree

| Document | Topic |
|----------|--------|
| `doc/RUN_NR_PRS.md` | Upstream PRS modes, `--phy-test`, multi‑gNB, **`prs_config`** |
| `doc/InterDigital_WI/SOFTMODEM_CMDLINE_PARAMS.md` | **CMDLINE_PARAMS_DESC** / **CHECK** alignment when adding options (InterDigital copy; may also exist as `doc/SOFTMODEM_CMDLINE_PARAMS.md` on some branches) |
| `doc/InterDigital_WI/5G_STACK_ARCHITECTURE_AND_DATA_FLOW.md` | Stack overview (InterDigital lab note) |
| `doc/InterDigital_WI/CSI_RECORD_MODIFICATIONS.md` | CSI recording / UE hooks (optional instrumentation) |

---

## 7. Maintenance notes

- Any new **`CMDLINE_PARAMS_DESC`** entry in **`softmodem-common.h`** requires a matching **`CMDLINE_PARAMS_CHECK_DESC`** line in the **same order** (see comment block in that file).
- Any new **`GNBPARAMS_DESC`** field needs **`GNBParamCheck`** and correct **`GNB_*_IDX`** indices in **`gnb_paramdef.h`**.
- After structural config changes, run a full **NGSetup** against your AMF once to validate **PLMN/S‑NSSAI/TAC** consistency.

---

*This guide lives at **`doc/InterDigital_WI/PRS_SA_AND_CONFIG_PORTING_GUIDE.md`**. It is descriptive of the fork’s intent and file touch points; always prefer **`git diff origin/develop`** (or your recorded base in **`doc/oai-base-commit.txt`**) as the source of truth when porting, and refresh **§1** when the diff footprint changes.*
