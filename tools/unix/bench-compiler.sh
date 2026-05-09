#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
suite="smoke"
size=""
out=""
sloppyc=""
compiler_profile="debug"
max_working_set_mb=""
compare_before=""
compare_after=""

usage() {
  cat <<'USAGE'
Usage:
  tools/unix/bench-compiler.sh --suite smoke --out artifacts/bench/compiler-smoke.json
  tools/unix/bench-compiler.sh --suite scale --size small,medium --out artifacts/bench/compiler-scale-smoke.json
  tools/unix/bench-compiler.sh --suite scale --size small,medium,large --compiler-profile release --out artifacts/bench/compiler-release.json
  tools/unix/bench-compiler.sh --compare artifacts/bench/before.json artifacts/bench/after.json
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --suite)
      suite="${2:?missing value for --suite}"
      shift 2
      ;;
    --size)
      size="${2:?missing value for --size}"
      shift 2
      ;;
    --out)
      out="${2:?missing value for --out}"
      shift 2
      ;;
    --sloppyc)
      sloppyc="${2:?missing value for --sloppyc}"
      shift 2
      ;;
    --compiler-profile)
      compiler_profile="${2:?missing value for --compiler-profile}"
      if [[ "$compiler_profile" != "debug" && "$compiler_profile" != "release" ]]; then
        echo "bench-compiler: --compiler-profile must be 'debug' or 'release'" >&2
        exit 2
      fi
      shift 2
      ;;
    --max-working-set-mb)
      max_working_set_mb="${2:?missing value for --max-working-set-mb}"
      shift 2
      ;;
    --compare)
      compare_before="${2:?missing first path for --compare}"
      compare_after="${3:?missing second path for --compare}"
      shift 3
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "bench-compiler: unknown argument '$1'" >&2
      exit 2
      ;;
  esac
done

if ! command -v node >/dev/null 2>&1; then
  echo "bench-compiler: node was not found; compiler benchmark generation requires Node.js." >&2
  exit 1
fi

args=("$repo_root/tools/compiler/bench-compiler.mjs")
if [[ -n "$compare_before" || -n "$compare_after" ]]; then
  args+=(--compare "$compare_before" "$compare_after")
else
  args+=(--suite "$suite")
  if [[ -n "$size" ]]; then
    args+=(--size "$size")
  fi
  args+=(--compiler-profile "$compiler_profile")
  if [[ -n "$max_working_set_mb" ]]; then
    args+=(--max-working-set-mb "$max_working_set_mb")
  fi
fi
if [[ -n "$out" ]]; then
  args+=(--out "$out")
fi
if [[ -n "$sloppyc" ]]; then
  if [[ ! -f "$sloppyc" ]]; then
    echo "bench-compiler: --sloppyc does not exist: $sloppyc" >&2
    exit 2
  fi
  args+=(--sloppyc "$sloppyc")
fi

node "${args[@]}"
