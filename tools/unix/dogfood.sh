#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
manifest_path="$repo_root/examples/dogfood/dogfood.json"
package_path=""
require_v8_runtime=0
json=0

usage() {
  cat <<'USAGE'
Usage: tools/unix/dogfood.sh [--manifest PATH] [--package-path PATH] [--require-v8-runtime] [--json]

Validates the dogfood catalog and reports Unix/package lanes honestly.
Source-input positive execution is V8-gated and is not claimed by this static Unix path.
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --manifest)
      manifest_path="${2:?missing value for --manifest}"
      shift 2
      ;;
    --package-path)
      package_path="${2:?missing value for --package-path}"
      shift 2
      ;;
    --require-v8-runtime)
      require_v8_runtime=1
      shift
      ;;
    --json)
      json=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "dogfood: unknown argument '$1'" >&2
      exit 2
      ;;
  esac
done

[[ -f "$manifest_path" ]] || { echo "dogfood: manifest missing: $manifest_path" >&2; exit 1; }

python_bin=""
if command -v python3 >/dev/null 2>&1; then
  python_bin="python3"
else
  echo "dogfood: python3 is required for manifest validation" >&2
  exit 1
fi

"$python_bin" - "$manifest_path" hello-artifact hello-source-input package-hello-artifact http-app https-app sqlite-app postgresql-app sqlserver-app framework-v2-app <<'PY'
import json
import sys

manifest_path = sys.argv[1]
required_ids = sys.argv[2:]

try:
    with open(manifest_path, "r", encoding="utf-8") as manifest_file:
        manifest = json.load(manifest_file)
except (OSError, json.JSONDecodeError) as exc:
    print(f"dogfood: invalid dogfood manifest '{manifest_path}': {exc}", file=sys.stderr)
    sys.exit(1)

scenarios = manifest.get("scenarios")
if not isinstance(scenarios, list):
    print("dogfood: manifest field 'scenarios' must be an array", file=sys.stderr)
    sys.exit(1)

scenario_ids = {
    scenario.get("id")
    for scenario in scenarios
    if isinstance(scenario, dict) and isinstance(scenario.get("id"), str)
}
for required_id in required_ids:
    if required_id not in scenario_ids:
        print(f"dogfood: manifest missing scenario '{required_id}'", file=sys.stderr)
        sys.exit(1)
PY

if [[ -n "$package_path" ]]; then
  package_args=(--package-path "$package_path")
  if [[ "$require_v8_runtime" -eq 1 ]]; then
    package_args+=(--require-v8-runtime)
  fi
  "$repo_root/tools/unix/test-package.sh" "${package_args[@]}"
  package_status="PASS"
  package_reason="package-mode dogfood ran against supplied archive"
else
  package_status="SKIPPED"
  package_reason="PackagePath was not provided."
fi

if [[ "$json" -eq 1 ]]; then
  "$python_bin" - "$manifest_path" "$package_status" "$package_reason" <<'PY'
import json
import sys

manifest_path, package_status, package_reason = sys.argv[1:4]
print(json.dumps({
    "schemaVersion": 1,
    "catalog": manifest_path,
    "results": [
        {
            "lane": "source-input",
            "status": "UNAVAILABLE",
            "reason": "Unix static dogfood validates the catalog only; positive source-input execution remains V8-gated.",
        },
        {
            "lane": "package outside-checkout",
            "status": package_status,
            "reason": package_reason,
        },
    ],
}, ensure_ascii=True, indent=2))
PY
else
  echo "dogfood: source-input: UNAVAILABLE - Unix static dogfood validates the catalog only; positive source-input execution remains V8-gated."
  echo "dogfood: package outside-checkout: $package_status - $package_reason"
  echo "dogfood harness completed."
fi
