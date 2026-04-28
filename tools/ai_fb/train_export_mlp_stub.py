#!/usr/bin/env python3
"""
Train and export AI-FB MLP-stub weights from UE CSI recordings.

Input dataset (already produced by OAI):
  --csi-record-path <dir>
    - csi_reports.csv
    - csi_rs_channels/H_*.bin

This script:
  1) loads H tensors from H_*.bin
  2) extracts dominant rank-1 beam vector v (same idea as runtime)
  3) builds x=[Re(v), Im(v)]
  4) trains a small autoencoder MLP for 4-port and/or 2-port subsets
  5) exports C header compatible with ai_fb_mlp_weights.h symbols

Usage:
  python3 tools/ai_fb/train_export_mlp_stub.py \
    --dataset-dir ./csi_ml_data \
    --output-header openair2/LAYER2/NR_MAC_COMMON/ai_fb_mlp_weights.h
"""

from __future__ import annotations

import argparse
import csv
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Tuple

import numpy as np

try:
    import torch
    import torch.nn as nn
except Exception as e:  # pragma: no cover
    raise SystemExit(f"PyTorch is required for training. Install torch first. Details: {e}")


@dataclass
class Sample:
    n_rx: int
    n_ports: int
    n_sc: int
    x: np.ndarray  # [2*n_ports]
    x_ang: np.ndarray  # [2*24*4] angular-delay feature tensor flattened (ports<4 are zero-padded)


def _read_h_bin(path: Path) -> Tuple[int, int, int, np.ndarray]:
    with path.open("rb") as f:
        header = f.read(4 * 5 + 8)
        if len(header) != 28:
            raise ValueError(f"Invalid header in {path}")
        i0, i1, i2, i3, i4, ts = struct.unpack("<iiiiiQ", header)
        payload = f.read()

    # OAI writer stores header as: frame, slot, nr_rx, n_ports, n_subc, ts
    # Keep fallback to older interpretation for compatibility.
    cand = [
        ("frame_slot_first", i2, i3, i4),
        ("nrx_first_legacy", i0, i1, i2),
    ]

    parsed = None
    for _, n_rx, n_ports, n_sc in cand:
        if n_rx <= 0 or n_ports <= 0 or n_sc <= 0:
            continue
        expected_i16 = n_rx * n_ports * n_sc * 2 * np.dtype(np.int16).itemsize
        expected_i32 = n_rx * n_ports * n_sc * 2 * np.dtype(np.int32).itemsize
        if len(payload) == expected_i16:
            parsed = (n_rx, n_ports, n_sc, np.frombuffer(payload, dtype=np.int16).reshape(n_rx, n_ports, n_sc, 2).astype(np.float32))
            break
        if len(payload) == expected_i32:
            parsed = (n_rx, n_ports, n_sc, np.frombuffer(payload, dtype=np.int32).reshape(n_rx, n_ports, n_sc, 2).astype(np.float32))
            break

    if parsed is None:
        raise ValueError(
            f"Unexpected payload size in {path} (payload={len(payload)} bytes, "
            f"header ints={[i0,i1,i2,i3,i4]}, ts={ts})"
        )

    n_rx, n_ports, n_sc, raw = parsed
    h = raw[..., 0] + 1j * raw[..., 1]
    return n_rx, n_ports, n_sc, h


def _dominant_v(h: np.ndarray) -> np.ndarray:
    # h shape: [n_rx, n_ports, n_sc]
    n_rx, n_ports, n_sc = h.shape
    gram = np.zeros((n_ports, n_ports), dtype=np.complex64)
    for rx in range(n_rx):
        hs = h[rx]  # [n_ports, n_sc]
        gram += hs @ hs.conj().T
    eigvals, eigvecs = np.linalg.eigh(gram)
    v = eigvecs[:, np.argmax(eigvals)].astype(np.complex64)
    # phase fix
    phi = np.angle(v[0])
    v = v * np.exp(-1j * phi)
    if np.real(v[0]) < 0:
        v = -v
    nrm = np.linalg.norm(v) + 1e-12
    v = v / nrm
    return v


def _angular_delay_features(h: np.ndarray, keep_delay_rows: int = 24) -> np.ndarray:
    # h shape: [n_rx, n_ports, n_sc]
    n_rx, n_ports, n_sc = h.shape
    # Reduce RX diversity into one effective channel per TX port for transport-size stability.
    h_eff = np.mean(h, axis=0)  # [n_ports, n_sc]
    h_eff = np.fft.fft(h_eff, axis=0, norm="ortho")
    h_eff = np.fft.ifft(h_eff, axis=1, norm="ortho")
    rows = min(keep_delay_rows, h_eff.shape[1])
    ad = h_eff[:, :rows]  # [n_ports, rows]
    out = np.zeros((4, keep_delay_rows), dtype=np.complex64)
    out[: min(4, n_ports), :rows] = ad[: min(4, n_ports), :rows]
    feat = np.concatenate([np.real(out).reshape(-1), np.imag(out).reshape(-1)]).astype(np.float32)
    return feat


