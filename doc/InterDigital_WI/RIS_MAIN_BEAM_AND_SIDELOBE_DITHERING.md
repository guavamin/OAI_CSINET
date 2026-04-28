# RIS: Main beam toward RX + sidelobe-only environment scan

**Readable math (recommended):**

- **[RIS_MAIN_BEAM_AND_SIDELOBE_DITHERING.html](RIS_MAIN_BEAM_AND_SIDELOBE_DITHERING.html)** — open in a web browser; equations render with MathJax (needs network once for the CDN, or use offline MathJax if you prefer).
- **[RIS_MAIN_BEAM_AND_SIDELOBE_DITHERING.tex](RIS_MAIN_BEAM_AND_SIDELOBE_DITHERING.tex)** — compile with `pdflatex RIS_MAIN_BEAM_AND_SIDELOBE_DITHERING.tex` to get a PDF with native LaTeX typesetting.
- **[RIS_MAIN_BEAM_AND_SIDELOBE_DITHERING.pdf](RIS_MAIN_BEAM_AND_SIDELOBE_DITHERING.pdf)** — pre-built PDF (same content as the `.tex`; regenerate after editing the source).

The Markdown source below may show raw `$$` / `\(...\)` unless your viewer enables MathJax or KaTeX.

---

This note reformulates the idea of **keeping the primary (focal) response aligned to the receiver** while using **dithering** to probe the environment—conceptually scanning **sidelobes** or “ambient” radiation—without destroying the main link.

---

## 1. What you are trying to do

You want a **two-part** surface modulation on the RIS:

| Part | Role |
|------|------|
| **Main term** | Keeps the RIS-assisted contribution **strong and stable** at the RX (the “beam” or **focus** pointed at the RX). |
| **Dither / scan term** | Changes how energy is distributed **elsewhere** (sidelobes / off-focus regions) to **sense** scatterers, blockers, or changes—ideally with **minimal** impact on the RX. |

Important: on a finite aperture you cannot literally move “only” sidelobes with **zero** effect on the main coupling unless you **constrain** how you dither. The rigorous formulation is: **keep the perturbation small and/or orthogonal to the RX coupling**.

---

## 2. Discrete RIS model (schematic)

Model the RIS by **\(N\)** unit cells. Let the **complex spatial modulation** (per cell) be stacked in a vector:

$$
\boldsymbol{\Gamma} = [\Gamma_1,\, \Gamma_2,\, \ldots,\, \Gamma_N]^{\mathsf{T}} \in \mathbb{C}^N.
$$

The **scalar** (or dominant) complex gain seen at the RX through the RIS path can be written schematically as an **inner product** with a **coupling vector** from the elements to the RX (this hides TX–RIS propagation inside the same effective model):

$$
h_{\mathrm{RIS}} \;\propto\; \mathbf{g}_{\mathrm{rx}}^{\mathsf{H}}\, \boldsymbol{\Gamma}.
$$

- \(\mathbf{g}_{\mathrm{rx}} \in \mathbb{C}^N\) encodes how each element couples to the RX (phases, amplitudes, path aggregation).
- Your **baseline** “point the main toward RX” pattern is some \(\boldsymbol{\Gamma}_0\) chosen so that \(\mathbf{g}_{\mathrm{rx}}^{\mathsf{H}} \boldsymbol{\Gamma}_0\) is large (after any normalization you use).

This is the right abstraction for both **far-field steering** (where \(\mathbf{g}_{\mathrm{rx}}\) looks like a plane-wave array response) and **near-field focusing** (where \(\mathbf{g}_{\mathrm{rx}}\) includes **range and curvature**, not just angle).

---

## 3. Splitting main and dither

### 3.1 Additive complex perturbation

$$
\boldsymbol{\Gamma}_k \;=\; \boldsymbol{\Gamma}_0 \;+\; \Delta\boldsymbol{\Gamma}_k.
$$

- \(\boldsymbol{\Gamma}_0\): fixed (or slowly tracked) **main** pattern toward the RX.
- \(\Delta\boldsymbol{\Gamma}_k\): **\(k\)-th dither** pattern for environment scanning.

You usually **renormalize** \(\boldsymbol{\Gamma}_k\) to satisfy hardware constraints (e.g. per-element magnitude limits).

### 3.2 Phase-only dither (common for passive RIS)

$$
\Gamma_{k,n} \;=\; \Gamma_{0,n}\, e^{\,j \delta_{k,n}}, \qquad n = 1,\ldots,N.
$$

- \(\boldsymbol{\delta}_k = [\delta_{k,1},\ldots,\delta_{k,N}]^{\mathsf{T}}\): small or structured **phase ripple** on top of the main profile.

---

## 4. Two ways to get “main stays, environment is probed”

### 4.1 Null-space / orthogonal dither (strongest interpretation)

You want \(\Delta\boldsymbol{\Gamma}_k\) to **not** move the RX coupling, in first order:

$$
\mathbf{g}_{\mathrm{rx}}^{\mathsf{H}}\, \Delta\boldsymbol{\Gamma}_k \;\approx\; 0.
$$

