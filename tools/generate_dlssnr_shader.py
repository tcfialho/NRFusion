from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import subprocess
from pathlib import Path

EXPECTED_FXC_BLOB = "987eb4cae3c343ab024a4b693dbb73660360dbc4"

VARIANTS = {
    "no-keep": {
        "source_blob": "4a6102820f736e9349ffed370259d094f2a7f4ae",
        "cso_blob": "8bbe804744e36ce81caa6ab85a66330f8849addf",
        "header_blob": "23f54a819a5503b078e99f2d3a05521eea1e6661",
        "cso_name": "DlssNr_NoKeep.cso",
        "header_name": "DlssNr_NoKeep.h",
        "symbol": "DlssNr_no_keep_cso",
    },
    "resolve-in-place": {
        "source_blob": "4a6102820f736e9349ffed370259d094f2a7f4ae",
        "cso_blob": "9391f936b80ce6c03240ce58c5693cf77312f6d9",
        "header_blob": "cd55850763ded67dab860524c545b8e376adf039",
        "cso_name": "DlssNr_ResolveInPlace.cso",
        "header_name": "DlssNr_ResolveInPlace.h",
        "symbol": "DlssNr_resolve_in_place_cso",
    },
    "main": {
        "source_blob": "4a6102820f736e9349ffed370259d094f2a7f4ae",
        "cso_blob": "d6eaab373d6f07142af5c283c1acc4b49edba351",
        "header_blob": "23429d34833b5f4ad761f83446998d518217183d",
        "cso_name": "DlssNr_Shader.cso",
        "header_name": "DlssNr_Shader.h",
        "symbol": "DlssNr_cso",
    },
    "residual": {
        "source_blob": "1aa829e15bd849be6b38e3f9a4265d0405f444be",
        "cso_blob": "085b00c6ef130240163bbbb4b0bb66b38e7d4349",
        "header_blob": "51f5a26b1bf6e4e01927fab7f69b2ca874c9ce76",
        "cso_name": "dlssnr_residual_Shader.cso",
        "header_name": "dlssnr_residual_Shader.h",
        "symbol": "dlssnr_residual_cso",
    },
    "residual-in-place": {
        "source_blob": "1aa829e15bd849be6b38e3f9a4265d0405f444be",
        "cso_blob": "e2c9978578eda151f5329ba15cc3ef87f3a995d5",
        "header_blob": "4bd8deb98373c5c8024964c2fe65bced8c438486",
        "cso_name": "DlssNr_ResidualInPlace.cso",
        "header_name": "DlssNr_ResidualInPlace.h",
        "symbol": "dlssnr_residual_in_place_cso",
    },
}


def git_blob_sha1(data: bytes) -> str:
    prefix = f"blob {len(data)}\0".encode("ascii")
    return hashlib.sha1(prefix + data).hexdigest()


def require_blob(
    data: bytes, expected: str, label: str, *, text: bool = False
) -> None:
    if text:
        data = data.replace(b"\r\n", b"\n")
    actual = git_blob_sha1(data)
    if actual != expected:
        raise RuntimeError(f"{label} blob mismatch: expected {expected}, got {actual}")


def find_fxc(explicit: str | None) -> Path:
    candidates: list[Path] = []
    if explicit:
        candidates.append(Path(explicit))
    configured = os.environ.get("NRFUSION_FXC")
    if configured:
        candidates.append(Path(configured))
    found = shutil.which("fxc.exe") or shutil.which("fxc")
    if found:
        candidates.append(Path(found))
    for candidate in candidates:
        if candidate.is_file() and git_blob_sha1(candidate.read_bytes()) == EXPECTED_FXC_BLOB:
            return candidate
    raise RuntimeError(
        "locked upstream fxc.exe not found; expected Git blob "
        f"{EXPECTED_FXC_BLOB}"
    )


def render_header(shader: bytes, symbol: str) -> bytes:
    chunks = ["#pragma once\n\n", f"inline static const unsigned char {symbol}[] = {{\n    "]
    for index, byte in enumerate(shader):
        chunks.append(f"0x{byte:02x}")
        if index < len(shader) - 1:
            chunks.append(
                ",\n    " if (index + 1) % 12 == 0 else ", "
            )
    chunks.append("\n};\n")
    return "".join(chunks).encode("utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--variant", choices=VARIANTS, default="main")
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--fxc")
    parser.add_argument("--verify-source-only", action="store_true")
    args = parser.parse_args()

    profile = VARIANTS[args.variant]
    source = args.source.read_bytes()
    require_blob(
        source, profile["source_blob"], args.source.name, text=True
    )
    if args.verify_source_only:
        print(f"verified {args.source.name} {profile['source_blob']}")
        return 0
    if args.output_dir is None:
        parser.error("--output-dir is required unless --verify-source-only is used")

    fxc = find_fxc(args.fxc)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    cso_path = args.output_dir / profile["cso_name"]
    header_path = args.output_dir / profile["header_name"]
    compile_source = args.source
    if args.variant == "no-keep":
        statement = b"gKeep[id.xy] = float4(frame, source.a);"
        if source.count(statement) != 1:
            raise RuntimeError("Expected unique encode keep store was not found")
        compile_source = args.output_dir / "DlssNr_NoKeep.hlsl"
        compile_source.write_bytes(source.replace(statement, b""))
    elif args.variant == "resolve-in-place":
        old_stmt_crlf = (
            b"    float4 originalSample = gCompareMode == 1 ? gOriginal.SampleLevel(gLinear, cmpUv, 0)\r\n"
            b"                                              : gOriginal.Load(int3(id.xy, 0));"
        )
        old_stmt_lf = (
            b"    float4 originalSample = gCompareMode == 1 ? gOriginal.SampleLevel(gLinear, cmpUv, 0)\n"
            b"                                              : gOriginal.Load(int3(id.xy, 0));"
        )
        new_stmt = b"    float4 originalSample = gTarget[id.xy];"
        if old_stmt_crlf in source:
            source_mod = source.replace(old_stmt_crlf, new_stmt)
        elif old_stmt_lf in source:
            source_mod = source.replace(old_stmt_lf, new_stmt)
        else:
            raise RuntimeError("Expected unique Resolve original sample statement was not found")
        compile_source = args.output_dir / "DlssNr_ResolveInPlace.hlsl"
        compile_source.write_bytes(source_mod)
    elif args.variant == "residual-in-place":
        old_stmt = b"        float4 base  = gSource.Load(int3(id.xy, 0));"
        new_stmt = b"        float4 base  = gTarget[id.xy];"
        if old_stmt not in source:
            raise RuntimeError("Expected unique residual load statement was not found")
        source_mod = source.replace(old_stmt, new_stmt)
        source_mod = source_mod.replace(b"    gTarget[id.xy] = gSource.Load(int3(id.xy, 0));", b"    /* in-place no-op */")
        compile_source = args.output_dir / "DlssNr_ResidualInPlace.hlsl"
        compile_source.write_bytes(source_mod)
    subprocess.run([
        str(fxc), "-T", "cs_5_0", "-E", "CSMain", "-O3",
        str(compile_source), "-Fo", str(cso_path),
    ], check=True)

    cso = cso_path.read_bytes()
    require_blob(cso, profile["cso_blob"], cso_path.name)
    header = render_header(cso, profile["symbol"])
    require_blob(header, profile["header_blob"], header_path.name)
    header_path.write_bytes(header)
    print(f"verified {cso_path.name} {profile['cso_blob']}")
    print(f"verified {header_path.name} {profile['header_blob']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
