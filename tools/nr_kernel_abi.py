"""Read proven parameter layout from cuobjdump metadata; semantic roles remain unknown."""

import argparse
import hashlib
import json
import re
from pathlib import Path


def inspect(folder):
    captured = json.loads((folder / "metadata.json").read_text(encoding="utf-8"))
    image = (folder / "module.image").read_bytes()
    if hashlib.sha256(image).hexdigest() != captured["module_sha256"]:
        raise ValueError("Kernel image hash differs from the observed module")
    metadata = (folder / "elf-metadata.txt").read_text(encoding="utf-8")
    name = captured["kernel"]
    marker = ".nv.info." + name
    match = re.search(r"(?m)^" + re.escape(marker) + r"\s*$", metadata)
    if not match:
        raise ValueError(f"No parameter metadata for {name}")
    start = match.end()
    next_section = re.search(r"(?m)^\.nv\.info\.", metadata[start:])
    section = metadata[start:start + next_section.start()] if next_section else metadata[start:]
    parameters = []
    pattern = (r"EIATTR_KPARAM_INFO.*?Index\s*:\s*(0x[0-9a-f]+).*?Ordinal\s*:\s*(0x[0-9a-f]+)"
               r".*?Offset\s*:\s*(0x[0-9a-f]+).*?Size\s*:\s*(0x[0-9a-f]+)")
    for _, ordinal, offset, size in re.findall(pattern, section, re.S):
        parameters.append({"index": int(ordinal, 16), "offset": int(offset, 16),
                           "size": int(size, 16), "type": "opaque aggregate; C++ type unknown"})
    parameters.sort(key=lambda parameter: parameter["index"])
    if not parameters or [parameter["index"] for parameter in parameters] != list(range(len(parameters))):
        raise ValueError("Parameter arity is unresolved")
    extent = max(parameter["offset"] + parameter["size"] for parameter in parameters)
    if any(observation["parameter_bytes"] != extent for observation in captured["observations"]):
        raise ValueError("Observed launch byte count differs from the proven ABI extent")
    bank = re.search(r"EIATTR_PARAM_CBANK.*?Value:\s*(0x[0-9a-f]+)\s+(0x[0-9a-f]+)", section, re.S)
    if not bank:
        raise ValueError("Parameter constant-bank range unresolved")
    packed_bank = int(bank.group(2), 16)
    bank_start, bank_extent = packed_bank & 0xFFFF, packed_bank >> 16
    if bank_extent != extent:
        raise ValueError("Constant-bank extent differs from parameter extent")
    sass = (folder / "kernel.sass").read_text(encoding="utf-8")
    fields = {}
    for line in sass.splitlines():
        read = re.search(r"\b(ULDC(?:\.64)?)\s+[^;]*c\[0x0\]\[(0x[0-9a-f]+)\]", line)
        if not read:
            continue
        offset = int(read.group(2), 16) - bank_start
        size = 8 if read.group(1).endswith(".64") else 4
        if 0 <= offset and offset + size <= extent:
            fields[(offset, size)] = {"offset": offset, "size": size,
                                     "evidence": "constant-bank load width in stock sm_89 SASS",
                                     "semantic_type": "unknown"}
    observations = []
    for observation in captured["observations"]:
        blob = (folder / observation["file"]).read_bytes()
        if len(blob) != extent:
            raise ValueError("Incomplete host parameter block")
        observations.append({"frame": observation["frame"], "sequence": observation["sequence"],
                             "fields": [{**field, "raw_bits_hex": "0x" +
                                         blob[field["offset"]:field["offset"] + field["size"]][::-1].hex()}
                                        for field in sorted(fields.values(), key=lambda item: item["offset"])]})
    return {"schema_version": 1, "kernel": name, "module_sha256": captured["module_sha256"],
            "gpu_architecture": "sm_89", "parameter_count": len(parameters), "parameters": parameters,
            "parameter_block_bytes": extent, "parameter_constant_bank_start": bank_start,
            "structural_abi_resolved": True, "semantic_abi_resolved": False,
            "pointer_classification": "not performed", "shape_semantics": "unresolved",
            "confidence": "arity and sizes confirmed by stock ELF metadata and actual launch byte count",
            "observations": observations}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("folder", type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    report = inspect(args.folder)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"{report['kernel']}: {report['parameter_count']} parameter(s), "
          f"{report['parameter_block_bytes']} bytes; semantic ABI unresolved")


if __name__ == "__main__":
    main()
