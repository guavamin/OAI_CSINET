# Week 01 – Software design (imscope and documentation)

Summary of changes made to the OpenAirInterface 5G (OAI) repository in this period, focused on **imscope** stability, UX when using window filtering, and documentation.

---

## 1. Imscope crash fix (moving/docking windows)

### Problem

Running `nr-softmodem` with `--imscope` and moving or docking windows (e.g. “CSI report parameters”) caused a crash with ImGui reporting **“Missing EndChild()”** and an assertion in `ImGui::ErrorRecoveryTryToRecoverState`.

### Cause

ImGui requires **every `ImGui::Begin()` to be matched by `ImGui::End()`**, even when `Begin()` returns `false` (e.g. window collapsed or being moved/docked). The code used:

```cpp
if (ImScopeShowWindow("...", window_filter) && ImGui::Begin("...")) {
  // ... content ...
  ImGui::End();
}
```

When `ImGui::Begin()` returned `false`, the block was skipped and `ImGui::End()` was never called, leaving the window/child stack inconsistent.

### Solution

All imscope windows were restructured to the correct pattern:

```cpp
if (ImScopeShowWindow("...", window_filter)) {
  if (ImGui::Begin("...")) {
    // ... content only when window is open ...
  }
  ImGui::End();   // always call after Begin(), regardless of Begin()'s return value
}
```

**Scope of change:** Applied in `openair1/PHY/TOOLS/imscope/imscope.cpp` to:

- **gNB:** CSI report parameters, PUSCH SLOT IQ, PUSCH LLRs, Time domain samples, SRS channel estimates.
- **UE:** UE KPI, UE PDCCH IQ, UE PDCCH LLR, UE PDSCH IQ, UE PDSCH Chan est, UE PDSCH IQ before compensation, UE CSI-RS channel estimates, UE CSI-RS (per RX-TX link), Time domain samples, Time domain samples - before sync, Broadcast channel.

**Related (ImPlot):** `ImPlot::EndPlot()` must only be called when `ImPlot::BeginPlot()` returns true (inside the same `if`). Size guards (e.g. `GetContentRegionAvail().x/y > 2.f`) before `BeginPlot()` were already in place to avoid issues when the window is being resized or docked.

---

## 2. Documentation of the Begin/End fix

### IMSCOPE_DEVELOPER_GUIDE.md

- **New §4.3 – ImGui window lifecycle: Begin/End pairing**  
  Describes the rule, the wrong pattern, the correct pattern, and the ImPlot rule so future changes and new windows stay correct.

- **Summary checklist (§8)**  
  - “New IQ window” and “New custom plot” now reference the §4.3 Begin/End pattern.  
  - New row: **“Avoid crash when moving/docking”** – do not use `ImScopeShowWindow(...) && ImGui::Begin(...)`; always call `ImGui::End()` after every `ImGui::Begin()` (see §4.3).

---

## 3. Automatic tabbed layout when using --imscope-windows

### Goal

When running with `--imscope-windows "Title1,Title2,..."`, the filtered windows (and the Status bar) should open **docked in the main viewport as tabs** instead of overlapping floating windows.

### Implementation (in `imscope.cpp`)

1. **`ImScopeParseFilterTitles(filter, out_titles)`**  
   Parses the comma-separated filter string (with trimmed spaces) into a `std::vector<std::string>` of window titles, using the same rules as `ImScopeShowWindow()`.

2. **Startup dock layout**  
   - When `window_filter` is non-empty and a one-time “layout applied” flag is false:
     - Parse the filter into titles.
     - Use ImGui’s **DockBuilder** API: for each title and for `"Status bar"`, call `ImGui::DockBuilderDockWindow(name, dockspace_id)` with the dockspace id from `ImGui::DockSpaceOverViewport()`, then `ImGui::DockBuilderFinish(dockspace_id)`.
   - All filtered windows and the Status bar then appear as tabs in the main area.
   - The layout is applied **once per session**; **Layout → Reset** clears the flag so the tabbed layout is re-applied on the next frame when a filter is set.

3. **Include**  
   `#include "imgui_internal.h"` was added so that `DockBuilderDockWindow` and `DockBuilderFinish` (internal ImGui API) are declared and the project compiles.

### Documentation

- **IMSCOPE_WINDOWS_FILTER.md**  
  - Behaviour: described “Startup layout” (filtered windows and Status bar auto-docked as tabs at first run; Layout → Reset re-applies).  
  - Code changes table: added `ImScopeParseFilterTitles`, and the filtered dock layout (DockBuilder usage and reset behaviour).

---

## 4. Clarification: “Read” checkbox (UE CSI-RS per RX-TX link)

The **“Read”** checkbox in the **“UE CSI-RS channel estimates (per RX-TX link)”** window was clarified:

- **Checked:** The window calls `TryCollect()` on the shared buffer each frame, so it shows the latest CSI-RS snapshot from the PHY.
- **Unchecked:** The window does not call `TryCollect()`; it keeps showing the last snapshot in the shared buffer (from a previous read or from the other “UE CSI-RS channel estimates” window).

So it acts as an explicit “update from PHY” switch for the per-link view.

---

## 5. Build fix (DockBuilder not a member of ImGui)

### Problem

Compilation failed with:

- `'DockBuilderDockWindow' is not a member of 'ImGui'`
- `'DockBuilderFinish' is not a member of 'ImGui'`

### Cause

Those functions are declared in **imgui_internal.h**, not in the public **imgui.h**.

### Solution

In `imscope.cpp`, add after `#include "imgui.h"`:

```cpp
#include "imgui_internal.h" /* DockBuilderDockWindow, DockBuilderFinish (internal API) */
```

---

## 6. Files touched (summary)

| Area              | File(s) |
|-------------------|--------|
| Imscope logic     | `openair1/PHY/TOOLS/imscope/imscope.cpp` (Begin/End pattern, parse filter, DockBuilder layout, `imgui_internal.h`) |
| Documentation     | `doc/IMSCOPE_DEVELOPER_GUIDE.md` (§4.3, checklist), `doc/IMSCOPE_WINDOWS_FILTER.md` (behaviour, code table) |

No changes to PHY scope types, copy paths, or command-line parsing beyond what was already in place for `--imscope-windows`.

---

## 7. How to verify

1. **Crash fix:** Run gNB (or UE) with `--imscope`, open and move/dock windows (e.g. “CSI report parameters”); the app should not crash with “Missing EndChild()”.
2. **Tabbed layout:** Run with `--imscope --imscope-windows "PUSCH SLOT IQ,SRS channel estimates,CSI report parameters"` (or other titles); at startup, those windows and the Status bar should appear as tabs in the main viewport. Use **Layout → Reset** to re-apply the tabbed layout.
3. **Build:** `nr-softmodem` (with imscope) builds successfully after adding `#include "imgui_internal.h"`.
