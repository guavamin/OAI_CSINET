# imscope Window Filter (--imscope-windows)

This document describes the **--imscope-windows** feature: what it does, what was changed to implement it, and how to add new scope windows so they are selectable via this argument.

---

## 1. Behaviour

- **Without `--imscope-windows`:** All imscope windows are shown (unchanged behaviour). One application window; all panels are visible and can be docked.
- **With `--imscope-windows "Title1,Title2,..."`:** Only the listed windows are created. Titles must match exactly (comma-separated; spaces around commas are ignored). The same single application window is used; only the chosen panels appear and can be docked/tabbed.
- **Startup layout:** When a filter is set, the filtered windows and the Status bar are **automatically docked into the main viewport at first run**, so they appear as **tabs** in one area (no overlapping floating windows). This layout is applied once per session; use **Layout → Reset** to re-apply it after changing layout.
- **Help:** Running the executable with `-h` (or your config’s help option) prints the `--imscope-windows` help string, which lists all currently available window titles for UE and gNB.

---

## 2. Summary of Code Changes

### 2.1 Command-line and config (executables)

| File | Change |
|------|--------|
| `executables/softmodem-common.h` | New param `--imscope-windows` with `CONFIG_HLP_IMSCOPE_WINDOWS` (help text listing all UE/gNB window titles). New macro `IMSCOPE_WINDOWS` → `softmodem_params.imscope_windows`. New field `imscope_windows` (char *) in `softmodem_params_t`. One extra entry in `CMDLINE_PARAMS_DESC` and `CMDLINE_PARAMS_CHECK_DESC`. |
| `executables/softmodem-common.c` | Implement `get_imscope_windows_filter()` returning `get_softmodem_params()->imscope_windows`. Include `openair1/PHY/TOOLS/phy_scope_interface.h` for the declaration. |

### 2.2 Scope interface (PHY)

| File | Change |
|------|--------|
| `openair1/PHY/TOOLS/phy_scope_interface.h` | New field in `scopeData_t`: `const char *imscope_windows`. New API: `const char *get_imscope_windows_filter(void);` (implemented in softmodem-common.c). |

### 2.3 imscope implementation

| File | Change |
|------|--------|
| `openair1/PHY/TOOLS/imscope/imscope_init.cpp` | In `imscope_autoinit()`, set `scope->imscope_windows = get_imscope_windows_filter();` after creating `scope`. |
| `openair1/PHY/TOOLS/imscope/imscope.cpp` | (1) `ImScopeShowWindow(title, filter)`: if `filter` is NULL or empty, return true; else parse comma-separated list (trim spaces) and return true only if `title` is in the list. (2) `ImScopeParseFilterTitles(filter, out_titles)`: parses the filter into a list of window titles for the dock layout. (3) `ShowUeScope`/`ShowGnbScope`: each window uses the Begin/End pattern from the developer guide; only created when `ImScopeShowWindow(...)` is true. (4) In `imscope_thread`, resolve `scopeData_t`, set `window_filter = scope->imscope_windows`, pass to scope functions. (5) Status bar shows “Windows: all” or “Windows: &lt;filter&gt;”. (6) **Filtered dock layout:** when `window_filter` is set, once per session we call `DockBuilderDockWindow` for each parsed title and for “Status bar” into the dockspace, then `DockBuilderFinish`, so filtered windows open as tabs. Layout → Reset clears the “applied” flag so the tabbed layout is re-applied on the next frame. |

No changes to PHY copy paths, scope types, or IQ data flow: only which ImGui windows are created is filtered.

---

## 3. Current Window Titles (for --imscope-windows)

Use these **exact** strings when passing `--imscope-windows`.

### UE (nr-uesoftmodem)

| Title |
|-------|
| UE KPI |
| UE PDCCH IQ |
| UE PDCCH LLR |
| UE PDSCH IQ |
| UE PDSCH Chan est |
| UE PDSCH IQ before compensation |
| UE CSI-RS channel estimates |
| UE CSI-RS channel estimates (per RX-TX link) |
| Time domain samples |
| Time domain samples - before sync |
| Broadcast channel |

### gNB (nr-softmodem)

| Title |
|-------|
| PUSCH SLOT IQ |
| PUSCH LLRs |
| Time domain samples |
| SRS channel estimates |
| SRS channel estimates (per RX-port link) |
| CSI report parameters |

*(“Time domain samples” exists on both UE and gNB; the filter is per executable, so listing it once in the filter is enough for that run.)*

---

## 4. How to Add a New Window and Make It Selectable via --imscope-windows

If you add a new imscope window and want it to be controllable by `--imscope-windows`, do the following.

### Step 1: Add the window in imscope.cpp

Create the window with a **fixed title** and wrap it with the filter check.

**UE** (`ShowUeScope` in `imscope.cpp`):

```cpp
if (ImScopeShowWindow("Your New Window Title", window_filter) && ImGui::Begin("Your New Window Title")) {
  // ... your widgets, TryCollect, Draw, etc. ...
  ImGui::End();
}
```

**gNB** (`ShowGnbScope` in `imscope.cpp`):

```cpp
if (ImScopeShowWindow("Your New Window Title", window_filter) && ImGui::Begin("Your New Window Title")) {
  // ... your widgets ...
  ImGui::End();
}
```

Rules:

- Use **one** string for both `ImScopeShowWindow` and `ImGui::Begin` (the same exact title).
- Always call `ImGui::End()` inside the `if` block so it runs only when `Begin` was called.

### Step 2: Update the help text (so users see the new title in -h)

In **`executables/softmodem-common.h`**, extend the `CONFIG_HLP_IMSCOPE_WINDOWS` string to include the new title.

- **UE window:** Append to the UE list, e.g. add `, Your New Window Title` in the “UE: …” part.
- **gNB window:** Append to the gNB list, e.g. add `, Your New Window Title` in the “gNB: …” part.

Example (after adding a UE window “UE My New Plot”):

```c
#define CONFIG_HLP_IMSCOPE_WINDOWS "Comma-separated list of imscope window titles to show; default: all windows. ... UE: UE KPI, UE PDCCH IQ, ... , UE My New Plot. gNB: ..."
```

### Step 3: (Optional) Update this doc

In **`doc/IMSCOPE_WINDOWS_FILTER.md`**, add the new title to the table in **Section 3** (UE or gNB) so the list of available windows stays accurate.

---

## 5. Checklist for a New Window

- [ ] Add the window in `imscope.cpp` with `ImScopeShowWindow("Exact Title", window_filter) && ImGui::Begin("Exact Title")` and `ImGui::End()` inside the block.
- [ ] Use the **exact same** string for both the filter and `ImGui::Begin`.
- [ ] Update `CONFIG_HLP_IMSCOPE_WINDOWS` in `softmodem-common.h` with the new title.
- [ ] Optionally add the title to the table in `doc/IMSCOPE_WINDOWS_FILTER.md`.

No changes are required in:

- `phy_scope_interface.h`
- `softmodem-common.c`
- `imscope_init.cpp`
- Config param definitions (the single `--imscope-windows` string param already carries the list)

---

## 6. Design Notes

- **Default = all windows:** If `--imscope-windows` is not passed, `imscope_windows` stays NULL; the GUI passes NULL as `window_filter`; `ImScopeShowWindow(title, NULL)` returns true for every title, so all windows are shown.
- **One app window:** All scope panels live in one GLFW window (one taskbar entry). Docking is enabled so users can arrange the selected panels.
- **Exact match:** The filter compares the full window title string; no partial or case-insensitive matching.
- **Help:** The list of available windows is only in the help string and in this doc; the GUI does not parse a separate “registry” of titles.
