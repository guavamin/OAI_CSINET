# USRP RU GPIO for TDD External mmWave Front-End

This note describes how to use the USRP (RU) GPIO to drive an external mmWave front-end, LNA, or PA so that a **single GPIO line** is **low in RX** and **high in TX**, enabling TDD operation with an external RF module.

---

## Goal

- **1 GPIO**: **LOW** when the gNB (or UE) is in **RX**; **HIGH** when in **TX**.
- Use the USRP as a front-end controller: its GPIO switches the external mmWave module between receive and transmit paths (e.g. LNA vs PA) in sync with the actual RF burst.

---

## How It Works

1. **OAI RU** runs TDD and tells the USRP when to TX (via normal TX burst timing).
2. **UHD** drives the USRP’s **ATR (Automatic Transmit/Receive)** lines based on actual RF transmit/receive state.
3. OAI configures **one GPIO pin** to follow ATR: **ATR_RX** → 0, **ATR_TX** / **ATR_XX** → 1. So the pin is low when receiving and high when transmitting; timing is **hardware-aligned** with the RF, not slot-based software.

---

## When does the GPIO state change? (slot, frame, radio latency)

The GPIO is driven by **UHD ATR in the USRP FPGA**, which switches based on the **radio’s actual TX/RX state**, not on slot or frame in host software.

- **Slot / frame:** The GPIO does **not** switch at the moment OAI “enters” a slot or frame. OAI sends TX bursts with a **timestamp** (slot-aligned); the USRP starts RF at that time. The ATR (and thus the GPIO) switches when the **radio** starts or stops transmitting. So relative to the **air interface**, the GPIO is aligned with the slot (because the RF is), but there can be **host and transfer delay** between “OAI slot boundary” and “burst actually sent to the USRP.” The GPIO follows the **radio**, so it switches when the RF switches.
- **Radio latency:** The same ATR logic that controls the internal RF front-end also drives the GPIO. So the GPIO and the **actual RF** (TX on/off) switch **together**; there may be a small fixed delay (device-dependent, often on the order of samples or a few nanoseconds). The GPIO does **not** change “after” the radio in a meaningful way—it changes **with** the radio.

**Summary:** The GPIO state changes **with the radio’s TX/RX state** (aligned with actual RF), not with the slot or frame index in software. It can appear “after” the slot/frame in wall-clock time because of host and transfer latency, but at the RF it is aligned with the burst. For an external mmWave front-end, this is usually what you want: the GPIO and the USRP’s RF switch at the same time. If you need GPIO to switch at an exact sample count or timestamp with no dependency on stream state, you would need **timed GPIO commands** (UHD timed commands) instead of ATR—that would require a code change.

### Can we be sure the GPIO triggers *before* the USRP starts the burst?

**With ATR only: no.** If you do **not** set an advance (see below), the GPIO is driven by **ATR** and is **not** guaranteed to go high before the first TX sample; it is approximately simultaneous with the RF.

**With timed GPIO advance: yes.** OAI supports a **configurable advance** so that the mmWave front-end GPIO goes **high a fixed time before** the burst and **low at burst end**, using **UHD timed commands**:

- Add **`gpio_tdd_advance_us=N`** to the **device args** (e.g. in **`sdr_addrs`**). **N** is the lead time in **microseconds** (e.g. `20` for 20 µs).
- OAI then uses **manual GPIO** plus **timed commands**: GPIO is driven **high** at time `(burst_start - N µs)` and **low** at `(burst_start + burst_duration)`. So the external front-end is switched on **N µs before** the first TX sample.

**Example (RU or UE config):**

- Set **`gpio_controller: "tdd_frontend"`** in the RU (or UE RUs) config block as usual.
- In the **device args** string (**`sdr_addrs`**), add **`gpio_tdd_advance_us=20`** (or your desired lead in µs). Example: **`sdr_addrs: "type=x300,addr=192.168.10.2,gpio_tdd_advance_us=20"`**. The advance is parsed at init and the key is stripped before the args are passed to UHD.

**Summary:**

| Setup | GPIO vs burst |
|-------|----------------|
| **`tdd_frontend`**, no advance | ATR: GPIO ≈ simultaneous with RF (no guaranteed lead). |
| **`tdd_frontend`** + **`gpio_tdd_advance_us=N`** in device args | Timed: GPIO high **N µs before** burst start, low at burst end. |

