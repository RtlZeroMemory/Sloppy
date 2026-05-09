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

This is package-layout smoke. Publishing, package-manager behavior, and live
provider validation use separate lanes. --require-v8-runtime requires the
manifest to record V8 runtime support and validates JS app execution from the
extracted package, including source-input execution.
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
  if [[ "$name" == "sloppy" ]]; then
    "$executable" doctor
  fi
}

invoke_outside_checkout_compiler_smoke() {
  local sloppyc_executable="$1"
  local sloppy_executable="$2"
  local stdlib_root="$3"
  local working_root="$4"
  local source_dir="$working_root/source"
  local artifact_dir="$working_root/artifacts"
  local run_output=""
  local run_status=0

  mkdir -p "$source_dir" "$artifact_dir"
  cat > "$source_dir/app.js" <<'APP'
import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();

app.mapGet("/", () => Results.text("Hello from packaged Sloppy"));

export default app;
APP

  (cd "$source_dir" && "$sloppyc_executable" build "$source_dir/app.js" --out "$artifact_dir")

  for artifact in app.plan.json app.js app.js.map; do
    [[ -f "$artifact_dir/$artifact" ]] || {
      echo "Package smoke missing compiled artifact: $artifact" >&2
      exit 1
    }
  done

  set +e
  run_output="$("$sloppy_executable" run --artifacts "$artifact_dir" --once GET / 2>&1)"
  run_status=$?
  set -e
  printf '%s\n' "$run_output"
  if [[ "$run_status" -eq 0 ]]; then
    grep -q "Hello from packaged Sloppy" <<<"$run_output" || {
      echo "packaged sloppy run succeeded but did not return the expected response." >&2
      exit 1
    }
    echo "Package smoke V8 artifact execution passed from extracted package layout."
    if [[ "$require_v8_runtime" -eq 1 ]]; then
      local source_run_output
      local source_run_status=0
      set +e
      source_run_output="$(cd "$source_dir" && "$sloppy_executable" run "$source_dir/app.js" --once GET / 2>&1)"
      source_run_status=$?
      set -e
      printf '%s\n' "$source_run_output"
      if [[ "$source_run_status" -ne 0 ]]; then
        echo "packaged sloppy source-input run failed: $source_run_output" >&2
        exit 1
      fi
      grep -q "Hello from packaged Sloppy" <<<"$source_run_output" || {
        echo "packaged sloppy source-input run did not return the expected response." >&2
        exit 1
      }
      echo "Package smoke V8 source-input execution passed from extracted package layout."
    fi
    return
  fi

  if [[ "$require_v8_runtime" -eq 1 ]]; then
    echo "Package smoke required V8 runtime execution, but packaged sloppy run failed: $run_output" >&2
    exit 1
  fi

  grep -q "requires V8-enabled build" <<<"$run_output" || {
    echo "packaged sloppy run failed for an unexpected reason: $run_output" >&2
    exit 1
  }
  echo "Package smoke artifact execution skipped/not configured: packaged sloppy is not V8-enabled."
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

top_level_count=0
package_root=""
while IFS= read -r -d '' top_level_entry; do
  top_level_count=$((top_level_count + 1))
  package_root="$top_level_entry"
done < <(find "$temp_root" -mindepth 1 -maxdepth 1 -print0)
if [[ "$top_level_count" -ne 1 ]]; then
  echo "Package smoke expected exactly one top-level archive entry, found $top_level_count." >&2
  exit 1
fi
if [[ ! -d "$package_root" ]]; then
  echo "Package smoke expected the top-level archive entry to be a directory." >&2
  exit 1
fi
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
for field in manifestSchema manifestVersion archiveName packageRoot platformTriplet releaseKind publicReleaseCreated canonicalDistribution npmPackageSource platformStatus runtimeUserStatus dependencyStatuses knownLimitations checksums; do
  grep -Eq "\"$field\"[[:space:]]*:" "$manifest_path" || {
    echo "Package smoke manifest is missing required release field: $field" >&2
    exit 1
  }
done
grep -Eq '"manifestSchema"[[:space:]]*:[[:space:]]*"sloppy.release-artifact.v1"' "$manifest_path" || {
  echo "Package smoke manifestSchema was not sloppy.release-artifact.v1." >&2
  exit 1
}
grep -Eq '"releaseKind"[[:space:]]*:[[:space:]]*"dry-run"' "$manifest_path" || {
  echo "Package smoke manifest must record releaseKind dry-run." >&2
  exit 1
}
grep -Eq '"publicReleaseCreated"[[:space:]]*:[[:space:]]*false' "$manifest_path" || {
  echo "Package smoke manifest must record publicReleaseCreated=false." >&2
  exit 1
}

if [[ "$require_v8_runtime" -eq 1 ]]; then
  grep -Eq '"containsV8Runtime"[[:space:]]*:[[:space:]]*true' "$manifest_path" || {
    echo "Package smoke required V8 runtime support, but manifest containsV8Runtime is not true." >&2
    exit 1
  }
elif grep -Eq '"containsV8Runtime"[[:space:]]*:[[:space:]]*true' "$manifest_path"; then
  echo "Package smoke note: manifest records V8 runtime support. This run validates layout only; use --require-v8-runtime for V8 execution proof."
fi

invoke_cli_smoke "$package_root/bin/sloppy" "sloppy"
invoke_cli_smoke "$package_root/bin/sloppyc" "sloppyc"

for required_file in README.md LICENSE docs/KNOWN_LIMITATIONS.md docs/LICENSES.md docs/NOTICE.md manifest.json; do
  [[ -f "$package_root/$required_file" ]] || {
    echo "Package smoke missing required package file: $required_file" >&2
    exit 1
  }
done
for required_directory in bin stdlib stdlib/sloppy templates examples docs; do
  [[ -d "$package_root/$required_directory" ]] || {
    echo "Package smoke missing required package directory: $required_directory" >&2
    exit 1
  }
done

stdlib_root="$package_root/stdlib/sloppy"
for asset in index.js app.js results.js schema.js data.js bootstrap.manifest.json internal/intrinsics.js; do
  [[ -f "$stdlib_root/$asset" ]] || { echo "Package smoke missing stdlib asset: $asset" >&2; exit 1; }
done

invoke_outside_checkout_compiler_smoke \
  "$package_root/bin/sloppyc" \
  "$package_root/bin/sloppy" \
  "$stdlib_root" \
  "$temp_root/outside-checkout-work"

for excluded in .git .sdeps build compiler/target target vcpkg_installed; do
  assert_missing "$package_root" "$excluded"
done
for excluded_sdk_file in engines/v8/include/v8.h engines/v8/lib/v8_monolith.lib; do
  assert_missing "$package_root" "$excluded_sdk_file"
done

local_path_scan_file="$temp_root/local-path-scan.txt"
if find "$package_root" -type f \( -name '*.json' -o -name '*.md' -o -name '*.txt' \) -print0 |
  xargs -0 grep -nE '([A-Z]:[\\/]|\\\\[^\\[:space:]"<>|]+\\[^\\[:space:]"<>|]+|(^|[[:space:]"=:,(])/(Users|home|Volumes|mnt|workspace|workspaces)(/|$))' 2>/dev/null |
  grep -vE 'https?://' >"$local_path_scan_file"; then
  cat "$local_path_scan_file" >&2
  rm -f "$local_path_scan_file"
  echo "Package smoke found maintainer-local absolute path text." >&2
  exit 1
fi
rm -f "$local_path_scan_file"

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
