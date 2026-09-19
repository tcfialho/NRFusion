#!/usr/bin/env python3
"""Read the argument block of the swin kernels out of the runtime the machine already has.

A kernel cannot be replaced without knowing what its single argument means, and that
meaning is in no header. It is in the shipped device code: the compiler records the size
of the block, and the instructions say which offset becomes a load address, which becomes
a store address, which is polled in a sleep loop, and which is only arithmetic.

So the block is read from `nvngx_dlssnr.dll` itself -- the user's own copy, nothing
downloaded, nothing redistributed -- instead of from a capture of a running game. The
layout that comes out holds for every game that loads this runtime, and a later dump taken
from a live frame only has to agree with it.

    python tools/decode_swin_abi.py data/nvngx_dlssnr.dll
    python tools/decode_swin_abi.py data/nvngx_dlssnr.dll --json abi.json --keep .build/cubins
"""
from __future__ import annotations
import argparse
import collections
import json
import pathlib
import re
import shutil
import struct
import subprocess
import sys
import tempfile

FATBIN_MAGIC = b"\x50\xed\x55\xba"

# The four resolution levels of the swin stack in the denoiser. The number after the head
# count is the channel width the layer was compiled for, which is what decides the tiling.
FAMILIES = ("1h_32_1", "2h_64_2", "4h_128_4", "8h_256_8")

# One variant never touches the whole block: the downsampling one writes a second output, the
# upsampling one reads a second input, the waiting one polls a semaphore the plain one ignores.
# Only the union over the variants shows every field.
VARIANTS = ("_fp8", "_chained_fp8", "_wait_fp8", "_ds_fp8", "_ds_wait_fp8",
            "_upsample_fp8", "_inpview_fp8", "_outview_fp8", "_tilesync_fp8")

ROLE_INPUT = "input"
ROLE_OUTPUT = "output"
ROLE_WAIT = "wait"
ROLE_PUBLISH = "publish"
ROLE_SCALAR = "scalar"

RANK = {ROLE_SCALAR: 0, ROLE_INPUT: 1, ROLE_OUTPUT: 1, ROLE_WAIT: 2, ROLE_PUBLISH: 2}


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


MEMORY_OP = re.compile(r"\b(LDG|STG|RED|ATOMG)\S*\s+(?:U?R\d+,\s*)?\[(U?R\d+)")
# The asynchronous copy names shared memory first and global memory second, so the address that
# says anything about the argument is the one in the SECOND pair of brackets.
ASYNC_COPY = re.compile(r"\bLDGSTS\S*\s+\[[^\]]+\],\s*\[(U?R\d+)")
CONST_USE = re.compile(r"c\[0x0\]\[(0x[0-9a-f]+)\]")
INSTRUCTION = re.compile(r"^(?:@!?U?P\d+\s+)?([A-Z][A-Z0-9._]*)\s*(.*)$")
REGISTER = re.compile(r"^-?\|?(U?R\d+)")

# An address is built out of adds, shifts and moves, and nothing else. Following only these keeps a
# pointer's trail from leaking into a register that merely happened to be reused for something else.
ADDRESS_ARITHMETIC = ("LEA", "IADD3", "IMAD", "MOV", "SHF", "ULDC", "UIADD3", "ULEA", "UIMAD", "UMOV")


def address_arithmetic(opcode: str) -> bool:
    return opcode.split(".")[0] in ADDRESS_ARITHMETIC


def operands(text: str) -> list[str]:
    return [part.strip() for part in text.split(",") if part.strip()]


