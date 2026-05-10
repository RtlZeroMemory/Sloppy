#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
preset=""
output_dir="artifacts/packages"
skip_package=0
skip_smoke=0
enable_v8=0
require_v8_runtime=0

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
    --enable-v8)
      enable_v8=1
      shift
      ;;
    --require-v8-runtime)
      require_v8_runtime=1
      enable_v8=1
      shift
      ;;
    -h|--help)
      cat <<'USAGE'
Usage: tools/unix/release-dry-run.sh [--preset PRESET] [--output-dir DIR] [--skip-package] [--skip-smoke] [--enable-v8] [--require-v8-runtime]

Builds an experimental package artifact, verifies SHA256SUMS.txt through package smoke,
and writes an ignored dry-run summary. Publishing, signing, and secrets are
separate release steps.
USAGE
      exit 0
      ;;
    *)
      echo "unknown argument: $1" >&2
      exit 2
      ;;
  esac
done

package_configuration() {
  case "$preset" in
    *debug*|*Debug*) printf 'Debug\n' ;;
    *) printf 'Release\n' ;;
  esac
}

if [[ "$skip_package" -eq 0 ]]; then
  package_args=(--configuration "$(package_configuration)" --output-dir "$output_dir")
  if [[ "$enable_v8" -eq 1 ]]; then
    package_args+=(--enable-v8)
  fi
  bash "$repo_root/tools/unix/package.sh" "${package_args[@]}"
fi

case "$output_dir" in
  /*) package_dir="$output_dir" ;;
  *) package_dir="$repo_root/$output_dir" ;;
esac
package_path="$(ls -t "$package_dir"/sloppy-*.tar.gz 2>/dev/null | head -n 1 || true)"
if [[ "$skip_smoke" -eq 0 ]]; then
  if [[ -z "$package_path" ]]; then
    echo "release dry-run could not find a package archive under $package_dir." >&2
    exit 1
  fi
  smoke_args=(test-package --package-path "$package_path")
  if [[ "$require_v8_runtime" -eq 1 ]]; then
    smoke_args+=(--require-v8-runtime)
  fi
  "$repo_root/tools/unix/dev.sh" "${smoke_args[@]}"
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
  "includeV8Runtime": $([[ "$enable_v8" -eq 1 ]] && printf 'true' || printf 'false'),
  "requireV8Runtime": $([[ "$require_v8_runtime" -eq 1 ]] && printf 'true' || printf 'false'),
  "publicReleaseCreated": false,
  "secretsRequired": false
}
JSON

echo "release dry-run summary: $summary_dir/${platform}-${arch}-summary.json"
echo "release dry-run completed; publishing is a separate step."