So \(\Delta\boldsymbol{\Gamma}_k\) lies approximately in the **orthogonal complement** of \(\mathbf{g}_{\mathrm{rx}}\) (the “null space” of the linear map to the RX scalar, in finite dimension).

**Construction (conceptual):**

1. Form \(\mathbf{g}_{\mathrm{rx}}\) (from geometry + your near-field or far-field model, or from a simulator).
2. Build an orthonormal basis \(\{\mathbf{u}_1, \ldots, \mathbf{u}_M\}\) spanning directions **orthogonal** to \(\mathbf{g}_{\mathrm{rx}}\):

   $$
   \mathbf{g}_{\mathrm{rx}}^{\mathsf{H}} \mathbf{u}_m = 0, \quad m = 1,\ldots,M.
   $$

3. Define dither codewords as:

   $$
   \Delta\boldsymbol{\Gamma}_k \;=\; \sum_{m=1}^{M} \alpha_{k,m}\, \mathbf{u}_m,
   $$

   with small coefficients \(\alpha_{k,m}\).

4. Full pattern:

   $$
   \boldsymbol{\Gamma}_k \;=\; \mathrm{Normalize}\bigl(\boldsymbol{\Gamma}_0 + \Delta\boldsymbol{\Gamma}_k\bigr).
   $$

**Intuition:** the RX inner product stays dominated by \(\boldsymbol{\Gamma}_0\); the dither mostly changes **how energy is distributed away from** the RX coupling—what you loosely call **sidelobes** or non-focal radiation.

---

### 4.2 Small high–spatial-frequency ripple (simpler, less controlled)

Add a **weak**, **rapidly varying** phase \(\boldsymbol{\delta}_k\) on top of \(\boldsymbol{\Gamma}_0\):

- Often leaves the **coherent sum toward RX** mostly unchanged if ripple is **small** and **balanced**.
- Still perturbs **off-boresight** structure so the environment is **illuminated differently** across codewords \(k\).

This is easier to implement than explicit null-space projection but typically causes **some** main-link variation.

---

## 5. What the near-field case changes

- **Far-field:** \(\mathbf{g}_{\mathrm{rx}}\) is often well approximated by a **plane-wave** array steering vector (depends mainly on **direction**).
- **Near-field (Fresnel region of the aperture):** \(\mathbf{g}_{\mathrm{rx}}\) must reflect **distance and wavefront curvature** across elements—not only angle.

So:

- **Main toward RX** should be a **near-field-aware** \(\boldsymbol{\Gamma}_0\) (e.g. focal / distance-corrected phase), not only a single-direction beam.
- **Null-space dither** should be orthogonal to this **near-field coupling vector**, not to a far-field steering vector only.

That is what enables “scanning” that is **spatially rich** (energy in different **3D** regions), not just scanning different **angles** from the array broadside.

---

## 6. Practical recipe (workflow)

| Step | Action |
|------|--------|
| 1 | Define \(\mathbf{g}_{\mathrm{rx}}\) for your layout (analytical approximation or from ray/EM simulation). |
| 2 | Set \(\boldsymbol{\Gamma}_0\) to maximize \(|\mathbf{g}_{\mathrm{rx}}^{\mathsf{H}} \boldsymbol{\Gamma}_0|\) under your constraints (often \(\boldsymbol{\Gamma}_0 \propto \mathbf{g}_{\mathrm{rx}}^{*}\) up to normalization). |
| 3 | Build basis \(\{\mathbf{u}_m\}\) with \(\mathbf{g}_{\mathrm{rx}}^{\mathsf{H}} \mathbf{u}_m = 0\). |
| 4 | Form codewords \(\boldsymbol{\Gamma}_k = \mathrm{Normalize}(\boldsymbol{\Gamma}_0 + \sum_m \alpha_{k,m} \mathbf{u}_m)\) with small \(\alpha_{k,m}\). |
| 5 | **Verify** \(|\mathbf{g}_{\mathrm{rx}}^{\mathsf{H}} \boldsymbol{\Gamma}_k|\) is stable across \(k\); reduce \(\alpha\) or re-orthogonalize if the main link moves too much. |

---

## 7. Caveats

1. **Exact** “no change on main” only holds in a **linear** sense; large phase-only dither still couples to \(\mathbf{g}_{\mathrm{rx}}\) nonlinearly through \(\exp(j\delta)\).
2. **Normalization** (unit-modulus elements, power constraints) can couple dither and main when you project back to feasible \(\boldsymbol{\Gamma}_k\).
3. **Sionna (and similar RT tools):** you implement \(\boldsymbol{\Gamma}_k\) as your own **phase/amplitude profiles** on the RIS grid; there is no built-in “sidelobe-only” mode—this math is **your codebook design**, then the simulator evaluates the scene.

---

## 8. One-line summary

**Keep a near-field-correct main pattern \(\boldsymbol{\Gamma}_0\) aligned to the RX; add dither \(\Delta\boldsymbol{\Gamma}_k\) that is approximately orthogonal to \(\mathbf{g}_{\mathrm{rx}}\) so the primary link is preserved while the rest of the field is modulated for environment sensing.**

---

*For rendered equations, use the `.html` (browser + MathJax) or `.tex` → PDF workflow above.*
