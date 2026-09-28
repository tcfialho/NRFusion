from __future__ import annotations

import pathlib
import re
import shutil
import struct
import subprocess


FATBIN_MAGIC = b"\x50\xed\x55\xba"


class ToolError(RuntimeError):
    pass


def find_cuobjdump(explicit: str | None) -> pathlib.Path:
    if explicit:
        return pathlib.Path(explicit)
    found = shutil.which("cuobjdump")
    if found:
        return pathlib.Path(found)
    roots = sorted(pathlib.Path("C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA").glob("v*"),
                   reverse=True)
    for root in roots:
        candidate = root / "bin" / "cuobjdump.exe"
        if candidate.exists():
            return candidate
    raise ToolError("cuobjdump not found: install the CUDA toolkit or pass --cuobjdump")


def carve_fatbins(dll: pathlib.Path, out_dir: pathlib.Path) -> list[pathlib.Path]:
    """The device code sits in the library as plain fat binaries, each with its own size field."""
    blob = dll.read_bytes()
    carved: list[pathlib.Path] = []
    for index, match in enumerate(re.finditer(re.escape(FATBIN_MAGIC), blob)):
        start = match.start()
        if start + 16 > len(blob):
            continue
        header_size, = struct.unpack_from("<H", blob, start + 6)
        payload_size, = struct.unpack_from("<Q", blob, start + 8)
        end = start + header_size + payload_size
        if header_size != 16 or end > len(blob):
            continue
        path = out_dir / f"fat{index:02d}.fatbin"
        path.write_bytes(blob[start:end])
        carved.append(path)
    if not carved:
        raise ToolError(f"no fat binary found in {dll}")
    return carved


def extract_cubins(cuobjdump: pathlib.Path, fatbins: list[pathlib.Path], arch: str) -> list[pathlib.Path]:
    out_dir = fatbins[0].parent
    for fatbin in fatbins:
        subprocess.run([str(cuobjdump), "-xelf", "all", fatbin.name],
                       cwd=out_dir, check=False, capture_output=True)
    return sorted(out_dir.glob(f"*.{arch}.cubin"))


_ELF_CACHE: dict[pathlib.Path, str] = {}


def param_block_size(cuobjdump: pathlib.Path, cubin: pathlib.Path, kernel: str) -> tuple[int, int]:
    """Size of the block and the constant-bank offset it starts at, as the compiler recorded them."""
    dump = _ELF_CACHE.get(cubin)
    if dump is None:
        dump = subprocess.run([str(cuobjdump), "-elf", str(cubin)],
                              capture_output=True, text=True, errors="replace").stdout
        _ELF_CACHE[cubin] = dump
    for block in re.split(r"\n\.nv\.info\.", dump)[1:]:
        if block.split("\n", 1)[0].strip() != kernel:
            continue
        size = re.search(r"EIATTR_CBANK_PARAM_SIZE\s*\n\s*Format:\s*EIFMT_HVAL\s*\n\s*Value:\s*(0x[0-9a-f]+)",
                         block)
        cbank = re.search(r"EIATTR_PARAM_CBANK\s*\n\s*Format:\s*EIFMT_SVAL\s*\n\s*Value:\s*\S+\s+(0x[0-9a-f]+)",
                          block)
        if size and cbank:
            return int(size.group(1), 16), int(cbank.group(1), 16) & 0xFFFF
    raise ToolError(f"{kernel}: no parameter block recorded in {cubin.name}")


def disassemble(cuobjdump: pathlib.Path, cubin: pathlib.Path, kernel: str) -> list[str]:
    dump = subprocess.run([str(cuobjdump), "-sass", "-fun", kernel, str(cubin)],
                          capture_output=True, text=True, errors="replace").stdout
    lines = []
    for raw in dump.splitlines():
        text = re.sub(r"/\*[0-9a-f]{4,}\*/", "", raw).split(";")[0].strip()
        if text:
            lines.append(text)
    return lines


