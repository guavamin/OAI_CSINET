# nrgNB custom stack: change log, replication, and staying aligned with OAI `develop`

This document summarizes **significant modifications** in this tree relative to upstream **OpenAirInterface5G** `develop`, how **teammates can reproduce** the same codebase from a fresh OAI clone, and **recommended practices** when upstream `develop` moves (bi-weekly or faster).

> **Note:** The exact file list evolves as you edit. Regenerate the patch and file list periodically (see [§3](#3-replicating-changes-on-a-fresh-clone)).

---

## 1. Recorded upstream base (snapshot)

When this document was last aligned with git, **`HEAD` matched `origin/develop`** at:

| Field | Value |
|--------|--------|
| **Remote** | `https://gitlab.eurecom.fr/oai/openairinterface5g.git` |
| **Branch** | `develop` |
| **Commit** | `582ce818d7` (message: *Merge branch 'integration_2026_w10' into 'develop'*) |

Your **local modifications** at that point were **uncommitted** changes on top of that commit (plus optional untracked `doc/` files and configs). **Commit or tag** your tree after important milestones so the base is explicit.

**Recommended:** After you commit your work on a dedicated branch:

```bash
git tag -a oai-upstream-base-2026-03-21 -m "OAI develop before nrgNB patches" 582ce818d7   # example
git tag -a nrgNB-v0.1 -m "nrgNB stack tested" HEAD
```

---

## 2. Significant changes (by area)

The following maps **themes** to **main files**. Deeper behaviour is documented in the linked `doc/*.md` files.

### 2.1 imscope (UE & gNB)

| Topic | Summary | Key files |
|-------|---------|-----------|
| **Window filter** | `--imscope-windows "Title1,Title2,..."` shows only selected panels; filtered dock/tab layout at startup. | `executables/softmodem-common.{c,h}`, `openair1/PHY/TOOLS/phy_scope_interface.h`, `openair1/PHY/TOOLS/imscope/imscope_{init,common}.cpp`, `imscope.cpp` |
| **CSI-RS scope types** | Extra scope channels for CSI-RS estimates and CSI report parameters at gNB. | `phy_scope_interface.h`, `imscope.cpp`, UE PHY (`csi_rx.c`, `defs_nr_UE.h`) |
| **SRS at gNB** | `gNBSrsChEstimate` feed from SRS RX; gNB windows for flattened SRS IQ and **per RX–port link** RMS plot. | `openair1/SCHED_NR/phy_procedures_nr_gNB.c`, `imscope.cpp` |
| **Help text** | New window titles must be listed in `CONFIG_HLP_IMSCOPE_WINDOWS` for `-h`. | `softmodem-common.h` |

**See also:** `doc/IMSCOPE_WINDOWS_FILTER.md`, `doc/IMSCOPE_CSI_MODIFICATIONS.md`, `doc/IMSCOPE_DEVELOPER_GUIDE.md`, `doc/SRS_PERIODICITY_AND_IMSCOPE_MODIFICATIONS.md`

**gNB imscope window titles** (for `--imscope-windows`) include at least:

- `PUSCH SLOT IQ`, `PUSCH LLRs`, `Time domain samples`, `SRS channel estimates`, `SRS channel estimates (per RX-port link)`, `CSI report parameters`

*(Update `IMSCOPE_WINDOWS_FILTER.md` if you add titles.)*

---

### 2.2 CSI recording & reporting (UE + gNB)

| Topic | Summary | Key files |
|-------|---------|-----------|
| **Paths / CLI** | `csi_record_path` (and related) for writing CSV/binary traces. | `softmodem-common.*`, `nr-uesoftmodem.*`, MAC/scheduler hooks |
| **UE CSI-RS** | Channel estimate export and report labelling in PHY CSI-RS processing. | `openair1/PHY/NR_UE_TRANSPORT/csi_rx.c`, `config_ue.c`, `defs_nr_UE.h` |
| **gNB UCI / scheduling** | Record decoded CSI and CSI-RS scheduling to CSV. | `gNB_scheduler_uci.c`, `gNB_scheduler_primitives.c`, `nr_mac_gNB.h` |

**See also:** `doc/CSI_RECORD_MODIFICATIONS.md`, `doc/RSRP_RECORDING_AND_NEAR_RIC.md`, `doc/SAVING_AND_TRANSFERRING_MODIFICATIONS.md`

---

### 2.3 SRS configuration (gNB)

| Topic | Summary | Key files |
|-------|---------|-----------|
| **`SRS_periodicity_slots`** | Configurable minimum SRS period (standard 3GPP periods + TDD constraints). | `gnb_paramdef.h`, `gnb_config.c`, `nr_radio_config.c` |

**See also:** `doc/SRS_PERIODICITY_AND_IMSCOPE_MODIFICATIONS.md`

---

### 2.4 Radio / USRP (optional hardware paths)

| Topic | Summary | Key files |
|-------|---------|-----------|
| **USRP / common** | Extensions (e.g. calibration skip, GPIO/TDD notes in docs). | `radio/USRP/usrp_lib.cpp`, `radio/COMMON/common_lib.h` |

**See also:** `doc/USRP_CALIBRATION_SKIP_RU.md`, `doc/USRP_GPIO_TDD_FRONTEND.md`

---

### 2.5 Executables / RU wiring

| Topic | Summary | Key files |
|-------|---------|-----------|
| **nr-ue / RU** | Small hooks for scope params, recording, or RU behaviour. | `executables/nr-ru.c`, `nr-ue-ru.c`, `nr-uesoftmodem.c`, `enb_paramdef.h` (if touched) |

---

### 2.6 Example configs (project-specific)

| File | Role |
|------|------|
| `targets/PROJECTS/GENERIC-NR-5GC/CONF/gnb.sa.band78.fr1.106PRB.usrpb210.conf` | Example gNB USRP config (CSI-RS / SRS related params). |
| `targets/PROJECTS/GENERIC-NR-5GC/gnb.sa.band78.106prb.rfsim_csi_rs.conf` | rfsim / CSI-RS oriented example (if present). |
| `targets/PROJECTS/GENERIC-NR-5GC/ue.conf` | Example UE config (if tracked). |

---

### 2.7 Documentation (recommended to ship with the stack)

Untracked or local `doc/*.md` files describe architecture, imscope, CSI, SRS, ptrs, softmodem flags, etc. **Copy the whole `doc/` subset your team needs** (or commit it on your branch) so onboarding does not depend on tribal knowledge.

---

## 3. Replicating changes on a fresh clone

Use **one** of these strategies. **Preferred for teams:** dedicated branch on a shared remote (§3.1).

### 3.1 Preferred: feature branch + remote (lowest friction)

1. Maintainer keeps all work on e.g. `nrgNB/develop` or `research/nrgNB-stack`.
2. Push to **your** GitLab/GitHub fork (or internal remote).
3. Teammates:

```bash
git clone https://gitlab.eurecom.fr/oai/openairinterface5g.git
cd openairinterface5g
git remote add nrgNB https://YOUR_SERVER/YOUR_GROUP/openairinterface5g.git   # or fork URL
git fetch nrgNB
git checkout -b nrgNB-stack nrgNB/nrgNB-develop    # example branch name
```

No patch application required; **merge upstream `develop` when you choose** (see [§4](#4-keeping-compatible-with-upstream-develop)).

---

### 3.2 Single patch file (good for “drop-in” without a fork)

**On the machine that has the modifications** (from repo root):

```bash
# Record exact base (the commit you want others to start from — must match patch)
git rev-parse HEAD > doc/oai-base-commit.txt

# Uncommitted changes only (typical while you work on develop):
git diff > nrgNB-local-changes.patch

# If you also have staged changes:
# git diff HEAD > nrgNB-local-changes.patch   # includes staged+unstaged vs HEAD
```

**On a fresh clone at the same base commit:**

```bash
git clone https://gitlab.eurecom.fr/oai/openairinterface5g.git
cd openairinterface5g
git checkout 582ce818d7    # MUST match the commit used when creating the patch

git apply --check nrgNB-local-changes.patch
git apply nrgNB-local-changes.patch

# Add untracked docs/configs manually or from a tarball:
# tar xf nrgNB-doc-configs.tar.gz

git checkout -b nrgNB-stack
git add -A
git commit -m "Apply nrgNB local modifications on top of OAI develop"
```

**Limitation:** If the clone is **not** exactly the same base commit, `git apply` may fail or apply incorrectly. Rebase or merge upstream first, then regenerate the patch from your branch.

---

### 3.3 Committed deltas: `format-patch` (best history)

After you commit on a branch:

```bash
git format-patch -o ../patches oai-upstream-base-2026-03-21..HEAD   # use your base tag/commit
```

Apply in order:

```bash
git am ../patches/*.patch
```

---

### 3.4 Helper script in this repo

`doc/export-modifications.sh` can export:

- **Committed range** (default): `BASE..HEAD` → `full-diff.patch`
- **Working tree** (uncommitted vs `HEAD` or another ref): `--worktree` → `working-tree.patch`

```bash
./doc/export-modifications.sh --help

# Uncommitted changes vs current HEAD (default output dir):
./doc/export-modifications.sh --worktree

# Same, custom output directory (single arg = outdir when it is not a git ref):
./doc/export-modifications.sh --worktree ../oai-modifications-export

# Explicit base ref and output dir:
./doc/export-modifications.sh --worktree HEAD ../oai-modifications-export

# Committed diff since single commit (original behaviour; default BASE is HEAD~1):
./doc/export-modifications.sh oai-base-tag ../patches-out
```

---

## 4. Keeping compatible with upstream `develop`

Upstream OAI updates `develop` frequently. Use a **regular integration rhythm** so conflicts stay small.

### 4.1 Branch workflow

| Practice | Why |
|----------|-----|
| Keep patches on **`nrgNB/develop`** (or similar), never commit OAI core edits directly on a throwaway clone only | Reproducible branch for the team |
| **Merge or rebase** `origin/develop` into your branch **often** (e.g. weekly) | Fewer massive conflicts |
| After each merge: **build + smoke test** (gNB/UE attach, your imscope/CSI paths) | Catch API renames early |
| **Tag** “known good” pairs: `nrgNB-works-with-oai-<date>` pointing at your branch commit, and note OAI `develop` SHA in the tag message | Rollback and bisect |

### 4.2 When conflicts happen

1. **Identify overlapping files** — usually `imscope.cpp`, MAC schedulers, `phy_procedures_nr_gNB.c`, `softmodem-common.*`.
2. **Prefer preserving upstream fixes** — take OAI’s version first, then re-apply your hunks manually if needed.
3. **Isolate new logic** where possible — extra functions in new files or `#ifdef` only if OAI policy allows; otherwise small wrapper functions in one place.
4. **Re-run** your doc checklist (`doc/SAVING_AND_TRANSFERRING_MODIFICATIONS.md`, feature-specific docs).

### 4.3 Long-term reproducibility

| Approach | Use when |
|----------|----------|
| **Fork + branch** | Team collaboration; CI |
| **Patch + pinned base** | Paper reproducibility, exact bit-for-bit comparison |
| **Submodule / vendor branch** | Multiple projects consume the same patch set |

---

## 5. Quick checklist for maintainers

- [ ] All changes **committed** on `nrgNB-*` branch with clear messages.
- [ ] Tag **OAI base** and **your release** tags.
- [ ] Export patch or push branch; update **`doc/oai-base-commit.txt`** if you use patches.
- [ ] Run **build** and **minimal runtime** test after merging `origin/develop`.
- [ ] Refresh this doc’s **§2** if major new areas appear (grep `git diff origin/develop...HEAD`).

---

## 6. Related documents

| Document | Content |
|----------|---------|
| `doc/SAVING_AND_TRANSFERRING_MODIFICATIONS.md` | Patches, branches, what to transfer |
| `doc/IMSCOPE_WINDOWS_FILTER.md` | `--imscope-windows` |
| `doc/SRS_PERIODICITY_AND_IMSCOPE_MODIFICATIONS.md` | SRS period + gNB SRS imscope |
| `doc/CSI_RECORD_MODIFICATIONS.md` | CSI recording pipeline |
| `doc/SOFTMODEM_CMDLINE_PARAMS.md` | CLI reference (if maintained) |

---

*Generated for the nrgNB / OAI custom stack. Update §1 base commit and §2 when you merge upstream or add features.*
