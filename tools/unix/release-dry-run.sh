#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
preset=""
output_dir="artifacts/packages"
skip_package=0
skip_smoke=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --preset)
      preset="${2:?missing value for --preset}"
      shift 2
      ;;
    --output-dir)
      output_dir="${2:?missing value for --output-dir}"
      shift 2
      ;;
    --skip-package)
      skip_package=1
      shift
      ;;
    --skip-smoke)
      skip_smoke=1
      shift
      ;;
    -h|--help)
      cat <<'USAGE'
Usage: tools/unix/release-dry-run.sh [--preset PRESET] [--output-dir DIR] [--skip-package] [--skip-smoke]

Builds an experimental package artifact, verifies SHA256SUMS.txt through package smoke,
and writes an ignored dry-run summary. It does not create a public release, sign artifacts,
or require secrets.
USAGE
      exit 0
      ;;
    *)
      echo "unknown argument: $1" >&2
      exit 2
      ;;
  esac
done

dev_args=()
if [[ -n "$preset" ]]; then
  dev_args+=(--preset "$preset")
fi

if [[ "$skip_package" -eq 0 ]]; then
  "$repo_root/tools/unix/dev.sh" package "${dev_args[@]}"
fi

package_dir="$repo_root/$output_dir"
package_path="$(ls -t "$package_dir"/sloppy-*.tar.gz 2>/dev/null | head -n 1 || true)"
if [[ "$skip_smoke" -eq 0 ]]; then
  if [[ -z "$package_path" ]]; then
    echo "release dry-run could not find a package archive under $package_dir." >&2
    exit 1
  fi
  "$repo_root/tools/unix/dev.sh" test-package --package-path "$package_path"
fi

summary_dir="$repo_root/artifacts/release-dry-run"
mkdir -p "$summary_dir"
platform="$(uname -s | tr '[:upper:]' '[:lower:]')"
arch="$(uname -m)"
cat > "$summary_dir/${platform}-${arch}-summary.json" <<JSON
{
  "kind": "sloppy-alpha-release-dry-run",
  "platform": "$platform",
  "arch": "$arch",
  "packageDirectory": "$package_dir",
  "packagePath": "$package_path",
  "checksumPath": "$package_dir/SHA256SUMS.txt",
  "packageBuilt": $([[ "$skip_package" -eq 0 ]] && printf 'true' || printf 'false'),
  "packageSmokeRun": $([[ "$skip_smoke" -eq 0 ]] && printf 'true' || printf 'false'),
  "publicReleaseCreated": false,
  "secretsRequired": false
}
JSON

echo "release dry-run summary: $summary_dir/${platform}-${arch}-summary.json"
echo "release dry-run completed without creating a public release."
