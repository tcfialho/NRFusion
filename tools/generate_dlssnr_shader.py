from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import subprocess
from pathlib import Path

EXPECTED_SOURCE_BLOB = "4a6102820f736e9349ffed370259d094f2a7f4ae"
EXPECTED_CSO_BLOB = "d6eaab373d6f07142af5c283c1acc4b49edba351"
EXPECTED_HEADER_BLOB = "23429d34833b5f4ad761f83446998d518217183d"


def git_blob_sha1(data: bytes) -> str:
    prefix = f"blob {len(data)}\0".encode("ascii")
    return hashlib.sha1(prefix + data).hexdigest()


def require_blob(data: bytes, expected: str, label: str) -> None:
    actual = git_blob_sha1(data)
    if actual != expected:
        raise RuntimeError(f"{label} blob mismatch: expected {expected}, got {actual}")


def find_fxc(explicit: str | None) -> Path:
    if explicit:
        candidate = Path(explicit)
        if candidate.is_file():
            return candidate
        raise RuntimeError(f"fxc.exe not found at {candidate}")

    found = shutil.which("fxc.exe") or shutil.which("fxc")
    if found:
        return Path(found)

    program_files = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"))
    kits = program_files / "Windows Kits" / "10" / "bin"
    candidates = sorted(kits.glob("*/x64/fxc.exe"), reverse=True)
    if candidates:
        return candidates[0]
    raise RuntimeError("fxc.exe not found; install the Windows SDK")


def render_header(shader: bytes) -> bytes:
    chunks = ["#pragma once\n\n", "inline static const unsigned char DlssNr_cso[] = {\n    "]
    for index, byte in enumerate(shader):
        chunks.append(f"0x{byte:02x}")
        if index < len(shader) - 1:
            chunks.append(", ")
        if (index + 1) % 12 == 0:
            chunks.append("\n    ")
    chunks.append("\n};\n")
    return "".join(chunks).encode("utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--fxc")
    parser.add_argument("--verify-source-only", action="store_true")
    args = parser.parse_args()

    source = args.source.read_bytes()
    require_blob(source, EXPECTED_SOURCE_BLOB, "dlssnr.hlsl")
    if args.verify_source_only:
        print(f"verified dlssnr.hlsl {EXPECTED_SOURCE_BLOB}")
        return 0
    if args.output_dir is None:
        parser.error("--output-dir is required unless --verify-source-only is used")

    fxc = find_fxc(args.fxc)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    cso_path = args.output_dir / "DlssNr_Shader.cso"
    header_path = args.output_dir / "DlssNr_Shader.h"

    subprocess.run([
        str(fxc), "-T", "cs_5_0", "-E", "CSMain", "-O3",
        str(args.source), "-Fo", str(cso_path),
    ], check=True)

    cso = cso_path.read_bytes()
    require_blob(cso, EXPECTED_CSO_BLOB, "DlssNr_Shader.cso")
    header = render_header(cso)
    require_blob(header, EXPECTED_HEADER_BLOB, "DlssNr_Shader.h")
    header_path.write_bytes(header)

    print(f"verified {cso_path.name} {EXPECTED_CSO_BLOB}")
    print(f"verified {header_path.name} {EXPECTED_HEADER_BLOB}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
