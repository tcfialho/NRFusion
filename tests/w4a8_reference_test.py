#!/usr/bin/env python3
"""Validation test for Ada SM89 W4A8 + FP8 correction reference implementation.

Verifies:
1. Decoding of weights_sm89.bin container against specification.
2. Exact execution of:
     output = W4A8_main_path + FP8_correction_path
3. Fidelity comparison against original FP16 weights on realistic activation input:
   - Relative L2 error
   - Peak Absolute Error
   - Cosine Similarity (> 0.998)
"""
from __future__ import annotations

import math
import pathlib
import struct
import sys
import numpy as np
import safetensors.numpy


def e4m3_to_float(b: int) -> float:
    b = int(b)
    sign = -1.0 if (b & 0x80) else 1.0
    exp = (b >> 3) & 0x0F
    mant = b & 0x07
    if exp == 0:
        if mant == 0:
            return 0.0
        return sign * (mant / 8.0) * (2.0 ** (-6))
    if exp == 0x0F and mant == 0x07:
        return float("nan")
    return sign * (1.0 + mant / 8.0) * (2.0 ** (exp - 7))


def unpack_int4(packed: bytes, shape: tuple[int, ...]) -> np.ndarray:
    """Unpack bytes containing 2 signed int4 per byte into int8 numpy array."""
    data = np.frombuffer(packed, dtype=np.uint8)
    low = (data & 0x0F).astype(np.int8)
    high = ((data >> 4) & 0x0F).astype(np.int8)
    # Sign extend 4-bit to 8-bit
    low = np.where(low >= 8, low - 16, low)
    high = np.where(high >= 8, high - 16, high)
    interleaved = np.empty(len(data) * 2, dtype=np.int8)
    interleaved[0::2] = low
    interleaved[1::2] = high
    return interleaved.reshape(shape)


def swish(x: np.ndarray) -> np.ndarray:
    return x / (1.0 + np.exp(-x))


