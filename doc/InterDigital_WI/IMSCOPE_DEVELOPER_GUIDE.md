# imscope Developer Guide

This document explains how to extend and modify the OAI imscope (Imgui-based soft scope) so you can add new windows, change existing ones, and understand how PHY data flows from the stack to the GUI. It uses the **UE CSI-RS channel estimates (per RX-TX link)** feature as a concrete example.

---

## 1. Overview: Data Flow from PHY to GUI

```
  PHY (e.g. csi_rx.c)          imscope (C++)              GUI
  ───────────────────          ─────────────────          ───
  UEscopeCopyWithMetadata()  →  copyDataThreadSafe()   →   scope_array[type]
       (or UEscopeCopy)             (imscope_common.cpp)        │
                                                                 │
  scope_array[type].is_data_ready = true                         │
  scope_array[type].data = { header + raw bytes }                │
                                                                 ▼
  ShowUeScope() / ShowGnbScope()  →  TryCollect(&scope_array[type])
                                    →  IQData (real, imag, power)
                                    →  ImPlot (windows)
```

- **PHY side (C):** When scope is enabled, PHY code calls `UEscopeCopy` or `UEscopeCopyWithMetadata` (or the gNB variants). That invokes the function pointer `scopeData->copyData`, which is set to `copyDataThreadSafe` at init.
- **imscope side:** `copyDataThreadSafe` copies the payload into a per-type buffer and sets `scope_array[type].is_data_ready = true`. The GUI thread later reads this via `IQData::TryCollect()` (or direct access for non-IQ types), which copies data into an `IQData` and sets `is_data_ready = false` until the next PHY update.

---

## 1.1 Limiting which windows are shown (--imscope-windows)

When you run with `--imscope`, you can pass **`--imscope-windows`** to show only a subset of windows. All scope windows live in a single application window (one taskbar entry); docking is enabled so you can arrange the selected panels inside it.

- **Default:** If **no** `--imscope-windows` argument is passed, **all** imscope windows are shown (unchanged behaviour).
- **Help:** Run the executable with `-h` to see the full list of available window titles in the `--imscope-windows` help text (UE and gNB).
- **Usage:** `--imscope --imscope-windows "UE KPI,UE PDCCH IQ,UE CSI-RS channel estimates"`
- **Value:** Comma-separated list of **exact** window titles (as used in `ImGui::Begin("...")`). Spaces around commas are ignored. If omitted or empty, all windows are shown.
- **UE window titles (examples):** `UE KPI`, `UE PDCCH IQ`, `UE PDCCH LLR`, `UE PDSCH IQ`, `UE PDSCH Chan est`, `UE PDSCH IQ before compensation`, `UE CSI-RS channel estimates`, `UE CSI-RS channel estimates (per RX-TX link)`, `Time domain samples`, `Time domain samples - before sync`, `Broadcast channel`.
- **gNB window titles (examples):** `PUSCH SLOT IQ`, `PUSCH LLRs`, `Time domain samples`, `SRS channel estimates`, `CSI report parameters`.
- **Implementation:** The string is stored in `scopeData_t::imscope_windows` at init (from `get_imscope_windows_filter()`). In `ShowUeScope` / `ShowGnbScope`, each window is only created when `ImScopeShowWindow(title, window_filter)` returns true (filter empty = show all; otherwise title must appear in the comma-separated list). The status bar shows “Windows: all” or “Windows: &lt;filter&gt;”.

See **[IMSCOPE_WINDOWS_FILTER.md](IMSCOPE_WINDOWS_FILTER.md)** for the full change log and a guide to adding new windows for `--imscope-windows`.

---

## 2. Key Files and Types

