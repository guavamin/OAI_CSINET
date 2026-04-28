# Exploring CSINet (autoencoder/decoder) with Type I single-panel codebook

OAI uses **Type I single-panel** codebook for CSI-RS feedback (see `openair2/LAYER2/NR_MAC_gNB/nr_radio_config.c` → `config_csi_codebook()`). There is **no CSINet or autoencoder** in the current source tree. You can still explore CSINet-style models using the existing **CSI recording** feature, which dumps raw channel estimates and Type I report labels.

---

## 1. What the current setup provides

| Component | Role |
|-----------|------|
| **Type I single-panel** | RRC config and UE PHY produce **RI, i1 (3 indices), i2, CQI**; these are encoded on PUCCH and decoded at the gNB. |
| **CSI recording** | When `--csi-record-path <dir>` is set, the **UE** writes raw CSI-RS channel estimates and a CSV of the Type I report; the **gNB** writes decoded CSI feedback and CSI-RS scheduling. |

So you get:

- **Raw channel matrix H** (input to a CSINet encoder): `H_f{frame}_s{slot}.bin`
- **Type I report** (labels / baseline): `csi_reports.csv` (UE) and `gnb_csi_feedback.csv` (gNB)

This is enough to **train and evaluate** a CSINet (or any CSI compression model) **offline** and to design an **online** integration path.

---

## 2. Data format for CSINet (UE recording)

### 2.1 Channel matrix: `H_f{frame}_s{slot}.bin`

- **Header**: 5 × `int32` = `frame`, `slot`, `nr_rx`, `n_ports`, `n_subc`.
- **Payload**: `nr_rx * n_ports * n_subc` × `c16_t` (complex int16: Re, Im), **row-major** (e.g. subcarrier index varies fastest, then port, then RX antenna).
- **Layout**: `H[rx][port][subc]` → flat index `(rx * n_ports + port) * n_subc + subc`.
- Only written when the UE actually runs channel estimation (e.g. report with RI/PMI/CQI, not RSRP-only).

For a typical **2×2** CSI-RS (2 ports, 1 RX or 2 RX): `n_ports` = 2, `nr_rx` = 1 or 2, `n_subc` = number of subcarriers in the CSI-RS bandwidth (e.g. 52 for 1 RB, 624 for 12 RB). So the channel is **2×2×N** or **1×2×N** (complex) per slot.

### 2.2 Labels: `csi_reports.csv`

- Columns: `frame,slot,rsrp_dBm,ri,i1_0,i1_1,i1_2,i2,cqi,sinr_dB`.
- **ri**: 1 or 2 (number of layers).
- **i1_0, i1_1, i1_2, i2**: Type I single-panel PMI indices (0 when RSRP-only).
- **cqi**: Wideband CQI.
- **sinr_dB**: Precoded SINR at UE.

You can use this CSV to align each `H_*.bin` with its Type I report (by `frame,slot`) for training (e.g. supervised / baseline) or evaluation (compare reconstructed H vs Type I precoder).

---

## 3. Exploration options

### Option A: Offline exploration (no OAI code changes)

1. **Collect data**  
   Run gNB and UE with CSI-RS and full report (RI/PMI/CQI) enabled, and set the same `--csi-record-path` (or separate dirs) so you get:
   - UE: `H_f*_s*.bin` + `csi_reports.csv`
   - gNB: `gnb_csi_feedback.csv`, `gnb_csirs_scheduling.csv` (optional, for context).

2. **Data loader**  
   - Read each `H_*.bin`: parse the 5-int32 header, then load `nr_rx * n_ports * n_subc` complex values; reshape to `(nr_rx, n_ports, n_subc)` (or `(n_ports, n_subc)` for SISO RX).
   - Join with `csi_reports.csv` on `(frame, slot)` to get Type I labels (ri, i1, i2, cqi) per sample.

3. **Train a CSINet-style model**  
   - Encoder: H → latent (e.g. real vector or bit stream).  
   - Decoder: latent → reconstructed H (or precoder / beamforming vector).  
   - Loss: e.g. NMSE or cosine similarity between true H and reconstructed H; optionally use Type I precoder as an auxiliary target.

