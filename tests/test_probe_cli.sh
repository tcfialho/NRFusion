#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
probe="$root/build/NRFusionProbe"
[ -x "$probe" ] || { echo "NRFusionProbe missing" >&2; exit 1; }

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
printf abc > "$tmp/abc"
hash="$($probe --sha256 "$tmp/abc")"
[ "$hash" = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" ]
"$probe" --verify-sha256 "$tmp/abc" "$hash"
if "$probe" --verify-sha256 "$tmp/abc" "0000000000000000000000000000000000000000000000000000000000000000"; then
  echo "FAIL: bad SHA-256 verified" >&2
  exit 1
fi

echo "NRFusion probe CLI/hash contract passed"