def _normalize_angular_feature(feat: np.ndarray) -> np.ndarray:
    max_abs = float(np.max(np.abs(feat))) if feat.size > 0 else 0.0
    if max_abs < 1e-9:
        return feat.astype(np.float32, copy=True)
    return (feat / max_abs).astype(np.float32, copy=False)


def load_samples(dataset_dir: Path) -> List[Sample]:
    csv_path = dataset_dir / "csi_reports.csv"
    if not csv_path.exists():
        raise FileNotFoundError(f"Missing {csv_path}")
    out: List[Sample] = []
    with csv_path.open() as f:
        reader = csv.DictReader(f)
        for row in reader:
            h_path = row.get("H_bin_path", "")
            if not h_path:
                continue
            p = Path(h_path)
            if not p.exists():
                continue
            n_rx, n_ports, n_sc, h = _read_h_bin(p)
            if n_ports not in (2, 4):
                continue
            v = _dominant_v(h)
            x = np.concatenate([np.real(v), np.imag(v)]).astype(np.float32)
            x_ang = _normalize_angular_feature(_angular_delay_features(h, keep_delay_rows=24))
            out.append(Sample(n_rx=n_rx, n_ports=n_ports, n_sc=n_sc, x=x, x_ang=x_ang))
    return out


class AE(nn.Module):
    def __init__(self, in_dim: int, hid_dim: int, lat_dim: int):
        super().__init__()
        self.enc1 = nn.Linear(in_dim, hid_dim)
        self.enc2 = nn.Linear(hid_dim, lat_dim)
        self.dec1 = nn.Linear(lat_dim, hid_dim)
        self.dec2 = nn.Linear(hid_dim, in_dim)
        self.act = nn.Tanh()

    def encode(self, x):
        return self.enc2(self.act(self.enc1(x)))

    def decode(self, z):
        return self.dec2(self.act(self.dec1(z)))

    def forward(self, x):
        z = self.encode(x)
        xr = self.decode(z)
        return xr, z


class RefineBlock(nn.Module):
    def __init__(self, ch: int):
        super().__init__()
        self.conv1 = nn.Conv2d(ch, ch, kernel_size=3, padding=1)
        self.bn1 = nn.BatchNorm2d(ch)
        self.conv2 = nn.Conv2d(ch, ch, kernel_size=3, padding=1)
        self.bn2 = nn.BatchNorm2d(ch)
        self.act = nn.LeakyReLU(0.1)

    def forward(self, x):
        y = self.act(self.bn1(self.conv1(x)))
        y = self.bn2(self.conv2(y))
        return self.act(x + y)


class AngularRefineAE(nn.Module):
    def __init__(self, latent_dim: int = 6, channels: int = 8, rows: int = 24, ports: int = 4):
        super().__init__()
        self.rows = rows
        self.ports = ports
        self.enc_conv = nn.Conv2d(2, channels, kernel_size=3, padding=1)
        self.enc_bn = nn.BatchNorm2d(channels)
        self.enc_act = nn.LeakyReLU(0.1)
        self.enc_fc = nn.Linear(channels * rows * ports, latent_dim)
        self.dec_fc = nn.Linear(latent_dim, channels * rows * ports)
        self.ref1 = RefineBlock(channels)
        self.ref2 = RefineBlock(channels)
        self.dec_out = nn.Conv2d(channels, 2, kernel_size=3, padding=1)

    def encode(self, x):
        h = x.view(-1, 2, self.rows, self.ports)
        h = self.enc_act(self.enc_bn(self.enc_conv(h)))
        h = h.view(h.shape[0], -1)
        return self.enc_fc(h)

    def decode(self, z):
        h = self.dec_fc(z).view(-1, 8, self.rows, self.ports)
        h = self.ref1(h)
        h = self.ref2(h)
        y = torch.tanh(self.dec_out(h))
        return y.view(y.shape[0], -1)

    def forward(self, x):
        z = self.encode(x)
        xr = self.decode(z)
        return xr, z


def _split_train_val(x: np.ndarray, val_ratio: float, seed: int) -> Tuple[np.ndarray, np.ndarray]:
    rng = np.random.default_rng(seed)
    idx = np.arange(x.shape[0])
    rng.shuffle(idx)
    n_val = max(1, int(x.shape[0] * val_ratio)) if x.shape[0] > 4 else 0
    if n_val == 0:
        return x, x
    return x[idx[n_val:]], x[idx[:n_val]]