| Purpose | File | What to know |
|--------|------|---------------|
| Scope type enum | `openair1/PHY/TOOLS/phy_scope_interface.h` | Add new entries to `scopeDataType` for new data sources. |
| Copy API (PHY → scope) | Same header | Macros: `UEscopeCopy`, `UEscopeCopyWithMetadata`, `UEscopeCopyUnsafe`, etc. |
| Scope payload descriptor | Same header | `scopeGraphData_t`: `dataSize`, `elementSz`, `colSz`, `lineSz`. Payload is stored right after the header in memory. |
| Scope buffer (GUI side) | `openair1/PHY/TOOLS/imscope/imscope_internal.h` | `scope_array[EXTRA_SCOPE_TYPES]`, each element is `ImScopeDataWrapper` (mutex, `is_data_ready`, `ImScopeData`). |
| Copy implementation | `openair1/PHY/TOOLS/imscope/imscope_common.cpp` | `copyDataThreadSafe`, `tryLockScopeData`, `copyDataUnsafeWithOffset`, `unlockScopeData`. |
| IQ collection | `imscope_internal.h` | `IQData`: `TryCollect()`, `Collect()`; fills `real`, `imag`, `power`, `len`, `meta`. |
| UE windows | `openair1/PHY/TOOLS/imscope/imscope.cpp` | `ShowUeScope()`: all UE windows. |
| gNB windows | Same | `ShowGnbScope()`: all gNB windows. |
| Scope init | `openair1/PHY/TOOLS/imscope/imscope_init.cpp` | Registers `copyData = copyDataThreadSafe` with UE/gNB so PHY copy calls reach imscope. |

---

## 3. Pushing Data from the Stack (PHY) to the GUI

### 3.1 Add a new scope type (if needed)

In `phy_scope_interface.h`, extend `enum scopeDataType`:

```c
enum scopeDataType {
  // ... existing ...
  ueCsirsChEstimate,
  myNewScopeType,   // add here
  EXTRA_SCOPE_TYPES
};
```

Do **not** skip or renumber existing values; the array `scope_array[type]` is indexed by this enum.

### 3.2 Copy data from PHY

**One-shot copy (full buffer):**

- `UEscopeCopy(ue, type, ptr, elementSz, colSz, lineSz, 0);`
- `UEscopeCopyWithMetadata(ue, type, ptr, elementSz, colSz, lineSz, 0, &meta);`

Parameters:

- `ptr`: pointer to the raw buffer (e.g. `&array[0][0][0]`).
- `elementSz`: size of one element in bytes (e.g. `sizeof(c16_t)` for complex IQ).
- `colSz`, `lineSz`: logical shape. Total bytes copied = `elementSz * colSz * lineSz`. The GUI often treats the buffer as a single row of length `lineSz` (see `IQData::Collect` which uses `iq_header->lineSz`).
- `meta`: optional slot/frame for the GUI.

Example (CSI-RS in `csi_rx.c`):

```c
const int lineSz = frame_parms->nb_antennas_rx * mapping_parms.ports
                   * (frame_parms->ofdm_symbol_size + FILTER_MARGIN);
UEscopeCopyWithMetadata(ue,
                        ueCsirsChEstimate,
                        &csi_rs_estimated_channel_freq[0][0][0],
                        sizeof(c16_t),
                        1,
                        lineSz,
                        0,
                        &meta);
```

So the PHY sends one “line” of length `lineSz` complex samples; the GUI later interprets that (e.g. per RX-TX link) using `ue->frame_parms`.

**Multi-part copy (e.g. PDSCH with multiple symbols):**

- PHY calls `UETryLockScopeData(ue, type, elementSz, colSz, lineSz, &meta)` once to lock and allocate.
- Then calls `UEscopeCopyUnsafe(ue, type, ptr, size, offset, copy_index)` for each chunk.
- Finally `UEunlockScopeData(ue, type)` sets `is_data_ready = true`.

---

## 4. Adding a New Window

### 4.1 Standard IQ window (Histogram / RMS / Scatter)

Use the same pattern as “UE PDSCH IQ” or “UE CSI-RS channel estimates”:

1. In `ShowUeScope()` (or `ShowGnbScope()`), add a new block:

```cpp
if (ImGui::Begin("My new window title")) {
  static auto iq_data = new IQData();
  static auto iq_hist = new IQHist("My IQ");
  bool new_data = false;
  if (iq_hist->ShouldReadData()) {
    new_data = iq_data->TryCollect(&scope_array[myScopeType], t, iq_hist->GetEpsilon(), iq_procedure_timer);
  }
  iq_hist->Draw(iq_data, t, new_data);
}
ImGui::End();
```

