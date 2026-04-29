#!/usr/bin/env bash
set -euo pipefail

configuration="Release"
output_dir="artifacts/packages"
include_examples=0
skip_build=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --configuration)
      configuration="${2:?missing value for --configuration}"
      shift 2
      ;;
    --output-dir)
      output_dir="${2:?missing value for --output-dir}"
      shift 2
      ;;
    --include-examples)
      include_examples=1
      shift
      ;;
    --skip-build)
      skip_build=1
      shift
      ;;
    -h|--help)
      cat <<'USAGE'
Usage: tools/unix/package.sh [--configuration Release|Debug] [--output-dir DIR] [--include-examples] [--skip-build]

Creates an experimental local tar.gz package from already supported Unix build outputs.
This script is intentionally small and does not install dependencies, fetch V8, or claim a
validated Linux/macOS release path until EPIC-26 adds cross-platform CI.
USAGE
      exit 0
      ;;
    *)
      echo "unknown argument: $1" >&2
      exit 2
      ;;
  esac
done

case "$configuration" in
  Release)
    build_type="Release"
    cargo_profile="release"
    cargo_args=(build --manifest-path compiler/Cargo.toml --release)
    ;;
  Debug)
    build_type="Debug"
    cargo_profile="debug"
    cargo_args=(build --manifest-path compiler/Cargo.toml)
    ;;
  *)
    echo "--configuration must be Release or Debug" >&2
    exit 2
    ;;
esac

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
configuration_lower="$(printf '%s' "$configuration" | tr '[:upper:]' '[:lower:]')"
build_dir="$repo_root/build/unix-$configuration_lower"
package_version="0.0.0-dev"
kernel_name="$(uname -s)"
machine_name="$(uname -m)"

case "$kernel_name" in
  Linux) platform="linux" ;;
  Darwin) platform="macos" ;;
  *) platform="$(printf '%s' "$kernel_name" | tr '[:upper:]' '[:lower:]')" ;;
esac

case "$machine_name" in
  x86_64|amd64) arch="x64" ;;
  arm64|aarch64) arch="arm64" ;;
  *) arch="$machine_name" ;;
esac

cd "$repo_root"

if [[ "$skip_build" -eq 0 ]]; then
  cmake -S "$repo_root" -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE="$build_type"
  cmake --build "$build_dir"
  cargo "${cargo_args[@]}"
fi

sloppy_bin="$build_dir/sloppy"
sloppyc_bin="$repo_root/compiler/target/$cargo_profile/sloppyc"
[[ -x "$sloppy_bin" ]] || { echo "missing built sloppy executable: $sloppy_bin" >&2; exit 1; }
[[ -x "$sloppyc_bin" ]] || { echo "missing built sloppyc executable: $sloppyc_bin" >&2; exit 1; }

package_name="sloppy-$package_version-$platform-$arch"
output_root="$repo_root/$output_dir"
stage_root="$output_root/stage/$package_name"
rm -rf "$stage_root"
mkdir -p "$stage_root/bin" "$stage_root/lib/sloppy/stdlib" "$stage_root/share/sloppy/licenses" "$stage_root/share/sloppy/schemas"

cp "$sloppy_bin" "$stage_root/bin/sloppy"
cp "$sloppyc_bin" "$stage_root/bin/sloppyc"
cp -R "$repo_root/stdlib/sloppy" "$stage_root/lib/sloppy/stdlib/sloppy"
cp "$repo_root/LICENSE.md" "$stage_root/LICENSE"
cat > "$stage_root/THIRD_PARTY_NOTICES.md" <<'NOTICES'
# Third Party Notices

This experimental local package may include runtime dependencies provided by the
host build environment. It does not include V8 SDK headers/import libraries,
database drivers, package manager metadata, installers, or signed release metadata.

Dependency license review and a complete release notice file are still required
before any public release.
NOTICES
cat > "$stage_root/README.md" <<'README'
# Sloppy Local Package

This is an experimental development artifact, not a public release. It does not install
anything, fetch dependencies, bundle a V8 SDK, provide package-manager behavior, or claim
production readiness.
README

if [[ "$include_examples" -eq 1 ]]; then
  mkdir -p "$stage_root/share/sloppy"
  cp -R "$repo_root/examples" "$stage_root/share/sloppy/examples"
fi

commit="$(git rev-parse --short HEAD 2>/dev/null || printf 'unknown')"
cat > "$stage_root/manifest.json" <<JSON
{
  "name": "sloppy",
  "version": "$package_version",
  "platform": "$platform",
  "arch": "$arch",
  "configuration": "$configuration",
  "commit": "$commit",
  "containsV8Runtime": false,
  "containsV8Sdk": false,
  "containsStdlib": true,
  "containsExamples": $([[ "$include_examples" -eq 1 ]] && printf true || printf false),
  "containsNativeRuntimeDependencies": false,
  "tools": ["sloppy", "sloppyc"],
  "layoutVersion": 1,
  "notes": ["experimental", "not production ready", "no installer", "no package manager"]
}
JSON

mkdir -p "$output_root"
archive_path="$output_root/$package_name.tar.gz"
tar -C "$output_root/stage" -czf "$archive_path" "$package_name"

if command -v sha256sum >/dev/null 2>&1; then
  (cd "$output_root" && sha256sum "$(basename "$archive_path")" > SHA256SUMS.txt)
else
  (cd "$output_root" && shasum -a 256 "$(basename "$archive_path")" > SHA256SUMS.txt)
fi

echo "Created package: $archive_path"
cat "$output_root/SHA256SUMS.txt"
