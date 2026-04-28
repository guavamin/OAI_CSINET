# Skipping USRP calibration during RU initialization

This note explains what “calibration” means in the USRP RU path and how to skip or reduce it.

---

## What happens at RU init (USRP)

When the NR-RU starts with a USRP (e.g. X3x0, N3x0, B2x0), the radio is loaded via **`openair0_device_load()`** → **`device_init()`** in **`radio/USRP/usrp_lib.cpp`**. That path:

1. **Creates the USRP** with the device args string (e.g. **`sdr_addrs`** from config).
2. **Sets rate, frequency, gain** per channel: **`set_rx_rate()`**, **`set_rx_freq()`**, **`set_tx_freq()`**, **`set_rx_gain()`**, etc.
3. **Applies an RX gain offset** from a **calibration table** (e.g. `calib_table_x310`, `calib_table_b210`) via **`set_rx_gain_offset()`** — this is a **lookup by frequency**, not a runtime calibration procedure.
4. **Creates TX/RX streamers**; then sets TX/RX bandwidth (after streamers, due to a known USRP/UHD issue).

OAI **does not** run UHD’s calibration utilities (e.g. **`uhd_cal_tx_iq_balance`**) at init. When you call **`set_rx_freq()`** / **`set_tx_freq()`**, **UHD** may:

- **Load and apply** stored calibration data (e.g. from **`~/.local/share/uhd/cal/`**) for that daughterboard, and/or  
- Run **device-internal** calibration (e.g. on N310/B200), depending on device and UHD version.

So “calibration” during RU init in practice means: **(1) UHD loading/applying .cal files when tuning, and (2) OAI applying the RX gain table.** There is no separate “run uhd_cal_* at startup” step in OAI.

---

## Yes — you can skip or reduce it

### 1. Skip applying UHD calibration files (recommended if you want no cal)

UHD can be told **not to load or apply** the daughterboard’s calibration file by passing **`ignore-cal-file=1`** in the **device args**.

**RU (nr-ru):**

- Set **`sdr_addrs`** (or the config parameter that becomes **`ru->openair0_cfg.sdr_addrs`**) to a string that includes **`,ignore-cal-file=1`**.
- Example (replace with your type/addr):  
  **`type=x300,addr=192.168.10.2,ignore-cal-file=1`**  
  or  
  **`type=n3xx,addr=192.168.20.2,ignore-cal-file=1`**

**Where to set it:**

- In the RU config file (e.g. the block that sets **`ru_sdr_addrs`** or **`sdr_addrs`** for the USRP RU).
- Or on the command line if your build supports overriding that param.

After this, at RU init UHD will **not** load or apply the stored .cal data when tuning; the RF front-end will use default/uncorrected behavior (no IQ/DC correction from file).

### 2. Skip OAI RX gain calibration table

The **RX gain offset** in OAI comes from **`set_rx_gain_offset()`**, which uses **`openair0_cfg->rx_gain_calib_table`** (e.g. B210, X310, N310, or “none”). That table is chosen in **`device_init()`** in **`radio/USRP/usrp_lib.cpp`** based on device type.

You can **skip** this table (so no frequency-dependent RX gain offset is applied) by adding **`skip_rx_gain_calib=1`** to the **device args** string (the same string used for **`sdr_addrs`** in the RU config). OAI will strip this key from the args before calling UHD, then set **`rx_gain_calib_table`** to **`calib_table_none`** (all offsets 0). **`set_rx_gain_offset()`** still runs, but applies no offset.

**Example (RU config):**

- **`type=x300,addr=192.168.10.2,skip_rx_gain_calib=1`**
- **`type=b200,skip_rx_gain_calib=1`**
- **`type=n3xx,addr=192.168.20.2,ignore-cal-file=1,skip_rx_gain_calib=1`** (skip both UHD cal and OAI table)

Works for B210, X3x0, N3x0; X4x0 already uses **`calib_table_none`** by default.

### 3. Reducing tuning-triggered calibration (advanced)

On some devices, **set_rx_freq()** / **set_tx_freq()** can trigger internal calibration or heavy work. UHD’s **`tune_request_t`** supports policies (e.g. **`POLICY_NONE`**) so that the RF front-end is not retuned. OAI currently uses the default tune request (no policy set). To **minimize** re-tuning and any associated calibration on **retunes**, you could change **`radio/USRP/usrp_lib.cpp`** to set e.g. **`rf_freq_policy = uhd::tune_request_t::POLICY_NONE`** where appropriate (and optionally expose this via a config option). That is a code change and device/use-case dependent.

---

## Summary

| Goal | What to do |
|------|------------|
| **Skip UHD loading/applying calibration files at RU init** | Add **`ignore-cal-file=1`** to the USRP device args string (e.g. **`sdr_addrs`** in RU config). Example: **`type=x300,addr=192.168.10.2,ignore-cal-file=1`**. |
| **Skip OAI RX gain table** | Add **`skip_rx_gain_calib=1`** to the USRP device args (e.g. **`sdr_addrs`**). OAI then uses **`calib_table_none`** (no RX gain offset). |
| **Reduce calibration on later retunes** | Possible via UHD **tune_request_t** policy (e.g. **POLICY_NONE**) in **usrp_lib.cpp**; requires code change. |

So: **yes, it is possible to skip USRP (UHD) calibration during RU initialization** by passing **`ignore-cal-file=1`** in the device args for the RU’s USRP.