So to **ensure the mmWave front-end is activated before the USRP stream burst**, set **`gpio_controller = "tdd_frontend"`** and add **`gpio_tdd_advance_us=20`** (or your desired lead in µs) to the **`sdr_addrs`** device string. No RF delay or scope measurement is required for a guaranteed lead.

---

## Full duplex (continuous RX + burst TX): does it affect TDD GPIO?

OAI runs the USRP with the **RX stream continuous** (`STREAM_MODE_START_CONTINUOUS`) and **TX in bursts**. So from the device’s point of view, during a TX burst both TX and RX are active → the ATR state can be **ATR_XX** (full duplex) rather than **ATR_TX** (transmit only). In receive-only slots the state is **ATR_RX**.

The **`tdd_frontend`** GPIO setup is written so that this does **not** break TDD behavior:

- **ATR_RX** → GPIO **0** (low) — receive only  
- **ATR_TX** → GPIO **1** (high) — transmit only  
- **ATR_XX** → GPIO **1** (high) — full duplex (TX + RX)

So whenever the radio is **transmitting** (whether the state is ATR_TX or ATR_XX), the GPIO is **high**. When it is **receive-only** (ATR_RX), the GPIO is **low**. Full duplex operation during TX bursts therefore does **not** affect the intended TDD GPIO meaning: low = RX, high = TX. The external mmWave front-end still sees the correct state.

---

## TDD periodicity and flexible (mixed) slot configuration

**Yes — they are taken into account.** The GPIO is driven by **ATR**, which follows **when the radio is actually transmitting**, not by slot or frame indices. OAI already sends **TX bursts** whose length and timing match the configured TDD pattern:

- **Full DL slot:** one TX burst over the full slot → GPIO high for the whole slot.
- **Full UL slot:** no TX burst → GPIO low for the whole slot.
- **Mixed (flexible) slot:** only the **DL symbols** are sent in the TX burst. In `executables/nr-ru.c`, for `NR_MIXED_SLOT` the RU sets `siglen` to the sample length for the **number of DL symbols** in that slot (from `tdd_table` / `slot_config`), not the full slot. So the USRP only transmits during those DL symbols; ATR (and the GPIO) goes high for that interval and low for guard and UL symbols.

So **TDD periodicity** (which slots are DL/UL/mixed) and **per-symbol direction** (which symbols in a mixed slot are DL vs UL vs guard) are respected: the GPIO is high only when the RF is actually on air for DL, and low otherwise. No extra logic is needed in the GPIO path; the existing burst timing (which already respects the TDD table) drives the radio and thus the ATR/GPIO.

---

## Default: does usrp_lib work if I don’t set these parameters?

**Yes.** Behaviour is unchanged if you omit the TDD GPIO options:

- **`gpio_controller`**  
  - **RU:** Default is **`"generic"`**. If you don’t set it (or set **`"none"`**), the **tdd_frontend** path is not used; **usrp_lib** uses the existing generic / none / interdigital logic.  
  - **UE:** Default is **`"none"`** (GPIO not driven). So without any change, **usrp_lib** works as before.

- **`gpio_tdd_advance_us`**  
  If you set **`gpio_controller = "tdd_frontend"`** but **do not** add **`gpio_tdd_advance_us`** to the device args, **`gpio_tdd_advance_sec`** stays **0**. Then:
  - At start: the TDD pin is driven by **ATR** (low = RX, high = TX), same as the original tdd_frontend behaviour.
  - In the write path: the timed-GPIO block is only run when **`advance > 0`**, so no timed commands are issued.

So **usrp_lib works as default** when these parameters are not provided; the new options are additive and optional.

---

## How to choose `gpio_tdd_advance_us`

**`gpio_tdd_advance_us`** is the time (in microseconds) by which the GPIO goes **high before** the first TX sample, so the external mmWave front-end (LNA/PA or switch) can settle before RF appears.

1. **From the front-end datasheet**  
   Use the **“switch settling time”**, **“enable to RF stable”**, or **“TX/RX switching time”** (or similar) in the mmWave module or switch datasheet. Set **`gpio_tdd_advance_us`** to that value (in µs), or slightly larger for margin.

