# PUSCH / PDSCH config in the gNB config file

In OAI there is **no single block** named `PUSCH_Config` or `PDSCH_Config` in the gNB `.conf`. The RRC PUSCH-Config and PDSCH-Config are **built in code** from:

1. **servingCellConfigCommon** – parameters that map to PUSCH-ConfigCommon / PDSCH-ConfigCommon (initial BWP).
2. **Top-level gNB parameters** (same level as `servingCellConfigCommon`) – antenna ports, PTRS, MCS options, etc., used to build the **dedicated** PUSCH-Config and PDSCH-Config.

---

## 1. PUSCH/PDSCH-related parameters inside `servingCellConfigCommon`

These live inside the first gNB block, under `servingCellConfigCommon = ( { ... } );`:

```c
servingCellConfigCommon = (
{
  physCellId                                                    = 0;

  dl_frequencyBand                                              = 78;   // or 41, 77, etc.
  dl_absoluteFrequencyPointA                                   = 640008;
  dl_offstToCarrier                                             = 0;
  dl_subcarrierSpacing                                          = 1;    // 0=15kHz, 1=30kHz, 2=60kHz, 3=120kHz
  dl_carrierBandwidth                                           = 106;

  initialDLBWPlocationAndBandwidth                             = 28875;
  initialDLBWPsubcarrierSpacing                                = 1;
  initialDLBWPcontrolResourceSetZero                           = 12;
  initialDLBWPsearchSpaceZero                                  = 0;

  ul_frequencyBand                                              = 78;
  ul_offstToCarrier                                             = 0;
  ul_subcarrierSpacing                                          = 1;
  ul_carrierBandwidth                                           = 106;
  pMax                                                          = 20;

  initialULBWPlocationAndBandwidth                             = 28875;
  initialULBWPsubcarrierSpacing                                = 1;

  // RACH / PRACH
  prach_ConfigurationIndex                                     = 98;
  msg1_SubcarrierSpacing                                        = 1;
  restrictedSetConfig                                           = 0;

  // PUSCH-ConfigCommon (Msg3, nominal power)
  msg3_DeltaPreamble                                            = 1;
  p0_NominalWithGrant                                           = -90;

  // PUCCH-ConfigCommon
  pucchGroupHopping                                             = 0;
  hoppingId                                                     = 40;
  p0_nominal                                                    = -90;

  ssb_PositionsInBurst_Bitmap                                  = 1;
  ssb_periodicityServingCell                                    = 2;

  // DMRS (affects both PUSCH and PDSCH)
  dmrs_TypeA_Position                                           = 0;    // 0 = pos2, 1 = pos3

  subcarrierSpacing                                             = 1;

  // TDD pattern
  referenceSubcarrierSpacing                                    = 1;
  dl_UL_TransmissionPeriodicity                                 = 6;
  nrofDownlinkSlots                                             = 7;
  nrofDownlinkSymbols                                           = 6;
  nrofUplinkSlots                                               = 2;
  nrofUplinkSymbols                                             = 4;

  ssPBCH_BlockPower                                             = -25;
}
);
```

- **PUSCH-ConfigCommon**: `msg3_DeltaPreamble`, `p0_NominalWithGrant`; time-domain allocation lists are filled in code.
- **PDSCH-ConfigCommon**: time-domain allocation list is filled in code from defaults / TDD.
- **DMRS**: `dmrs_TypeA_Position` applies to both PUSCH and PDSCH.

---

## 2. Top-level parameters that drive dedicated PUSCH/PDSCH (and PTRS)

Same gNB block, **same level as** `servingCellConfigCommon` (not inside it):

| Parameter | Effect | Example |
|-----------|--------|--------|
| `pusch_AntennaPorts` | UL layers / antenna ports for PUSCH | `1`, `2`, `4` |
| `pdsch_AntennaPorts_N1` | PDSCH antenna ports (type 1) | `1`, `2` |
| `pdsch_AntennaPorts_XP` | PDSCH antenna ports (cross-polarized) | `1`, `2` |
| `phaseTrackingRS` | PTRS for PUSCH/PDSCH (see PTRS_CONFIGURATION.md) | `( { ... } )` |
| `use_deltaMCS` | Delta MCS in PUSCH-Config | optional |
| `force_UL256qam_off` | Restrict UL to 64QAM | optional |

Example snippet (e.g. after the closing `);` of `servingCellConfigCommon`, before `SCTP`):

```c
    # Dedicated PUSCH/PDSCH-related (RRC built in nr_radio_config.c)
    # pusch_AntennaPorts                                        = 1;   # default 1
    # pdsch_AntennaPorts_N1                                     = 2;   # for 2 layers / CSI
    # pdsch_AntennaPorts_XP                                     = 2;

    # PTRS (optional; applied to PUSCH-Config / PDSCH-Config when present)
    phaseTrackingRS = (
    {
      dl_ptrsFreqDensity0_0   = 0;
      dl_ptrsFreqDensity1_0   = 0;
      dl_ptrsTimeDensity0_0    = -1;
      dl_ptrsTimeDensity1_0    = -1;
      dl_ptrsTimeDensity2_0    = -1;
      dl_ptrsEpreRatio_0       = -1;
      dl_ptrsReOffset_0        = -1;
      ul_ptrsFreqDensity0_0    = 0;
      ul_ptrsFreqDensity1_0    = 0;
      ul_ptrsTimeDensity0_0    = -1;
      ul_ptrsTimeDensity1_0    = -1;
      ul_ptrsTimeDensity2_0    = -1;
      ul_ptrsReOffset_0        = -1;
      ul_ptrsMaxPorts_0        = 0;
      ul_ptrsPower_0           = 0;
    }
    );

    SCTP : { ... };
```

---

## 3. MAC/L1 params (scheduling, not RRC PUSCH/PDSCH)

These are under **MACRLCs** / **L1s**, not under the first gNB block:

- `pusch_TargetSNRx10` – MAC target SNR for PUSCH.
- `max_pdschReferenceSignalPower` – used for PDSCH power / pathloss.

Example (inside `MACRLCs = ( { ... } )`):

```c
pusch_TargetSNRx10          = 150;
```

Example (inside `L1s` or RU):

```c
max_pdschReferenceSignalPower = -27;
```

---

## Summary

- **PUSCH/PDSCH “config” in the gNB file** = (1) **servingCellConfigCommon** (common PUSCH/PDSCH params + DMRS position) + (2) **top-level** `pusch_AntennaPorts`, `pdsch_AntennaPorts_*`, `phaseTrackingRS`, etc.
- The actual RRC **PUSCH-Config** and **PDSCH-Config** (with DMRS, PTRS, MCS tables) are **not** written as such in the .conf; they are built in `openair2/LAYER2/NR_MAC_gNB/nr_radio_config.c` from the above parameters.

For PTRS only, see **doc/PTRS_CONFIGURATION.md**.