def classify(lines: list[str], base: int, size: int) -> dict[int, dict]:
    """Give every offset in the block the strongest role its own instructions justify.

    A load address is an input and a store address an output. A load that spins next to a
    NANOSLEEP is not an input but a semaphore the kernel waits on, and a store fenced by
    MEMBAR is the one it publishes -- the pair that chains one layer to the next.
    """
    fields: dict[int, dict] = {}

    def entry_for(offset: int) -> dict | None:
        if not base <= offset < base + size:
            return None
        return fields.setdefault(offset - base,
                                 {"role": ROLE_SCALAR, "evidence": set(), "ops": collections.Counter()})

    def note(offset: int, role: str, evidence: str) -> None:
        entry = entry_for(offset)
        if entry is None:
            return
        if RANK[role] >= RANK[entry["role"]]:
            entry["role"] = role
        entry["evidence"].add(evidence)

    # A sleep loop and a fence are local facts. Mark the windows they cover first, so the
    # memory operations inside them read as semaphore traffic rather than as data traffic.
    sleepy = [i for i, line in enumerate(lines) if "NANOSLEEP" in line]
    fenced = [i for i, line in enumerate(lines) if "MEMBAR" in line or "ERRBAR" in line]

    provenance: dict[str, set[int]] = {}
    for index, line in enumerate(lines):
        match = ASYNC_COPY.search(line)
        if match:
            for offset in provenance.get(match.group(1), ()):
                note(offset, ROLE_INPUT, "asynchronous copy into shared memory")
            continue
        match = MEMORY_OP.search(line)
        if match:
            op, register = match.group(1), match.group(2)
            near_sleep = any(abs(index - i) <= 6 for i in sleepy)
            near_fence = any(abs(index - i) <= 4 for i in fenced)
            for offset in provenance.get(register, ()):
                if op == "LDG" and near_sleep:
                    note(offset, ROLE_WAIT, "polled beside NANOSLEEP")
                elif op != "LDG" and near_fence:
                    note(offset, ROLE_PUBLISH, "stored behind MEMBAR")
                elif op == "LDG":
                    note(offset, ROLE_INPUT, "load address")
                else:
                    note(offset, ROLE_OUTPUT, "store address")
            continue

        parsed = INSTRUCTION.match(line)
        if not parsed:
            continue
        opcode, rest = parsed.group(1), parsed.group(2)
        parts = operands(rest)
        destination = next((REGISTER.match(part).group(1) for part in parts
                            if REGISTER.match(part)), None)
        if destination is None:
            continue
        if not address_arithmetic(opcode):
            provenance.pop(destination, None)          # written by something that is not an address
            continue

        carried: set[int] = set()
        for part in parts[1:]:
            for constant in CONST_USE.finditer(part):
                offset = int(constant.group(1), 16)
                if base <= offset < base + size:
                    carried.add(offset)
            source = REGISTER.match(part)
            if source:
                carried |= provenance.get(source.group(1), set())
        if carried:
            provenance[destination] = carried
            if opcode.startswith("ULDC.64") and destination.startswith("UR"):
                provenance[f"UR{int(destination[2:]) + 1}"] = set(carried)
        else:
            provenance.pop(destination, None)

    for line in lines:
        head = re.sub(r"^@!?U?P\d+\s*", "", line).split()[0]
        for match in CONST_USE.finditer(line):
            entry = entry_for(int(match.group(1), 16))
            if entry is not None:
                entry["ops"][head] += 1
    return dict(sorted(fields.items()))


def merge_field(merged: dict[int, dict], offset: int, entry: dict, variant: str) -> None:
    """Keep the strongest role any variant justifies, and record which one justified it."""
    target = merged.setdefault(offset, {"role": ROLE_SCALAR, "evidence": set(),
                                        "variants": set(), "ops": collections.Counter()})
    if RANK[entry["role"]] > RANK[target["role"]]:
        target["role"] = entry["role"]
        target["variants"] = {variant}
    elif RANK[entry["role"]] == RANK[target["role"]] and entry["role"] != ROLE_SCALAR:
        target["variants"].add(variant)
    target["evidence"] |= entry["evidence"]
    target["ops"] += entry["ops"]


