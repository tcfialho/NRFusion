#!/usr/bin/env python3
"""Build one W4A8 container per block that carries the grouped feed-forward shape.

Sixteen blocks of the network use the same 8 x (64 -> 256 -> 64) feed-forward, and the same
kernel serves all of them -- but not the same weights, and not the same correction budget.
Quantizing them all at one budget was measured to leave fourteen of the sixteen below the
fidelity the reference check demands, with the deeper blocks the worst.

So the budget is searched per block instead of assumed: the smallest share of weights kept in
FP8 that still passes `tests/w4a8_reference_test.py`. Smallest matters, because every corrected
weight is work the kernel has to do at runtime.

A block that cannot pass at any budget on the ladder is reported and left out of the manifest,
so packaging can refuse to ship a family whose weights never met the bar.

    python tools/build_w4a8_containers.py data/pesos/logical.safetensors -o data/w4a8
"""
from __future__ import annotations
import argparse
import hashlib
import json
import pathlib
import re
import struct
import subprocess
import sys

LADDER = (0.08, 0.12, 0.16, 0.20, 0.24)
L2_TARGET = 0.08
COSINE_TARGET = 0.997

MEASURED = re.compile(r"Relative L2 Error:\s*([0-9.]+).*?Cosine Similarity:\s*([0-9.]+)", re.S)


def blocks_with_grouped_ffn(safetensors: pathlib.Path) -> list[int]:
    """Read the container index only -- the tensors themselves are hundreds of megabytes."""
    with safetensors.open("rb") as handle:
        length, = struct.unpack("<Q", handle.read(8))
        header = json.loads(handle.read(length))
    found = set()
    for name in header:
        match = re.fullmatch(r"block(\d+)\.layer0\.group_expand_weight", name)
        if match:
            found.add(int(match.group(1)))
    return sorted(found)


def quantize(root: pathlib.Path, safetensors: pathlib.Path, block: int,
             ratio: float, group_size: int, out: pathlib.Path, weight_bits: int = 8) -> bool:
    result = subprocess.run(
        [sys.executable, str(root / "tools" / "quantize_w4a8_sm89.py"), str(safetensors),
         "--block", str(block), "--outlier-ratio", str(ratio),
         "--group-size", str(group_size), "--weight-bits", str(weight_bits), "-o", str(out)],
        capture_output=True, text=True, cwd=root)
    if result.returncode != 0:
        print(result.stderr.strip(), file=sys.stderr)
    return result.returncode == 0


def fidelity(root: pathlib.Path, container: pathlib.Path,
             safetensors: pathlib.Path) -> tuple[float, float] | None:
    """Ask the reference check, so the bar is the same one the tests enforce."""
    result = subprocess.run(
        [sys.executable, str(root / "tests" / "w4a8_reference_test.py"), str(container), str(safetensors)],
        capture_output=True, text=True, cwd=root)
    match = MEASURED.search(result.stdout)
    return (float(match.group(1)), float(match.group(2))) if match else None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("safetensors", type=pathlib.Path)
    parser.add_argument("-o", "--out", type=pathlib.Path, default=pathlib.Path("dist/w4a8"))
    parser.add_argument("--group-size", type=int, default=64,
                        help="scale group; the kernel folds one group per accumulator, "
                             "so 64 halves the epilogue against 32 (default: 64)")
    parser.add_argument("--blocks", nargs="*", type=int, default=None)
    parser.add_argument("--ladder", nargs="*", type=float, default=list(LADDER))
    parser.add_argument("--weight-bits", type=int, choices=(4, 8), default=8,
                        help="8 drops the sparse correction the narrow form needs (default: 8)")
    args = parser.parse_args()

    root = pathlib.Path(__file__).resolve().parent.parent
    if not args.safetensors.exists():
        print(f"{args.safetensors}: not found", file=sys.stderr)
        return 2

    blocks = args.blocks if args.blocks is not None else blocks_with_grouped_ffn(args.safetensors)
    if not blocks:
        print("no block in this container carries the grouped feed-forward", file=sys.stderr)
        return 1

    args.out.mkdir(parents=True, exist_ok=True)
    manifest: dict[str, dict] = {}
    failed: list[int] = []

    for block in blocks:
        container = args.out / f"weights_sm89_block{block}.bin"
        passed = None
        ladder = args.ladder if args.weight_bits == 4 else [0.0]
        for ratio in ladder:
            if not quantize(root, args.safetensors, block, ratio, args.group_size, container,
                            args.weight_bits):
                break
            measured = fidelity(root, container, args.safetensors)
            if measured is None:
                break
            l2, cosine = measured
            print(f"block {block:>2}  budget {ratio:.2f}  L2 {l2:.4f}  cosine {cosine:.6f}"
                  f"  {'ok' if l2 < L2_TARGET and cosine > COSINE_TARGET else 'below bar'}")
            if l2 < L2_TARGET and cosine > COSINE_TARGET:
                passed = (ratio, l2, cosine)
                break
        if passed is None:
            failed.append(block)
            container.unlink(missing_ok=True)
            continue
        ratio, l2, cosine = passed
        manifest[str(block)] = {
            "file": container.name,
            "outlierRatio": ratio,
            "groupSize": args.group_size,
            "weightBits": args.weight_bits,
            "relativeL2": round(l2, 6),
            "cosine": round(cosine, 6),
            "sha256": hashlib.sha256(container.read_bytes()).hexdigest(),
            "bytes": container.stat().st_size,
        }

    (args.out / "manifest.json").write_text(
        json.dumps({"l2Target": L2_TARGET, "cosineTarget": COSINE_TARGET,
                    "blocks": manifest, "failed": failed}, indent=2), encoding="ascii")

    print(f"\n{len(manifest)} of {len(blocks)} blocks met the bar; manifest in {args.out / 'manifest.json'}")
    if failed:
        print(f"below the bar at every budget on the ladder: {failed}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
