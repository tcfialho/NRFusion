#!/usr/bin/env python3
"""Join the packed tensors to the logical ones and say what each piece is for.

The packed container names everything `blockN.layerM.layer` and keeps the bytes flat, so
the raw file alone cannot tell an attention projection from a feed-forward expansion.
The logical decoder knows, and this joins the two views into one table:

    raw_name | raw_offset | raw_size | logical_name | logical_shape | block | operator

That table is the thing a precision or fusion effort is aimed at: it says which matrix
multiplications are big enough to matter, how many copies of each exist, and which of
them are bias or scale tables that must not be touched.

    python tools/map_weights.py data/pesos/inventario.json data/pesos/logical.safetensors
"""
from __future__ import annotations

import argparse
import collections
import csv
import json
import pathlib
import re
import sys

BLOCK_LAYER = re.compile(r"^block(?P<block>\d+)\.layer(?P<layer>\d+)\.(?P<field>.+)$")

# What each logical suffix does in the graph. Anything unlisted is reported as unknown
# rather than guessed: a wrong label here would send kernel work at the wrong tensor.
OPERATORS = {
    "qkv_weight": ("atencao", "projecao qkv"),
    "projection_weight": ("atencao", "projecao de saida"),
    "attn_bias": ("atencao", "vies (layout de fragmento)"),
    "attn_scale": ("atencao", "escala"),
    "attn_cos_skip": ("atencao", "atalho cosseno"),
    "attention_scalar": ("atencao", "escalar"),
    "ffn_expand_weight": ("rede densa", "expansao"),
    "ffn_branch_projection_weight": ("rede densa", "projecao de ramo"),
    "ffn_output_projection_weight": ("rede densa", "projecao de saida"),
    "ffn_cos_skip": ("rede densa", "atalho cosseno"),
    "group_expand_weight": ("rede densa por grupo", "expansao 64->256"),
    "group_project_weight": ("rede densa por grupo", "projecao 256->64"),
    "first_projection_weight": ("entrada", "primeira projecao"),
    "input_adapter_weight": ("entrada", "adaptador"),
    "sin": ("posicional", "seno"),
}


def classify(field: str) -> tuple[str, str]:
    if field in OPERATORS:
        return OPERATORS[field]
    if re.fullmatch(r"weight\d*", field):
        return ("transicao", "peso de amostragem/ponte")
    return ("desconhecido", field)


def refine_with_block_context(rows: list[dict]) -> None:
    """Name the bare `weight` tensors from what sits beside them, not from their size.

    A global block stores its feed-forward as two plain `weight` entries in consecutive
    layers, so the name alone says nothing. Two things together do: the shapes are each
    other's transpose, and the second layer also carries an `ffn_cos_skip`, which only the
    feed-forward path has. Size alone would be a guess; this is the block telling us.
    """
    by_block: dict[int, dict[int, list[dict]]] = collections.defaultdict(
        lambda: collections.defaultdict(list))
    for r in rows:
        if r["block"] != "":
            by_block[r["block"]][r["layer"]].append(r)

    for layers in by_block.values():
        has_ffn_skip = {n for n, entries in layers.items()
                        if any(e["logical_name"].endswith("ffn_cos_skip") for e in entries)}
        for number, entries in layers.items():
            for entry in entries:
                if not entry["logical_name"].endswith(".weight"):
                    continue
                shape = entry["logical_shape"].split("x")
                if len(shape) != 2:
                    continue
                nxt = layers.get(number + 1, [])
                prev = layers.get(number - 1, [])

                def transposed(others: list[dict]) -> bool:
                    return any(o["logical_name"].endswith(".weight")
                               and o["logical_shape"] == f"{shape[1]}x{shape[0]}"
                               for o in others)

                if number + 1 in has_ffn_skip and transposed(nxt):
                    entry["family"], entry["operator"] = "rede densa", "expansao"
                elif number in has_ffn_skip and transposed(prev):
                    entry["family"], entry["operator"] = "rede densa", "projecao de saida"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("inventory", type=pathlib.Path)
    parser.add_argument("logical", type=pathlib.Path)
    parser.add_argument("-o", "--out", type=pathlib.Path, default=None,
                        help="CSV de saida (padrao: ao lado do inventario)")
    args = parser.parse_args()

    try:
        from safetensors import safe_open
    except ImportError:
        print("falta o pacote safetensors: python -m pip install safetensors", file=sys.stderr)
        return 2

    inventory = json.loads(args.inventory.read_text(encoding="utf-8"))
    raw = {t["nome"]: t for t in inventory["tensoresDetalhados"]}

    rows = []
    with safe_open(str(args.logical), framework="numpy") as handle:
        for key in handle.keys():
            match = BLOCK_LAYER.match(key)
            if not match:
                rows.append({"raw_name": "", "raw_offset": "", "raw_size": "",
                             "logical_name": key, "logical_shape": "",
                             "block": "", "layer": "", "family": "desconhecido",
                             "operator": key, "elements": ""})
                continue
            block = int(match.group("block"))
            layer = int(match.group("layer"))
            field = match.group("field")
            shape = list(handle.get_slice(key).get_shape())
            elements = 1
            for d in shape:
                elements *= d
            source = raw.get(f"block{block}.layer{layer}.layer")
            family, operator = classify(field)
            rows.append({
                "raw_name": source["nome"] if source else "",
                "raw_offset": source["offset"] if source and "offset" in source else "",
                "raw_size": source["bytes"] if source else "",
                "logical_name": key,
                "logical_shape": "x".join(str(d) for d in shape),
                "block": block,
                "layer": layer,
                "family": family,
                "operator": operator,
                "elements": elements,
            })

    refine_with_block_context(rows)
    rows.sort(key=lambda r: (r["block"] if r["block"] != "" else 999, r["layer"] or 0,
                             r["logical_name"]))
    destination = args.out or args.inventory.parent / "mapa.csv"
    with destination.open("w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)

    # A matrix multiply repeated across blocks is the cheapest thing to attack: the kernel is
    # written once. Group by shape and operator so the report ranks that, not one-offs.
    groups: dict[tuple, dict] = {}
    for r in rows:
        if r["family"] in ("desconhecido", "posicional") or not r["logical_shape"]:
            continue
        if len(r["logical_shape"].split("x")) < 2:
            continue
        key = (r["family"], r["operator"], r["logical_shape"])
        g = groups.setdefault(key, {"copias": 0, "elementos": 0, "blocos": []})
        g["copias"] += 1
        g["elementos"] += r["elements"]
        g["blocos"].append(r["block"])

    total_elements = sum(g["elementos"] for g in groups.values()) or 1
    print(f"{len(rows)} tensores logicos, {len(groups)} formas distintas de matriz\n")
    print(f"{'peso':>7}  {'copias':>6}  {'forma':>12}  operador")
    for (family, operator, shape), g in sorted(groups.items(), key=lambda kv: -kv[1]["elementos"])[:18]:
        blocks = sorted(set(g["blocos"]))
        span = f"{blocks[0]}..{blocks[-1]}" if len(blocks) > 2 else "+".join(map(str, blocks))
        print(f"{g['elementos'] / total_elements * 100:6.1f}%  {g['copias']:6}  {shape:>12}  "
              f"{family}: {operator}  [blocos {span}]")
    print(f"\ntabela completa em {destination}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
