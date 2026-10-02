"""Build an identity-aware inventory from NRFusion's retired NVAPI kernel trace."""

import argparse
import csv
import hashlib
import json
from collections import defaultdict
from pathlib import Path


SIGNATURE_FIELDS = ("backend", "function_identity", "module_identity", "generation",
                    "device_identity", "queue_identity", "name", "module_sha256",
                    "grid_x", "grid_y", "grid_z", "block_x", "block_y", "block_z", "shared_bytes")


def digest(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def inventory(trace):
    metadata = json.loads(Path(str(trace) + ".metadata.json").read_text(encoding="utf-8"))
    with trace.open(newline="", encoding="utf-8") as source:
        rows = list(csv.DictReader(source))
    if not rows:
        raise ValueError(f"No launches: {trace}")
    if any(row["schema"] != "1" or row["success"] != "1" for row in rows):
        raise ValueError(f"Unknown schema or failed launch: {trace}")
    frames = defaultdict(list)
    entries = {}
    total_ms = identified_ms = 0.0
    dropped = max(int(row["droppedSamples"]) for row in rows)
    for row in rows:
        frame, sequence = int(row["frame"]), int(row["sequence"])
        frames[frame].append(sequence)
        if row["chain_index"] == "0":
            total_ms += float(row["chain_gpu_ms"])
        signature = {field: row[field] for field in SIGNATURE_FIELDS}
        signature.update(runtime_sha256=metadata["runtime_sha256"],
                         gpu_architecture=metadata["gpu_architecture"])
        identity = hashlib.sha256(json.dumps(signature, sort_keys=True).encode()).hexdigest()
        if identity not in entries:
            entries[identity] = {"identity": identity, **signature, "calls": 0, "total_ms": 0.0,
                                 "individually_timed_calls": 0, "identified": bool(row["name"]) and
                                 row["name_truncated"] == "0" and bool(row["module_sha256"])}
        entry = entries[identity]
        entry["calls"] += 1
        if row["gpu_ms"]:
            ms = float(row["gpu_ms"])
            if ms <= 0:
                raise ValueError(f"Invalid kernel duration: {trace}")
            entry["total_ms"] += ms
            entry["individually_timed_calls"] += 1
            if entry["identified"]:
                identified_ms += ms
    for frame, sequences in frames.items():
        if sequences != list(range(len(sequences))):
            raise ValueError(f"Missing or duplicate launch sequence in frame {frame}: {trace}")
    for entry in entries.values():
        timed = entry["individually_timed_calls"]
        entry["calls_per_frame"] = entry["calls"] / len(frames)
        entry["us_per_call"] = entry["total_ms"] * 1000 / timed if timed else None
        entry["ms_per_frame"] = entry["total_ms"] / len(frames) if timed else None
        entry["share_percent"] = entry["total_ms"] * 100 / total_ms if timed and total_ms else None
    coverage = identified_ms * 100 / total_ms if total_ms else 0
    result = {"schema_version": 1, "trace": str(trace.resolve()), "trace_sha256": digest(trace),
              "metadata": metadata, "frames": len(frames), "first_frame": min(frames),
              "last_frame": max(frames), "launches": len(rows), "droppedSamples": dropped,
              "measured_chain_ms_per_frame": total_ms / len(frames),
              "identified_individual_time_percent": coverage,
              "gate_2a_1": coverage >= 70 and dropped == 0,
              "kernels": sorted(entries.values(), key=lambda entry: -entry["total_ms"])}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path, nargs="+")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    reports = [inventory(trace) for trace in args.trace]
    for index, report in enumerate(reports, 1):
        (args.out / f"inventory-{index}.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
        columns = ("name", "identity", "calls_per_frame", "us_per_call", "ms_per_frame", "share_percent",
                   "grid_x", "grid_y", "grid_z", "block_x", "block_y", "block_z", "shared_bytes")
        with (args.out / f"inventory-{index}.csv").open("w", newline="", encoding="utf-8") as target:
            writer = csv.DictWriter(target, fieldnames=columns, extrasaction="ignore")
            writer.writeheader()
            writer.writerows(report["kernels"])
        print(f"{report['trace']}: {report['frames']} frames, {report['launches']} launches, "
              f"coverage={report['identified_individual_time_percent']:.3f}%, dropped={report['droppedSamples']}")
    return 0 if all(report["gate_2a_1"] for report in reports) else 1


if __name__ == "__main__":
    raise SystemExit(main())
