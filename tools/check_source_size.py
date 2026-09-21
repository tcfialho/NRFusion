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
SKIP_PREFIXES = ("tests/fixture/", "third_party/", "vendor/", "external/")
GENERATED_MARKER = "NRFUSION_GENERATED_FILE"


def git_paths(*args):
    raw = subprocess.check_output(["git", *args])
    return {p.decode("utf-8") for p in raw.split(b"\0") if p}


def source_path(path):
    if path.startswith(SKIP_PREFIXES):
        return False
    p = Path(path)
    return p.name == "CMakeLists.txt" or p.suffix.lower() in EXTS


def paths_for(mode, base):
    if mode == "all":
        return git_paths("ls-files", "-z")
    paths = git_paths("diff", "--name-only", "-z", "--diff-filter=ACMR", base, "HEAD")
    paths |= git_paths("diff", "--name-only", "-z", "--diff-filter=ACMR")
    paths |= git_paths("diff", "--cached", "--name-only", "-z", "--diff-filter=ACMR")
    paths |= git_paths("ls-files", "--others", "--exclude-standard", "-z")
    return paths


def count_lines(path):
    text = Path(path).read_text(encoding="utf-8", errors="replace")
    if GENERATED_MARKER in "\n".join(text.splitlines()[:5]):
        return None
    return len(text.splitlines())


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=("changed", "all"))
    parser.add_argument("--base", default="master")
    parser.add_argument("--strict", action="store_true", help="fail in all mode too")
    args = parser.parse_args()

    violations = []
    for path in sorted(paths_for(args.mode, args.base)):
        if not source_path(path) or not Path(path).is_file():
            continue
        lines = count_lines(path)
        if lines is not None and lines > LIMIT:
            violations.append((path, lines))

    for path, lines in violations:
        print(f"{path}: {lines} lines > {LIMIT}")
    print(f"source-size: {len(violations)} violation(s), mode={args.mode}")
    return int(bool(violations) and (args.mode == "changed" or args.strict))


if __name__ == "__main__":
    sys.exit(main())