4. **Use Type I as baseline**  
   - From ri, i1, i2 build the Type I single-panel precoder (per 38.214).  
   - Compare achievable rate or NMSE of “Type I feedback” vs “CSINet feedback” on the same recorded H.

This works entirely **outside** the OAI binary; only the recorded files and the Type I codebook definition (e.g. 38.214 tables) are needed.

### Option B: Online integration (code changes)

To actually **send** CSINet feedback over the air and use it at the gNB:

1. **UE (encoder)**  
   - **Where**: After channel estimation in `openair1/PHY/NR_UE_TRANSPORT/csi_rx.c` (e.g. in or after `nr_process_csi_rs()`). You have `csi_rs_estimated_channel_freq` and the same dimensions as in the H bin.  
   - **What**: Run a small encoder (C or C++ inference, or export to a fixed-point / lookup table) on the channel matrix → latent vector (or quantized bits).  
   - **Feedback channel**: Type I report is carried on PUCCH (part1/part2). Replacing it with CSINet would require either:  
     - Encoding the latent in the same PUCCH CSI part1/part2 bit budget (if size fits), or  
     - Using PUSCH for larger payloads (then RRC/scheduler changes).

2. **gNB (decoder)**  
   - **Where**: In `openair2/LAYER2/NR_MAC_gNB/gNB_scheduler_uci.c`, after decoding CSI (e.g. `extract_pucch_csi_report()`).  
   - **What**: Decode the received bits into the latent, run the CSINet decoder → reconstructed H or precoder, and feed that into the PDSCH precoding path instead of (or in addition to) the current Type I PMI-based precoder.

3. **Compatibility**  
   - Today the gNB expects Type I (RI, i1, i2, CQI). If you send a CSINet payload instead, the gNB must be configured to interpret it (new report type or reuse of an existing field with a different meaning). So **coexistence with Type I** (e.g. fallback or A/B test) requires a clear split: e.g. report config ID or a flag that says “this report is CSINet, not Type I.”

---

### Option B (recommended): Python inference sidecar + thin C bridge

To use **Python for model inference** in real time **without** embedding Python in the main C code or heavily editing existing `.c` files, use a **separate Python process** (inference sidecar) and a **thin C bridge** that talks to it over IPC.

#### Architecture

- **Python process (encoder at UE, decoder at gNB)**  
  - Runs your CSINet encoder/decoder (e.g. PyTorch/TensorFlow).  
  - Listens on a **socket** (Unix domain or TCP localhost) or reads/writes a **shared memory** segment.  
  - Receives one message per CSI occasion (e.g. H or received latent bytes), runs inference, sends back the result (latent bits or reconstructed H).

- **C side (OAI)**  
  - **One new file** (e.g. `openair1/PHY/NR_UE_TRANSPORT/csi_ml_bridge.c` at UE and a similar one or the same in `openair2/LAYER2/NR_MAC_gNB/` at gNB) that:  
    - Opens a connection to the Python process (e.g. `connect()` to a fixed path or port).  
    - Serializes the payload (e.g. header + flat H or latent bytes), sends it, waits for the response with a **timeout** (e.g. 1–2 ms).  
    - Returns the result to the caller; on timeout or error, returns a fallback (e.g. “use Type I” or zeros).
  - **Minimal changes to existing .c**:  
    - In `csi_rx.c`: after you have the channel estimate, **one conditional call** (e.g. `if (csi_ml_encoder_socket_path) csi_ml_encoder_send_h(...)` ) that copies H into a buffer and calls the bridge; the bridge returns latent bytes that you then pass to the MAC/feedback path.  
    - In `gNB_scheduler_uci.c` (or the place you decode CSI): **one conditional call** (e.g. `if (csi_ml_decoder_socket_path) csi_ml_decoder_get_h(...)` ) that sends the received bits to the bridge and gets back reconstructed H or precoder for the scheduler.  
  - No Python headers or interpreter in the OAI build; only standard C and sockets (or shm).

#### Why this approach

