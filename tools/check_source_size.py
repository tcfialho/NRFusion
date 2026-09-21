#!/usr/bin/env python3
import argparse
import subprocess
import sys
from pathlib import Path

LIMIT = 300
EXTS = {
    ".c", ".cc", ".cpp", ".cxx", ".h", ".hpp", ".hxx", ".inl", ".inc",
    ".cu", ".cuh", ".hlsl", ".glsl", ".vert", ".frag", ".py", ".ps1",
    ".sh", ".bat", ".cmd", ".cmake", ".nsi", ".yml", ".yaml", ".rc", ".def",
}
EXEMPT_PREFIXES = ("tests/fixture/", "third_party/", "vendor/", "external/")


def git_paths(*args):
    raw = subprocess.check_output(["git", *args])
    return {p.decode() for p in raw.split(b"\0") if p}


def is_source(path, allow_exempt):
    if allow_exempt and path.startswith(EXEMPT_PREFIXES):
        return False
    p = Path(path)
    return p.name == "CMakeLists.txt" or p.suffix.lower() in EXTS


def changed_paths(base):
    merge_base = subprocess.check_output(
        ["git", "merge-base", base, "HEAD"], text=True
    ).strip()
    paths = git_paths("diff", "--name-only", "-z", "--diff-filter=ACMR", merge_base, "HEAD")
    paths |= git_paths("diff", "--name-only", "-z", "--diff-filter=ACMR")
    paths |= git_paths("diff", "--cached", "--name-only", "-z", "--diff-filter=ACMR")
    paths |= git_paths("ls-files", "--others", "--exclude-standard", "-z")
    return paths


def line_count(path):
    return len(Path(path).read_text(encoding="utf-8", errors="replace").splitlines())


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=("changed", "all"))
    parser.add_argument("--base", default="master")
    parser.add_argument("--strict", action="store_true")
    args = parser.parse_args()

    paths = git_paths("ls-files", "-z") if args.mode == "all" else changed_paths(args.base)
    violations = []
    for path in sorted(paths):
        if not is_source(path, args.mode == "all") or not Path(path).is_file():
            continue
        lines = line_count(path)
        if lines > LIMIT:
            violations.append((path, lines))

    for path, lines in violations:
        print(f"{path}: {lines} lines > {LIMIT}")
    print(f"source-size: {len(violations)} violation(s), mode={args.mode}")
    return int(bool(violations) and (args.mode == "changed" or args.strict))


if __name__ == "__main__":
    sys.exit(main())
