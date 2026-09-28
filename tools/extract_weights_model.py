from __future__ import annotations

from dataclasses import dataclass

from extract_weights_pe import (
    FormatError,
    PACKED_DTYPE_CODE,
    PACKED_ELEMENT_BYTES,
    _need,
    _u32,
    _u64,
)


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