def report(kernel: str, size: int, base: int, fields: dict[int, dict]) -> str:
    out = [kernel, f"  block: {size} bytes at c[0x0][{base:#06x}]"]
    for offset, entry in fields.items():
        evidence = ", ".join(sorted(entry["evidence"])) or "arithmetic only"
        where = ",".join(sorted(v.strip("_") for v in entry.get("variants", ()))) or "-"
        out.append(f"  +{offset:<3d} {entry['role']:<8s} {evidence:<38s} {where}")
    return "\n".join(out)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("dll", type=pathlib.Path, help="the machine's own nvngx_dlssnr.dll")
    parser.add_argument("--arch", default="sm_89", help="device architecture to read (default sm_89)")
    parser.add_argument("--variants", nargs="*", default=list(VARIANTS),
                        help="kernel suffixes to decode; roles are merged across them")
    parser.add_argument("--families", nargs="*", default=list(FAMILIES))
    parser.add_argument("--kernels", nargs="*", default=None,
                        help="decode these kernels by full name instead of the swin families")
    parser.add_argument("--cuobjdump", default=None)
    parser.add_argument("--json", type=pathlib.Path, default=None)
    parser.add_argument("--keep", type=pathlib.Path, default=None, help="where to leave the carved cubins")
    parser.add_argument("--cubins", type=pathlib.Path, default=None,
                        help="reuse cubins carved by an earlier run instead of re-carving the library")
    args = parser.parse_args()

    try:
        cuobjdump = find_cuobjdump(args.cuobjdump)
    except ToolError as error:
        print(error, file=sys.stderr)
        return 2

    if args.cubins:
        cubins = sorted(args.cubins.glob(f"*.{args.arch}.cubin"))
        workdir = None
    else:
        if not args.dll.exists():
            print(f"{args.dll}: not found", file=sys.stderr)
            return 2
        workdir = args.keep or pathlib.Path(tempfile.mkdtemp(prefix="swin-abi-"))
        workdir.mkdir(parents=True, exist_ok=True)
        cubins = extract_cubins(cuobjdump, carve_fatbins(args.dll, workdir), args.arch)
    try:
        if not cubins:
            print(f"no {args.arch} device code found", file=sys.stderr)
            return 1

        contents = {cubin: cubin.read_bytes() for cubin in cubins}
        result: dict[str, dict] = {}

        # The swin layers are one kernel each, but the wider blocks split their layer into stages
        # that are launched separately. Those stages are named in full instead of by family.
        for kernel in args.kernels or ():
            holder = next((c for c in cubins if kernel.encode("ascii") in contents[c]), None)
            if holder is None:
                print(f"{kernel}: not present in this runtime", file=sys.stderr)
                continue
            size, base = param_block_size(cuobjdump, holder, kernel)
            fields = classify(disassemble(cuobjdump, holder, kernel), base, size)
            merged: dict[int, dict] = {}
            for offset, entry in fields.items():
                merge_field(merged, offset, entry, kernel)
            print(report(kernel, size, base, merged))
            print()
            result[kernel] = {
                "cubin": holder.name,
                "blockBytes": size,
                "constantBankOffset": base,
                "fields": {str(offset): {"role": entry["role"], "evidence": sorted(entry["evidence"])}
                           for offset, entry in merged.items()},
            }
        if args.kernels:
            if args.json:
                args.json.write_text(json.dumps(result, indent=2), encoding="ascii")
                print(f"wrote {args.json}")
            return 0 if result else 1

        for family in args.families:
            merged: dict[int, dict] = {}
            size = base = 0
            holder_name = ""
            for variant in args.variants:
                kernel = f"cc_tinlayout_fused_swin_{family}{variant}"
                holder = next((c for c in cubins if kernel.encode("ascii") in contents[c]), None)
                if holder is None:
                    continue
                size, base = param_block_size(cuobjdump, holder, kernel)
                holder_name = holder.name
                for offset, entry in classify(disassemble(cuobjdump, holder, kernel), base, size).items():
                    merge_field(merged, offset, entry, variant)
            if not merged:
                print(f"swin {family}: no matching kernel in this runtime", file=sys.stderr)
                continue
            merged = dict(sorted(merged.items()))
            print(report(f"cc_tinlayout_fused_swin_{family}", size, base, merged))
            print()
            result[family] = {
                "cubin": holder_name,
                "variants": list(args.variants),
                "blockBytes": size,
                "constantBankOffset": base,
                "fields": {str(offset): {"role": entry["role"],
                                         "evidence": sorted(entry["evidence"]),
                                         "variants": sorted(entry["variants"]),
                                         "instructions": dict(entry["ops"])}
                           for offset, entry in merged.items()},
            }
        if args.json:
            args.json.write_text(json.dumps(result, indent=2), encoding="ascii")
            print(f"wrote {args.json}")
        return 0 if result else 1
    finally:
        if workdir is not None and args.keep is None:
            shutil.rmtree(workdir, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
