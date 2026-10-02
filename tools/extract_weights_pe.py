from __future__ import annotations

import struct
from dataclasses import dataclass


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