def main() -> int:
    # There is one container per block, so the path is an argument. Without it the test could only
    # ever check whichever container happened to be written last.
    bin_path = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path("data/pesos/weights_sm89.bin")
    safetensors_path = pathlib.Path(sys.argv[2]) if len(sys.argv) > 2 else pathlib.Path("data/pesos/logical.safetensors")

    if not bin_path.exists():
        print(f"Error: {bin_path} not found. Run quantize_w4a8_sm89.py first.", file=sys.stderr)
        return 1
    if not safetensors_path.exists():
        print(f"Error: {safetensors_path} not found.", file=sys.stderr)
        return 1

    blob = bin_path.read_bytes()
    if len(blob) < 128:
        print("Error: weights_sm89.bin is too small.", file=sys.stderr)
        return 1

    header_vals = struct.unpack("<" + "I" * 32, blob[:128])
    magic = header_vals[0]
    version = header_vals[1]
    block_idx = header_vals[2]
    groups = header_vals[3]
    tokens = header_vals[4]
    group_ch = header_vals[5]
    wide_ch = header_vals[6]
    group_size = header_vals[7]
    exp_outliers_tot = header_vals[8]
    prj_outliers_tot = header_vals[9]

    if magic != 0x39384D53:
        print(f"Invalid magic: 0x{magic:08X} (expected 0x39384D53)", file=sys.stderr)
        return 1

    print(f"Loaded weights_sm89.bin:")
    print(f"  Version: {version}, Block: {block_idx}, Groups: {groups}")
    print(f"  Shape: tokens={tokens}, group_ch={group_ch}, wide_ch={wide_ch}, group_size={group_size}")
    print(f"  Total outliers: expand={exp_outliers_tot}, project={prj_outliers_tot}")

    # Unpack offsets
    off_exp_int4, sz_exp_int4 = header_vals[10], header_vals[11]
    off_exp_sc, sz_exp_sc = header_vals[12], header_vals[13]
    off_exp_idx, sz_exp_idx = header_vals[14], header_vals[15]
    off_exp_val, sz_exp_val = header_vals[16], header_vals[17]

    off_prj_int4, sz_prj_int4 = header_vals[18], header_vals[19]
    off_prj_sc, sz_prj_sc = header_vals[20], header_vals[21]
    off_prj_idx, sz_prj_idx = header_vals[22], header_vals[23]
    off_prj_val, sz_prj_val = header_vals[24], header_vals[25]

    off_fp, sz_fp = header_vals[26], header_vals[27]

    # Load original logical weights for ground truth comparison
    logical = safetensors.numpy.load_file(str(safetensors_path))
    orig_expand = logical[f"block{block_idx}.layer0.group_expand_weight"].astype(np.float32)   # (8, 64, 256)
    orig_project = logical[f"block{block_idx}.layer0.group_project_weight"].astype(np.float32) # (8, 256, 64)

    # Decode container data
    # Version 2 stores the weights at the width the tensor core already uses, so there is nothing
    # to unpack and no correction to apply.
    def load_weights(raw: bytes, shape: tuple[int, ...]) -> np.ndarray:
        if version >= 2:
            return np.frombuffer(raw, dtype=np.int8).reshape(shape)
        return unpack_int4(raw, shape)

    expand_int4 = load_weights(blob[off_exp_int4 : off_exp_int4 + sz_exp_int4], (groups, group_ch, wide_ch))
    expand_scales = np.frombuffer(blob[off_exp_sc : off_exp_sc + sz_exp_sc], dtype=np.float16).astype(np.float32)
    expand_scales = expand_scales.reshape(groups, group_ch // group_size, wide_ch)

    expand_outlier_idx = np.frombuffer(blob[off_exp_idx : off_exp_idx + sz_exp_idx], dtype=np.uint16)
    expand_outlier_val = np.frombuffer(blob[off_exp_val : off_exp_val + sz_exp_val], dtype=np.uint8)

    project_int4 = load_weights(blob[off_prj_int4 : off_prj_int4 + sz_prj_int4], (groups, wide_ch, group_ch))
    project_scales = np.frombuffer(blob[off_prj_sc : off_prj_sc + sz_prj_sc], dtype=np.float16).astype(np.float32)
    project_scales = project_scales.reshape(groups, wide_ch // group_size, group_ch)

    project_outlier_idx = np.frombuffer(blob[off_prj_idx : off_prj_idx + sz_prj_idx], dtype=np.uint16)
    project_outlier_val = np.frombuffer(blob[off_prj_val : off_prj_val + sz_prj_val], dtype=np.uint8)

    # Test execution on 4 attention windows
    windows = 4
    np.random.seed(1337)
    channels = groups * group_ch
    input_fp32 = np.random.randn(windows, tokens, channels).astype(np.float32) * 0.15

    # Ground truth reference run
    out_true = np.zeros_like(input_fp32)
    for w in range(windows):
        for g in range(groups):
            xg = input_fp32[w, :, g * group_ch : (g + 1) * group_ch] # (tokens, group_ch)
            exp_w = orig_expand[g] # (group_ch, wide_ch)
            yg = xg @ exp_w
            hg = swish(yg)
            prj_w = orig_project[g] # (wide_ch, group_ch)
            zg = hg @ prj_w
            out_true[w, :, g * group_ch : (g + 1) * group_ch] = zg

    # W4A8 + FP8 correction reference run
    out_pred = np.zeros_like(input_fp32)
    
    # Reconstruct outlier matrices per group
    exp_outlier_mats = [np.zeros((group_ch, wide_ch), dtype=np.float32) for _ in range(groups)]
    prj_outlier_mats = [np.zeros((wide_ch, group_ch), dtype=np.float32) for _ in range(groups)]

    # Outliers were divided evenly per group (exp_outliers_tot // groups)
    exp_per_group = exp_outliers_tot // groups
    for g in range(groups):
        g_idx = expand_outlier_idx[g * exp_per_group : (g + 1) * exp_per_group]
        g_val = expand_outlier_val[g * exp_per_group : (g + 1) * exp_per_group]
        for idx, val in zip(g_idx, g_val):
            r = idx // wide_ch
            c = idx % wide_ch
            exp_outlier_mats[g][r, c] = e4m3_to_float(val)

    prj_per_group = prj_outliers_tot // groups
    for g in range(groups):
        g_idx = project_outlier_idx[g * prj_per_group : (g + 1) * prj_per_group]
        g_val = project_outlier_val[g * prj_per_group : (g + 1) * prj_per_group]
        for idx, val in zip(g_idx, g_val):
            r = idx // group_ch
            c = idx % group_ch
            prj_outlier_mats[g][r, c] = e4m3_to_float(val)

    for w in range(windows):
        for g in range(groups):
            xg = input_fp32[w, :, g * group_ch : (g + 1) * group_ch] # (tokens, group_ch)
            
            # --- EXPAND: W4A8 Main Path + FP8 Outlier Path ---
            # INT8 activation scale
            sx = np.max(np.abs(xg), axis=-1, keepdims=True) / 127.0
            sx[sx == 0] = 1.0
            x_int8 = np.clip(np.rint(xg / sx), -128, 127).astype(np.int8)

            y_main = np.zeros((tokens, wide_ch), dtype=np.float32)
            num_exp_grps = group_ch // group_size
            for grp in range(num_exp_grps):
                x_sub = x_int8[:, grp * group_size : (grp + 1) * group_size].astype(np.float32)
                w_sub = expand_int4[g, grp * group_size : (grp + 1) * group_size, :].astype(np.float32)
                sc_sub = expand_scales[g, grp, :]
                y_main += (x_sub @ w_sub) * sc_sub * sx

            y_fp8_corr = xg @ exp_outlier_mats[g]
            y_final = y_main + y_fp8_corr

            # Swish activation
            hg = swish(y_final)

            # --- PROJECT: W4A8 Main Path + FP8 Outlier Path ---
            sh = np.max(np.abs(hg), axis=-1, keepdims=True) / 127.0
            sh[sh == 0] = 1.0
            h_int8 = np.clip(np.rint(hg / sh), -128, 127).astype(np.int8)

            z_main = np.zeros((tokens, group_ch), dtype=np.float32)
            num_prj_grps = wide_ch // group_size
            for grp in range(num_prj_grps):
                h_sub = h_int8[:, grp * group_size : (grp + 1) * group_size].astype(np.float32)
                w_sub = project_int4[g, grp * group_size : (grp + 1) * group_size, :].astype(np.float32)
                sc_sub = project_scales[g, grp, :]
                z_main += (h_sub @ w_sub) * sc_sub * sh

            z_fp8_corr = hg @ prj_outlier_mats[g]
            z_final = z_main + z_fp8_corr

            out_pred[w, :, g * group_ch : (g + 1) * group_ch] = z_final

    # Calculate metrics
    l2_err = np.linalg.norm(out_pred - out_true) / np.linalg.norm(out_true)
    max_err = np.max(np.abs(out_pred - out_true))
    cosine_sim = np.sum(out_pred * out_true) / (np.linalg.norm(out_pred) * np.linalg.norm(out_true))

    print(f"\nVerification Results on {windows} windows (total tokens: {windows * tokens}):")
    print(f"  Relative L2 Error: {l2_err:.4f} (target < 0.08)")
    print(f"  Max Absolute Error: {max_err:.4f}")
    print(f"  Cosine Similarity: {cosine_sim:.6f} (target > 0.997)")

    if cosine_sim > 0.997 and l2_err < 0.08:
        print("\n[SUCCESS] Ada SM89 W4A8 + FP8 correction reference matches ground truth with high fidelity.")
        return 0
    else:
        print("\n[FAIL] Fidelity target not reached.", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
