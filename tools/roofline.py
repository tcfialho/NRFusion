#!/usr/bin/env python3
"""Say whether the neural pass is limited by arithmetic or by memory, from its own shapes.

A profiler answers this by measurement; this answers it by arithmetic, which needs no
toolkit and is enough to choose a direction. For each weight shape the model actually has,
it computes how many operations the hardware does per byte it must read, and compares that
against the ratio the GPU itself can sustain.

Below the GPU's ratio the pass waits on memory, and a cheaper number format helps because it
halves the bytes. Above it the pass waits on arithmetic, and only fusing work or removing it
helps. The crossover depends on how many tokens share one weight load, which is why the
answer is a table over token counts rather than a single verdict.

    python tools/roofline.py data/pesos/mapa.csv
"""
from __future__ import annotations

import argparse
import collections
import csv
import pathlib

# Nominal figures for the RTX 4050 Laptop: 96-bit GDDR6 at 16 Gbps, and the dense FP8
# tensor-core rate. They are the vendor's numbers, not measured here, so treat the
# crossover as an order of magnitude rather than a threshold.
DEFAULT_BANDWIDTH_GBS = 192.0
DEFAULT_FP8_TFLOPS = 97.0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("map", type=pathlib.Path)
    parser.add_argument("--bandwidth", type=float, default=DEFAULT_BANDWIDTH_GBS,
                        help="banda de memoria em GB/s")
    parser.add_argument("--tflops", type=float, default=DEFAULT_FP8_TFLOPS,
                        help="vazao de matriz em TFLOPS no formato atual")
    parser.add_argument("--bytes-per-weight", type=float, default=1.0,
                        help="bytes por peso no formato atual (FP8 = 1)")
    args = parser.parse_args()

    ridge = args.tflops * 1e12 / (args.bandwidth * 1e9)
    print(f"A placa sustenta {ridge:.0f} operacoes por byte lido "
          f"({args.tflops:.0f} TFLOPS / {args.bandwidth:.0f} GB/s).")
    print("Abaixo disso o trabalho espera a memoria; acima, espera a conta.\n")

    shapes: dict[tuple, dict] = {}
    for row in csv.DictReader(args.map.open(encoding="utf-8")):
        dims = [int(d) for d in row["logical_shape"].split("x") if d]
        if len(dims) < 2 or row["family"] in ("desconhecido", "posicional"):
            continue
        reduction, output = dims[-2], dims[-1]
        outer = 1
        for d in dims[:-2]:
            outer *= d
        key = (row["logical_shape"], row["family"], row["operator"])
        entry = shapes.setdefault(key, {"copias": 0, "pesos": outer * reduction * output,
                                        "reducao": reduction, "saida": output * outer})
        entry["copias"] += 1

    ranked = sorted(shapes.items(), key=lambda kv: -kv[1]["pesos"] * kv[1]["copias"])[:8]
    token_counts = (1, 8, 64, 256, 1024, 4096)

    print(f"{'forma':>12}  {'operador':<34}" +
          "".join(f"{t:>8}" for t in token_counts))
    print(f"{'':>12}  {'tokens que dividem um carregamento:':<34}" + "")
    for (shape, family, operator), entry in ranked:
        weights_bytes = entry["pesos"] * args.bytes_per_weight
        cells = []
        for tokens in token_counts:
            flops = 2 * tokens * entry["pesos"]
            # Activations in and out are read and written once per token, in the same format.
            traffic = weights_bytes + tokens * (entry["reducao"] + entry["saida"]) * args.bytes_per_weight
            intensity = flops / traffic
            cells.append(f"{intensity:8.0f}" if intensity < ridge else f"{intensity:7.0f}*")
        print(f"{shape:>12}  {family + ': ' + operator:<34}" + "".join(cells))

    print("\n* acima da linha da placa: nesse ponto o trabalho passa a esperar a conta.")
    print("Cada coluna e o numero de tokens que compartilham um mesmo carregamento de pesos.")
    print("Numa passagem neural por janelas, esse numero e o tamanho da janela, nao a resolucao.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
