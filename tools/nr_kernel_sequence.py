"""Validate observed launch order; this does not establish data dependencies."""

import argparse
import csv
import gzip
import hashlib
import json
from collections import Counter, defaultdict
from pathlib import Path


STRUCTURAL_FIELDS = ("module_sha256", "name", "grid_x", "grid_y", "grid_z",
                     "block_x", "block_y", "block_z", "shared_bytes", "param_bytes")


def signature(row):
    return tuple(row[field] for field in STRUCTURAL_FIELDS)


def load_frames(path):
    opener = gzip.open if path.suffix == ".gz" else open
    frames = defaultdict(list)
    with opener(path, "rt", encoding="utf-8", newline="") as source:
        for row in csv.DictReader(source):
            if row["success"] != "1" or int(row["droppedSamples"]):
                raise ValueError(f"Failed or dropped launch in {path}")
            frames[int(row["frame"])].append(row)
    if not frames:
        raise ValueError(f"Empty trace: {path}")
    for frame, launches in frames.items():
        if [int(row["sequence"]) for row in launches] != list(range(len(launches))):
            raise ValueError(f"Incomplete launch order at frame {frame}: {path}")
    return frames


def fingerprint(sequence):
    return hashlib.sha256(json.dumps(sequence, separators=(",", ":")).encode()).hexdigest()


def repeated_sequences(names, width):
    counts = Counter(tuple(names[index:index + width]) for index in range(len(names) - width + 1))
    return [{"sequence": list(sequence), "occurrences_per_frame": count,
             "status": "observed adjacent launches; no data dependency established"}
            for sequence, count in counts.most_common(12) if count > 1]


def analyze(paths):
    executions = []
    reference = None
    all_stable = True
    for path in paths:
        frames = load_frames(path)
        variants = Counter()
        for launches in frames.values():
            variants.update([fingerprint([signature(row) for row in launches])])
        representative = next(iter(frames.values()))
        canonical = [signature(row) for row in representative]
        stable = len(variants) == 1
        same_as_reference = reference is None or canonical == reference
        all_stable &= stable and same_as_reference
        if reference is None:
            reference = canonical
        names = [row["name"] for row in representative]
        executions.append({"trace": str(path.resolve()), "frames": len(frames),
                           "launches_per_frame": len(representative), "stable_within_execution": stable,
                           "matches_reference_execution": same_as_reference,
                           "sequence_variants": dict(variants),
                           "representative_sequence": [{field: row[field] for field in STRUCTURAL_FIELDS}
                                                       for row in representative],
                           "repeated_pairs": repeated_sequences(names, 2),
                           "repeated_triples": repeated_sequences(names, 3)})
    return {"schema_version": 1, "gate_2a_2": all_stable,
            "qualification": "launch sequence only; data dependencies remain unconfirmed",
            "executions": executions}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", nargs="+", type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    report = analyze(args.trace)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2), encoding="utf-8")
    for execution in report["executions"]:
        print(f"{execution['frames']} frames, {execution['launches_per_frame']} launches/frame; "
              f"stable={execution['stable_within_execution']}; cross-run={execution['matches_reference_execution']}")
    print("Observed launch sequence; data dependencies are not established.")
    return 0 if report["gate_2a_2"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
