#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
area="all"
runner_preset="linux-clang"
target_preset="linux-clang"
verify="false"
require_v8="false"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --area) area="${2:?missing value for --area}"; shift 2 ;;
    --preset) runner_preset="${2:?missing value for --preset}"; target_preset="$runner_preset"; shift 2 ;;
    --runner-preset) runner_preset="${2:?missing value for --runner-preset}"; shift 2 ;;
    --target-preset) target_preset="${2:?missing value for --target-preset}"; shift 2 ;;
    --verify) verify="true"; shift ;;
    --require-v8) require_v8="true"; shift ;;
    -h|--help)
      echo "Usage: tools/unix/update-goldens.sh [--area AREA] [--runner-preset PRESET] [--target-preset PRESET] [--verify] [--require-v8]"
      exit 0
      ;;
    *) echo "update-goldens: unknown option '$1'" >&2; exit 2 ;;
  esac
done

runner="$repo_root/build/$runner_preset/sloppy"
sloppy="$repo_root/build/$target_preset/sloppy"
if [[ -x "$repo_root/compiler/target/release/sloppyc" ]]; then
  sloppyc="$repo_root/compiler/target/release/sloppyc"
else
  sloppyc="$repo_root/compiler/target/debug/sloppyc"
fi
mode="default"
if [[ "$require_v8" == "true" ]]; then mode="v8"; fi

invoke_alpha_proof() {
  local run_area="$1"
  shift
  local suffix
  suffix="$(printf '%s-%s' "$run_area" "$*" | sed -E 's/[^A-Za-z0-9_.-]+/-/g; s/^-+|-+$//g')"
  [[ -n "$suffix" ]] || suffix="all"
  local work_root="$repo_root/artifacts/alpha-proof/update-$$-$runner_preset-$target_preset-$mode-$suffix"
  local args=(run "$repo_root/tools/golden/alpha-proof.ts" -- --root "$repo_root" --sloppy "$sloppy" --sloppyc "$sloppyc" --area "$run_area" --work-root "$work_root")
  args+=("$@")
  if [[ "$verify" != "true" ]]; then args+=(--update); fi
  if [[ "$require_v8" == "true" ]]; then args+=(--require-v8); fi
  "$runner" "${args[@]}"
}

if [[ "$area" == "all" || "$area" == "cli" ]]; then
  for section in help web program; do invoke_alpha_proof cli --cli-section "$section"; done
fi
if [[ "$area" == "all" || "$area" == "compiler" ]]; then
  for case_name in hello-mapget grouped-route http-methods framework-metadata full-framework-app-graph realistic-users-api provider-capability partial-body-without-schema function-module source-map; do
    invoke_alpha_proof compiler --compiler-case "$case_name"
  done
fi
if [[ "$area" == "all" || "$area" == "templates" ]]; then
  for template in api minimal-api program cli package-api node-compat; do invoke_alpha_proof templates --template "$template"; done
fi
if [[ "$area" == "all" || "$area" == "diagnostics" ]]; then
  invoke_alpha_proof diagnostics
fi
if [[ "$area" == "all" || "$area" == "alpha-flows" ]]; then
  for flow in api minimal-api program cli package-api node-compat direct-program direct-web; do invoke_alpha_proof alpha-flows --flow "$flow"; done
fi
if [[ "$area" == "all" || "$area" == "examples" ]]; then
  invoke_alpha_proof examples --example classification
  python3 - "$repo_root/tests/golden/examples/examples.manifest.json" <<'PY' | while IFS= read -r example; do invoke_alpha_proof examples --example "$example"; done
import json, sys
with open(sys.argv[1], encoding="utf-8") as handle:
    manifest = json.load(handle)
for entry in manifest["examples"]:
    if entry.get("prSmoke") is True:
        print(entry["name"])
PY
fi
if [[ "$area" == "all" || "$area" == "docs-snippets" ]]; then
  invoke_alpha_proof docs-snippets
fi
