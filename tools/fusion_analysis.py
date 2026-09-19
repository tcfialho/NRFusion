#!/usr/bin/env python3
"""Find which real operator chains in the DLSS-NR graph can collapse into one SM89 kernel.

This does not ask whether fusion helps in general. It reads the chains the recovered graph
actually has, and for each one computes what a fused kernel would stop doing: intermediate
tensors never written, global round-trips never made, launches and barriers never issued,
address arithmetic never repeated. Then it checks whether the fused form fits on the
hardware, because a chain that does not fit in shared memory or registers is not a candidate
no matter how much traffic it would save.

Precision is deliberately held fixed at what ships. Mixing a format change into a fusion
experiment makes the result unattributable.

    python tools/fusion_analysis.py data/pesos/mapa.csv
    python tools/fusion_analysis.py data/pesos/mapa.csv --tokens 256
"""
from __future__ import annotations

import argparse
import collections
import csv
import json
import pathlib

# Ada SM89, per NVIDIA's occupancy rules.
SHARED_PER_BLOCK_MAX = 100 * 1024      # opt-in maximum; 48 KiB without the opt-in
REGISTERS_PER_SM = 65536
THREADS_PER_SM = 1536
BYTES_PER_VALUE = 1                     # the shipped weights and activations are E4M3

# The window blocks carry a 64x64 attention bias, so one window is 64 tokens. The global
# blocks (31..38) carry no bias: their attention spans the whole deepest grid, so their token
# count is that grid, not a window. Both defaults are stated rather than hidden.
WINDOW_TOKENS = 64
GLOBAL_TOKENS = 1024


def load(path: pathlib.Path) -> dict[int, dict[int, list[dict]]]:
    blocks: dict[int, dict[int, list[dict]]] = collections.defaultdict(
        lambda: collections.defaultdict(list))
    for row in csv.DictReader(path.open(encoding="utf-8")):
        if row["block"] == "":
            continue
        row["dims"] = [int(d) for d in row["logical_shape"].split("x") if d]
        row["field"] = row["logical_name"].split(".", 2)[2]
        blocks[int(row["block"])][int(row["layer"])].append(row)
    return blocks


def signature(layers: dict[int, list[dict]]) -> tuple:
    return tuple(sorted((number, tuple(sorted(e["field"] for e in entries)))
                        for number, entries in layers.items()))


def find(entries: list[dict], field: str) -> dict | None:
    return next((e for e in entries if e["field"] == field), None)


