#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
RUNNER="${REPO_ROOT}/benchmarks/realistic/runner/bench-realistic.ts"
SUITE="http"
RUNTIME="sloppy,node,bun,deno"
CATEGORY=""
WORKLOAD=""
DURATION_SECONDS=""
WARMUP_SECONDS=""
CONNECTIONS=""
ITERATIONS=""
OUT="artifacts/bench/realistic"
REQUIRE_RUNTIME=""
SLOPPY_EXE=""
QUICK="false"
FULL="false"
DRY_RUN="false"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --suite)
      SUITE="$2"
      shift 2
      ;;
    --runtime|--runtimes)
      RUNTIME="$2"
      shift 2
      ;;
    --category|--variant)
      CATEGORY="$2"
      shift 2
      ;;
    --workload|--workloads)
      WORKLOAD="$2"
      shift 2
      ;;
    --duration-seconds)
      DURATION_SECONDS="$2"
      shift 2
      ;;
    --warmup-seconds)
      WARMUP_SECONDS="$2"
      shift 2
      ;;
    --connections)
      CONNECTIONS="$2"
      shift 2
      ;;
    --iterations)
      ITERATIONS="$2"
      shift 2
      ;;
    --out)
      OUT="$2"
      shift 2
      ;;
    --require-runtime|--require-runtimes)
      REQUIRE_RUNTIME="$2"
      shift 2
      ;;
    --sloppy-exe)
      SLOPPY_EXE="$2"
      shift 2
      ;;
    --quick)
      QUICK="true"
      shift
      ;;
    --full)
      FULL="true"
      shift
      ;;
    --dry-run)
      DRY_RUN="true"
      shift
      ;;
    --help|-h)
      cat <<'HELP'
Usage: tools/unix/bench-realistic.sh [options]
  --suite http|startup|stress|all
  --runtime sloppy,node,bun,deno
  --category baseline,framework,feature-rich
  --workload health,json-small,route-param,query,post-json-small,middleware-request-id,large-routes,static-ish-payload,mixed-realistic
  --duration-seconds 30
  --warmup-seconds 10
  --connections 1,16,64
  --iterations 5
  --out artifacts/bench/realistic
  --require-runtime sloppy,node
  --sloppy-exe path/to/sloppy
  --quick
  --full
  --dry-run
HELP
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      exit 2
      ;;
  esac
done

cd "${REPO_ROOT}"

if [[ -z "${SLOPPY_EXE}" ]]; then
  for candidate in \
    "${REPO_ROOT}/build/unix-relwithdebinfo/sloppy" \
    "${REPO_ROOT}/build/unix-release/sloppy" \
    "${REPO_ROOT}/build/unix-debug/sloppy"; do
    if [[ -x "${candidate}" ]]; then
      SLOPPY_EXE="${candidate}"
      break
    fi
  done
fi
if [[ -z "${SLOPPY_EXE}" ]]; then
  if command -v sloppy >/dev/null 2>&1; then
    SLOPPY_EXE="$(command -v sloppy)"
  else
    echo "Sloppy executable was not found. Build Sloppy or pass --sloppy-exe." >&2
    exit 1
  fi
fi

if [[ "${OUT}" != /* ]]; then
  OUT="${REPO_ROOT}/${OUT}"
fi
mkdir -p "${OUT}"
CONFIG_PATH="${OUT}/bench-realistic.config.json"
SLOPPYC_EXE=""
if [[ -x "${REPO_ROOT}/compiler/target/release/sloppyc" ]]; then
  SLOPPYC_EXE="${REPO_ROOT}/compiler/target/release/sloppyc"
elif [[ -x "${REPO_ROOT}/compiler/target/debug/sloppyc" ]]; then
  SLOPPYC_EXE="${REPO_ROOT}/compiler/target/debug/sloppyc"
fi

cat > "${CONFIG_PATH}" <<JSON
{
  "suite": "${SUITE}",
  "runtimes": "${RUNTIME}",
  "categories": "${CATEGORY}",
  "workloads": "${WORKLOAD}",
  "durationSeconds": ${DURATION_SECONDS:-null},
  "warmupSeconds": ${WARMUP_SECONDS:-null},
  "connections": "${CONNECTIONS}",
  "iterations": ${ITERATIONS:-null},
  "out": "${OUT}",
  "requireRuntimes": "${REQUIRE_RUNTIME}",
  "quick": ${QUICK},
  "full": ${FULL},
  "dryRun": ${DRY_RUN},
  "repoRoot": "${REPO_ROOT}",
  "sloppyExe": "${SLOPPY_EXE}",
  "sloppycExe": "${SLOPPYC_EXE}",
  "runtimePaths": {
    "node": "$(command -v node 2>/dev/null || true)",
    "bun": "$(command -v bun 2>/dev/null || true)",
    "deno": "$(command -v deno 2>/dev/null || true)"
  }
}
JSON

SLOPPY_BENCH_CONFIG="${CONFIG_PATH}" SLOPPY_BENCH_REPO="${REPO_ROOT}" exec "${SLOPPY_EXE}" run "${RUNNER}" --kind program
