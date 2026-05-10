#!/usr/bin/env bash
set -u

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
tier="pr"
area="all"
seed="12345"
fuzz_iterations="0"
stress_seconds="0"
out=""
failed=0
started_at="$(date -u +%Y-%m-%dT%H:%M:%SZ)"

usage() {
  cat <<'USAGE'
Usage: tools/unix/test-engine.sh [--tier pr|extended|torture] [--area all|static|native|compiler|js|fuzz|http2|package|sanitizer|stress|v8|provider|meta] [--seed N] [--fuzz-iterations N] [--stress-seconds N] [--out PATH]

Examples:
  tools/unix/test-engine.sh --tier pr
  tools/unix/test-engine.sh --tier extended
  tools/unix/test-engine.sh --tier torture --fuzz-iterations 120000 --stress-seconds 300
  tools/unix/test-engine.sh --area fuzz --tier pr --seed 12345
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --tier)
      tier="${2:?missing value for --tier}"
      shift 2
      ;;
    --area)
      area="${2:?missing value for --area}"
      shift 2
      ;;
    --seed)
      seed="${2:?missing value for --seed}"
      shift 2
      ;;
    --fuzz-iterations)
      fuzz_iterations="${2:?missing value for --fuzz-iterations}"
      shift 2
      ;;
    --stress-seconds)
      stress_seconds="${2:?missing value for --stress-seconds}"
      shift 2
      ;;
    --out)
      out="${2:?missing value for --out}"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "test-engine: unknown option '$1'" >&2
      exit 2
      ;;
  esac
done

case "$tier" in
  pr|extended|torture) ;;
  *) echo "test-engine: invalid --tier '$tier'" >&2; exit 2 ;;
esac

case "$area" in
  all|static|native|compiler|js|fuzz|http2|package|sanitizer|stress|v8|provider|meta) ;;
  *) echo "test-engine: invalid --area '$area'" >&2; exit 2 ;;
esac

declare -a lane_ids=()
declare -a lane_statuses=()
declare -a lane_durations=()
declare -a lane_commands=()
declare -a lane_notes=()

summary_pass=0
summary_fail=0
summary_skipped=0
summary_unavailable=0

json_escape() {
  local value="$1"
  value="${value//\\/\\\\}"
  value="${value//\"/\\\"}"
  value="${value//$'\n'/\\n}"
  value="${value//$'\r'/}"
  printf '%s' "$value"
}

add_lane() {
  local id="$1"
  local status="$2"
  local duration="$3"
  local command="$4"
  local notes="${5:-}"
  lane_ids+=("$id")
  lane_statuses+=("$status")
  lane_durations+=("$duration")
  lane_commands+=("$command")
  lane_notes+=("$notes")
  case "$status" in
    pass) summary_pass=$((summary_pass + 1)) ;;
    fail) summary_fail=$((summary_fail + 1)); failed=$((failed + 1)) ;;
    skipped) summary_skipped=$((summary_skipped + 1)) ;;
    unavailable) summary_unavailable=$((summary_unavailable + 1)) ;;
  esac
  printf '%s\t%s\t%s\n' "$id" "$(tr '[:lower:]' '[:upper:]' <<<"$status")" "$notes"
}

duration_ms_since() {
  local start_seconds="$1"
  local end_seconds
  end_seconds="$(date +%s)"
  printf '%s\n' $(((end_seconds - start_seconds) * 1000))
}