def _ae_loss(xr: torch.Tensor, x: torch.Tensor, z: torch.Tensor) -> Tuple[torch.Tensor, Dict[str, float]]:
    mse = torch.mean((xr - x) ** 2)
    cos = 1.0 - torch.mean(torch.nn.functional.cosine_similarity(xr, x, dim=1))
    # Mild latent regularization keeps exported scales stable for quantization.
    lat = torch.mean(z ** 2)
    loss = mse + 0.1 * cos + 1e-4 * lat
    return loss, {"mse": float(mse.item()), "cos": float(cos.item()), "lat": float(lat.item())}


def train_ae(x: np.ndarray, in_dim: int, hid_dim: int, epochs: int, lr: float, val_ratio: float, seed: int, tag: str) -> AE:
    dev = torch.device("cpu")
    m = AE(in_dim=in_dim, hid_dim=hid_dim, lat_dim=6).to(dev)
    x_train, x_val = _split_train_val(x, val_ratio=val_ratio, seed=seed)
    t = torch.from_numpy(x_train).to(dev)
    tv = torch.from_numpy(x_val).to(dev)
    opt = torch.optim.Adam(m.parameters(), lr=lr)
    best = 1e30
    bad_epochs = 0
    for e in range(epochs):
        xr, _ = m(t)
        z = m.encode(t)
        loss, tr = _ae_loss(xr, t, z)
        opt.zero_grad()
        loss.backward()
        opt.step()
        with torch.no_grad():
            xrv, zv = m(tv)
            vloss, va = _ae_loss(xrv, tv, zv)
        if vloss.item() < best - 1e-8:
            best = vloss.item()
            bad_epochs = 0
        else:
            bad_epochs += 1
        if (e + 1) % 20 == 0 or e == 0 or e == epochs - 1:
            print(
                f"[{tag}] epoch={e+1}/{epochs} "
                f"train_loss={loss.item():.6f} (mse={tr['mse']:.6f},cos={tr['cos']:.6f}) "
                f"val_loss={vloss.item():.6f} (mse={va['mse']:.6f},cos={va['cos']:.6f})"
            )
        if bad_epochs >= 50:
            print(f"[{tag}] early-stop at epoch {e+1} (best_val={best:.6f})")
            break
    return m


def train_refine_ae(x: np.ndarray, epochs: int, lr: float, val_ratio: float, seed: int, tag: str) -> AngularRefineAE:
    dev = torch.device("cpu")
    torch.manual_seed(seed)
    m = AngularRefineAE().to(dev)
    x_train, x_val = _split_train_val(x, val_ratio=val_ratio, seed=seed)
    t = torch.from_numpy(x_train).to(dev)
    tv = torch.from_numpy(x_val).to(dev)
    opt = torch.optim.Adam(m.parameters(), lr=lr)
    for e in range(epochs):
        xr, z = m(t)
        loss, tr = _ae_loss(xr, t, z)
        opt.zero_grad()
        loss.backward()
        opt.step()
        if (e + 1) % 20 == 0 or e == 0 or e == epochs - 1:
            with torch.no_grad():
                xrv, zv = m(tv)
                vloss, va = _ae_loss(xrv, tv, zv)
            print(f"[{tag}] epoch={e+1}/{epochs} train_loss={loss.item():.6f} val_loss={vloss.item():.6f} "
                  f"(train_mse={tr['mse']:.6f}, val_mse={va['mse']:.6f})")
    return m


def _arr2d(name: str, a: np.ndarray) -> str:
    r, c = a.shape
    lines = [f"static const float {name}[{r}][{c}] = {{"]
    for i in range(r):
        vals = ", ".join(f"{a[i, j]:.8f}f" for j in range(c))
        lines.append(f"    {{{vals}}},")
    lines.append("};")
    return "\n".join(lines)


def _arr1d(name: str, a: np.ndarray) -> str:
    vals = ", ".join(f"{v:.8f}f" for v in a.reshape(-1))
    return f"static const float {name}[{a.size}] = {{{vals}}};"


