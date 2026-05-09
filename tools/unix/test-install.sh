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
Usage: tools/unix/test-install.sh --package-path PATH [--require-v8-runtime] [--keep-temp]

Extracts a Sloppy tar.gz archive, puts packaged bin/ on PATH, then verifies
sloppy create, sloppy build, sloppy package, and sloppy run --once from a new
app outside the checkout.
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
  *) echo "Unix install smoke expects a .tar.gz archive: $package_path" >&2; exit 1 ;;
esac

temp_root="$(mktemp -d "${TMPDIR:-/tmp}/sloppy-install-smoke.XXXXXX")"
extract_root="$temp_root/extract"
work_root="$temp_root/work"
mkdir -p "$extract_root" "$work_root"

cleanup() {
  if [[ "$keep_temp" -eq 1 ]]; then
    echo "Keeping install smoke temp directory: $temp_root"
  else
    rm -rf "$temp_root"
  fi
}
trap cleanup EXIT

tar -xzf "$package_path" -C "$extract_root"
mapfile -t roots < <(find "$extract_root" -mindepth 1 -maxdepth 1 -type d)
if [[ "${#roots[@]}" -ne 1 ]]; then
  echo "Expected one package root in archive, found ${#roots[@]}." >&2
  exit 1
fi

package_root="${roots[0]}"
bin_root="$package_root/bin"
sloppy="$bin_root/sloppy"
sloppyc="$bin_root/sloppyc"
for required in "$sloppy" "$sloppyc" "$package_root/templates/minimal-api/sloppy.json"; do
  [[ -e "$required" ]] || { echo "Install smoke missing packaged file: $required" >&2; exit 1; }
done

export PATH="$bin_root:$PATH"
export SLOPPY_SLOPPYC="$sloppyc"

"$sloppy" --version
"$sloppy" doctor

create_output="$(cd "$work_root" && "$sloppy" create install-app --template minimal-api --format json)"
if [[ "$create_output" != *'"created":true'* ]]; then
  echo "sloppy create did not report JSON success: $create_output" >&2
  exit 1
fi

app_root="$work_root/install-app"
(cd "$app_root" && "$sloppy" build)
(cd "$app_root" && "$sloppy" package --format json)

set +e
run_output="$(cd "$app_root" && "$sloppy" run --once GET /health 2>&1)"
run_status=$?
set -e
if [[ "$require_v8_runtime" -eq 1 ]]; then
  if [[ "$run_status" -ne 0 || "$run_output" != *"ok"* ]]; then
    echo "V8-required install smoke did not return /health ok." >&2
    echo "$run_output" >&2
    exit 1
  fi
elif [[ "$run_status" -ne 0 && "$run_output" != *"requires V8-enabled build"* ]]; then
  echo "Non-V8 install smoke failed for an unexpected reason." >&2
  echo "$run_output" >&2
  exit 1
fi

echo "Unix install smoke passed: sloppy create/build/package/run from extracted archive."