def candidates_for(block: int, layers: dict[int, list[dict]], tokens: int) -> list[dict]:
    """Read the chains this block really has. Nothing is assumed to exist."""
    out = []
    numbers = sorted(layers)

    # A) Grouped feed-forward: one projection, then per-group expand and project inside the
    #    same layer. The whole chain lives in one layer, so today's dispatch boundary between
    #    expand and project is pure overhead -- there is no other consumer of the expansion.
    for n in numbers:
        expand = find(layers[n], "group_expand_weight")
        project = find(layers[n], "group_project_weight")
        if not (expand and project):
            continue
        groups, inner, wide = expand["dims"][0], expand["dims"][1], expand["dims"][2]
        first = find(layers[n], "first_projection_weight")
        after = find(layers.get(n + 1, []), "weight3")
        chain = ([f"projecao {first['logical_shape']}"] if first else []) + [
            f"expansao por grupo {groups}x{inner}->{wide}", "ativacao",
            f"projecao por grupo {groups}x{wide}->{inner}"]
        if after:
            chain.append(f"projecao {after['logical_shape']} + atalho")
        # The expansion is the intermediate that disappears; the per-group projection output
        # only disappears too when the next layer's projection joins the same kernel.
        intermediate = groups * wide * tokens * BYTES_PER_VALUE
        if after:
            intermediate += groups * inner * tokens * BYTES_PER_VALUE
        out.append({
            "candidato": "rede densa por grupo",
            "bloco": block,
            "camadas": [n] + ([n + 1] if after else []),
            "cadeia": chain,
            "pesos": [e["logical_name"] for e in
                      [first, expand, project, after] if e],
            "dispatchesHoje": len(chain),
            "dispatchesFundido": 1,
            "bytesIntermediarios": intermediate,
            # One group's expansion for the whole window is the working set a CTA must hold.
            "trabalhoPorCtaBytes": wide * tokens * BYTES_PER_VALUE,
            "dimensaoLarga": wide,
            "tipo": "produtor/consumidor",
        })

    # B) Wide feed-forward pair: expand in one layer, project in the next, shapes transposed.
    for n in numbers:
        expand = next((e for e in layers[n] if e["operator"] == "expansao"
                       and len(e["dims"]) == 2), None)
        if not expand:
            continue
        nxt = layers.get(n + 1, [])
        project = next((e for e in nxt if len(e["dims"]) == 2
                        and e["dims"] == expand["dims"][::-1]), None)
        if not project:
            continue
        narrow, wide = expand["dims"]
        skip = find(nxt, "ffn_cos_skip")
        chain = [f"expansao {narrow}->{wide}", "ativacao", f"projecao {wide}->{narrow}"]
        if skip:
            chain.append("atalho cosseno")
        out.append({
            "candidato": "rede densa larga",
            "bloco": block,
            "camadas": [n, n + 1],
            "cadeia": chain,
            "pesos": [e["logical_name"] for e in [expand, project, skip] if e],
            "dispatchesHoje": len(chain),
            "dispatchesFundido": 1,
            "bytesIntermediarios": wide * tokens * BYTES_PER_VALUE,
            "trabalhoPorCtaBytes": wide * tokens * BYTES_PER_VALUE,
            "dimensaoLarga": wide,
            "tipo": "produtor/consumidor",
        })

    # C) Attention tail: the output projection and its skip sit in their own layer, fed by the
    #    attention result. This is epilogue work, not a second matrix product chain.
    for n in numbers:
        qkv = find(layers[n], "qkv_weight")
        if not qkv:
            continue
        nxt = layers.get(n + 1, []) + layers.get(n + 2, [])
        projection = find(nxt, "projection_weight")
        if not projection:
            continue
        inner, three = qkv["dims"]
        out.append({
            "candidato": "epilogo de atencao",
            "bloco": block,
            "camadas": [n],
            "cadeia": [f"qkv {inner}->{three}", "atencao", f"projecao {projection['logical_shape']}",
                       "escala + atalho"],
            "pesos": [e["logical_name"] for e in [qkv, projection] if e],
            "dispatchesHoje": 4,
            "dispatchesFundido": 2,
            "bytesIntermediarios": inner * tokens * BYTES_PER_VALUE,
            "trabalhoPorCtaBytes": inner * tokens * BYTES_PER_VALUE,
            "dimensaoLarga": inner,
            "tipo": "epilogo",
        })
    return out