def export_header(out_path: Path, model4: AE | None, model2: AE | None, hidden4: int, hidden2: int):
    def get_or_default(m, layer, shape):
        if m is None:
            return np.zeros(shape, dtype=np.float32)
        w = getattr(m, layer).weight.detach().cpu().numpy().astype(np.float32)
        return w

    def getb_or_default(m, layer, n):
        if m is None:
            return np.zeros((n,), dtype=np.float32)
        b = getattr(m, layer).bias.detach().cpu().numpy().astype(np.float32)
        return b

    w = []
    w.append("#ifndef AI_FB_MLP_WEIGHTS_H")
    w.append("#define AI_FB_MLP_WEIGHTS_H")
    w.append("")
    w.append("#define AI_FB_MLP_ENC4_IN 8")
    w.append(f"#define AI_FB_MLP_ENC4_H {hidden4}")
    w.append("#define AI_FB_MLP_ENC4_OUT 6")
    w.append("#define AI_FB_MLP_DEC4_IN 6")
    w.append(f"#define AI_FB_MLP_DEC4_H {hidden4}")
    w.append("#define AI_FB_MLP_DEC4_OUT 8")
    w.append("#define AI_FB_MLP_ENC2_IN 4")
    w.append(f"#define AI_FB_MLP_ENC2_H {hidden2}")
    w.append("#define AI_FB_MLP_ENC2_OUT 6")
    w.append("#define AI_FB_MLP_DEC2_IN 6")
    w.append(f"#define AI_FB_MLP_DEC2_H {hidden2}")
    w.append("#define AI_FB_MLP_DEC2_OUT 4")
    w.append("")

    if model4 is not None:
        w.append(_arr2d("ai_fb_mlp_enc4_w1", model4.enc1.weight.detach().cpu().numpy()))
        w.append(_arr1d("ai_fb_mlp_enc4_b1", model4.enc1.bias.detach().cpu().numpy()))
        w.append(_arr2d("ai_fb_mlp_enc4_w2", model4.enc2.weight.detach().cpu().numpy()))
        w.append(_arr1d("ai_fb_mlp_enc4_b2", model4.enc2.bias.detach().cpu().numpy()))
        w.append(_arr2d("ai_fb_mlp_dec4_w1", model4.dec1.weight.detach().cpu().numpy()))
        w.append(_arr1d("ai_fb_mlp_dec4_b1", model4.dec1.bias.detach().cpu().numpy()))
        w.append(_arr2d("ai_fb_mlp_dec4_w2", model4.dec2.weight.detach().cpu().numpy()))
        w.append(_arr1d("ai_fb_mlp_dec4_b2", model4.dec2.bias.detach().cpu().numpy()))
    else:
        # Keep zero placeholders if no 4-port samples were present.
        w.append(_arr2d("ai_fb_mlp_enc4_w1", np.zeros((hidden4, 8), dtype=np.float32)))
        w.append(_arr1d("ai_fb_mlp_enc4_b1", np.zeros((hidden4,), dtype=np.float32)))
        w.append(_arr2d("ai_fb_mlp_enc4_w2", np.zeros((6, hidden4), dtype=np.float32)))
        w.append(_arr1d("ai_fb_mlp_enc4_b2", np.zeros((6,), dtype=np.float32)))
        w.append(_arr2d("ai_fb_mlp_dec4_w1", np.zeros((hidden4, 6), dtype=np.float32)))
        w.append(_arr1d("ai_fb_mlp_dec4_b1", np.zeros((hidden4,), dtype=np.float32)))
        w.append(_arr2d("ai_fb_mlp_dec4_w2", np.zeros((8, hidden4), dtype=np.float32)))
        w.append(_arr1d("ai_fb_mlp_dec4_b2", np.zeros((8,), dtype=np.float32)))

    if model2 is not None:
        w.append(_arr2d("ai_fb_mlp_enc2_w1", model2.enc1.weight.detach().cpu().numpy()))
        w.append(_arr1d("ai_fb_mlp_enc2_b1", model2.enc1.bias.detach().cpu().numpy()))
        w.append(_arr2d("ai_fb_mlp_enc2_w2", model2.enc2.weight.detach().cpu().numpy()))
        w.append(_arr1d("ai_fb_mlp_enc2_b2", model2.enc2.bias.detach().cpu().numpy()))
        w.append(_arr2d("ai_fb_mlp_dec2_w1", model2.dec1.weight.detach().cpu().numpy()))
        w.append(_arr1d("ai_fb_mlp_dec2_b1", model2.dec1.bias.detach().cpu().numpy()))
        w.append(_arr2d("ai_fb_mlp_dec2_w2", model2.dec2.weight.detach().cpu().numpy()))
        w.append(_arr1d("ai_fb_mlp_dec2_b2", model2.dec2.bias.detach().cpu().numpy()))
    else:
        w.append(_arr2d("ai_fb_mlp_enc2_w1", np.zeros((hidden2, 4), dtype=np.float32)))
        w.append(_arr1d("ai_fb_mlp_enc2_b1", np.zeros((hidden2,), dtype=np.float32)))
        w.append(_arr2d("ai_fb_mlp_enc2_w2", np.zeros((6, hidden2), dtype=np.float32)))
        w.append(_arr1d("ai_fb_mlp_enc2_b2", np.zeros((6,), dtype=np.float32)))
        w.append(_arr2d("ai_fb_mlp_dec2_w1", np.zeros((hidden2, 6), dtype=np.float32)))
        w.append(_arr1d("ai_fb_mlp_dec2_b1", np.zeros((hidden2,), dtype=np.float32)))
        w.append(_arr2d("ai_fb_mlp_dec2_w2", np.zeros((4, hidden2), dtype=np.float32)))
        w.append(_arr1d("ai_fb_mlp_dec2_b2", np.zeros((4,), dtype=np.float32)))

    w.append("")
    w.append("#endif /* AI_FB_MLP_WEIGHTS_H */")
    out_path.write_text("\n".join(w) + "\n")


