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
import struct
import sys
from dataclasses import dataclass, field

RESOURCE_NAME = "WEIGHTS_HT"
RCDATA = 10
PACKED_DTYPE_CODE = 1
PACKED_ELEMENT_BYTES = 2


class FormatError(RuntimeError):
    """The file is not the expected library, or its resource is not the expected shape."""


def _need(data: bytes, offset: int, size: int, what: str) -> None:
    if offset < 0 or size < 0 or offset + size > len(data):
        raise FormatError(f"truncado: {what}")


def _u16(d: bytes, o: int, w: str) -> int:
    _need(d, o, 2, w)
    return struct.unpack_from("<H", d, o)[0]


def _u32(d: bytes, o: int, w: str) -> int:
    _need(d, o, 4, w)
    return struct.unpack_from("<I", d, o)[0]


def _u64(d: bytes, o: int, w: str) -> int:
    _need(d, o, 8, w)
    return struct.unpack_from("<Q", d, o)[0]


@dataclass(frozen=True)
class Section:
    virtual_address: int
    virtual_size: int
    raw_offset: int
    raw_size: int

    def file_offset(self, rva: int, size: int) -> int | None:
        if rva < self.virtual_address:
            return None
        delta = rva - self.virtual_address
        if delta + size > min(self.virtual_size or self.raw_size, self.raw_size):
            return None
        return self.raw_offset + delta


def _pe_resource_root(data: bytes) -> tuple[int, int, list[Section]]:
    if data[:2] != b"MZ":
        raise FormatError("nao e um executavel do Windows (falta MZ)")
    pe = _u32(data, 0x3C, "cabecalho DOS")
    if data[pe:pe + 4] != b"PE\0\0":
        raise FormatError("nao e um executavel do Windows (falta assinatura PE)")
    coff = pe + 4
    section_count = _u16(data, coff + 2, "cabecalho COFF")
    optional_size = _u16(data, coff + 16, "cabecalho COFF")
    optional = coff + 20
    if _u16(data, optional, "cabecalho opcional") != 0x20B:
        raise FormatError("esperado um binario de 64 bits")
    rva = _u32(data, optional + 112 + 16, "diretorio de recursos")
    size = _u32(data, optional + 112 + 20, "diretorio de recursos")
    if not rva or not size:
        raise FormatError("o binario nao tem diretorio de recursos")
    base = optional + optional_size
    sections = [
        Section(
            virtual_size=_u32(data, base + i * 40 + 8, "tabela de secoes"),
            virtual_address=_u32(data, base + i * 40 + 12, "tabela de secoes"),
            raw_size=_u32(data, base + i * 40 + 16, "tabela de secoes"),
            raw_offset=_u32(data, base + i * 40 + 20, "tabela de secoes"),
        )
        for i in range(section_count)
    ]
    return rva, size, sections


def _to_file_offset(sections: list[Section], rva: int, size: int, what: str) -> int:
    for section in sections:
        offset = section.file_offset(rva, size)
        if offset is not None:
            return offset
    raise FormatError(f"{what}: endereco fora das secoes gravadas no arquivo")


def read_resource(data: bytes, name: str = RESOURCE_NAME) -> bytes:
    """Walk the three-level PE resource tree (type -> name -> language) to the payload."""
    root_rva, root_size, sections = _pe_resource_root(data)
    root = _to_file_offset(sections, root_rva, root_size, "diretorio de recursos")

    def at(relative: int, size: int, what: str) -> int:
        if relative < 0 or size < 0 or relative > root_size - size:
            raise FormatError(f"truncado: {what}")
        absolute = root + relative
        _need(data, absolute, size, what)
        return absolute

    def children(relative: int) -> list[tuple[int, int]]:
        directory = at(relative, 16, "diretorio de recursos")
        count = _u16(data, directory + 12, "diretorio") + _u16(data, directory + 14, "diretorio")
        table = at(relative + 16, count * 8, "entradas de recurso")
        return [(_u32(data, table + i * 8, "entrada"), _u32(data, table + i * 8 + 4, "entrada"))
                for i in range(count)]

    def entry_name(value: int) -> str | None:
        if not value & 0x8000_0000:
            return None
        string_at = at(value & 0x7FFF_FFFF, 2, "nome de recurso")
        length = _u16(data, string_at, "nome de recurso")
        raw = at((value & 0x7FFF_FFFF) + 2, length * 2, "nome de recurso")
        return data[raw:raw + length * 2].decode("utf-16-le")

    type_directory = next(
        (child & 0x7FFF_FFFF for key, child in children(0)
         if child & 0x8000_0000 and key == RCDATA), None)
    if type_directory is None:
        raise FormatError("o binario nao tem recursos do tipo RCDATA")

    name_directory = next(
        (child & 0x7FFF_FFFF for key, child in children(type_directory)
         if child & 0x8000_0000 and entry_name(key) == name), None)
    if name_directory is None:
        raise FormatError(f"o binario nao tem o recurso {name}")

    languages = children(name_directory)
    if len(languages) != 1 or languages[0][1] & 0x8000_0000:
        raise FormatError(f"o recurso {name} nao tem exatamente uma entrada de idioma")
    data_entry = at(languages[0][1], 16, "descritor de dados")
    payload_rva = _u32(data, data_entry, "descritor de dados")
    payload_size = _u32(data, data_entry + 4, "descritor de dados")
    payload = _to_file_offset(sections, payload_rva, payload_size, f"recurso {name}")
    _need(data, payload, payload_size, f"recurso {name}")
    return data[payload:payload + payload_size]


