#!/usr/bin/env bash
set -euo pipefail

target_arch="x64"
depot_tools_root=".sdeps/depot_tools"
work_root=".sdeps/v8-work-macos-$target_arch"
sdk_root=".sdeps/v8/macos-$target_arch"
archive_dir="artifacts/v8-sdk"
v8_revision="7221f49fdb6c89cce6be08005732ebcab3c45b38"
skip_fetch=0
skip_build=0
package_only=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --target-arch)
      target_arch="${2:?missing value for --target-arch}"
      shift 2
      ;;
    --depot-tools-root)
      depot_tools_root="${2:?missing value for --depot-tools-root}"
      shift 2
      ;;
    --work-root)
      work_root="${2:?missing value for --work-root}"
      shift 2
      ;;
    --sdk-root)
      sdk_root="${2:?missing value for --sdk-root}"
      shift 2
      ;;
    --archive-dir)
      archive_dir="${2:?missing value for --archive-dir}"
      shift 2
      ;;
    --v8-revision)
      v8_revision="${2:?missing value for --v8-revision}"
      shift 2
      ;;
    --skip-fetch)
      skip_fetch=1
      shift
      ;;
    --skip-build)
      skip_build=1
      shift
      ;;
    --package-only)
      package_only=1
      shift
      ;;
    -h|--help)
      cat <<'USAGE'
Usage: tools/unix/build-v8-macos.sh [--target-arch x64|arm64] [--depot-tools-root DIR] [--work-root DIR]
                                    [--sdk-root DIR] [--archive-dir DIR] [--v8-revision SHA]
                                    [--skip-fetch] [--skip-build] [--package-only]

Builds and packages Sloppy's pinned macOS V8 SDK as a GitHub-release-asset-ready tarball.
USAGE
      exit 0
      ;;
    *)
      echo "build-v8-macos: unknown argument: $1" >&2
      exit 2
      ;;
  esac
done

case "$target_arch" in
  x64|arm64) ;;
  *) echo "build-v8-macos: --target-arch must be x64 or arm64" >&2; exit 2 ;;
esac

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
depot_tools_git="https://chromium.googlesource.com/chromium/tools/depot_tools.git"