2. Ensure the PHY calls `UEscopeCopy` / `UEscopeCopyWithMetadata` for `myScopeType` with the correct `elementSz`, `colSz`, `lineSz` so that `lineSz` matches what `IQData::Collect` expects (it uses `len = iq_header->lineSz`).

3. `IQHist::Draw` provides:
   - Freeze / Load next
   - Plot type: Histogram, RMS, Scatter (if not disabled)
   - For RMS it plots `iq_data->power.data()` of length `iq_data->len` (one curve).

### 4.2 Custom plot (e.g. per-link overlay)

If you want a different visualization (e.g. one curve per RX-TX link on one plot):

1. **Reuse the same scope type** so you don’t duplicate PHY copy logic.
2. **Option A – Same window, different plot:** Use one `IQData` and one `IQHist` for the standard view, and in the same `ImGui::Begin` block add another `ImPlot::BeginPlot` that interprets `iq_data->power` (and `len`) yourself (see “Sharing data between windows” below for layout).
3. **Option B – Second window sharing data:** Keep the first window as above, and add a second window that uses a **shared** `IQData` and triggers `TryCollect` when its own “Read” is enabled, then draws a custom plot from `iq_data->power`/`len`.

Example skeleton for a second window that shares `csirs_shared_iq_data` and draws a custom per-link RMS plot:

```cpp
if (ImGui::Begin("UE CSI-RS channel estimates (per RX-TX link)")) {
  if (read_csirs_perlink) {
    csirs_shared_iq_data->TryCollect(&scope_array[ueCsirsChEstimate], t, ...);
  }
  ImGui::Checkbox("Read", &read_csirs_perlink);
  // Interpret csirs_shared_iq_data->len and ->power using ue->frame_parms,
  // e.g. n_sc = ofdm_symbol_size + FILTER_MARGIN, num_links = len / n_sc, etc.
  if (ImPlot::BeginPlot("...")) {
    ImPlot::SetupAxes("Channel coefficient index", "RMS", ...);
    for (int link = 0; link < num_links; link++)
      ImPlot::PlotLine("RX%d-TX%d", ..., &power[link * n_sc], n_sc);
    ImPlot::EndPlot();
  }
}
ImGui::End();
```

---

### 4.3 ImGui window lifecycle: Begin/End pairing (docking and moving windows)

When users move or dock imscope windows, ImGui can report **"Missing EndChild()"** and the application may abort. This is caused by incorrect **Begin/End pairing**.

**Rule:** ImGui requires **every `ImGui::Begin()` to be matched by `ImGui::End()`**, even when `Begin()` returns `false` (e.g. window collapsed or being moved/docked). If you call `Begin()` but skip `End()` when `Begin()` is false, the internal window/child stack becomes inconsistent and ImGui asserts.

**Wrong pattern (do not use):**

```cpp
if (ImScopeShowWindow("My window", window_filter) && ImGui::Begin("My window")) {
  // ... draw content ...
  ImGui::End();
}
```

When `ImGui::Begin("My window")` returns false, the block is not entered and `ImGui::End()` is never called, so the stack is left unbalanced.

**Correct pattern (use this for every imscope window):**

```cpp
if (ImScopeShowWindow("My window", window_filter)) {
  if (ImGui::Begin("My window")) {
    // ... draw content only when the window is open and visible ...
  }
  ImGui::End();   // always call after Begin(), regardless of Begin()'s return value
}
```

- Call `ImGui::Begin()` only when the window is selected by the filter (`ImScopeShowWindow`).
- Call `ImGui::End()` **unconditionally** after that `Begin()`, so the stack stays balanced when the window is collapsed, moved, or docked.

All imscope windows in `ShowUeScope()` and `ShowGnbScope()` follow this pattern. When adding a new window, use the same structure.

**Related (ImPlot):** For `ImPlot::BeginPlot()` / `ImPlot::EndPlot()`, the rule is the opposite: call **`EndPlot()` only when `BeginPlot()` returned true** (inside the `if (ImPlot::BeginPlot(...)) { ... }` block). Calling `EndPlot()` without a successful `BeginPlot()` causes "Mismatched BeginPlot()/EndPlot()!". In addition, only call `BeginPlot()` when the content region size is usable (e.g. `GetContentRegionAvail().x/y > 2.f`) to avoid issues when the window is being resized or docked.