def export_model_txt(out_path: Path, model4: AE | None, model2: AE | None, hidden4: int, hidden2: int):
    def arr(m, layer, shape):
        if m is None:
            return np.zeros(shape, dtype=np.float32)
        return getattr(m, layer).weight.detach().cpu().numpy().astype(np.float32)

    def vec(m, layer, n):
        if m is None:
            return np.zeros((n,), dtype=np.float32)
        return getattr(m, layer).bias.detach().cpu().numpy().astype(np.float32)

    tensors = {
        "enc4_w1": arr(model4, "enc1", (hidden4, 8)),
        "enc4_b1": vec(model4, "enc1", hidden4),
        "enc4_w2": arr(model4, "enc2", (6, hidden4)),
        "enc4_b2": vec(model4, "enc2", 6),
        "dec4_w1": arr(model4, "dec1", (hidden4, 6)),
        "dec4_b1": vec(model4, "dec1", hidden4),
        "dec4_w2": arr(model4, "dec2", (8, hidden4)),
        "dec4_b2": vec(model4, "dec2", 8),
        "enc2_w1": arr(model2, "enc1", (hidden2, 4)),
        "enc2_b1": vec(model2, "enc1", hidden2),
        "enc2_w2": arr(model2, "enc2", (6, hidden2)),
        "enc2_b2": vec(model2, "enc2", 6),
        "dec2_w1": arr(model2, "dec1", (hidden2, 6)),
        "dec2_b1": vec(model2, "dec1", hidden2),
        "dec2_w2": arr(model2, "dec2", (4, hidden2)),
        "dec2_b2": vec(model2, "dec2", 4),
    }

    lines = ["AI_FB_MODEL_TXT_V1"]
    for k, v in tensors.items():
        flat = v.reshape(-1)
        vals = " ".join(f"{x:.9g}" for x in flat.tolist())
        lines.append(f"{k} {flat.size} {vals}")
    out_path.write_text("\n".join(lines) + "\n")


def export_model_bin(out_path: Path, model4: AE | None, model2: AE | None, hidden4: int, hidden2: int):
    def arr(m, layer, shape):
        if m is None:
            return np.zeros(shape, dtype=np.float32)
        return getattr(m, layer).weight.detach().cpu().numpy().astype(np.float32)

    def vec(m, layer, n):
        if m is None:
            return np.zeros((n,), dtype=np.float32)
        return getattr(m, layer).bias.detach().cpu().numpy().astype(np.float32)

    tensors = {
        "enc4_w1": arr(model4, "enc1", (hidden4, 8)),
        "enc4_b1": vec(model4, "enc1", hidden4),
        "enc4_w2": arr(model4, "enc2", (6, hidden4)),
        "enc4_b2": vec(model4, "enc2", 6),
        "dec4_w1": arr(model4, "dec1", (hidden4, 6)),
        "dec4_b1": vec(model4, "dec1", hidden4),
        "dec4_w2": arr(model4, "dec2", (8, hidden4)),
        "dec4_b2": vec(model4, "dec2", 8),
        "enc2_w1": arr(model2, "enc1", (hidden2, 4)),
        "enc2_b1": vec(model2, "enc1", hidden2),
        "enc2_w2": arr(model2, "enc2", (6, hidden2)),
        "enc2_b2": vec(model2, "enc2", 6),
        "dec2_w1": arr(model2, "dec1", (hidden2, 6)),
        "dec2_b1": vec(model2, "dec1", hidden2),
        "dec2_w2": arr(model2, "dec2", (4, hidden2)),
        "dec2_b2": vec(model2, "dec2", 4),
    }

    with out_path.open("wb") as f:
        # magic='AFBM' little-endian 0x4D424641, version=1
        f.write(struct.pack("<III", 0x4D424641, 1, len(tensors)))
        for name, v in tensors.items():
            flat = v.reshape(-1).astype(np.float32)
            name_b = name.encode("ascii")
            f.write(struct.pack("<II", len(name_b), flat.size))
            f.write(name_b)
            f.write(flat.tobytes(order="C"))


