#!/usr/bin/env bash
set -u

if (( BASH_VERSINFO[0] < 4 )); then
  echo "tools/unix/fuzz.sh requires Bash 4+; install Homebrew bash on macOS." >&2
  exit 2
fi

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
tier="pr"
target=""
iterations="0"
seed="12345"
all="false"
failed=0

declare -A native_ctest=(
  ["plan"]='fuzz\.plan_parse\.seed_replay'
  ["route-pattern"]='fuzz\.route_pattern\.seed_replay'
  ["http-request"]='fuzz\.http_request\.seed_replay'
  ["http-route-dispatch"]='fuzz\.http_route_dispatch\.seed_replay'
  ["http-query"]='fuzz\.http_query\.seed_replay'
  ["http2-frame"]='fuzz\.http2_frame\.seed_replay'
  ["http2-hpack"]='fuzz\.http2_hpack\.seed_replay'
  ["http2-session"]='fuzz\.http2_session\.seed_replay'
  ["diagnostics-render"]='fuzz\.diagnostics_render\.seed_replay'
  ["memory-primitives"]='fuzz\.memory_primitives\.seed_replay'
)

declare -A native_corpus=(
  ["plan"]="plan"
  ["route-pattern"]="route-pattern"
  ["http-request"]="http-request"
  ["http-route-dispatch"]="http-route-dispatch"
  ["http-query"]="http-query"
  ["http2-frame"]="http2-frame"
  ["http2-hpack"]="http2-hpack"
  ["http2-session"]="http2-session"
  ["diagnostics-render"]="diagnostics-render"
  ["memory-primitives"]="memory-primitives"
)

declare -A native_exe=(
  ["plan"]="fuzz_plan_parse_libfuzzer"
  ["route-pattern"]="fuzz_route_pattern_libfuzzer"
  ["http-request"]="fuzz_http_request_libfuzzer"
  ["http-route-dispatch"]="fuzz_http_route_dispatch_libfuzzer"
  ["http-query"]="fuzz_http_query_libfuzzer"
  ["http2-frame"]="fuzz_http2_frame_libfuzzer"
  ["http2-hpack"]="fuzz_http2_hpack_libfuzzer"
  ["http2-session"]="fuzz_http2_session_libfuzzer"
  ["diagnostics-render"]="fuzz_diagnostics_render_libfuzzer"
  ["memory-primitives"]="fuzz_memory_primitives_libfuzzer"
)

js_targets=(
  "config-json"
  "openapi-plan"
  "headers"
  "query-string"
  "percent-decoding"
  "logging-json"
  "package-manifest"
  "route-table"
  "required-features"
  "http-client-options"
  "results-headers"
  "schema-validation"
  "json-serialization"
  "request-media"
  "realtime-metadata"
  "worker-queue"
  "h2-client-options"
  "stdlib-import-shapes"
)

usage() {
  cat <<'USAGE'
Usage: tools/unix/fuzz.sh [--tier pr|extended|torture] [--target NAME|--all] [--iterations N] [--seed N]

Examples:
  tools/unix/fuzz.sh --tier pr
  tools/unix/fuzz.sh --target http2-frame --iterations 10000 --seed 123
  tools/unix/fuzz.sh --all --iterations 120000
USAGE
}

is_unsigned_integer() {
  [[ "$1" =~ ^[0-9]+$ ]]
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --tier)
      tier="${2:?missing value for --tier}"
      shift 2
      ;;
    --target)
      target="${2:?missing value for --target}"
      shift 2
      ;;
    --iterations)
      iterations="${2:?missing value for --iterations}"
      shift 2
      ;;
    --seed)
      seed="${2:?missing value for --seed}"
      shift 2
      ;;
    --all)
      all="true"
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "fuzz: unknown option '$1'" >&2
      exit 2
      ;;
  esac
done

case "$tier" in
  pr|extended|torture) ;;
  *) echo "fuzz: invalid --tier '$tier'" >&2; exit 2 ;;
esac

if ! is_unsigned_integer "$iterations"; then
  echo "fuzz: --iterations must be an unsigned integer" >&2
  exit 2
fi
if ! is_unsigned_integer "$seed"; then
  echo "fuzz: --seed must be an unsigned integer" >&2
  exit 2
fi

if [[ "$all" == "true" && -n "$target" ]]; then
  echo "fuzz: use either --all or --target, not both." >&2
  exit 2
fi

if [[ "$all" == "false" && -z "$target" ]]; then
  all="true"
fi

run_count() {
  if [[ "$iterations" -gt 0 ]]; then
    printf '%s\n' "$iterations"
  elif [[ "$tier" == "extended" ]]; then
    printf '10000\n'
  elif [[ "$tier" == "torture" ]]; then
    printf '120000\n'
  else
    printf '1000\n'
  fi
}