---

## 5. Modifying an Existing Window

### 5.1 Change behavior of an IQ window

- **Same scope, different default plot type:** The window uses `IQHist`. `IQHist` has `plot_type` (0=Histogram, 1=RMS, 2=Scatter). You could add a constructor parameter or a different default if you add a new `IQHist` variant or subclass (the class is in `imscope.cpp`).
- **Different axes or labels:** Edit the `IQHist::Draw` branch for the relevant `plot_type` (e.g. the `plot_type == 1` block for RMS: `ImPlot::SetupAxes`, `ImPlot::PlotLine`).
- **Extra controls (e.g. epsilon, filter):** `IQHist` already has `epsilon`, `min_nonzero_percentage`, Freeze, etc. You can add more members and ImGui widgets in `Draw()`.

### 5.2 Add a second view of the same data (shared data)

To show the same snapshot in two different ways (e.g. one flattened RMS + one per-link overlay):

1. **Shared `IQData`:** Allocate one `IQData*` (and optionally one `IQHist*` for the standard view) in a scope visible to both windows (e.g. at the start of `ShowUeScope()` as `static`).
2. **Single source of truth:** Only one buffer holds the snapshot. Either:
   - Only the first window calls `TryCollect` and the second just reads from the same `IQData`, or
   - Both windows can call `TryCollect` on the **same** `IQData` and the same `scope_array[type]`; the first call that sees `is_data_ready == true` consumes the snapshot, so both windows will show the same data whenever either triggers a read.
3. **First window:** Use `IQHist::Draw(iq_data, t, new_data)` for the standard view (e.g. one RMS line over full length).
4. **Second window:** Don’t call `IQHist::Draw` for the same buffer; instead implement a custom plot that interprets `iq_data->len` and `iq_data->power` (and, if needed, `ue->frame_parms`) to compute per-link segments and call `ImPlot::PlotLine` for each link with a legend label.

The CSI-RS per-link window is implemented this way: shared `csirs_shared_iq_data`, first window = “UE CSI-RS channel estimates” (flatten RMS), second = “UE CSI-RS channel estimates (per RX-TX link)” (per-link RMS overlay with legend).

---

## 6. Buffer Layout and Custom Interpretation

The PHY sends a contiguous buffer. The header is `scopeGraphData_t`; the payload starts at `(scope_graph_data + 1)`.

- **IQData::Collect** assumes the payload is `lineSz` elements of the same type (e.g. `c16_t`). It sets `len = iq_header->lineSz` and fills `real`, `imag`, `power` from `(c16_t*)(iq_header + 1)`.

So the **only** layout contract the GUI gets from the header is `lineSz` (and total size). Any further structure (e.g. “first n_sc samples = link 0, next n_sc = link 1”) is defined by the PHY and must be matched in the GUI.

**Example – CSI-RS per-link:**

- PHY sends one row of length `lineSz = nb_antennas_rx * ports * (ofdm_symbol_size + FILTER_MARGIN)`.
- So number of “links” = `nb_antennas_rx * ports`, and each link has `n_sc = (ofdm_symbol_size + FILTER_MARGIN)` coefficients.
- In the GUI you have `ue->frame_parms.nb_antennas_rx` and `ue->frame_parms.ofdm_symbol_size`; you must use the same `FILTER_MARGIN` as in the PHY (e.g. 32 in `csi_rx.c`). Then:
  - `n_sc = ofdm_symbol_size + 32`
  - `num_links = csirs_shared_iq_data->len / n_sc`
  - `num_ports = num_links / nb_antennas_rx`
  - Link index `link` → RX = `link / num_ports`, TX = `link % num_ports`; the segment in `power[]` is `[link * n_sc, (link+1) * n_sc)`.

Use the same layout in the PHY and GUI (and document it, e.g. in a comment in `imscope.cpp`) so the x-axis “Channel coefficient index” 0..n_sc-1 is consistent.

---

## 7. Non-IQ Scope Types (LLR, structs, etc.)

Not all scope data is IQ. Examples:

- **LLR (e.g. PDCCH/PDSCH LLR):** Uses `scope_array[pdcchLlr]` etc. The window (e.g. `LLRPlot::Draw`) checks `scope_data.is_data_ready`, then reads `(int16_t*)(scope_data.data.scope_graph_data + 1)` and length from the header, and plots without using `IQData`.
- **CSI report params (gNB):** `scope_array[gNBCsiReportParams]` holds a struct (e.g. `csi_report_scope_payload_t`). The GUI checks `is_data_ready` and casts the payload to the struct, then displays fields; no `IQData` or `TryCollect`.

So:

- **IQ (complex) data:** Prefer `IQData::TryCollect` + `IQHist::Draw` or your own plot from `iq_data->power`/`real`/`imag`.
- **Other types:** Read from `scope_array[type].data.scope_graph_data` and the payload after the header; set `is_data_ready = false` after consuming to avoid reusing the same snapshot.

---

## 8. Summary Checklist

| Task | Where | Action |
|------|--------|--------|
| Limit scope windows | Command line | Use `--imscope --imscope-windows "Title1,Title2"`; titles must match `ImGui::Begin("...")` exactly. |
| New data source | `phy_scope_interface.h` | Add enum to `scopeDataType` (before `EXTRA_SCOPE_TYPES`). |
| PHY → scope | PHY .c file | Call `UEscopeCopy` / `UEscopeCopyWithMetadata` (or gNB/Unsafe variants) with correct type, ptr, elementSz, colSz, lineSz. |
| New IQ window | `imscope.cpp` in `ShowUeScope`/`ShowGnbScope` | Use the **Begin/End pattern** in §4.3: `if (ImScopeShowWindow(...)) { if (ImGui::Begin(...)) { ... } ImGui::End(); }`. Inside: static `IQData` + `IQHist` → `TryCollect(&scope_array[type])` when `ShouldReadData()` → `IQHist::Draw`. |
| New custom plot | Same | Same pattern as above; use shared `IQData` and your own `ImPlot::BeginPlot` / `PlotLine` (call `EndPlot()` only when `BeginPlot()` returned true). |
| Modify existing IQ view | `imscope.cpp` | Edit `IQHist::Draw` (e.g. axes, labels) or add another plot in the same window. |
| Share data between two windows | `imscope.cpp` | One static `IQData*` (and optionally one `IQHist*`); both windows use that pointer; one or both call `TryCollect`; second window draws from the same `IQData` with custom interpretation. |
| Match buffer layout | PHY + GUI | Agree on lineSz and on how it is split (e.g. n_sc per link); use same constants (e.g. FILTER_MARGIN) in PHY and GUI. |
| Avoid crash when moving/docking | `imscope.cpp` | Never use `if (ImScopeShowWindow(...) && ImGui::Begin(...))`. Always call `ImGui::End()` after every `ImGui::Begin()`, even when `Begin()` returned false (see §4.3). |

---

## 9. CSI-RS Per-Link Example (Reference)

- **Purpose:** Same CSI-RS channel snapshot shown in two ways: (1) one flattened RMS curve (existing “UE CSI-RS channel estimates”), (2) one plot with one RMS curve per RX-TX link over coefficient index, with legend.
- **Shared state:** `csirs_shared_iq_data`, `csirs_iq_hist_flatten`, `read_csirs_perlink` (static in `ShowUeScope`).
- **First window:** When `csirs_iq_hist_flatten->ShouldReadData()`, call `csirs_shared_iq_data->TryCollect(&scope_array[ueCsirsChEstimate], ...)`; then `csirs_iq_hist_flatten->Draw(csirs_shared_iq_data, t, new_data)`.
- **Second window:** When `read_csirs_perlink`, call `TryCollect` on the same `csirs_shared_iq_data` and same scope. Then compute `n_sc`, `num_links`, `num_ports` from `ue->frame_parms` and `csirs_shared_iq_data->len`, and for each link plot `ImPlot::PlotLine("RX%d-TX%d", ..., &power[link*n_sc], n_sc)`.
- **Constants:** `CSIRS_FILTER_MARGIN` (32) in `imscope.cpp` must match `FILTER_MARGIN` in `csi_rx.c` so the per-link length is correct.

This gives you a clear pattern for adding new windows, modifying existing ones, and sharing or reinterpreting PHY data in the imscope GUI.
