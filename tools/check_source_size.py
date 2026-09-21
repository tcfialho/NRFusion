#!/usr/bin/env python3
import argparse
import subprocess
import sys
from pathlib import Path

LIMIT = 300
EXTS = {
    ".c", ".cc", ".cpp", ".cxx", ".h", ".hpp", ".hxx", ".inl", ".inc",
    ".cu", ".cuh", ".hlsl", ".glsl", ".vert", ".frag", ".py", ".pyi",
    ".ps1", ".psm1", ".sh", ".bat", ".cmd", ".cmake", ".nsi", ".nsh",
    ".yml", ".yaml", ".rc", ".def",
}
EXEMPT_PREFIXES = ("tests/fixture/", "third_party/", "vendor/", "external/")
GENERATED_PREFIXES = ("generated/", "src/generated/", "include/generated/", "shaders/generated/")
GENERATED_MARKER = "NRFUSION_GENERATED_FILE"


def git(*args, check=True):
    return subprocess.run(["git", *args], check=check, capture_output=True)


def git_paths(*args):
    return {p.decode() for p in git(*args).stdout.split(b"\0") if p}


def resolve_base(base):
    refs = [base]
    if "/" not in base and not base.startswith("refs/"):
        refs.append(f"origin/{base}")
    for ref in refs:
        if git("rev-parse", "--verify", f"{ref}^{{commit}}", check=False).returncode == 0:
            return ref
    return None


def changed_paths(base):
    merge_base = git("merge-base", base, "HEAD").stdout.decode().strip()
    paths = git_paths("diff", "--name-only", "-z", "--diff-filter=ACMR", merge_base, "HEAD")
    paths |= git_paths("diff", "--name-only", "-z", "--diff-filter=ACMR")
    paths |= git_paths("diff", "--cached", "--name-only", "-z", "--diff-filter=ACMR")
    paths |= git_paths("ls-files", "--others", "--exclude-standard", "-z")
    return paths


def is_source(path):
    p = Path(path)
    return p.name == "CMakeLists.txt" or p.suffix.lower() in EXTS


def is_generated(path):
    if not path.startswith(GENERATED_PREFIXES):
        return False
    head = Path(path).read_text(encoding="utf-8", errors="replace").splitlines()[:5]
    return any(GENERATED_MARKER in line for line in head)


def line_count(path):
    return len(Path(path).read_text(encoding="utf-8", errors="replace").splitlines())


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=("changed", "all"))
    parser.add_argument("--base", default="master")
    parser.add_argument("--strict", action="store_true")
    args = parser.parse_args()

    base = resolve_base(args.base)
    if base is None:
        parser.error(f"base ref not found: {args.base}")

    tracked = git_paths("ls-files", "-z")
    untracked = git_paths("ls-files", "--others", "--exclude-standard", "-z")
    modified = changed_paths(base)
    paths = modified if args.mode == "changed" else tracked | untracked

    violations = []
    for path in sorted(paths):
        if not is_source(path) or not Path(path).is_file() or is_generated(path):
            continue
        if path.startswith(EXEMPT_PREFIXES) and path not in modified:
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