2. **If the value is unknown**  
   Start with a **conservative** value, e.g. **20–50 µs**. Many switches and PA/LNA enables settle in a few to tens of µs. Then check with a scope (GPIO vs RF or vs first symbol) and reduce if you want to tighten timing.

3. **Verify with a scope**  
   Trigger on the GPIO rising edge and measure the delay to the first TX sample (or to RF power). The advance should be **≥** that settling requirement so that RF is valid when the first symbol is transmitted.

**Example:** If the datasheet says “switch settling time 15 µs”, use **`gpio_tdd_advance_us=20`** (or **25**) in the device args.

---

## Clock and sync: 10 MHz, 1 PPS, and gNB/UE alignment

### Does the USRP need external 10 MHz and 1 PPS for timed-command GPIO?

**No.** The timed GPIO commands use the **same time base** as the TX burst on that USRP. You schedule “GPIO high at (burst_time − advance)” and “TX at burst_time” in the **device’s** time. So:

- With **internal** clock and time: the USRP time counter runs from its internal reference. The GPIO and the burst are still **relatively** correct (GPIO leads by `gpio_tdd_advance_us`). So for **one** USRP driving an mmWave front-end, **external 10 MHz and 1 PPS are not required** for the timed GPIO to work.
- With **external** 10 MHz and/or 1 PPS (or GPSDO): you get a stable and/or aligned time base; the timed commands behave the same way, but the device time can be aligned to other nodes if needed.

So: **timed-command GPIO does not by itself require** external 10 MHz or 1 PPS.

### Do gNB and UE need to be synchronized (same ref and 1 PPS)?

**For TDD with gNB and UE together: yes** (or some equivalent sync). That is a **TDD system** requirement, not specific to GPIO:

- In TDD, gNB and UE must use the **same** frame and slot boundaries. Otherwise one side can TX while the other RX on the same frequency and the link fails.
- So in practice, gNB and UE (and their USRPs) are **synchronized** via:
  - **Same 10 MHz reference** (and same 1 PPS if used), or  
  - **GPSDO** on both (same GPS time and 1 PPS), or  
  - Another common time reference (e.g. PTP-driven 1 PPS).

If each side uses **internal** clock and time only, their frame/slot boundaries will drift relative to each other and TDD will break, regardless of GPIO.

**Summary**

| Scenario | 10 MHz / 1 PPS required? |
|----------|---------------------------|
| **Single USRP** (gNB or UE only) with timed GPIO + mmWave front-end | **No.** Internal clock is enough for correct GPIO advance before burst. |
| **gNB + UE** TDD link (both running) | **Yes** (or GPSDO / equivalent). Both sides must share the same time reference so frame/slot boundaries align. |

---

## Configuration

### RU (nr-ru) — gNB side

Set **`gpio_controller`** to **`tdd_frontend`** in the RU configuration (the same block where you set `sdr_addrs`, etc.).

**Example (YAML-style RU section):**

```yaml
ru:
  sdr_addrs: "type=x300,addr=192.168.10.2"
  gpio_controller: "tdd_frontend"
```

**Example (CONF-style):**

```
ru_sdr_addrs = "type=b200";
gpio_controller = "tdd_frontend";
```

Other valid values for `gpio_controller` are **`none`**, **`generic`**, and **`interdigital`**. Use **`tdd_frontend`** specifically for the single-pin RX/TX switching described here.

### UE (nr-ue) — UE side

To drive the same TDD GPIO from the **UE** (e.g. when the UE uses a USRP with an external mmWave front-end), set **`gpio_controller`** to **`tdd_frontend`** in the **UE’s RU config**. This is the **RUs** section in the UE configuration file (not the gNB config).

**Example (UE config file with RUs):**

```yaml
RUs:
  - sdr_addrs: "type=b200"
    gpio_controller: "tdd_frontend"
```

If the UE is started **without** a config file (command-line only), `gpio_controller` remains **`none`** and the GPIO is not driven; use a config file with the RUs section and **`gpio_controller: "tdd_frontend"`** to enable TDD front-end mode on the UE.

---

## Hardware — which GPIO pin?