@dataclass
class Tensor:
    name: str
    dimensions: tuple[int, ...]
    byte_count: int
    offset: int
    metadata0: int
    metadata1: int

    @property
    def elements(self) -> int:
        total = 1
        for d in self.dimensions:
            total *= d
        return total


def parse_map(blob: bytes) -> list[Tensor]:
    """The resource is a self-sized map: total length, then name/record pairs."""
    declared = _u64(blob, 0, "mapa de pesos")
    if declared != len(blob):
        raise FormatError(f"tamanho declarado {declared} difere do real {len(blob)}")
    out: list[Tensor] = []
    seen: set[str] = set()
    offset = 8
    while offset < len(blob):
        name_length = _u64(blob, offset, "tamanho do nome")
        offset += 8
        if not 0 < name_length <= 4096:
            raise FormatError(f"tamanho de nome invalido: {name_length}")
        _need(blob, offset, name_length, "nome do tensor")
        name = blob[offset:offset + name_length].decode("utf-8")
        offset += name_length
        if name in seen:
            raise FormatError(f"tensor repetido: {name}")
        seen.add(name)

        outer = _u64(blob, offset, f"{name}: tamanho do registro")
        offset += 8
        # The record's length is counted from just after its own length field.
        record_end = offset + outer
        _need(blob, offset, outer, f"{name}: registro")
        if outer < 40:
            raise FormatError(f"registro pequeno demais para {name}")
        inner = _u64(blob, offset, f"{name}: tamanho interno")
        if inner != outer:
            raise FormatError(f"{name}: tamanho interno {inner} difere de {outer}")
        offset += 8
        byte_count = _u64(blob, offset, f"{name}: bytes")
        offset += 8
        dtype = _u32(blob, offset, f"{name}: tipo")
        offset += 4
        if dtype != PACKED_DTYPE_CODE:
            raise FormatError(f"{name}: tipo {dtype} nao suportado")
        payload_at = offset
        offset += byte_count

        metadata0 = _u32(blob, offset, f"{name}: metadados")
        metadata1 = _u32(blob, offset + 4, f"{name}: metadados")
        dimension_count = _u64(blob, offset + 8, f"{name}: numero de dimensoes")
        if not 0 < dimension_count <= 16:
            raise FormatError(f"{name}: {dimension_count} dimensoes")
        if record_end - (offset + 16) != dimension_count * 4:
            raise FormatError(f"{name}: tamanho de metadados inesperado")
        dimensions = tuple(_u32(blob, offset + 16 + i * 4, f"{name}: dimensao")
                           for i in range(dimension_count))
        offset = record_end

        tensor = Tensor(name, dimensions, byte_count, payload_at, metadata0, metadata1)
        if 0 in dimensions:
            raise FormatError(f"{name}: dimensao zero")
        if byte_count != tensor.elements * PACKED_ELEMENT_BYTES:
            raise FormatError(f"{name}: {byte_count} bytes para {tensor.elements} elementos")
        out.append(tensor)

    if offset != len(blob):
        raise FormatError("o mapa terminou num deslocamento invalido")
    if not out:
        raise FormatError("o mapa nao contem tensores")
    return out


def block_of(name: str) -> tuple[str, str]:
    """Split `block31.layer1.layer` into ("block31", "layer1"). The packed container keeps no
    logical shapes, so the block index is the only structure the file itself offers."""
    parts = name.split(".")
    return (parts[0] if parts else name, parts[1] if len(parts) > 1 else "")


def inventory(tensors: list[Tensor]) -> dict:
    total_bytes = sum(t.byte_count for t in tensors)

    blocks: dict[str, dict] = {}
    for t in tensors:
        block, layer = block_of(t.name)
        entry = blocks.setdefault(block, {"bytes": 0, "camadas": {}})
        entry["bytes"] += t.byte_count
        entry["camadas"][layer or t.name] = t.byte_count

    # Blocks whose layer sizes are identical are the repeated stack. They are the target that
    # matters: one kernel written once applies to every copy, so their share of the model is
    # the ceiling on what fusion or a cheaper format can reach.
    signatures: dict[tuple, list[str]] = {}
    for name, entry in blocks.items():
        signature = tuple(sorted(entry["camadas"].values()))
        signatures.setdefault(signature, []).append(name)
    repeated = sorted(
        ({"blocos": sorted(names, key=lambda n: (len(n), n)),
          "repeticoes": len(names),
          "bytesPorBloco": sum(signature),
          "bytesTotais": sum(signature) * len(names),
          "fatiaDoModelo": sum(signature) * len(names) / total_bytes if total_bytes else 0.0}
         for signature, names in signatures.items() if len(names) > 1),
        key=lambda g: -g["bytesTotais"])

    return {
        "tensores": len(tensors),
        "bytesTotais": total_bytes,
        "blocos": len(blocks),
        "gruposRepetidos": repeated,
        "maioresBlocos": [
            {"bloco": name, "bytes": entry["bytes"], "camadas": len(entry["camadas"])}
            for name, entry in sorted(blocks.items(), key=lambda kv: -kv[1]["bytes"])[:12]
        ],
        "maioresTensores": [
            {"nome": t.name, "bytes": t.byte_count, "elementos": t.elements}
            for t in sorted(tensors, key=lambda t: -t.byte_count)[:12]
        ],
    }


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