def export_angular_model_txt(out_path: Path, model: AE, hidden_ang: int):
    tensors = {
        "encad_w1": model.enc1.weight.detach().cpu().numpy().astype(np.float32),
        "encad_b1": model.enc1.bias.detach().cpu().numpy().astype(np.float32),
        "encad_w2": model.enc2.weight.detach().cpu().numpy().astype(np.float32),
        "encad_b2": model.enc2.bias.detach().cpu().numpy().astype(np.float32),
        "decad_w1": model.dec1.weight.detach().cpu().numpy().astype(np.float32),
        "decad_b1": model.dec1.bias.detach().cpu().numpy().astype(np.float32),
        "decad_w2": model.dec2.weight.detach().cpu().numpy().astype(np.float32),
        "decad_b2": model.dec2.bias.detach().cpu().numpy().astype(np.float32),
    }
    lines = ["AI_FB_MODEL_TXT_V2_ANGULAR_DELAY", f"encad_hidden {hidden_ang}"]
    for k, v in tensors.items():
        flat = v.reshape(-1)
        vals = " ".join(f"{x:.9g}" for x in flat.tolist())
        lines.append(f"{k} {flat.size} {vals}")
    out_path.write_text("\n".join(lines) + "\n")


def export_angular_model_bin(out_path: Path, model: AE):
    tensors = {
        "encad_w1": model.enc1.weight.detach().cpu().numpy().astype(np.float32),
        "encad_b1": model.enc1.bias.detach().cpu().numpy().astype(np.float32),
        "encad_w2": model.enc2.weight.detach().cpu().numpy().astype(np.float32),
        "encad_b2": model.enc2.bias.detach().cpu().numpy().astype(np.float32),
        "decad_w1": model.dec1.weight.detach().cpu().numpy().astype(np.float32),
        "decad_b1": model.dec1.bias.detach().cpu().numpy().astype(np.float32),
        "decad_w2": model.dec2.weight.detach().cpu().numpy().astype(np.float32),
        "decad_b2": model.dec2.bias.detach().cpu().numpy().astype(np.float32),
    }
    with out_path.open("wb") as f:
        f.write(struct.pack("<III", 0x4D424641, 2, len(tensors)))
        for name, v in tensors.items():
            flat = v.reshape(-1).astype(np.float32)
            name_b = name.encode("ascii")
            f.write(struct.pack("<II", len(name_b), flat.size))
            f.write(name_b)
            f.write(flat.tobytes(order="C"))


def export_refinenet_model_txt(out_path: Path, model: AngularRefineAE):
    tensors = {
        "rn_enc_conv_w": model.enc_conv.weight.detach().cpu().numpy().astype(np.float32),
        "rn_enc_conv_b": model.enc_conv.bias.detach().cpu().numpy().astype(np.float32),
        "rn_enc_bn_g": model.enc_bn.weight.detach().cpu().numpy().astype(np.float32),
        "rn_enc_bn_b": model.enc_bn.bias.detach().cpu().numpy().astype(np.float32),
        "rn_enc_bn_m": model.enc_bn.running_mean.detach().cpu().numpy().astype(np.float32),
        "rn_enc_bn_v": model.enc_bn.running_var.detach().cpu().numpy().astype(np.float32),
        "rn_enc_fc_w": model.enc_fc.weight.detach().cpu().numpy().astype(np.float32),
        "rn_enc_fc_b": model.enc_fc.bias.detach().cpu().numpy().astype(np.float32),
        "rn_dec_fc_w": model.dec_fc.weight.detach().cpu().numpy().astype(np.float32),
        "rn_dec_fc_b": model.dec_fc.bias.detach().cpu().numpy().astype(np.float32),
        "rn_ref1_conv1_w": model.ref1.conv1.weight.detach().cpu().numpy().astype(np.float32),
        "rn_ref1_conv1_b": model.ref1.conv1.bias.detach().cpu().numpy().astype(np.float32),
        "rn_ref1_bn1_g": model.ref1.bn1.weight.detach().cpu().numpy().astype(np.float32),
        "rn_ref1_bn1_b": model.ref1.bn1.bias.detach().cpu().numpy().astype(np.float32),
        "rn_ref1_bn1_m": model.ref1.bn1.running_mean.detach().cpu().numpy().astype(np.float32),
        "rn_ref1_bn1_v": model.ref1.bn1.running_var.detach().cpu().numpy().astype(np.float32),
        "rn_ref1_conv2_w": model.ref1.conv2.weight.detach().cpu().numpy().astype(np.float32),
        "rn_ref1_conv2_b": model.ref1.conv2.bias.detach().cpu().numpy().astype(np.float32),
        "rn_ref1_bn2_g": model.ref1.bn2.weight.detach().cpu().numpy().astype(np.float32),
        "rn_ref1_bn2_b": model.ref1.bn2.bias.detach().cpu().numpy().astype(np.float32),
        "rn_ref1_bn2_m": model.ref1.bn2.running_mean.detach().cpu().numpy().astype(np.float32),
        "rn_ref1_bn2_v": model.ref1.bn2.running_var.detach().cpu().numpy().astype(np.float32),
        "rn_ref2_conv1_w": model.ref2.conv1.weight.detach().cpu().numpy().astype(np.float32),
        "rn_ref2_conv1_b": model.ref2.conv1.bias.detach().cpu().numpy().astype(np.float32),
        "rn_ref2_bn1_g": model.ref2.bn1.weight.detach().cpu().numpy().astype(np.float32),
        "rn_ref2_bn1_b": model.ref2.bn1.bias.detach().cpu().numpy().astype(np.float32),
        "rn_ref2_bn1_m": model.ref2.bn1.running_mean.detach().cpu().numpy().astype(np.float32),
        "rn_ref2_bn1_v": model.ref2.bn1.running_var.detach().cpu().numpy().astype(np.float32),
        "rn_ref2_conv2_w": model.ref2.conv2.weight.detach().cpu().numpy().astype(np.float32),
        "rn_ref2_conv2_b": model.ref2.conv2.bias.detach().cpu().numpy().astype(np.float32),
        "rn_ref2_bn2_g": model.ref2.bn2.weight.detach().cpu().numpy().astype(np.float32),
        "rn_ref2_bn2_b": model.ref2.bn2.bias.detach().cpu().numpy().astype(np.float32),
        "rn_ref2_bn2_m": model.ref2.bn2.running_mean.detach().cpu().numpy().astype(np.float32),
        "rn_ref2_bn2_v": model.ref2.bn2.running_var.detach().cpu().numpy().astype(np.float32),
        "rn_dec_out_w": model.dec_out.weight.detach().cpu().numpy().astype(np.float32),
        "rn_dec_out_b": model.dec_out.bias.detach().cpu().numpy().astype(np.float32),
    }
    lines = ["AI_FB_MODEL_TXT_V3_ANGULAR_REFINENET"]
    for k, v in tensors.items():
        flat = v.reshape(-1)
        vals = " ".join(f"{x:.9g}" for x in flat.tolist())
        lines.append(f"{k} {flat.size} {vals}")
    out_path.write_text("\n".join(lines) + "\n")


