#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
package_path=""
output_dir="artifacts/npm"
skip_install_smoke=0
keep_temp=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --package-path)
      package_path="${2:?missing value for --package-path}"
      shift 2
      ;;
    --output-dir)
      output_dir="${2:?missing value for --output-dir}"
      shift 2
      ;;
    --skip-install-smoke)
      skip_install_smoke=1
      shift
      ;;
    --keep-temp)
      keep_temp=1
      shift
      ;;
    -h|--help)
      cat <<'USAGE'
Usage: tools/unix/npm-dry-run.sh --package-path PATH [--output-dir DIR] [--skip-install-smoke]

Stages @rtlzeromemory/sloppy plus the matching platform package from a tested
release archive, runs npm pack --dry-run, creates local tarballs, and optionally
installs those tarballs into a temp prefix for launcher/create/build smoke.
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
command -v npm >/dev/null 2>&1 || { echo "npm is required for npm dry-run packaging." >&2; exit 1; }
command -v node >/dev/null 2>&1 || { echo "node is required for npm launcher smoke." >&2; exit 1; }

case "$package_path" in
  *.tar.gz) ;;
  *) echo "Unix npm dry-run expects a .tar.gz release archive: $package_path" >&2; exit 1 ;;
esac

if [[ "$output_dir" != /* ]]; then
  output_root="$repo_root/$output_dir"
else
  output_root="$output_dir"
fi

temp_root="$(mktemp -d "${TMPDIR:-/tmp}/sloppy-npm-dry-run.XXXXXX")"
extract_root="$temp_root/extract"
stage_root="$output_root/stage"
tarball_root="$output_root/tarballs"

cleanup() {
  if [[ "$keep_temp" -eq 1 ]]; then
    echo "Keeping npm dry-run temp directory: $temp_root"
  else
    rm -rf "$temp_root"
  fi
}
trap cleanup EXIT

rm -rf "$stage_root" "$tarball_root"
mkdir -p "$extract_root" "$stage_root" "$tarball_root"
tar -xzf "$package_path" -C "$extract_root"

mapfile -t roots < <(find "$extract_root" -mindepth 1 -maxdepth 1 -type d)
if [[ "${#roots[@]}" -ne 1 ]]; then
  echo "Expected one package root in archive for npm staging, found ${#roots[@]}." >&2
  exit 1
fi
package_root="${roots[0]}"
manifest_path="$package_root/manifest.json"
[[ -f "$manifest_path" ]] || { echo "Archive package is missing manifest.json." >&2; exit 1; }

node "$repo_root/tools/unix/scripts/validate-npm-release-manifest.mjs" "$manifest_path" "$(basename "$package_root")"
triplet="$(node -e 'const fs=require("fs"); const manifest=JSON.parse(fs.readFileSync(process.argv[1],"utf8")); process.stdout.write(String(manifest.platformTriplet || ""));' "$manifest_path")"
case "$triplet" in
  linux-x64) platform_package_dir="sloppy-linux-x64" ;;
  macos-arm64|macos-x64)
    echo "macOS npm packages are not staged by this alpha workflow yet; hosted package proof is future work." >&2
    exit 1
    ;;
  windows-x64) platform_package_dir="sloppy-win32-x64" ;;
  *) echo "Unsupported npm platform package triplet in manifest: $triplet" >&2; exit 1 ;;
esac

copy_dir_contents() {
  local source="$1"
  local destination="$2"
  mkdir -p "$destination"
  cp -R "$source"/. "$destination"/
}

runtime_stage="$stage_root/sloppy"
platform_stage="$stage_root/$platform_package_dir"
copy_dir_contents "$repo_root/packages/npm/sloppy" "$runtime_stage"
copy_dir_contents "$repo_root/packages/npm/$platform_package_dir" "$platform_stage"

for entry in bin stdlib templates examples docs manifest.json LICENSE README.md; do
  source="$package_root/$entry"
  [[ -e "$source" ]] || { echo "Archive package is missing npm platform package content: $entry" >&2; exit 1; }
  cp -R "$source" "$platform_stage/"
done

node "$repo_root/tools/unix/scripts/validate-npm-package-policy.mjs" "$runtime_stage/package.json"
node "$repo_root/tools/unix/scripts/validate-npm-package-policy.mjs" "$platform_stage/package.json"

(cd "$runtime_stage" && npm pack --dry-run --silent --json >/dev/null)
(cd "$platform_stage" && npm pack --dry-run --silent --json >/dev/null)
(cd "$platform_stage" && npm pack --pack-destination "$tarball_root" >/dev/null)
(cd "$runtime_stage" && npm pack --pack-destination "$tarball_root" >/dev/null)

runtime_tarball_name="$(node -e 'const p=require(process.argv[1]); let n=p.name; if (n.startsWith("@")) n=n.slice(1); process.stdout.write(`${n.replace("/", "-")}-${p.version}.tgz`);' "$runtime_stage/package.json")"
platform_tarball_name="$(node -e 'const p=require(process.argv[1]); let n=p.name; if (n.startsWith("@")) n=n.slice(1); process.stdout.write(`${n.replace("/", "-")}-${p.version}.tgz`);' "$platform_stage/package.json")"
runtime_tarball="$tarball_root/$runtime_tarball_name"
platform_tarball="$tarball_root/$platform_tarball_name"
[[ -f "$runtime_tarball" && -f "$platform_tarball" ]] || {
  echo "npm dry-run did not produce both root and platform tarballs." >&2
  exit 1
}

if [[ "$skip_install_smoke" -eq 0 ]]; then
  install_root="$temp_root/install"
  mkdir -p "$install_root"
  npm install --prefix "$install_root" "$runtime_tarball" "$platform_tarball" >/dev/null
  launcher="$install_root/node_modules/@rtlzeromemory/sloppy/bin/sloppy.js"
  node "$launcher" --version
  node "$launcher" doctor

  create_root="$temp_root/created-work"
  mkdir -p "$create_root"
  (cd "$create_root" && node "$launcher" create tmp-npm-app --template minimal-api)
  created_app="$create_root/tmp-npm-app"
  (cd "$created_app" && node "$launcher" build)
  set +e
  run_output="$(cd "$created_app" && node "$launcher" run --once GET /health 2>&1)"
  run_status=$?
  set -e
  if [[ "$run_status" -eq 0 ]]; then
    if [[ "$run_output" != *"ok"* ]]; then
      echo "npm launcher run smoke did not return /health ok." >&2
      echo "$run_output" >&2
      exit 1
    fi
  elif [[ "$run_output" != *"requires V8-enabled build"* ]]; then
    echo "npm launcher run smoke failed unexpectedly." >&2
    echo "$run_output" >&2
    exit 1
  fi

  missing_root="$temp_root/missing-platform"
  mkdir -p "$missing_root"
  npm install --prefix "$missing_root" --omit=optional "$runtime_tarball" >/dev/null
  missing_launcher="$missing_root/node_modules/@rtlzeromemory/sloppy/bin/sloppy.js"
  if node "$missing_launcher" --version >/dev/null 2>&1; then
    echo "missing-platform launcher unexpectedly succeeded." >&2
    exit 1
  fi
fi

mkdir -p "$output_root"
node -e '
const fs = require("fs");
const summaryPath = process.argv[1];
const summary = {
  kind: "sloppy-npm-dry-run",
  packageArchive: process.argv[2],
  rootPackage: "@rtlzeromemory/sloppy",
  platformPackage: process.argv[3],
  publishTag: "alpha",
  nativeInstallScripts: false,
  nodeGyp: false,
  postinstallBuildOrDownload: false,
  npmPublished: false,
  tarballDirectory: process.argv[4],
  installSmokeRun: process.argv[5] === "true"
};
fs.writeFileSync(summaryPath, JSON.stringify(summary, null, 2) + "\n");
' "$output_root/summary.json" "$package_path" "@rtlzeromemory/$platform_package_dir" "$tarball_root" "$([[ "$skip_install_smoke" -eq 0 ]] && echo true || echo false)"

echo "npm dry-run completed without publishing packages."
echo "npm tarballs: $tarball_root"