- **No direct Python in .c**: All model code stays in Python; OAI only does IPC.  
- **Minimal edits**: Existing logic remains; you add a single bridge module and a few guarded calls.  
- **Real-time**: Use a short timeout and fallback to Type I if the sidecar is slow or down.  
- **Same binary**: Bridge can be compiled in only when a macro (e.g. `CSI_ML_BRIDGE`) or runtime option is set; without it, the calls are no-ops.

#### Implementation sketch

1. **Protocol (e.g. over Unix domain socket)**  
   - **UE → Python (encoder)**: send `[len][frame,slot,nr_rx,n_ports,n_subc][flat H as int16 re/im]`; receive `[len][latent_bytes]`.  
   - **gNB → Python (decoder)**: send `[len][latent_bytes]`; receive `[len][reconstructed H or precoder]`.  
   - Use a small fixed header (e.g. 4B length + 5×4B dimensions) so both sides know how much to read.

2. **Python sidecar**  
   - Script or service that: `socket.bind(path)` (UE encoder) or connects to gNB; in a loop: `recv(len)`, `recv(payload)`, run model, `send(result)`.  
   - Load the model once (e.g. `torch.load()` or ONNX) and run inference per message; keep process alive so latency is dominated by inference, not startup.

3. **C bridge API (new file, no change to core PHY logic)**  
   - `csi_ml_encoder_send_h(ue, H_flat, nr_rx, n_ports, n_subc, frame, slot, latent_buf, latent_max_len)` → returns number of bytes or -1 on error/timeout.  
   - `csi_ml_decoder_get_h(latent_buf, latent_len, H_out_buf, dims)` → 0 on success, -1 on error/timeout.  
   - Inside the bridge: connect, send, recv with `SO_RCVTIMEO`/`SO_SNDTIMEO`; on failure, return fallback.

4. **Integration points (minimal)**  
   - **csi_rx.c**: After `nr_process_csi_rs()` has computed the channel and Type I report, if `csi_ml_encoder_socket_path` is set, call the bridge with the same H pointer/dimensions you use for recording; take the returned latent and pass it to the feedback path (or a parallel path that the MAC treats as “CSI payload”).  
   - **gNB_scheduler_uci.c**: After decoding the CSI payload, if it is marked as “CSINet” (e.g. by report config) and `csi_ml_decoder_socket_path` is set, call the bridge with the decoded bits; use the returned H/precoder for precoding instead of Type I PMI.

5. **Fallback**  
   - If the bridge returns error or timeout, use the existing Type I report (UE sends Type I; gNB uses Type I precoder). That keeps the stack working when the Python process is not running or is overloaded.

#### Summary

- **Recommended approach for “Python inference, real-time stack”**: run the encoder/decoder in a **separate Python process**, and add a **thin C bridge** (one new file + a few guarded calls in existing files) that does socket (or shared-memory) IPC with a timeout and fallback to Type I. No need to change the rest of the existing .c logic; the model stays entirely in Python while still driving the real-time CSI feedback and precoding path.

---

## 4. Summary

- **Yes, you can explore CSINet** with the current **Type I single-panel** setup: the existing CSI recording gives you **raw H** and **Type I labels** in a well-defined format.
- **Offline**: Use `--csi-record-path`, parse `H_*.bin` and `csi_reports.csv`, train/evaluate CSINet in Python (or elsewhere); use Type I as baseline. No OAI code change.
- **Online**: Add an encoder in `csi_rx.c` and a decoder in the gNB UCI/scheduling path; decide how to carry the latent (PUCCH vs PUSCH) and how to switch between Type I and CSINet (report config or flag).
- **Online with Python inference (recommended)**: Keep the model in **Python** and run it in a **separate process** (sidecar). Add a **thin C bridge** (one new file) that does socket IPC to the Python encoder/decoder; in existing `.c` files add only a few guarded calls and a fallback to Type I on timeout. No Python embedded in OAI; real-time stack still uses the model output.

For recording setup and file formats, see **doc/CSI_RECORD_MODIFICATIONS.md** and **doc/5G_CHANNELS_IMPLEMENTATION_AND_TRACING_GUIDE.md**.
