#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
preset="linux-clang"
list=0
smoke=0
json=0
include_v8=0
bench_name=""
runtime_engine=0
suite_value=""
runtime_value=""
out_path=""
compare_before=""
compare_after=""

usage() {
  cat <<'USAGE'
Usage:
  tools/unix/bench.sh [--preset PRESET] [--list] [--smoke] [--json] [--include-v8] [--bench NAME]
  tools/unix/bench.sh --suite SUITE [--runtime RUNTIME[,RUNTIME]] [--out PATH]
  tools/unix/bench.sh --compare BEFORE AFTER [--out PATH]

The native sloppy_bench wrapper is available on Unix when the CMake preset is configured.
The BENCH-01 local runtime comparison engine is currently Windows-only and reports
UNAVAILABLE here instead of faking Unix support.
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --preset)
      preset="${2:?missing value for --preset}"
      shift 2
      ;;
    --list)
      list=1
      shift
      ;;
    --smoke)
      smoke=1
      shift
      ;;
    --json)
      json=1
      shift
      ;;
    --include-v8)
      include_v8=1
      shift
      ;;
    --bench)
      bench_name="${2:?missing value for --bench}"
      shift 2
      ;;
    --suite)
      runtime_engine=1
      suite_value="${2:?missing value for --suite}"
      shift 2
      ;;
    --runtime)
      runtime_engine=1
      runtime_value="${2:?missing value for --runtime}"
      shift 2
      ;;
    --out)
      out_path="${2:?missing value for --out}"
      shift 2
      ;;
    --compare)
      runtime_engine=1
      compare_before="${2:?missing first path for --compare}"
      compare_after="${3:?missing second path for --compare}"
      shift 3
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "bench: unknown argument '$1'" >&2
      exit 2
      ;;
  esac
done

if [[ "$runtime_engine" -eq 1 ]]; then
  if [[ -n "$out_path" ]]; then
    mkdir -p "$(dirname "$out_path")"
  fi
  python3 - "$out_path" "$compare_before" "$compare_after" <<'PY'
import json
import sys
from datetime import datetime, timezone

out_path, compare_before, compare_after = sys.argv[1:4]
doc = {
    "schemaVersion": 1,
    "startedAt": datetime.now(timezone.utc).isoformat(),
    "runtimes": {},
    "benchmarks": [
        {
            "id": "local-runtime-engine.unix",
            "suite": "tooling",
            "runtime": "unix",
            "status": "UNAVAILABLE",
            "reason": "BENCH-01 local runtime comparison is currently implemented by tools/windows/bench.ps1; this Unix wrapper preserves the command shape and reports the lane honestly.",
            "warmupRequests": 0,
            "requests": 0,
            "p50Ms": None,
            "p95Ms": None,
            "p99Ms": None,
            "requestsPerSecond": None,
            "errorCount": 0,
            "startupMs": None,
            "allocations": None,
            "bytesCopied": None,
            "correctness": {
                "checked": False,
                "status": "UNAVAILABLE",
                "details": "No Unix local runtime comparison engine is implemented yet.",
            },
        }
    ],
}
if compare_before and compare_after:
    doc["before"] = compare_before
    doc["after"] = compare_after
text = json.dumps(doc, ensure_ascii=True, indent=2)
if out_path:
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(text + "\n")
else:
    print(text)
PY
  exit 0
fi

build_dir="$repo_root/build/$preset"
bench_exe="$build_dir/sloppy_bench"
if [[ ! -x "$bench_exe" ]]; then
  cmake --build --preset "$preset" --target sloppy_bench
fi

args=()
if [[ "$list" -eq 1 ]]; then
  args+=(--list)
fi
if [[ "$smoke" -eq 1 ]]; then
  args+=(--smoke)
fi
if [[ "$json" -eq 1 ]]; then
  args+=(--format json)
fi
if [[ "$include_v8" -eq 1 ]]; then
  args+=(--include-v8)
fi
if [[ -n "$bench_name" ]]; then
  args+=(--bench "$bench_name")
fi

if [[ "${#args[@]}" -gt 0 ]]; then
  "$bench_exe" "${args[@]}"
else
  "$bench_exe"
fi
