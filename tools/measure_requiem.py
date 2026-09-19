"""Bounded original-path diagnostic. NGX timings are not NR qualification timings."""

import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys


RUNTIME_SHA256 = "6eb209e764f39872625debd6abaf45e2bb6322f6f270f781f70c059ae30b3927"
ROOT = Path(__file__).resolve().parents[1]


def configure_original(contents):
    # Preserve comments, other sections and the original file for restoration.
    sections = {
        "DlssNr": {
            "Enabled": "true", "FusionMode": "4", "Precision": "0",
            "WorkingScale": "1.0", "Passes": "1", "RunBeforeSR": "false",
            "DeferredDLSS": "false", "ResidualAcrossRR": "false", "ResidualFG": "false",
            "AutoCapture": "false", "Preset": "0", "Style": "0", "Intensity": "1.0",
            "LocalStructure": "1.0", "LocalTone": "1.0", "SkinStructure": "-1.0",
            "AutoMask": "true", "TransferStrength": "1.0", "ColourStrength": "1.0",
            "WhitePointScale": "1.0", "MaxRatio": "2.0", "DebugView": "0",
        },
    }
    for section, settings in sections.items():
        match = re.search(rf"(?ms)^\[{re.escape(section)}\][^\n]*\n(.*?)(?=^\[|\Z)", contents)
        if not match:
            raise RuntimeError(f"Missing INI section: {section}")
        block = match.group(1)
        for key, value in settings.items():
            expression = rf"(?m)^{re.escape(key)}\s*=.*$"
            if re.search(expression, block):
                block = re.sub(expression, f"{key}={value}", block)
            else:
                block += f"\n{key}={value}\n"
        contents = contents[:match.start(1)] + block + contents[match.end(1):]
    return contents


def percentile(values, fraction):
    ordered = sorted(values)
    position = (len(ordered) - 1) * fraction
    lower = int(position)
    upper = min(lower + 1, len(ordered) - 1)
    return ordered[lower] + (ordered[upper] - ordered[lower]) * (position - lower)


def measure(args):
    executable = args.testbed.resolve()
    runtime = executable.parent / "nvngx_dlssnr.dll"
    ini = executable.parent / "OptiScaler.ini"
    if not executable.is_file() or not ini.is_file():
        raise RuntimeError(f"Missing testbed or configuration beside {executable}")
    runtime_hash = sha256(runtime)
    if runtime_hash != RUNTIME_SHA256:
        raise RuntimeError(f"Runtime SHA-256 mismatch: {runtime_hash}")
    if args.frames <= args.warmup or args.warmup < 0:
        raise RuntimeError("--frames must exceed --warmup >= 0")
    output = args.out.resolve()
    # Every run owns a new directory. Never combine old captures/logs with a new measurement.
    output.mkdir(parents=True, exist_ok=False)
    original_ini = ini.read_bytes()
    (output / "OptiScaler.original.ini").write_bytes(original_ini)
    fixed_ini = configure_original(original_ini.decode("utf-8-sig"))
    (output / "OptiScaler.measured.ini").write_text(fixed_ini, encoding="utf-8")
    width, height = (1280, 720) if args.height == 720 else (1920, 1080)
    command = [str(executable), "--width", str(width), "--height", str(height),
               "--frames", str(args.frames), "--warmup", str(args.warmup),
               "--csv", str(output / "evaluation.csv"), "--capture", str(output / "calculated.ppm"),
               "--deterministic-motion" if args.motion else "--fixed-scene"]
    if args.reference_on:
        command.append("--reference-on")
    try:
        ini.write_text(fixed_ini, encoding="utf-8")
        with (output / "run.log").open("wb") as log:
            completed = subprocess.run(command, cwd=executable.parent, stdout=log,
                                       stderr=subprocess.STDOUT, timeout=args.timeout, check=False)
    finally:
        ini.write_bytes(original_ini)
    if completed.returncode:
        raise RuntimeError(f"Requiem failed ({completed.returncode}); see {output / 'run.log'}")
    log = (output / "run.log").read_text(encoding="utf-8", errors="replace")
    upstream_commit = json.loads((ROOT / "upstreams.lock.json").read_text())["build"]["optiscaler"]["commit"]
    if f"({upstream_commit[:8]}) loaded" not in log:
        raise RuntimeError("Host does not report the locked upstream commit")
    if f"evaluations={args.frames};" not in log:
        raise RuntimeError("Missing complete NGX evaluation count")
    if re.search(r"DLSS-NR (?:unavailable|did not run)|DLSS-NR.*(?:failed|failure)", log, re.I):
        raise RuntimeError("Host reported an NR failure; see run.log")
    # This proves host activity, not a fresh NR sample for every measured frame.
    costs = re.findall(r"DLSS-NR cost: ([0-9.]+) ms total = ([0-9.]+) ms model", log)
    if not costs or "DLSS-NR composition:" not in log:
        raise RuntimeError("NR execution/timing not demonstrated; no NR baseline accepted")
    if any(float(total) <= 0 or float(model) <= 0 for total, model in costs):
        raise RuntimeError("Host NR timing is not positive")
    if f"model {width}x{height}, passes 1" not in log:
        raise RuntimeError("Host did not confirm the requested NR dimensions/pass count")
    with (output / "evaluation.csv").open(newline="") as source:
        rows = list(csv.DictReader(source))
    expected_frames = list(range(args.warmup, args.frames))
    if [int(row["frame"]) for row in rows] != expected_frames:
        raise RuntimeError("Evaluation samples are missing, duplicated or out of order")
    summary = {
        "qualification_complete": False,
        "limitation": "NGX includes SR. Host NR cost entries are sparse; NR p50/p95 are unavailable.",
        "runtime_sha256": runtime_hash,
        "upstream_reported_commit": upstream_commit,
        "testbed_sha256": sha256(executable),
        "proxy_sha256": sha256(executable.parent / "dxgi.dll"),
        "command": command, "warmup": args.warmup, "measured_frames": len(rows),
        "input": [width * 2 // 3, height * 2 // 3], "output": [width, height],
        "nr_working_scale": 1.0, "nr_model": [width, height],
        "host_nr_cost_samples_ms": [{"total": float(total), "model": float(model)} for total, model in costs],
    }
    for column in ("ngx_evaluation_gpu_ms", "frame_wall_ms"):
        values = [float(row[column]) for row in rows]
        if any(not (0 < value < float("inf")) for value in values):
            raise RuntimeError(f"Invalid timing in {column}")
        summary[column] = {"p50": percentile(values, .5), "p95": percentile(values, .95)}
    (output / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(f"Diagnostic recorded: {output}. NR performance qualification remains incomplete.")


def sha256(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--testbed", type=Path, default=ROOT / "dist/RequiemGame/RequiemGame.exe")
    parser.add_argument("--frames", type=int, default=720)
    parser.add_argument("--warmup", type=int, default=120)
    parser.add_argument("--height", type=int, choices=(720, 1080), default=720)
    parser.add_argument("--motion", action="store_true")
    parser.add_argument("--reference-on", action="store_true")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--timeout", type=int, default=180)
    try:
        measure(parser.parse_args())
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"Measurement failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
