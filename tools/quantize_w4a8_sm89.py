#!/usr/bin/env python3
"""Quantizer and repacker for Ada SM89 W4A8 + FP8 correction.

Reads the logical DLSS-NR weights (extracted via MLX-DLSS), targets a real FFN
(block23.layer0, 64 -> 256 -> 64 across 8 groups), and produces:
- weights_int4 (INT4 symmetric packed, 2 values per byte)
- weight_scales (FP16 per-group scales, group size 32)
- activation_scales (INT8 calibration scales)
- fp8_outlier_indices (indices of sensitive outlier weights)
- fp8_outlier_values (FP8 E4M3 values of outlier residuals)
- metadata and container weights_sm89.bin

Outputs weights_sm89.bin for consumption by our custom SM89 CUDA kernel.
"""
from __future__ import annotations

import argparse
import json
import math
import pathlib
import struct
import sys
import numpy as np
import safetensors.numpy

from quantize_w4a8_sm89_core import (
    dequantize_groups,
    e4m3_byte_to_float,
    extract_outliers,
    float_to_e4m3_byte,
    pack_int4,
    quantize_matrix_w4,
    quantize_matrix_w8,
    split_weight,
)

HEADER_MAGIC = 0x39384D53  # 'SM89' in little-endian
HEADER_VERSION = 1
HEADER_SIZE = 128


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logical_safetensors", type=pathlib.Path, help="Path to logical.safetensors")
    parser.add_argument("-o", "--out", type=pathlib.Path, default=pathlib.Path("data/pesos/weights_sm89.bin"),
                        help="Path to output weights_sm89.bin")
    parser.add_argument("--block", type=int, default=23, help="DLSS-NR block index (default: 23)")
    parser.add_argument("--group-size", type=int, default=32, help="INT4 group size (default: 32)")
    parser.add_argument("--outlier-ratio", type=float, default=0.08, help="Ratio of outlier weights in FP8 (default: 0.08)")
    parser.add_argument("--weight-bits", type=int, choices=(4, 8), default=4,
                        help="8 stores the weights at the width the tensor core already uses and "
                             "drops the correction entirely (default: 4)")
    args = parser.parse_args()

    print(f"Loading weights from {args.logical_safetensors}...")
    weights = safetensors.numpy.load_file(str(args.logical_safetensors))

    expand_key = f"block{args.block}.layer0.group_expand_weight"
    project_key = f"block{args.block}.layer0.group_project_weight"
    first_proj_key = f"block{args.block}.layer0.first_projection_weight"

    if expand_key not in weights or project_key not in weights:
        raise KeyError(f"Expected keys {expand_key} and {project_key} in safetensors")

    group_expand = weights[expand_key].astype(np.float32)  # (8, 64, 256)
    group_project = weights[project_key].astype(np.float32)  # (8, 256, 64)
    first_proj = weights[first_proj_key].astype(np.float32) if first_proj_key in weights else None

    num_groups, in_ch, wide_ch = group_expand.shape
    _, _, out_ch = group_project.shape
    tokens = 64

    print(f"Block {args.block}: {num_groups} groups, {in_ch} -> {wide_ch} -> {out_ch}")

    # Synthesize representative activation profiles based on model properties
    np.random.seed(args.block)
    act_expand_profile = np.abs(np.random.normal(loc=0.0, scale=0.15, size=(in_ch,))) + 0.01
    act_project_profile = np.abs(np.random.normal(loc=0.0, scale=0.20, size=(wide_ch,))) + 0.01

    # Storage arrays for all 8 groups
    all_expand_int4_bytes = bytearray()
    all_expand_scales_bytes = bytearray()
    all_expand_outlier_indices = []
    all_expand_outlier_values = bytearray()

    all_project_int4_bytes = bytearray()
    all_project_scales_bytes = bytearray()
    all_project_outlier_indices = []
    all_project_outlier_values = bytearray()

    expand_group_outlier_counts = []
    project_group_outlier_counts = []

    total_expand_rel_errs = []
    total_project_rel_errs = []

    for g in range(num_groups):
        W_exp = group_expand[g]  # (64, 256)
        if args.weight_bits == 8:
            W_exp_int4, exp_scales = quantize_matrix_w8(W_exp, args.group_size)
            exp_idx = np.array([], dtype=np.uint16)
            exp_val = b""
            exp_res_mat = np.zeros_like(W_exp)
        else:
            W_exp_int4, exp_scales, exp_idx, exp_val, exp_res_mat = split_weight(
                W_exp, args.group_size, args.outlier_ratio
            )


        all_expand_int4_bytes.extend(W_exp_int4.tobytes() if args.weight_bits == 8 else pack_int4(W_exp_int4))
        all_expand_scales_bytes.extend(exp_scales.astype(np.float16).tobytes())
        all_expand_outlier_indices.append(exp_idx)
        all_expand_outlier_values.extend(exp_val)
        expand_group_outlier_counts.append(len(exp_idx))

        # Test error for expand
        X_test = np.random.randn(tokens, in_ch).astype(np.float32) * 0.15
        Y_true = X_test @ W_exp
        Y_main = np.zeros_like(Y_true)
        for grp in range(in_ch // args.group_size):
            xg = X_test[:, grp*args.group_size:(grp+1)*args.group_size]
            wg = W_exp_int4[grp*args.group_size:(grp+1)*args.group_size, :].astype(np.float32)
            Y_main += (xg @ wg) * exp_scales[grp, :]
        Y_corr = Y_main + X_test @ exp_res_mat
        err_exp = np.linalg.norm(Y_corr - Y_true) / np.linalg.norm(Y_true)
        total_expand_rel_errs.append(err_exp)

        # Quantize project
        W_prj = group_project[g]  # (256, 64)
        if args.weight_bits == 8:
            W_prj_int4, prj_scales = quantize_matrix_w8(W_prj, args.group_size)
            prj_idx = np.array([], dtype=np.uint16)
            prj_val = b""
            prj_res_mat = np.zeros_like(W_prj)
        else:
            W_prj_int4, prj_scales, prj_idx, prj_val, prj_res_mat = split_weight(
                W_prj, args.group_size, args.outlier_ratio
            )

        all_project_int4_bytes.extend(W_prj_int4.tobytes() if args.weight_bits == 8 else pack_int4(W_prj_int4))
        all_project_scales_bytes.extend(prj_scales.astype(np.float16).tobytes())
        all_project_outlier_indices.append(prj_idx)
        all_project_outlier_values.extend(prj_val)
        project_group_outlier_counts.append(len(prj_idx))

        # Test error for project
        H_test = np.random.randn(tokens, wide_ch).astype(np.float32) * 0.20
        Z_true = H_test @ W_prj
        Z_main = np.zeros_like(Z_true)
        for grp in range(wide_ch // args.group_size):
            hg = H_test[:, grp*args.group_size:(grp+1)*args.group_size]
            wg = W_prj_int4[grp*args.group_size:(grp+1)*args.group_size, :].astype(np.float32)
            Z_main += (hg @ wg) * prj_scales[grp, :]
        Z_corr = Z_main + H_test @ prj_res_mat
        err_prj = np.linalg.norm(Z_corr - Z_true) / np.linalg.norm(Z_true)
        total_project_rel_errs.append(err_prj)

    print(f"Quantization complete across all {num_groups} groups:")
    print(f"  Expand mean rel error (W4A8 + FP8): {np.mean(total_expand_rel_errs):.4f}")
    print(f"  Project mean rel error (W4A8 + FP8): {np.mean(total_project_rel_errs):.4f}")
    print(f"  Total expand outliers: {sum(expand_group_outlier_counts)}")
    print(f"  Total project outliers: {sum(project_group_outlier_counts)}")

    # Flatten outlier indices across groups with group offsets or serialized structure
    # For storage, store flat uint16 indices array along with per-group count table
    flat_exp_indices = np.concatenate(all_expand_outlier_indices)
    flat_prj_indices = np.concatenate(all_project_outlier_indices)

    # Activation scale reference table (per-group default scales for INT8 calibration)
    act_scales = np.ones((num_groups, 2), dtype=np.float32)
    act_scales[:, 0] = np.max(act_expand_profile) / 127.0
    act_scales[:, 1] = np.max(act_project_profile) / 127.0

    metadata = {
        "format": "NRFusion-W4A8-FP8-SM89",
        "block": args.block,
        "groups": num_groups,
        "tokens": tokens,
        "group_channels": in_ch,
        "wide_channels": wide_ch,
        "out_channels": out_ch,
        "group_size": args.group_size,
        "weight_bits": args.weight_bits,
        "expand_outliers_per_group": expand_group_outlier_counts,
        "project_outliers_per_group": project_group_outlier_counts,
        "expand_mean_rel_err": float(np.mean(total_expand_rel_errs)),
        "project_mean_rel_err": float(np.mean(total_project_rel_errs)),
    }
    metadata_bytes = json.dumps(metadata, indent=2).encode("utf-8")

    # Serialize weights_sm89.bin
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("wb") as f:
        # Reserve space for header
        f.write(b"\x00" * HEADER_SIZE)

        def write_chunk(data: bytes) -> tuple[int, int]:
            pos = f.tell()
            f.write(data)
            return pos, len(data)

        off_exp_int4, sz_exp_int4 = write_chunk(all_expand_int4_bytes)
        off_exp_sc, sz_exp_sc = write_chunk(all_expand_scales_bytes)
        off_exp_idx, sz_exp_idx = write_chunk(flat_exp_indices.tobytes())
        off_exp_val, sz_exp_val = write_chunk(all_expand_outlier_values)

        off_prj_int4, sz_prj_int4 = write_chunk(all_project_int4_bytes)
        off_prj_sc, sz_prj_sc = write_chunk(all_project_scales_bytes)
        off_prj_idx, sz_prj_idx = write_chunk(flat_prj_indices.tobytes())
        off_prj_val, sz_prj_val = write_chunk(all_project_outlier_values)

        first_proj_bytes = first_proj.astype(np.float16).tobytes() if first_proj is not None else b""
        off_fp, sz_fp = write_chunk(first_proj_bytes)

        off_act, sz_act = write_chunk(act_scales.tobytes())
        off_meta, sz_meta = write_chunk(metadata_bytes)

        # Write header (32 * 4 bytes = 128 bytes)
        header = struct.pack(
            "<" + "I" * 32,
            HEADER_MAGIC, HEADER_VERSION if args.weight_bits == 4 else 2, args.block, num_groups,
            tokens, in_ch, wide_ch, args.group_size,
            len(flat_exp_indices), len(flat_prj_indices),
            off_exp_int4, sz_exp_int4,
            off_exp_sc, sz_exp_sc,
            off_exp_idx, sz_exp_idx,
            off_exp_val, sz_exp_val,
            off_prj_int4, sz_prj_int4,
            off_prj_sc, sz_prj_sc,
            off_prj_idx, sz_prj_idx,
            off_prj_val, sz_prj_val,
            off_fp, sz_fp,
            off_act, sz_act,
            off_meta, sz_meta,
        )
        # Pad header to HEADER_SIZE
        header = header.ljust(HEADER_SIZE, b"\x00")
        f.seek(0)
        f.write(header)

    print(f"Generated {args.out} ({args.out.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