def export_refinenet_model_bin(out_path: Path, model: AngularRefineAE):
    txt_path = out_path.with_suffix(".tmp.txt")
    export_refinenet_model_txt(txt_path, model)
    lines = txt_path.read_text().splitlines()[1:]
    with out_path.open("wb") as f:
        f.write(struct.pack("<III", 0x4D424641, 3, len(lines)))
        for ln in lines:
            parts = ln.split()
            name = parts[0]
            cnt = int(parts[1])
            vals = np.asarray([float(x) for x in parts[2:]], dtype=np.float32)
            assert vals.size == cnt
            name_b = name.encode("ascii")
            f.write(struct.pack("<II", len(name_b), cnt))
            f.write(name_b)
            f.write(vals.tobytes(order="C"))
    txt_path.unlink(missing_ok=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dataset-dir", required=True, type=Path)
    ap.add_argument("--output-header", required=True, type=Path)
    ap.add_argument("--output-model-txt", type=Path, default=None)
    ap.add_argument("--output-model-bin", type=Path, default=None)
    ap.add_argument("--output-onnx-enc", type=Path, default=None)
    ap.add_argument("--output-onnx-dec", type=Path, default=None)
    ap.add_argument("--epochs", type=int, default=2000)
    ap.add_argument("--lr", type=float, default=1e-3)
    ap.add_argument("--hidden4", type=int, default=8)
    ap.add_argument("--hidden2", type=int, default=8)
    ap.add_argument("--hidden-ang", type=int, default=24)
    ap.add_argument("--val-ratio", type=float, default=0.1)
    ap.add_argument("--seed", type=int, default=7)
    args = ap.parse_args()

    samples = load_samples(args.dataset_dir)
    x4 = np.stack([s.x for s in samples if s.n_ports == 4], axis=0) if any(s.n_ports == 4 for s in samples) else None
    x2 = np.stack([s.x for s in samples if s.n_ports == 2], axis=0) if any(s.n_ports == 2 for s in samples) else None

    m4 = train_ae(x4, in_dim=8, hid_dim=args.hidden4, epochs=args.epochs, lr=args.lr, val_ratio=args.val_ratio, seed=args.seed, tag="4-port") if x4 is not None else None
    m2 = train_ae(x2, in_dim=4, hid_dim=args.hidden2, epochs=args.epochs, lr=args.lr, val_ratio=args.val_ratio, seed=args.seed, tag="2-port") if x2 is not None else None
    x_ang = np.stack([s.x_ang for s in samples], axis=0) if len(samples) > 0 else None
    m_ang = train_ae(x_ang,
                     in_dim=2 * 24 * 4,
                     hid_dim=args.hidden_ang,
                     epochs=args.epochs,
                     lr=args.lr,
                     val_ratio=args.val_ratio,
                     seed=args.seed,
                     tag="angular-delay") if x_ang is not None else None
    m_ref = train_refine_ae(x_ang,
                            epochs=args.epochs,
                            lr=args.lr,
                            val_ratio=args.val_ratio,
                            seed=args.seed,
                            tag="angular-delay-refinenet") if x_ang is not None else None

    args.output_header.parent.mkdir(parents=True, exist_ok=True)
    export_header(args.output_header, m4, m2, args.hidden4, args.hidden2)
    print(f"Exported weights to {args.output_header}")
    if args.output_model_txt is None:
        args.output_model_txt = args.output_header.with_suffix(".txt")
    args.output_model_txt.parent.mkdir(parents=True, exist_ok=True)
    export_model_txt(args.output_model_txt, m4, m2, args.hidden4, args.hidden2)
    print(f"Exported model text to {args.output_model_txt}")
    if args.output_model_bin is None:
        args.output_model_bin = args.output_header.with_suffix(".bin")
    args.output_model_bin.parent.mkdir(parents=True, exist_ok=True)
    export_model_bin(args.output_model_bin, m4, m2, args.hidden4, args.hidden2)
    print(f"Exported model binary to {args.output_model_bin}")
    if m_ang is not None:
      ang_txt = args.output_model_txt.with_name(args.output_model_txt.stem + "_angular.txt")
      ang_bin = args.output_model_bin.with_name(args.output_model_bin.stem + "_angular.bin")
      export_angular_model_txt(ang_txt, m_ang, args.hidden_ang)
      export_angular_model_bin(ang_bin, m_ang)
      print(f"Exported angular-delay model text to {ang_txt}")
      print(f"Exported angular-delay model binary to {ang_bin}")
    if m_ref is not None:
      ref_txt = args.output_model_txt.with_name(args.output_model_txt.stem + "_refinenet.txt")
      ref_bin = args.output_model_bin.with_name(args.output_model_bin.stem + "_refinenet.bin")
      export_refinenet_model_txt(ref_txt, m_ref)
      export_refinenet_model_bin(ref_bin, m_ref)
      print(f"Exported angular-delay RefineNet model text to {ref_txt}")
      print(f"Exported angular-delay RefineNet model binary to {ref_bin}")

    # Optional ONNX export (single encoder/decoder pair). Prefer 4-port model if available.
    sel = m4 if m4 is not None else m2
    sel_in_dim = 8 if m4 is not None else (4 if m2 is not None else 0)
    if sel is not None:
        if args.output_onnx_enc is None:
            args.output_onnx_enc = args.output_header.with_name(args.output_header.stem + "_encoder.onnx")
        if args.output_onnx_dec is None:
            args.output_onnx_dec = args.output_header.with_name(args.output_header.stem + "_decoder.onnx")
        args.output_onnx_enc.parent.mkdir(parents=True, exist_ok=True)
        args.output_onnx_dec.parent.mkdir(parents=True, exist_ok=True)

        class EncWrap(nn.Module):
            def __init__(self, m):
                super().__init__()
                self.m = m

            def forward(self, x):
                return self.m.encode(x)

        class DecWrap(nn.Module):
            def __init__(self, m):
                super().__init__()
                self.m = m

            def forward(self, z):
                return self.m.decode(z)

        enc = EncWrap(sel).eval()
        dec = DecWrap(sel).eval()
        x_dummy = torch.zeros((1, sel_in_dim), dtype=torch.float32)
        z_dummy = torch.zeros((1, 6), dtype=torch.float32)
        try:
            # Use legacy exporter path for broad compatibility; avoids requiring onnxscript.
            torch.onnx.export(
                enc,
                x_dummy,
                str(args.output_onnx_enc),
                input_names=["x"],
                output_names=["z"],
                opset_version=13,
                dynamo=False,
            )
            torch.onnx.export(
                dec,
                z_dummy,
                str(args.output_onnx_dec),
                input_names=["z"],
                output_names=["xhat"],
                opset_version=13,
                dynamo=False,
            )
            print(f"Exported ONNX encoder to {args.output_onnx_enc} (input_dim={sel_in_dim}, latent_dim=6)")
            print(f"Exported ONNX decoder to {args.output_onnx_dec} (latent_dim=6, output_dim={sel_in_dim})")
        except Exception as e:
            print(f"WARNING: ONNX export failed ({e}). Install optional deps: pip install onnx onnxscript")
    if x4 is not None:
        print(f"4-port samples: {x4.shape[0]}")
    if x2 is not None:
        print(f"2-port samples: {x2.shape[0]}")


if __name__ == "__main__":
    main()