The TDD front-end control uses **a single pin: GPIO bit 0** (LSB) of the active GPIO bank. In code this is **`TDD_FRONTEND_GPIO_MASK = 1`** in `radio/USRP/usrp_lib.cpp`.

| Device | GPIO bank | Pin used | Physical connector |
|--------|-----------|----------|--------------------|
| **B210** | **FP0** | **Bit 0** (Data[0]) | FP0 header; see B210 manual for pinout. |
| **X310 / X300** | **FP0** | **Bit 0** (Data[0]) | Front-panel FP0: Data[0] is typically **pin 2** (with pin 1 = +3.3V, 14–15 = GND). Confirm in [X3x0 GPIO docs](https://files.ettus.com/manual/page_gpio_api.html). |
| **N310 / N300** | **FP0** | **Bit 0** (Data[0]) | Front-panel GPIO; Data[0] = bit 0. See N3x0 manual for connector pinout. |
| **X400** | **GPIO0** | **Bit 0** (Data[0]) | GPIO0, after `set_gpio_src` to DB0_RF0. See X4x0 manual for pinout. |

- **Levels**: **0 = RX**, **1 = TX** (idle and RX both drive the pin low).
- **Timing**: The pin is driven by UHD ATR in sync with the USRP’s TX/RX RF path.

Wire **Data[0] / bit 0** of the appropriate bank to your external mmWave TDD control input (use a level shifter if needed). Use a common ground between USRP and the external module. GPIO pins are typically 3.3 V and current-limited (e.g. 5 mA per pin); check your USRP model’s manual.

---

## Will gNB and UE work with tdd_frontend?

**Yes.** The **`tdd_frontend`** mode only configures one GPIO pin to follow the USRP’s ATR (low = RX, high = TX). It does **not** change scheduling, PHY, or protocol behavior. So:

- **gNB (with RU)** and **UE** both work normally with **`gpio_controller = "tdd_frontend"`**.
- Set **`tdd_frontend`** on the **RU** config when the gNB uses a USRP with an external mmWave front-end.
- Set **`tdd_frontend`** in the **UE’s RUs** config when the UE uses a USRP with an external mmWave front-end.
- You can use **`tdd_frontend`** on one side and **`none`** or **`generic`** on the other if only one side has the external front-end.

---

## Code References

| What | Where |
|------|--------|
| GPIO controller enum | `radio/COMMON/common_lib.h`: `RU_GPIO_CONTROL_TDD_FRONTEND` |
| Advance (seconds) | `radio/COMMON/common_lib.h`: `openair0_config_t.gpio_tdd_advance_sec` |
| ATR vs timed setup | `radio/USRP/usrp_lib.cpp`: `trx_usrp_start_tdd_frontend_gpio()` (if advance > 0: manual GPIO; else ATR) |
| Parse `gpio_tdd_advance_us` | `radio/USRP/usrp_lib.cpp`: `device_init()` from device args, stripped before UHD |
| Timed GPIO (high before burst, low at end) | `radio/USRP/usrp_lib.cpp`: `trx_usrp_write()`, `trx_usrp_write_thread()` when `gpio_controller == TDD_FRONTEND && gpio_tdd_advance_sec > 0` |
| RU config parsing (gNB) | `executables/nr-ru.c`: `gpio_controller` param, string `"tdd_frontend"` |
| UE RU config parsing | `executables/nr-ue-ru.c`: `NRUE_RU_GPIO_CONTROL`, `cfg->gpio_controller` in `nrue_init_openair0()` |
| No per-slot GPIO flags | `executables/nr-ru.c`: `get_gpio_flags()` returns 0 for `RU_GPIO_CONTROL_TDD_FRONTEND` |

---

## Summary

| Step | Action |
|------|--------|
| 1 | **gNB:** Set **`gpio_controller = "tdd_frontend"`** in the RU config (default is **`generic`**). **UE:** Set **`gpio_controller: "tdd_frontend"`** in the UE config file’s **RUs** section (default is **`none`**). |
| 2 | Connect GPIO bit 0 (FP0 on B210/X310/N310, GPIO0 on X400) to your external mmWave TDD control input. |
| 3 | Run the RU and/or UE as usual; the pin will be low in RX and high in TX, aligned with the RF burst. |

This achieves TDD operation with an external mmWave RF module using a single USRP GPIO line on both gNB and UE when configured.