resolve_repo_path() {
  case "$1" in
    /*) printf '%s\n' "$1" ;;
    *) printf '%s\n' "$repo_root/$1" ;;
  esac
}

assert_safe_local_path() {
  local name="$1"
  local path="$2"
  local full
  local parent
  parent="$(dirname "$path")"
  mkdir -p "$parent"
  full="$(cd "$parent" && pwd)/$(basename "$path")"
  case "$full" in
    ""|"/"|"$repo_root"|"$repo_root/."|"$repo_root/..")
      echo "build-v8-macos: refusing to use unsafe $name: $path" >&2
      exit 1
      ;;
  esac
  printf '%s\n' "$full"
}

assert_tool() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "build-v8-macos: missing required tool: $1" >&2
    exit 1
  fi
}

copy_required_file() {
  local source="$1"
  local destination="$2"
  if [[ ! -f "$source" ]]; then
    echo "build-v8-macos: expected build output is missing: $source" >&2
    exit 1
  fi
  cp -f "$source" "$destination"
}

copy_static_archive_as_full() {
  local source="$1"
  local destination="$2"
  local member_root="$3"
  local archive_tool="ar"
  local members=()
  local object_paths=()
  local member

  if [[ ! -f "$source" ]]; then
    echo "build-v8-macos: expected build output is missing: $source" >&2
    exit 1
  fi
  if command -v llvm-ar >/dev/null 2>&1; then
    archive_tool="llvm-ar"
  elif xcrun --find llvm-ar >/dev/null 2>&1; then
    archive_tool="$(xcrun --find llvm-ar)"
  fi
  if ! file "$source" | grep -q "thin archive"; then
    cp -f "$source" "$destination"
    return
  fi
  while IFS= read -r member; do
    members+=("$member")
  done < <("$archive_tool" t "$source" 2>/dev/null) || {
    copy_archive_from_ninja_inputs "$source" "$destination"
    return
  }
  if [[ "${#members[@]}" -eq 0 ]]; then
    copy_archive_from_ninja_inputs "$source" "$destination"
    return
  fi
  for member in "${members[@]}"; do
    [[ -n "$member" ]] || continue
    if [[ "$member" = /* ]]; then
      [[ -f "$member" ]] || { echo "build-v8-macos: archive member is missing: $member" >&2; exit 1; }
      object_paths+=("$member")
    elif [[ -f "$member_root/$member" ]]; then
      object_paths+=("$member")
    else
      match="$(find "$member_root" -name "$member" -type f | head -n 1)"
      [[ -n "$match" ]] || { echo "build-v8-macos: archive member is missing under $member_root: $member" >&2; exit 1; }
      object_paths+=("$match")
    fi
  done
  rm -f "$destination"
  create_static_archive "$destination" "$member_root" "${object_paths[@]}"
}

create_static_archive() {
  local destination="$1"
  local working_dir="$2"
  shift 2

  rm -f "$destination"
  if [[ "$(uname -s)" == "Darwin" ]]; then
    (cd "$working_dir" && libtool -static -o "$destination" "$@")
  else
    (cd "$working_dir" && ar crs "$destination" "$@")
  fi
}

copy_archive_from_ninja_inputs() {
  local source="$1"
  local destination="$2"
  local target
  local input
  local objects=()

  case "$source" in
    "$v8_build_dir"/*) target="${source#"$v8_build_dir"/}" ;;
    *) echo "build-v8-macos: cannot derive Ninja target for archive: $source" >&2; exit 1 ;;
  esac

  while IFS= read -r input; do
    [[ "$input" == *.o ]] || continue
    if [[ "$input" = /* ]]; then
      objects+=("$input")
    elif [[ -f "$v8_build_dir/$input" ]]; then
      objects+=("$v8_build_dir/$input")
    fi
  done < <(ninja -C "$v8_build_dir" -t inputs "$target" 2>/dev/null)

  if [[ "${#objects[@]}" -eq 0 ]]; then
    echo "build-v8-macos: could not list object inputs for archive target: $target" >&2
    exit 1
  fi

  create_static_archive "$destination" "/" "${objects[@]}"
}

write_gn_args() {
  local args_path="$1"
  cat > "$args_path" <<GNARGS
is_debug = false
target_os = "mac"
target_cpu = "$target_arch"
v8_target_cpu = "$target_arch"
is_component_build = false
v8_monolithic = true
v8_use_external_startup_data = false
v8_enable_temporal_support = false
v8_enable_webassembly = false
v8_enable_sandbox = true
use_custom_libcxx = true
use_thin_archives = false
use_allocator_shim = false
use_clang_modules = false
treat_warnings_as_errors = false
symbol_level = 1
GNARGS
}

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "build-v8-macos: this SDK builder requires macOS." >&2
  exit 1
fi

for tool in git python3 ninja tar file libtool; do
  assert_tool "$tool"
done

platform="macos-$target_arch"
asset_platform="darwin-$target_arch"
depot_tools_root="$(assert_safe_local_path "DepotToolsRoot" "$(resolve_repo_path "$depot_tools_root")")"
work_root="$(assert_safe_local_path "WorkRoot" "$(resolve_repo_path "$work_root")")"
sdk_root="$(assert_safe_local_path "SdkRoot" "$(resolve_repo_path "$sdk_root")")"
archive_dir="$(assert_safe_local_path "ArchiveDir" "$(resolve_repo_path "$archive_dir")")"
v8_checkout="$work_root/v8"
v8_build_dir="$v8_checkout/out.gn/$target_arch.release"

if [[ "$package_only" -eq 0 && ( "$skip_fetch" -eq 0 || "$skip_build" -eq 0 ) ]]; then
  if [[ ! -d "$depot_tools_root" ]]; then
    git clone "$depot_tools_git" "$depot_tools_root"
  fi
  export PATH="$depot_tools_root:$PATH"
  gclient --version >/dev/null
fi

if [[ "$skip_fetch" -eq 0 && "$package_only" -eq 0 ]]; then
  mkdir -p "$work_root"
  if [[ ! -d "$v8_checkout" ]]; then
    (cd "$work_root" && fetch v8)
  else
    git -C "$v8_checkout" fetch origin
  fi
  git -C "$v8_checkout" checkout "$v8_revision"
  (cd "$v8_checkout" && gclient sync)
fi

if [[ "$package_only" -eq 0 ]]; then
  mkdir -p "$v8_build_dir"
  write_gn_args "$v8_build_dir/args.gn"
fi

if [[ "$skip_build" -eq 0 && "$package_only" -eq 0 ]]; then
  ninja_jobs="${SLOPPY_V8_NINJA_JOBS:-$(sysctl -n hw.ncpu)}"
  (cd "$v8_checkout" && gn gen "out.gn/$target_arch.release")
  (cd "$v8_checkout" && ninja -j "$ninja_jobs" -C "out.gn/$target_arch.release" v8_monolith v8_libplatform v8_libbase)
fi

revision="$(git -C "$v8_checkout" rev-parse HEAD)"
if [[ "$revision" != "$v8_revision" ]]; then
  echo "build-v8-macos: V8 checkout revision $revision does not match pinned revision $v8_revision." >&2
  exit 1
fi

rm -rf "$sdk_root"
mkdir -p "$sdk_root/include" "$sdk_root/lib" "$sdk_root/share" "$sdk_root/support/libcxx/include" "$sdk_root/support/libcxx/buildtools"
cp -R "$v8_checkout/include/." "$sdk_root/include/"
copy_static_archive_as_full "$v8_build_dir/obj/libv8_monolith.a" "$sdk_root/lib/libv8_monolith.a" "$v8_build_dir/obj"
copy_static_archive_as_full "$v8_build_dir/obj/libv8_libplatform.a" "$sdk_root/lib/libv8_libplatform.a" "$v8_build_dir/obj"
copy_static_archive_as_full "$v8_build_dir/obj/libv8_libbase.a" "$sdk_root/lib/libv8_libbase.a" "$v8_build_dir/obj"
if [[ -f "$v8_build_dir/obj/libv8_libsampler.a" ]]; then
  copy_static_archive_as_full "$v8_build_dir/obj/libv8_libsampler.a" "$sdk_root/lib/libv8_libsampler.a" "$v8_build_dir/obj"
fi
copy_required_file "$v8_build_dir/v8_features.json" "$sdk_root/share/v8_features.json"
copy_required_file "$v8_build_dir/v8_build_config.json" "$sdk_root/share/v8_build_config.json"
if [[ -d "$v8_checkout/third_party/libc++/src/include" && -f "$v8_checkout/buildtools/third_party/libc++/__config_site" ]]; then
  cp -R "$v8_checkout/third_party/libc++/src/include/." "$sdk_root/support/libcxx/include/"
  copy_required_file "$v8_checkout/buildtools/third_party/libc++/__config_site" "$sdk_root/support/libcxx/buildtools/__config_site"
  copy_required_file "$v8_checkout/buildtools/third_party/libc++/__assertion_handler" "$sdk_root/support/libcxx/buildtools/__assertion_handler"
fi

python3 - "$sdk_root/share/v8_features.json" "$sdk_root/share/sloppy-v8-sdk.json" "$revision" "$platform" "$target_arch" <<'PY'
import json
import sys

features_path, manifest_path, revision, platform, target_arch = sys.argv[1:6]
with open(features_path, encoding="utf-8") as handle:
    features = json.load(handle)

manifest = {
    "name": "sloppy-v8-sdk",
    "platform": platform,
    "source": "sloppy-built-v8",
    "v8Revision": revision,
    "buildType": "release",
    "crtCompatibility": "macos clang-libc++ static-v8",
    "abi": {
        "v8TargetArch": target_arch,
        "v8CompressPointers": bool(features["v8_enable_pointer_compression"]),
        "v8CompressPointersInSharedCage": bool(features["v8_enable_pointer_compression_shared_cage"]),
        "v8_31BitSmisOn64BitArch": bool(features["v8_enable_31bit_smis_on_64bit_arch"]),
        "v8EnableSandbox": bool(features["v8_enable_sandbox"]),
        "v8EnableI18nSupport": bool(features["v8_enable_i18n_support"]),
        "v8EnableWebAssembly": bool(features["v8_enable_webassembly"]),
    },
    "publicFeatureMetadata": "share/v8_features.json",
    "buildConfigMetadata": "share/v8_build_config.json",
    "gnArgs": [
        "is_debug=false",
        "target_os=mac",
        f"target_cpu={target_arch}",
        f"v8_target_cpu={target_arch}",
        "is_component_build=false",
        "v8_monolithic=true",
        "v8_use_external_startup_data=false",
        "v8_enable_temporal_support=false",
        "v8_enable_webassembly=false",
        "v8_enable_sandbox=true",
        "use_custom_libcxx=true",
        "use_thin_archives=false",
        "use_allocator_shim=false",
        "use_clang_modules=false",
        "treat_warnings_as_errors=false",
        "symbol_level=1",
    ],
}

with open(manifest_path, "w", encoding="utf-8") as handle:
    json.dump(manifest, handle, indent=2)
    handle.write("\n")
PY

"$repo_root/tools/unix/resolve-v8-sdk.sh" --mode REQUIRED --v8-root "$sdk_root" --quiet >/dev/null

mkdir -p "$archive_dir/stage" "$archive_dir/out"
rm -rf "$archive_dir/stage/v8"
cp -R "$sdk_root" "$archive_dir/stage/v8"

compiler_line="$(clang --version | head -n 1 | tr ' /:' '____' | tr -cd 'A-Za-z0-9_.-')"
sdk_key="$asset_platform-v8-$revision-$compiler_line-sdk-v1"
archive_name="sloppy-v8-sdk-$asset_platform-$sdk_key.tar.gz"
archive_path="$archive_dir/out/$archive_name"
metadata_path="$archive_dir/out/sloppy-v8-sdk-$asset_platform-$sdk_key.json"

python3 - "$metadata_path" "$sdk_key" "$asset_platform" "$platform" "$revision" "$compiler_line" <<'PY'
import json
import os
import subprocess
import sys
from datetime import datetime, timezone

metadata_path, sdk_key, asset_platform, sdk_platform, revision, compiler = sys.argv[1:7]
metadata = {
    "schemaVersion": 1,
    "platform": asset_platform,
    "sdkLayoutPlatform": sdk_platform,
    "sdkKey": sdk_key,
    "v8VersionOrRevision": revision,
    "toolchain": compiler,
    "os": subprocess.check_output(["sw_vers"], text=True, stderr=subprocess.DEVNULL),
    "arch": target_arch if (target_arch := sdk_platform.split("-")[-1]) else "unknown",
    "gitCommit": subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip(),
    "builtAtUtc": datetime.now(timezone.utc).isoformat(),
    "layoutVersion": 1,
    "source": "github-actions-macos",
    "notes": "Built on macOS for Sloppy alpha release packaging.",
}
with open(metadata_path, "w", encoding="utf-8") as handle:
    json.dump(metadata, handle, indent=2)
    handle.write("\n")
PY
cp "$metadata_path" "$archive_dir/stage/v8-sdk.json"

tar -C "$archive_dir/stage" -czf "$archive_path" v8 v8-sdk.json
if command -v sha256sum >/dev/null 2>&1; then
  (cd "$archive_dir/out" && sha256sum "$archive_name" > "$archive_name.sha256")
else
  (cd "$archive_dir/out" && shasum -a 256 "$archive_name" > "$archive_name.sha256")
fi

echo "Packaged Sloppy macOS V8 SDK: $sdk_root"
echo "Archive: $archive_path"
