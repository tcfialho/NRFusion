#!/usr/bin/env python3
"""Measure what each weight tensor loses under a cheaper number format.

The question a precision effort has to answer first is not how fast a format is, but
which layers survive it. This answers that offline, per tensor, with no GPU and no game:
quantise the weights, multiply them by random activations, and compare the result against
the same product computed at full precision.

The error that matters is the one in the product, not the one in the weights. A tensor can
look badly damaged element by element and still produce an almost identical output, and the
reverse happens too. Both numbers are reported so the difference is visible.

    python tools/precision_lab.py data/pesos/logical.safetensors
    python tools/precision_lab.py data/pesos/logical.safetensors --tensors block31.layer0.weight
"""
from __future__ import annotations

import argparse
import csv
import json
import pathlib
import sys

import numpy


def quantize_int(weights: numpy.ndarray, bits: int, group: int) -> numpy.ndarray:
    """Symmetric integer quantisation with one scale per group along the input axis.

    One scale for a whole 4096-wide row would be ruined by a single outlier, which is why
    every real 4-bit scheme groups. The group size is the knob: smaller keeps more shape and
    costs more scale bytes.
    """
    original = weights.shape
    flat = weights.reshape(-1, original[-1]).astype(numpy.float32)
    columns = flat.shape[1]
    padding = (-columns) % group
    if padding:
        flat = numpy.pad(flat, ((0, 0), (0, padding)))
    grouped = flat.reshape(flat.shape[0], -1, group)

    limit = (1 << (bits - 1)) - 1
    scale = numpy.abs(grouped).max(axis=2, keepdims=True) / limit
    scale[scale == 0] = 1.0
    codes = numpy.clip(numpy.rint(grouped / scale), -limit - 1, limit)
    restored = (codes * scale).reshape(flat.shape[0], -1)
    if padding:
        restored = restored[:, :columns]
    return restored.reshape(original)


def to_fp8_e4m3(weights: numpy.ndarray) -> numpy.ndarray:
    """Round to the E4M3 grid: 4 exponent bits, 3 mantissa bits, no infinities.

    This is the format the shipped model already uses, so it is the fidelity baseline every
    cheaper candidate is measured against, not an extra candidate itself.
    """
    values = weights.astype(numpy.float32)
    out = numpy.zeros_like(values)
    finite = numpy.isfinite(values) & (values != 0)
    magnitude = numpy.abs(values[finite])
    exponent = numpy.floor(numpy.log2(magnitude))
    exponent = numpy.clip(exponent, -6, 8)          # E4M3 bias 7: normals from 2^-6 to 2^8
    step = numpy.exp2(exponent - 3)                  # 3 mantissa bits
    quantized = numpy.rint(magnitude / step) * step
    quantized = numpy.minimum(quantized, 448.0)      # E4M3 max finite
    out[finite] = numpy.sign(values[finite]) * quantized
    return out


def sparsify_2of4(weights: numpy.ndarray) -> numpy.ndarray:
    """Keep the two largest of every four consecutive inputs, zero the rest.

    Ada runs this pattern at double rate in hardware, but only this pattern: two of four,
    along the reduction axis. Anything else is ordinary dense work with holes in it.
    """
    original = weights.shape
    flat = weights.reshape(-1, original[-1]).astype(numpy.float32)
    columns = flat.shape[1]
    padding = (-columns) % 4
    if padding:
        flat = numpy.pad(flat, ((0, 0), (0, padding)))
    quads = flat.reshape(flat.shape[0], -1, 4)
    order = numpy.argsort(-numpy.abs(quads), axis=2)
    mask = numpy.zeros_like(quads, dtype=bool)
    numpy.put_along_axis(mask, order[:, :, :2], True, axis=2)
    kept = numpy.where(mask, quads, 0.0).reshape(flat.shape[0], -1)
    if padding:
        kept = kept[:, :columns]
    return kept.reshape(original)


CANDIDATES = {
    "fp8-e4m3": (lambda w: to_fp8_e4m3(w), 8, "linha de base: o formato que o modelo ja usa"),
    "int8-g128": (lambda w: quantize_int(w, 8, 128), 8, "inteiro de 8 bits, grupos de 128"),
    "int4-g128": (lambda w: quantize_int(w, 4, 128), 4, "inteiro de 4 bits, grupos de 128"),
    "int4-g64": (lambda w: quantize_int(w, 4, 64), 4, "inteiro de 4 bits, grupos de 64"),
    "int4-g32": (lambda w: quantize_int(w, 4, 32), 4, "inteiro de 4 bits, grupos de 32"),
    "esparso2de4": (lambda w: sparsify_2of4(w), 16, "esparsidade 2:4 sobre o peso original"),
    "int4-g64+2de4": (lambda w: quantize_int(sparsify_2of4(w), 4, 64), 4,
                      "esparsidade 2:4 e depois 4 bits"),
}


