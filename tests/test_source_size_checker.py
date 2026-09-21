#!/usr/bin/env python3
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CHECKER = ROOT / "tools" / "check_source_size.py"


def run(cwd, *args):
    return subprocess.run(args, cwd=cwd, text=True, capture_output=True)


def git(cwd, *args):
    result = run(cwd, "git", *args)
    if result.returncode:
        raise RuntimeError(result.stderr)
    return result


def write_lines(path, count, first="x"):
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [first] + ["x"] * (count - 1)
    path.write_text("\n".join(lines), encoding="utf-8")


def check(result, code, needle=None):
    assert result.returncode == code, result.stderr + result.stdout
    if needle:
        assert needle in result.stdout + result.stderr


def main():
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        git(root, "init", "-b", "master")
        git(root, "config", "user.email", "test@nrfusion.local")
        git(root, "config", "user.name", "NRFusion Test")
        (root / "tools").mkdir()
        shutil.copy2(CHECKER, root / "tools" / CHECKER.name)
        write_lines(root / "vendor/lib.cpp", 301)
        write_lines(root / "generated/auto.cpp", 302, "// NRFUSION_GENERATED_FILE")
        git(root, "add", ".")
        git(root, "commit", "-m", "baseline")

        check(run(root, sys.executable, "tools/check_source_size.py", "all", "--strict"), 0)

        git(root, "checkout", "-b", "feature")
        write_lines(root / "installer/part.nsh", 301)
        check(run(root, sys.executable, "tools/check_source_size.py", "changed"), 1, "part.nsh")
        (root / "installer/part.nsh").unlink()

        vendor = root / "vendor/lib.cpp"
        lines = vendor.read_text(encoding="utf-8").splitlines()
        lines[0] = "modified"
        vendor.write_text("\n".join(lines), encoding="utf-8")
        git(root, "add", "vendor/lib.cpp")
        git(root, "commit", "-m", "modify vendor")
        check(run(root, sys.executable, "tools/check_source_size.py", "all", "--strict"), 1, "vendor/lib.cpp")

        write_lines(root / "new.cpp", 301)
        check(run(root, sys.executable, "tools/check_source_size.py", "all", "--strict"), 1, "new.cpp")

        fake = root / "fake_generated.cpp"
        write_lines(fake, 302, "// NRFUSION_GENERATED_FILE")
        check(run(root, sys.executable, "tools/check_source_size.py", "changed"), 1, "fake_generated.cpp")

        missing = run(root, sys.executable, "tools/check_source_size.py", "changed", "--base", "missing")
        check(missing, 2, "base ref not found")
        assert "Traceback" not in missing.stderr

    print("source-size checker regression tests passed")


if __name__ == "__main__":
    main()
