#!/usr/bin/env bash
set -euo pipefail

package_path=""
require_v8_runtime=0
keep_temp=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --package-path)
      package_path="${2:?missing value for --package-path}"
      shift 2
      ;;
    --require-v8-runtime)
      require_v8_runtime=1
      shift
      ;;
    --keep-temp)
      keep_temp=1
      shift
      ;;
    -h|--help)
      cat <<'USAGE'
Usage: tools/unix/test-package.sh --package-path PATH [--require-v8-runtime] [--keep-temp]

Extracts an experimental Sloppy tar.gz package outside the checkout, runs basic
sloppy/sloppyc --version and --help smoke checks, verifies stdlib assets, validates
manifest fields, checks excluded build/dependency directories, and verifies SHA256SUMS.txt
when present.

This is package-layout smoke. It is not a public release, package-manager, live provider,
or V8 execution gate. --require-v8-runtime only validates packaged runtime files and the
manifest bit; V8 execution still requires a separately configured V8-enabled package
smoke.
USAGE
      exit 0
      ;;
    *)
      echo "unknown argument: $1" >&2
      exit 2
      ;;
  esac
done

[[ -n "$package_path" ]] || { echo "--package-path is required" >&2; exit 2; }
[[ -f "$package_path" ]] || { echo "package does not exist: $package_path" >&2; exit 1; }

case "$package_path" in
  *.tar.gz) ;;
  *) echo "Unix package smoke expects a .tar.gz archive: $package_path" >&2; exit 1 ;;
esac

invoke_cli_smoke() {
  local executable="$1"
  local name="$2"
  [[ -x "$executable" ]] || { echo "Package smoke missing $name: $executable" >&2; exit 1; }
  "$executable" --version
  "$executable" --help
}

assert_missing() {
  local root="$1"
  local relative="$2"
  if [[ -e "$root/$relative" ]]; then
    echo "Package smoke found excluded path: $relative" >&2
    exit 1
  fi
}

temp_root="$(mktemp -d "${TMPDIR:-/tmp}/sloppy-package-smoke.XXXXXX")"
cleanup() {
  if [[ "$keep_temp" -eq 1 ]]; then
    echo "Keeping smoke temp directory: $temp_root"
  else
    rm -rf "$temp_root"
  fi
}
trap cleanup EXIT

tar -xzf "$package_path" -C "$temp_root"

mapfile -t roots < <(find "$temp_root" -mindepth 1 -maxdepth 1 -type d)
if [[ "${#roots[@]}" -ne 1 ]]; then
  echo "Package smoke expected exactly one archive root directory, found ${#roots[@]}." >&2
  exit 1
fi

package_root="${roots[0]}"
manifest_path="$package_root/manifest.json"
[[ -f "$manifest_path" ]] || { echo "Package smoke missing manifest.json" >&2; exit 1; }

grep -Eq '"name"[[:space:]]*:[[:space:]]*"sloppy"' "$manifest_path" || {
  echo "Package smoke manifest name was not 'sloppy'." >&2
  exit 1
}
grep -Eq '"containsStdlib"[[:space:]]*:[[:space:]]*true' "$manifest_path" || {
  echo "Package smoke manifest does not record stdlib inclusion." >&2
  exit 1
}
grep -Eq '"containsV8Sdk"[[:space:]]*:[[:space:]]*false' "$manifest_path" || {
  echo "Package smoke manifest must not record V8 SDK inclusion." >&2
  exit 1
}

if [[ "$require_v8_runtime" -eq 1 ]]; then
  grep -Eq '"containsV8Runtime"[[:space:]]*:[[:space:]]*true' "$manifest_path" || {
    echo "Package smoke required V8 runtime files, but manifest containsV8Runtime is not true." >&2
    exit 1
  }
elif grep -Eq '"containsV8Runtime"[[:space:]]*:[[:space:]]*true' "$manifest_path"; then
  echo "Package smoke note: manifest records V8 runtime files. This run validates layout only; V8 execution still requires a V8-enabled package smoke."
fi

invoke_cli_smoke "$package_root/bin/sloppy" "sloppy"
invoke_cli_smoke "$package_root/bin/sloppyc" "sloppyc"

stdlib_root="$package_root/lib/sloppy/stdlib/sloppy"
for asset in index.js app.js results.js schema.js data.js bootstrap.manifest.json internal/intrinsics.js; do
  [[ -f "$stdlib_root/$asset" ]] || { echo "Package smoke missing stdlib asset: $asset" >&2; exit 1; }
done

for excluded in .git .sdeps build compiler/target target vcpkg_installed; do
  assert_missing "$package_root" "$excluded"
done
for excluded_sdk_file in lib/sloppy/engines/v8/include/v8.h lib/sloppy/engines/v8/lib/v8_monolith.lib; do
  assert_missing "$package_root" "$excluded_sdk_file"
done

if [[ "$require_v8_runtime" -eq 1 ]]; then
  v8_runtime_root="$package_root/lib/sloppy/engines/v8"
  [[ -d "$v8_runtime_root" ]] || {
    echo "Package smoke required V8 runtime files, but lib/sloppy/engines/v8 is missing." >&2
    exit 1
  }
  runtime_count="$(find "$v8_runtime_root" -maxdepth 1 -type f \( -name '*.dll' -o -name '*.so' -o -name '*.dylib' \) | wc -l | tr -d ' ')"
  [[ "$runtime_count" != "0" ]] || {
    echo "Package smoke required V8 runtime files, but no DLL/shared-library files were found." >&2
    exit 1
  }
fi

checksum_path="$(dirname "$package_path")/SHA256SUMS.txt"
if [[ -f "$checksum_path" ]]; then
  package_name="$(basename "$package_path")"
  if command -v sha256sum >/dev/null 2>&1; then
    actual_hash="$(sha256sum "$package_path" | awk '{print $1}')"
  else
    actual_hash="$(shasum -a 256 "$package_path" | awk '{print $1}')"
  fi
  awk -v hash="$actual_hash" -v name="$package_name" '
    $1 == hash && ($2 == name || $2 == "*" name) { found = 1 }
    END { exit found ? 0 : 1 }
  ' "$checksum_path" || {
    echo "Package smoke checksum file entry for archive does not match archive hash." >&2
    exit 1
  }
fi

echo "Package smoke passed: $package_path"
echo "Extracted package root: $package_root"
