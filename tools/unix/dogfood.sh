#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
manifest_path="$repo_root/examples/dogfood/alpha-dogfood.json"
package_path=""
require_v8_runtime=0
json=0

usage() {
  cat <<'USAGE'
Usage: tools/unix/dogfood.sh [--manifest PATH] [--package-path PATH] [--require-v8-runtime] [--json]

Validates the ALPHA-INFRA dogfood catalog and reports Unix/package lanes honestly.
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

for required in hello-artifact hello-source-input package-hello-artifact http-app https-app sqlite-app postgresql-app sqlserver-app framework-v2-app; do
  if ! grep -q "\"id\": \"$required\"" "$manifest_path"; then
    echo "dogfood: manifest missing scenario '$required'" >&2
    exit 1
  fi
done

if [[ -n "$package_path" ]]; then
  "$repo_root/tools/unix/test-package.sh" --package-path "$package_path" $(if [[ "$require_v8_runtime" -eq 1 ]]; then printf '%s' '--require-v8-runtime'; fi)
  package_status="PASS"
  package_reason="package-mode dogfood ran against supplied archive"
else
  package_status="SKIPPED"
  package_reason="PackagePath was not provided."
fi

if [[ "$json" -eq 1 ]]; then
  cat <<JSON
{
  "schemaVersion": 1,
  "catalog": "$manifest_path",
  "results": [
    {
      "lane": "source-input",
      "status": "UNAVAILABLE",
      "reason": "Unix static dogfood validates the catalog only; positive source-input execution remains V8-gated."
    },
    {
      "lane": "package outside-checkout",
      "status": "$package_status",
      "reason": "$package_reason"
    }
  ]
}
JSON
else
  echo "dogfood: source-input: UNAVAILABLE - Unix static dogfood validates the catalog only; positive source-input execution remains V8-gated."
  echo "dogfood: package outside-checkout: $package_status - $package_reason"
  echo "alpha dogfood harness completed."
fi