run_lane() {
  local id="$1"
  shift
  local command_text="$*"
  local tool="$1"
  if [[ "$tool" != */* ]] && ! command -v "$tool" >/dev/null 2>&1; then
    add_lane "$id" "unavailable" "0" "$command_text" "$tool is not available"
    return
  fi
  local start
  start="$(date +%s)"
  (cd "$repo_root" && "$@")
  local code=$?
  local duration
  duration="$(duration_ms_since "$start")"
  if [[ "$code" -eq 0 ]]; then
    add_lane "$id" "pass" "$duration" "$command_text" ""
  else
    add_lane "$id" "fail" "$duration" "$command_text" "exit code $code"
  fi
}

host_preset() {
  case "$(uname -s)" in
    Darwin) printf 'macos-clang\n' ;;
    Linux) printf 'linux-clang\n' ;;
    *) printf 'unknown\n' ;;
  esac
}

ctest_lane() {
  local id="$1"
  local preset="$2"
  shift 2
  if [[ ! -d "$repo_root/build/$preset" ]]; then
    add_lane "$id" "unavailable" "0" "ctest --preset $preset" "build preset directory does not exist: $repo_root/build/$preset"
    return
  fi
  run_lane "$id" ctest --preset "$preset" --output-on-failure "$@"
}

tier_iterations() {
  if [[ "$fuzz_iterations" -gt 0 ]]; then
    printf '%s\n' "$fuzz_iterations"
  elif [[ "$tier" == "extended" ]]; then
    printf '10000\n'
  elif [[ "$tier" == "torture" ]]; then
    printf '120000\n'
  else
    printf '1000\n'
  fi
}

tier_stress_seconds() {
  if [[ "$stress_seconds" -gt 0 ]]; then
    printf '%s\n' "$stress_seconds"
  elif [[ "$tier" == "extended" ]]; then
    printf '60\n'
  elif [[ "$tier" == "torture" ]]; then
    printf '300\n'
  else
    printf '10\n'
  fi
}

run_node_check() {
  if ! command -v node >/dev/null 2>&1; then
    add_lane "test-engine.static.node_check" "unavailable" "0" "node --check <tracked js/mjs>" "node is not available"
    return
  fi
  local start
  start="$(date +%s)"
  local file
  while IFS= read -r file; do
    [[ -n "$file" ]] || continue
    node --check "$repo_root/$file" || {
      add_lane "test-engine.static.node_check" "fail" "$(duration_ms_since "$start")" "node --check <tracked js/mjs>" "$file failed syntax check"
      return
    }
  done < <(git -C "$repo_root" ls-files -- '*.js' '*.mjs')
  add_lane "test-engine.static.node_check" "pass" "$(duration_ms_since "$start")" "node --check <tracked js/mjs>" ""
}

run_added_line_guardrails() {
  local base
  base="$(git -C "$repo_root" merge-base HEAD origin/main 2>/dev/null || true)"
  if [[ -z "$base" ]]; then
    add_lane "test-engine.static.guardrails" "skipped" "0" "git diff --unified=0 <merge-base>" "origin/main merge-base was not available"
    return
  fi
  local start
  start="$(date +%s)"
  local violations=()
  local path=""
  local goal_phrase="/"
  goal_phrase="${goal_phrase}goal"
  local release_phrase="auto-generated"
  release_phrase="${release_phrase} release notes"
  local generated_phrase="generated"
  generated_phrase="${generated_phrase} with"
  local legacy_agent_phrase="Claude"
  legacy_agent_phrase="${legacy_agent_phrase} Code"
  local speed_phrase="blazing"
  speed_phrase="${speed_phrase} fast"
  local production_phrase="production"
  production_phrase="${production_phrase} ready"
  local codex_token="CO"
  codex_token="${codex_token}DEX"
  local blocked_phrase_pattern="($goal_phrase|$release_phrase|$generated_phrase|$speed_phrase|$production_phrase|$legacy_agent_phrase|\\b$codex_token\\b)"
  while IFS= read -r line; do
    if [[ "$line" =~ ^\+\+\+\ b/(.+)$ ]]; then
      path="${BASH_REMATCH[1]}"
      continue
    fi
    [[ "$line" == +* && "$line" != +++* ]] || continue
    local added="${line:1}"
    if [[ ! "$path" =~ ^(AGENTS\.md|AGENTS_CONTRIBUTING\.md|docs/archive/) ]] &&
      grep -Eiq "$blocked_phrase_pattern" <<<"$added"; then
      violations+=("$path: disallowed generated/prompt-leakage wording")
    fi
    if [[ "$path" =~ ^(src|include|tests)/.*\.(c|h|cc|cpp|hpp)$ ]] &&
      grep -Eq '\b(malloc|free|memcpy|memmove)[[:space:]]*\(' <<<"$added"; then
      violations+=("$path: added direct allocation/copy pattern needs review")
    fi
    if [[ "$path" == include/* ]] && grep -Eq 'v8::|<v8\.h>|libuv|uv_' <<<"$added"; then
      violations+=("$path: public header exposes V8/libuv detail")
    fi
  done < <(git -C "$repo_root" diff --unified=0 "$base" -- docs README.md .github tools src include tests compiler stdlib examples)
  if [[ "${#violations[@]}" -gt 0 ]]; then
    add_lane "test-engine.static.guardrails" "fail" "$(duration_ms_since "$start")" "git diff --unified=0 $base -- <guarded paths>" "$(printf '%s; ' "${violations[@]}")"
  else
    add_lane "test-engine.static.guardrails" "pass" "$(duration_ms_since "$start")" "git diff --unified=0 $base -- <guarded paths>" ""
  fi
}

run_static() {
  local start_index="${#lane_ids[@]}"
  run_lane "test-engine.static.git_diff_check" git diff --check
  run_lane "test-engine.static.c_standards_selftest" bash tools/unix/check-c-standards.sh --self-test
  run_lane "test-engine.static.c_standards" bash tools/unix/check-c-standards.sh
  local start
  start="$(date +%s)"
  local script
  for script in "$repo_root"/tools/unix/*.sh; do
    bash -n "$script" || {
      add_lane "test-engine.static.bash_syntax" "fail" "$(duration_ms_since "$start")" "bash -n tools/unix/*.sh" "$(basename "$script") failed syntax check"
      return
    }
  done
  add_lane "test-engine.static.bash_syntax" "pass" "$(duration_ms_since "$start")" "bash -n tools/unix/*.sh" ""
  run_node_check
  run_added_line_guardrails
  run_lane "test-engine.static.cargo_fmt" cargo fmt --manifest-path compiler/Cargo.toml -- --check
  run_lane "test-engine.static.cargo_clippy" cargo clippy --manifest-path compiler/Cargo.toml --all-targets -- -D warnings
  run_lane "test-engine.static.cargo_test" cargo test --manifest-path compiler/Cargo.toml
  local status="pass"
  local index
  for ((index=start_index; index<${#lane_statuses[@]}; index++)); do
    if [[ "${lane_statuses[$index]}" == "fail" ]]; then status="fail"; break; fi
    if [[ "$status" == "pass" && "${lane_statuses[$index]}" == "unavailable" ]]; then status="unavailable"; fi
    if [[ "$status" == "pass" && "${lane_statuses[$index]}" == "skipped" ]]; then status="skipped"; fi
  done
  add_lane "test-engine.static" "$status" "0" "static guardrail aggregate" "aggregate over $((${#lane_ids[@]} - start_index)) static checks"
}

run_meta() {
  run_lane "test-engine.meta.help" bash tools/unix/test-engine.sh --help
  run_lane "test-engine.meta.fuzz_help" bash tools/unix/fuzz.sh --help
}

run_native() {
  ctest_lane "native.unit" "$(host_preset)" -R '^(core\.|data\.|conformance\.(foundation|http|sqlite|data|capability|net|transport)|smoke\.transport)'
}

run_compiler() {
  run_lane "compiler.cargo_tests" cargo test --manifest-path compiler/Cargo.toml
  ctest_lane "compiler.ctest_fixtures" "$(host_preset)" -R 'compiler|source_input'
}

run_js() {
  local iterations
  iterations="$(tier_iterations)"
  run_lane "js.property" node tests/bootstrap/property/run_property_tests.mjs --seed "$seed" --iterations "$iterations"
  ctest_lane "js.bootstrap" "$(host_preset)" -R 'bootstrap\.stdlib'
}

run_fuzz() {
  local iterations
  iterations="$(tier_iterations)"
  run_lane "fuzz.runner" bash tools/unix/fuzz.sh --tier "$tier" --all --iterations "$iterations" --seed "$seed"
}

run_http2() {
  run_lane "http2.conformance" bash tools/unix/test-http2.sh --preset "$(host_preset)"
}

run_package() {
  local package_path
  package_path="$(ls -t "$repo_root"/artifacts/packages/sloppy-*.tar.gz 2>/dev/null | head -n 1 || true)"
  if [[ -z "$package_path" ]]; then
    add_lane "package.outside_checkout" "skipped" "0" "tools/unix/dev.sh test-package" "no package archive found under artifacts/packages"
    return
  fi
  run_lane "package.outside_checkout" bash tools/unix/dev.sh test-package --package-path "$package_path"
}

run_sanitizer() {
  mkdir -p "$repo_root/artifacts/test-engine/sanitizers"
  run_lane "sanitizer.linux.configure" bash tools/unix/dev.sh configure --preset linux-sanitizers
  run_lane "sanitizer.linux.build" bash tools/unix/dev.sh build --preset linux-sanitizers
  run_lane "sanitizer.linux.ctest" ctest --preset linux-sanitizers --output-on-failure
}

run_stress() {
  local seconds
  seconds="$(tier_stress_seconds)"
  ctest_lane "stress.ctest_smoke" "$(host_preset)" -R 'stress\.'
  add_lane "stress.budget" "pass" "0" "stress budget" "tier $tier uses ${seconds}s default stress budget for manual stress helpers"
}

run_v8() {
  if [[ -z "${SLOPPY_V8_ROOT:-}" ]]; then
    add_lane "v8.gated" "unavailable" "0" "tools/unix/dev.sh configure --enable-v8" "SLOPPY_V8_ROOT is not set"
    return
  fi
  run_lane "v8.configure" bash tools/unix/dev.sh configure --enable-v8 --v8-root "$SLOPPY_V8_ROOT"
  run_lane "v8.build" bash tools/unix/dev.sh build --enable-v8 --v8-root "$SLOPPY_V8_ROOT"
  run_lane "v8.test" bash tools/unix/dev.sh test --enable-v8 --v8-root "$SLOPPY_V8_ROOT"
}

run_provider() {
  ctest_lane "provider.default" "$(host_preset)" -R 'data\.|conformance\.(sqlite|postgres|sqlserver)'
}

should_run() {
  [[ "$area" == "all" || "$area" == "$1" ]]
}

should_run meta && run_meta
should_run static && run_static
should_run native && run_native
should_run compiler && run_compiler
should_run js && run_js
should_run fuzz && run_fuzz
should_run http2 && run_http2
should_run stress && run_stress
if [[ "$area" == "package" || ( "$area" == "all" && "$tier" != "pr" ) ]]; then run_package; fi
if [[ "$area" == "sanitizer" || ( "$area" == "all" && "$tier" != "pr" ) ]]; then run_sanitizer; fi
if [[ "$area" == "v8" || ( "$area" == "all" && "$tier" == "torture" ) ]]; then run_v8; fi
if [[ "$area" == "provider" || ( "$area" == "all" && "$tier" != "pr" ) ]]; then run_provider; fi

if [[ -n "$out" ]]; then
  if [[ "$out" != /* ]]; then
    out="$repo_root/$out"
  fi
  mkdir -p "$(dirname "$out")"
  {
    printf '{\n'
    printf '  "schemaVersion": 1,\n'
    printf '  "tier": "%s",\n' "$(json_escape "$tier")"
    printf '  "area": "%s",\n' "$(json_escape "$area")"
    printf '  "seed": %s,\n' "$seed"
    printf '  "startedAt": "%s",\n' "$(json_escape "$started_at")"
    printf '  "git": { "branch": "%s", "commit": "%s" },\n' "$(json_escape "$(git -C "$repo_root" branch --show-current 2>/dev/null || true)")" "$(json_escape "$(git -C "$repo_root" rev-parse HEAD 2>/dev/null || true)")"
    printf '  "host": { "os": "%s", "arch": "%s", "cpu": "", "logicalCores": %s },\n' "$(json_escape "$(uname -s)")" "$(json_escape "$(uname -m)")" "$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '1')"
    printf '  "lanes": [\n'
    for ((i=0; i<${#lane_ids[@]}; i++)); do
      printf '    { "id": "%s", "status": "%s", "durationMs": %s, "command": "%s", "notes": "%s" }' \
        "$(json_escape "${lane_ids[$i]}")" \
        "$(json_escape "${lane_statuses[$i]}")" \
        "${lane_durations[$i]}" \
        "$(json_escape "${lane_commands[$i]}")" \
        "$(json_escape "${lane_notes[$i]}")"
      if [[ "$i" -lt $((${#lane_ids[@]} - 1)) ]]; then printf ','; fi
      printf '\n'
    done
    printf '  ],\n'
    printf '  "summary": { "pass": %s, "fail": %s, "skipped": %s, "unavailable": %s },\n' "$summary_pass" "$summary_fail" "$summary_skipped" "$summary_unavailable"
    printf '  "finishedAt": "%s"\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    printf '}\n'
  } >"$out"
  echo "test-engine report: $out"
fi

if [[ "$failed" -gt 0 ]]; then
  exit 1
fi
exit 0