def output_error(reference: numpy.ndarray, candidate: numpy.ndarray,
                 samples: int, generator: numpy.random.Generator) -> tuple[float, float]:
    """Relative error of the product, and the worst single element of it.

    Activations are drawn normal because the real ones are unknown; what the number measures
    is the tensor's sensitivity, not the network's end quality. Only a capture of real
    activations turns this into a claim about the image.
    """
    rows = reference.reshape(-1, reference.shape[-1]).shape[0]
    inputs = generator.standard_normal((samples, rows), dtype=numpy.float32)
    exact = inputs @ reference.reshape(rows, -1).astype(numpy.float32)
    approx = inputs @ candidate.reshape(rows, -1).astype(numpy.float32)
    difference = exact - approx
    scale = numpy.sqrt((exact ** 2).mean()) or 1.0
    return float(numpy.sqrt((difference ** 2).mean()) / scale), float(numpy.abs(difference).max() / scale)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("logical", type=pathlib.Path)
    parser.add_argument("--tensors", nargs="*", default=None,
                        help="nomes especificos; por padrao, os maiores de cada forma")
    parser.add_argument("--top", type=int, default=8, help="quantas formas avaliar")
    parser.add_argument("--samples", type=int, default=64, help="linhas de ativacao por teste")
    parser.add_argument("--seed", type=int, default=20260914)
    parser.add_argument("-o", "--out", type=pathlib.Path, default=None)
    args = parser.parse_args()

    try:
        from safetensors import safe_open
    except ImportError:
        print("falta o pacote safetensors: python -m pip install safetensors", file=sys.stderr)
        return 2

    generator = numpy.random.default_rng(args.seed)
    with safe_open(str(args.logical), framework="numpy") as handle:
        names = list(handle.keys())
        if args.tensors:
            chosen = [n for n in args.tensors if n in names]
            missing = [n for n in args.tensors if n not in names]
            for n in missing:
                print(f"nao encontrado: {n}", file=sys.stderr)
        else:
            # One representative per shape, biggest first: copies of the same shape behave
            # the same, and measuring all of them says nothing new.
            by_shape: dict[tuple, str] = {}
            for name in names:
                shape = tuple(handle.get_slice(name).get_shape())
                if len(shape) < 2 or numpy.prod(shape) < 1 << 16:
                    continue
                by_shape.setdefault(shape, name)
            chosen = [n for _, n in sorted(by_shape.items(),
                                           key=lambda kv: -numpy.prod(kv[0]))][:args.top]

        rows = []
        for name in chosen:
            reference = handle.get_tensor(name).astype(numpy.float32)
            shape = "x".join(str(d) for d in reference.shape)
            baseline_bytes = reference.size * 2
            print(f"\n{name}  {shape}")
            print(f"  {'formato':>14}  {'erro no produto':>15}  {'pior elemento':>13}  "
                  f"{'erro no peso':>12}  {'bytes':>8}")
            for label, (transform, bits, _) in CANDIDATES.items():
                candidate = transform(reference)
                relative, worst = output_error(reference, candidate, args.samples, generator)
                weight_error = float(numpy.abs(reference - candidate).mean() /
                                     (numpy.abs(reference).mean() or 1.0))
                stored = reference.size * bits / 8
                if label.startswith("int"):
                    group = int(label.split("g")[1].split("+")[0])
                    stored += reference.size / group * 2   # one fp16 scale per group
                if "2de4" in label:
                    stored += reference.size / 8           # two index bits per kept pair
                rows.append({"tensor": name, "forma": shape, "formato": label,
                             "erroProduto": relative, "piorElemento": worst,
                             "erroPeso": weight_error, "bytes": int(stored),
                             "fracaoDoOriginal": stored / baseline_bytes})
                print(f"  {label:>14}  {relative:15.5f}  {worst:13.5f}  {weight_error:12.5f}  "
                      f"{stored / baseline_bytes:7.2f}x")

    destination = args.out or args.logical.parent / "precisao.csv"
    with destination.open("w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)
    print(f"\ntabela em {destination}")
    print("O erro e do produto contra o mesmo produto em precisao total, com ativacoes")
    print("aleatorias. Ele mede a sensibilidade do tensor, nao a qualidade final da imagem:")
    print("so uma captura das ativacoes reais transforma isto numa afirmacao sobre o que se ve.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