status_line() {
  local name="$1"
  local status="$2"
  local detail="${3:-}"
  printf '%s\t%s\t%s\n' "$name" "$status" "$detail"
  if [[ "$status" == "FAIL" ]]; then
    failed=$((failed + 1))
  fi
}

host_preset() {
  case "$(uname -s)" in
    Darwin) printf 'macos-clang\n' ;;
    Linux) printf 'linux-clang\n' ;;
    *) printf 'unknown\n' ;;
  esac
}

run_seed_replay() {
  local name="$1"
  local regex="$2"
  local preset
  preset="$(host_preset)"
  if [[ ! -d "$repo_root/build/$preset" ]]; then
    status_line "$name" "UNAVAILABLE" "build/$preset does not exist; configure and build first"
    return
  fi
  (cd "$repo_root" && ctest --preset "$preset" --output-on-failure -R "$regex")
  local code=$?
  if [[ "$code" -eq 0 ]]; then
    status_line "$name" "PASS" "seed replay passed"
  else
    status_line "$name" "FAIL" "ctest exited with code $code"
  fi
}

run_all_seed_replay() {
  local preset
  preset="$(host_preset)"
  if [[ ! -d "$repo_root/build/$preset" ]]; then
    status_line "fuzz.seed_replay" "UNAVAILABLE" "build/$preset does not exist; configure and build first"
    return
  fi
  (cd "$repo_root" && ctest --preset "$preset" --output-on-failure -L fuzz)
  local code=$?
  if [[ "$code" -eq 0 ]]; then
    status_line "fuzz.seed_replay" "PASS" "seed replay passed"
  else
    status_line "fuzz.seed_replay" "FAIL" "ctest exited with code $code"
  fi
}

run_libfuzzer() {
  local name="$1"
  local runs="$2"
  local exe="$repo_root/build/linux-libfuzzer/${native_exe[$name]}"
  local corpus="$repo_root/tests/fuzz/corpus/${native_corpus[$name]}"
  if [[ "$(uname -s)" == "Darwin" ]]; then
    exe="$repo_root/build/macos-libfuzzer/${native_exe[$name]}"
  fi
  if [[ ! -x "$exe" ]]; then
    status_line "fuzz.$name.mutation" "UNAVAILABLE" "libFuzzer target is not built: $exe"
    return
  fi
  if [[ ! -d "$corpus" ]]; then
    status_line "fuzz.$name.mutation" "FAIL" "corpus directory missing: $corpus"
    return
  fi
  echo "seed=$seed target=$name iterations=$runs"
  "$exe" "$corpus" "-runs=$runs" "-seed=$seed"
  local code=$?
  if [[ "$code" -eq 0 ]]; then
    status_line "fuzz.$name.mutation" "PASS" "libFuzzer completed $runs runs"
  else
    status_line "fuzz.$name.mutation" "FAIL" "libFuzzer exited with code $code; rerun: $exe $corpus -runs=$runs -seed=$seed"
  fi
}

is_js_target() {
  local name="$1"
  local item
  for item in "${js_targets[@]}"; do
    [[ "$item" == "$name" ]] && return 0
  done
  return 1
}

run_js_target() {
  local name="$1"
  local runs
  runs="$(run_count)"
  if ! command -v node >/dev/null 2>&1; then
    status_line "fuzz.$name.random" "UNAVAILABLE" "node is not available"
    return
  fi
  local repro="node tests/fuzz/js_fuzz_targets.mjs --target $name --iterations $runs --seed $seed"
  echo "seed=$seed target=$name iterations=$runs"
  (cd "$repo_root" && node tests/fuzz/js_fuzz_targets.mjs --target "$name" --iterations "$runs" --seed "$seed" --failure-root artifacts/fuzz/failures --repro-command "$repro")
  local code=$?
  if [[ "$code" -eq 0 ]]; then
    status_line "fuzz.$name.random" "PASS" "random fuzz completed $runs iterations"
  else
    status_line "fuzz.$name.random" "FAIL" "failure artifact under artifacts/fuzz/failures/$name; repro: $repro"
  fi
}

runs="$(run_count)"
echo "Sloppy fuzz tier=$tier seed=$seed iterations=$runs"

if [[ "$all" == "true" ]]; then
  run_all_seed_replay
  for js_target in "${js_targets[@]}"; do
    run_js_target "$js_target"
  done
elif [[ -n "${native_ctest[$target]:-}" ]]; then
  run_seed_replay "fuzz.$target.seed_replay" "${native_ctest[$target]}"
  run_libfuzzer "$target" "$runs"
elif is_js_target "$target"; then
  run_js_target "$target"
else
  echo "fuzz: unknown target '$target'. Run tools/unix/fuzz.sh --help." >&2
  exit 2
fi

if [[ "$failed" -gt 0 ]]; then
  exit 1
fi
exit 0
