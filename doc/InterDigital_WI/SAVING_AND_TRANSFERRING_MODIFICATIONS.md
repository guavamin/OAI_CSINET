# Saving and Transferring Repository Modifications

This guide recommends how to **save** the modifications made to this OAI 5G repository (CSI-RS imscope/recording, architecture docs, CSI-RS periodicity, etc.) and **transfer** them to a new repository or another machine.

---

## 1. Recommended strategy: Git branch + patches

### 1.1 Keep all changes on a single branch

1. **Create a dedicated branch** (if not already):
   ```bash
   git checkout -b research-modifications   # or feature/csi-imscope-record
   git add -A
   git status   # review
   git commit -m "CSI-RS imscope/record, N1=1 fix, CSI_RS_periodicity_slots, docs"
   ```

2. **Tag the base** (optional but useful): before your first modification commit, tag the upstream commit you started from, e.g.:
   ```bash
   git tag oai-base-before-modifications <commit-hash>
   ```
   So later you know which OAI version the patches are relative to.

3. **Push the branch** to your own remote (GitHub/GitLab fork or new repo):
   ```bash
   git remote add myremote https://github.com/YOUR_USER/openairinterface5g.git
   git push myremote research-modifications
   ```

### 1.2 Transfer to a new repository via patches

**Option A – Apply patches in a fresh clone**

1. In the **modified** repo, export patches from your branch (relative to a base tag or commit):
   ```bash
   git format-patch -o ../oai-patches oai-base-before-modifications..HEAD
   ```
   This creates one `.patch` file per commit in `../oai-patches/`.

2. Copy the `oai-patches` folder to the new machine or repo location.

3. In a **fresh clone** of the same OAI version (same base commit/tag):
   ```bash
   git clone https://gitlab.eurecom.fr/oai/openairinterface5g.git new-oai
   cd new-oai
   git checkout -b research-modifications <base-commit-or-tag>
   git am ../oai-patches/*.patch
   ```

**Option B – Single diff patch**

1. Export one big patch:
   ```bash
   git diff oai-base-before-modifications..HEAD > ../oai-modifications.patch
   ```

2. In a fresh clone at the same base:
   ```bash
   git apply --check ../oai-modifications.patch   # dry run
   git apply ../oai-modifications.patch
   git add -A && git commit -m "Apply research modifications"
   ```

---

## 2. What to include when transferring

### 2.1 Code and config

- All modified source files (see list below).
- Config examples if you changed them (e.g. `gnb.sa.band78.fr1.106PRB.usrpb210.conf` with `CSI_RS_periodicity_slots`).

### 2.2 Documentation (so others can understand and re-apply)

Keep these in `doc/` and transfer them with the repo or patch set:

| Document | Purpose |
|----------|--------|
| `doc/IMSCOPE_CSI_MODIFICATIONS.md` | ImScope CSI-RS and CSI report changes |
| `doc/CSI_RECORD_MODIFICATIONS.md` | CSI recording (gNB + UE), file formats |
| `doc/5G_CHANNELS_IMPLEMENTATION_AND_TRACING_GUIDE.md` | Channel implementation and tracing |
| `doc/5G_STACK_ARCHITECTURE_AND_DATA_FLOW.md` | Stack architecture and DL/UL flow |
| `doc/SAVING_AND_TRANSFERRING_MODIFICATIONS.md` | This file |
| `doc/NRGNB_CUSTOM_STACK_AND_DEVELOP_SYNC.md` | **Team onboarding:** summary of nrgNB vs `develop`, replication commands, staying aligned with upstream |

### 2.3 Summary of modified areas (for manual re-apply or review)

- **UE PHY (CSI-RS):** `openair1/PHY/NR_UE_TRANSPORT/csi_rx.c` (measurement_bitmap >= 1, imscope/record).
- **PHY scope API:** `openair1/PHY/TOOLS/phy_scope_interface.h` (ueCsirsChEstimate, gNBCsiReportParams, `csi_report_scope_payload_t`).
- **ImScope GUI:** `openair1/PHY/TOOLS/imscope/imscope_common.cpp`, `imscope/imscope.cpp` (windows + CSI payload display).
- **gNB MAC (UCI/CSI):** `openair2/LAYER2/NR_MAC_gNB/gNB_scheduler_uci.c` (feed gNBCsiReportParams to imscope).
- **gNB config / CSI-RS periodicity:** `openair2/LAYER2/NR_MAC_gNB/nr_radio_config.c`, `nr_mac_gNB.h`; `openair2/GNB_APP/gnb_paramdef.h`, `gnb_config.c`; `targets/.../gnb.sa.band78.fr1.106PRB.usrpb210.conf` (comment/example for `CSI_RS_periodicity_slots`).

---

## 3. New repository options

### 3.1 Fork + branch

- Fork the upstream OAI repo (Eurecom GitLab or GitHub mirror).
- Push your branch to the fork.
- To “transfer” to another machine: clone the fork and checkout your branch.

### 3.2 New repo with upstream as remote

- Create a new empty repo (e.g. `my-oai-research`).
- Clone upstream OAI, add your repo as remote, push your branch:
  ```bash
  git remote add origin https://github.com/YOUR_USER/my-oai-research.git
  git push -u origin research-modifications
  ```
- Others clone `my-oai-research` and checkout `research-modifications`; you can add `upstream` pointing to OAI for future merges.

### 3.3 Patches-only repo

- Repo containing only:
  - `patches/*.patch` (from `git format-patch`),
  - `doc/` (copies of the modification docs),
  - `README.md` with: base OAI commit/tag, how to apply patches, and list of modified files.

---

## 4. Quick export script (optional)

A ready-made script is in the repo:

```bash
# From repo root (optional: chmod +x doc/export-modifications.sh)
./doc/export-modifications.sh [BASE_REF] [OUTDIR]
# Example: ./doc/export-modifications.sh v1.2.0
# Example: ./doc/export-modifications.sh abc1234 ../my-patches
```

It writes `full-diff.patch`, `changed-files.txt`, `commits.txt`, and `README.txt` into `OUTDIR` (default: `../oai-modifications-export`).

- **Save:** run the script with your base tag/commit; keep the export folder (and commit the script to your branch if you like).
- **Transfer:** copy the export folder (and the docs under `doc/`) to the new repo location; in a clone at the same base, apply with `git apply full-diff.patch` (fix any conflicts if the base differs).

---

## 5. Checklist before transferring

- [ ] All changes committed (or stashed and documented).
- [ ] Branch name and base commit/tag recorded (e.g. in README or this doc).
- [ ] Documentation under `doc/` is up to date and included.
- [ ] Config examples (e.g. `CSI_RS_periodicity_slots`) are in the patch or in a separate config snippet.
- [ ] Optional: run `doc/export-modifications.sh` and keep the export folder for transfer.

Using a **single feature branch** plus **format-patch or a single diff** keeps the modifications easy to save, review, and re-apply on a new clone or new repository.
