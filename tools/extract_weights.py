#!/usr/bin/env python3
"""Extract the DLSS Neural Rendering weights from the user's own nvngx_dlssnr.dll.

Nothing is downloaded and nothing is redistributed: the library is the user's, the
extraction happens on the user's machine, and the output stays there. NVIDIA's terms
for the library apply to whatever comes out of it.

The point is not the tensors themselves but the inventory: which layers exist, how big
each one is, and which matrix multiplications are large enough that a cheaper format or
a fused kernel would actually show up in the frame time. Without that list, any work on
precision is aimed at a target nobody has seen.

The container layout (a WEIGHTS_HT RCDATA resource holding a length-prefixed map of
u8-packed tensors) is documented by the MLX-DLSS project, Apache-2.0,
https://github.com/iamwavecut/MLX-DLSS -- this is an independent reader of that format.

    python tools/extract_weights.py <nvngx_dlssnr.dll> [-o DIR] [--json]
"""
from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import sys

from extract_weights_pe import (
    FormatError,
    PACKED_DTYPE_CODE,
    PACKED_ELEMENT_BYTES,
    RCDATA,
    RESOURCE_NAME,
    Section,
    _need,
    _pe_resource_root,
    _to_file_offset,
    _u16,
    _u32,
    _u64,
    read_resource,
)
from extract_weights_model import Tensor, block_of, inventory, parse_map

PACKED_FORMAT = "dlssnr-WEIGHTS_HT-packed-u8-v2"


def write_packed(blob: bytes, tensors: list[Tensor], destination: pathlib.Path,
                 library_sha256: str, resource_sha256: str) -> None:
    """Write the packed container in the layout MLX-DLSS's logical decoder reads.

    The payloads stay exactly as they sit in the library: opaque packed bytes, with the
    record's own metadata carried alongside. Decoding them into layers is that decoder's
    job, not this one's -- inferring layouts from sizes is how you get a plausible answer
    that is wrong.
    """
    import numpy
    from safetensors.numpy import save_file

    records = {
        t.name: {"dtype_code": t.dtype_code if hasattr(t, "dtype_code") else PACKED_DTYPE_CODE,
                 "metadata0": t.metadata0, "metadata1": t.metadata1,
                 "dimensions": list(t.dimensions)}
        for t in sorted(tensors, key=lambda t: t.name)
    }
    payloads = {
        t.name: numpy.frombuffer(blob, dtype=numpy.uint8, count=t.byte_count, offset=t.offset).copy()
        for t in sorted(tensors, key=lambda t: t.name)
    }
    save_file(payloads, str(destination), metadata={
        "format": PACKED_FORMAT,
        "source_kind": "WEIGHTS_HT-resource-blob",
        "source_sha256": library_sha256,
        "resource_sha256": resource_sha256,
        "tensor_count": str(len(tensors)),
        "payload_byte_count": str(sum(t.byte_count for t in tensors)),
        "payload_contract": "opaque backend-specific packed bytes; no dense dtype or shape is inferred",
        "source_tensor_records": json.dumps(records, separators=(",", ":"), sort_keys=True),
    })


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("library", type=pathlib.Path, help="caminho da sua nvngx_dlssnr.dll")
    parser.add_argument("-o", "--out", type=pathlib.Path, default=pathlib.Path("data/pesos"),
                        help="onde gravar (padrao: data/pesos)")
    parser.add_argument("--json", action="store_true", help="imprime o inventario em JSON")
    parser.add_argument("--no-blob", action="store_true",
                        help="nao grava o recurso, so inventaria")
    parser.add_argument("--packed", action="store_true",
                        help="grava tambem packed.safetensors, a entrada do decodificador "
                             "logico do MLX-DLSS")
    args = parser.parse_args()

    try:
        data = args.library.read_bytes()
    except OSError as error:
        print(f"nao consegui ler a biblioteca: {error}", file=sys.stderr)
        return 2

    try:
        blob = read_resource(data)
        tensors = parse_map(blob)
    except FormatError as error:
        print(f"formato inesperado: {error}", file=sys.stderr)
        print("Esta ferramenta le a nvngx_dlssnr.dll 310.8. Outra versao pode ter outro formato.",
              file=sys.stderr)
        return 1

    report = inventory(tensors)
    report["biblioteca"] = str(args.library)
    report["sha256Biblioteca"] = hashlib.sha256(data).hexdigest()
    report["sha256Recurso"] = hashlib.sha256(blob).hexdigest()

    args.out.mkdir(parents=True, exist_ok=True)
    if not args.no_blob:
        (args.out / "weights_ht.bin").write_bytes(blob)
    if args.packed:
        write_packed(blob, tensors, args.out / "packed.safetensors",
                     report["sha256Biblioteca"], report["sha256Recurso"])
    (args.out / "inventario.json").write_text(
        json.dumps({**report, "tensoresDetalhados": [
            {"nome": t.name, "dimensoes": list(t.dimensions), "bytes": t.byte_count,
             "offset": t.offset, "metadados": [t.metadata0, t.metadata1]} for t in tensors]},
            indent=1, ensure_ascii=False), encoding="utf-8")

    if args.json:
        print(json.dumps(report, indent=1, ensure_ascii=False))
        return 0

    mib = report["bytesTotais"] / (1 << 20)
    print(f"{report['tensores']} tensores em {report['blocos']} blocos, {mib:.1f} MiB de pesos")
    print(f"sha256 da biblioteca: {report['sha256Biblioteca'][:16]}...")

    print("\nblocos que se repetem (o mesmo kernel serve a todos):")
    for group in report["gruposRepetidos"]:
        names = group["blocos"]
        span = f"{names[0]}..{names[-1]}" if len(names) > 2 else "+".join(names)
        print(f"  {group['bytesTotais'] / (1 << 20):7.2f} MiB  "
              f"{group['fatiaDoModelo'] * 100:5.1f}%  "
              f"{group['repeticoes']:3}x {group['bytesPorBloco'] / (1 << 20):.2f} MiB  {span}")
    coberto = sum(g["fatiaDoModelo"] for g in report["gruposRepetidos"])
    print(f"  ---> {coberto * 100:.1f}% do modelo esta em blocos repetidos")

    print("\nmaiores blocos:")
    for b in report["maioresBlocos"]:
        print(f"  {b['bytes'] / (1 << 20):7.2f} MiB  {b['camadas']:2} camadas  {b['bloco']}")
    print(f"\ngravado em {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