def verdict(working_set: int, wide: int, tokens: int) -> tuple[str, str]:
    """Where the fused chain's working set lives, and what that costs in occupancy.

    A working set over the shared-memory limit does not kill the fusion. The inner dimension
    can be walked in strips, accumulating the second product as each strip is produced, so
    only one strip is ever live. What the limit decides is the strip width -- and with it how
    many times the chain re-reads its input.
    """
    if working_set <= 48 * 1024:
        return "cabe na memoria compartilhada padrao", "baixo"
    if working_set <= SHARED_PER_BLOCK_MAX:
        return "so com memoria compartilhada estendida (1 CTA por SM)", "alto"
    if wide and tokens:
        # A CTA takes a tile of tokens and a strip of the wide dimension. 64 tokens is the
        # natural tile: it is one window in the window blocks, and a comfortable tile in the
        # global ones. The strip is then whatever fits beside it.
        token_tile = min(tokens, 64)
        strip = min(wide, (48 * 1024) // (token_tile * BYTES_PER_VALUE))
        strips = -(-wide // strip) if strip else 0
        if strip >= 128:
            return (f"faixas de {strip} da dimensao larga x {token_tile} tokens "
                    f"({strips} faixas)", "medio")
    return "nao cabe nem em faixas uteis", "bloqueante"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("map", type=pathlib.Path)
    parser.add_argument("--tokens", type=int, default=None,
                        help=f"tokens por chamada (padrao: {WINDOW_TOKENS} em blocos com janela, "
                             f"{GLOBAL_TOKENS} nos globais)")
    parser.add_argument("-o", "--out", type=pathlib.Path, default=None)
    args = parser.parse_args()

    blocks = load(args.map)

    # Blocks with the same layer signature run the same kernel, so a candidate found in one is
    # worth its copy count. Counting each copy separately would rank a rare chain like a common one.
    families: dict[tuple, list[int]] = collections.defaultdict(list)
    for block, layers in blocks.items():
        families[signature(layers)].append(block)

    rows = []
    for sig, members in families.items():
        block = min(members)
        is_global = not any(e["field"] == "attn_bias"
                            for entries in blocks[block].values() for e in entries)
        tokens = args.tokens or (GLOBAL_TOKENS if is_global else WINDOW_TOKENS)
        for candidate in candidates_for(block, blocks[block], tokens):
            fit, occupancy = verdict(candidate["trabalhoPorCtaBytes"],
                                     candidate.get("dimensaoLarga", 0), tokens)
            copies = len(members)
            candidate.update({
                "copias": copies,
                "blocos": sorted(members),
                "tokens": tokens,
                "janela": "global" if is_global else "janela",
                "bytesIntermediariosTotais": candidate["bytesIntermediarios"] * copies,
                "lancamentosRemovidos": (candidate["dispatchesHoje"] -
                                         candidate["dispatchesFundido"]) * copies,
                "ondeCabe": fit,
                "riscoOcupacao": occupancy,
            })
            rows.append(candidate)

    rows.sort(key=lambda r: -r["bytesIntermediariosTotais"])
    print(f"{len(rows)} cadeias fundiveis encontradas no grafo\n")
    header = (f"{'candidato':<24} {'blocos':<10} {'tokens':>6} {'interm./chamada':>16} "
              f"{'total':>10} {'lanc.':>6}  cabe onde")
    print(header)
    print("-" * len(header))
    for r in rows:
        blocks_label = (f"{r['blocos'][0]}..{r['blocos'][-1]}" if len(r["blocos"]) > 2
                        else "+".join(map(str, r["blocos"])))
        print(f"{r['candidato']:<24} {blocks_label:<10} {r['tokens']:>6} "
              f"{r['bytesIntermediarios'] / 1024:>13.0f} KiB "
              f"{r['bytesIntermediariosTotais'] / (1 << 20):>7.2f} MiB "
              f"{r['lancamentosRemovidos']:>6}  {r['ondeCabe']}")

    print("\ncadeias, em detalhe:")
    for r in rows[:4]:
        print(f"\n  {r['candidato']} — blocos {r['blocos'][0]}..{r['blocos'][-1]} "
              f"({r['copias']}x, {r['janela']}, {r['tokens']} tokens)")
        print(f"    hoje: {' -> '.join(r['cadeia'])}")
        print(f"    {r['dispatchesHoje']} dispatches por bloco viram {r['dispatchesFundido']}")
        print(f"    intermediario por chamada: {r['bytesIntermediarios'] / 1024:.0f} KiB "
              f"({r['tipo']})")
        print(f"    conjunto de trabalho por CTA: {r['trabalhoPorCtaBytes'] / 1024:.0f} KiB"
              f"  -> {r['ondeCabe']}, risco de ocupacao {r['riscoOcupacao']}")
        for w in r["pesos"]:
            print(f"      {w}")

    destination = args.out or args.map.parent / "fusao.json"
    destination.write_text(json.dumps(rows, indent=1, ensure_ascii=False), encoding="utf-8")
    print(f"\ntabela completa em {destination}")
    print("Intermediarios em E4M3 (1 byte), o formato que o modelo ja usa. Nenhuma troca de")
    print("precisao entra nesta conta: misturar as duas coisas torna o resultado inatribuivel.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
